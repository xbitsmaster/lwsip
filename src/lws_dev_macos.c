/**
 * @file lws_dev_macos.c
 * @brief lwsip macOS device backend implementation (AudioQueue API)
 */

#ifdef __APPLE__

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <AudioToolbox/AudioQueue.h>

#include "lws_dev.h"
#include "lws_err.h"
#include "lws_log.h"
#include "lws_dev_intl.h"

/* ========================================
 * macOS后端数据结构
 * ======================================== */

#define NUM_BUFFERS 3
#define BUFFER_DURATION_MS 20

typedef struct {
    /* AudioQueue */
    AudioQueueRef audio_queue;
    AudioQueueBufferRef buffers[NUM_BUFFERS];

    /* 音频格式 */
    AudioStreamBasicDescription format;

    /* 参数 */
    int sample_rate;
    int channels;
    int frame_duration_ms;
    int is_capture;  /* 1=capture, 0=playback */

    /* 缓冲区管理 */
    uint8_t* ring_buffer;
    size_t ring_buffer_size;
    size_t ring_buffer_read_pos;
    size_t ring_buffer_write_pos;
    size_t ring_buffer_used;

    /* 同步 */
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    /* 设备引用 */
    lws_dev_t* dev;

    /* 运行状态 */
    int is_running;
} lws_dev_macos_data_t;

/* ========================================
 * AudioQueue回调函数
 * ======================================== */

/**
 * @brief 音频采集回调（输入）
 */
static void audio_input_callback(
    void* user_data,
    AudioQueueRef queue,
    AudioQueueBufferRef buffer,
    const AudioTimeStamp* start_time,
    UInt32 num_packets,
    const AudioStreamPacketDescription* packet_descs
) {
    lws_dev_macos_data_t* data = (lws_dev_macos_data_t*)user_data;

    (void)queue;
    (void)start_time;
    (void)num_packets;
    (void)packet_descs;

    pthread_mutex_lock(&data->mutex);

    /* 将采集的数据写入环形缓冲区 */
    size_t bytes = buffer->mAudioDataByteSize;
    size_t available = data->ring_buffer_size - data->ring_buffer_used;

    if (bytes <= available) {
        /* 计算写入位置 */
        size_t write_pos = data->ring_buffer_write_pos;
        size_t to_end = data->ring_buffer_size - write_pos;

        if (bytes <= to_end) {
            /* 一次写入 */
            memcpy(data->ring_buffer + write_pos, buffer->mAudioData, bytes);
            data->ring_buffer_write_pos = (write_pos + bytes) % data->ring_buffer_size;
        } else {
            /* 分两次写入 */
            memcpy(data->ring_buffer + write_pos, buffer->mAudioData, to_end);
            memcpy(data->ring_buffer, (uint8_t*)buffer->mAudioData + to_end, bytes - to_end);
            data->ring_buffer_write_pos = bytes - to_end;
        }

        data->ring_buffer_used += bytes;

        /* 唤醒等待读取的线程 */
        pthread_cond_signal(&data->cond);
    } else {
        lws_log_warn(0, "[DEV_MACOS] Ring buffer overflow, dropping %zu bytes\n", bytes);
    }

    pthread_mutex_unlock(&data->mutex);

    /* 重新入队缓冲区 */
    if (data->is_running) {
        AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
    }
}

/**
 * @brief 音频播放回调（输出）
 */
static void audio_output_callback(
    void* user_data,
    AudioQueueRef queue,
    AudioQueueBufferRef buffer
) {
    lws_dev_macos_data_t* data = (lws_dev_macos_data_t*)user_data;

    (void)queue;

    pthread_mutex_lock(&data->mutex);

    /* 从环形缓冲区读取数据 */
    size_t bytes = buffer->mAudioDataBytesCapacity;
    size_t available = data->ring_buffer_used;

    if (available >= bytes) {
        /* 有足够数据 */
        size_t read_pos = data->ring_buffer_read_pos;
        size_t to_end = data->ring_buffer_size - read_pos;

        if (bytes <= to_end) {
            /* 一次读取 */
            memcpy(buffer->mAudioData, data->ring_buffer + read_pos, bytes);
            data->ring_buffer_read_pos = (read_pos + bytes) % data->ring_buffer_size;
        } else {
            /* 分两次读取 */
            memcpy(buffer->mAudioData, data->ring_buffer + read_pos, to_end);
            memcpy((uint8_t*)buffer->mAudioData + to_end, data->ring_buffer, bytes - to_end);
            data->ring_buffer_read_pos = bytes - to_end;
        }

        data->ring_buffer_used -= bytes;
        buffer->mAudioDataByteSize = bytes;
    } else {
        /* 数据不足，填充静音 */
        memset(buffer->mAudioData, 0, bytes);
        buffer->mAudioDataByteSize = bytes;
    }

    pthread_mutex_unlock(&data->mutex);

    /* 入队缓冲区 */
    if (data->is_running) {
        AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
    }
}

/* ========================================
 * 内部辅助函数
 * ======================================== */

/**
 * @brief 初始化AudioStreamBasicDescription
 */
static void setup_audio_format(
    AudioStreamBasicDescription* format,
    lws_audio_format_t lws_format,
    int sample_rate,
    int channels
) {
    memset(format, 0, sizeof(AudioStreamBasicDescription));

    format->mSampleRate = sample_rate;
    format->mChannelsPerFrame = channels;

    switch (lws_format) {
        case LWS_AUDIO_FMT_PCM_S16LE:
            format->mFormatID = kAudioFormatLinearPCM;
            format->mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
            format->mBitsPerChannel = 16;
            format->mBytesPerFrame = 2 * channels;
            format->mBytesPerPacket = format->mBytesPerFrame;
            format->mFramesPerPacket = 1;
            break;

        case LWS_AUDIO_FMT_PCMU:
            format->mFormatID = kAudioFormatULaw;
            format->mBitsPerChannel = 8;
            format->mBytesPerFrame = 1 * channels;
            format->mBytesPerPacket = format->mBytesPerFrame;
            format->mFramesPerPacket = 1;
            break;

        case LWS_AUDIO_FMT_PCMA:
            format->mFormatID = kAudioFormatALaw;
            format->mBitsPerChannel = 8;
            format->mBytesPerFrame = 1 * channels;
            format->mBytesPerPacket = format->mBytesPerFrame;
            format->mFramesPerPacket = 1;
            break;

        default:
            lws_log_error(0, "[DEV_MACOS] Unsupported audio format: %d\n", lws_format);
            break;
    }
}

/* ========================================
 * macOS后端操作函数实现
 * ======================================== */

static int macos_open(lws_dev_t* dev) {
    if (!dev) {
        return -1;
    }

    lws_dev_macos_data_t* data = (lws_dev_macos_data_t*)malloc(sizeof(lws_dev_macos_data_t));
    if (!data) {
        lws_log_error(0, "[DEV_MACOS] Failed to allocate macOS data\n");
        return -1;
    }

    memset(data, 0, sizeof(lws_dev_macos_data_t));

    /* 初始化参数 */
    data->sample_rate = dev->config.audio.sample_rate;
    data->channels = dev->config.audio.channels;
    data->frame_duration_ms = dev->config.audio.frame_duration_ms;
    data->is_capture = (dev->type == LWS_DEV_AUDIO_CAPTURE) ? 1 : 0;
    data->dev = dev;

    /* 初始化同步对象 */
    pthread_mutex_init(&data->mutex, NULL);
    pthread_cond_init(&data->cond, NULL);

    /* 初始化音频格式 */
    setup_audio_format(&data->format,
                       dev->config.audio.format,
                       data->sample_rate,
                       data->channels);

    /* 创建AudioQueue */
    OSStatus status;
    if (data->is_capture) {
        status = AudioQueueNewInput(
            &data->format,
            audio_input_callback,
            data,
            NULL,  /* run loop */
            kCFRunLoopCommonModes,
            0,     /* flags */
            &data->audio_queue
        );
    } else {
        status = AudioQueueNewOutput(
            &data->format,
            audio_output_callback,
            data,
            NULL,  /* run loop */
            kCFRunLoopCommonModes,
            0,     /* flags */
            &data->audio_queue
        );
    }

    if (status != noErr) {
        lws_log_error(0, "[DEV_MACOS] Failed to create AudioQueue: %d\n", (int)status);
        pthread_mutex_destroy(&data->mutex);
        pthread_cond_destroy(&data->cond);
        free(data);
        return -1;
    }

    /* 计算缓冲区大小 */
    int samples_per_buffer = (data->sample_rate * BUFFER_DURATION_MS) / 1000;
    int bytes_per_sample = data->format.mBytesPerFrame;
    int buffer_size = samples_per_buffer * bytes_per_sample;

    /* 分配并入队AudioQueue缓冲区 */
    for (int i = 0; i < NUM_BUFFERS; i++) {
        status = AudioQueueAllocateBuffer(
            data->audio_queue,
            buffer_size,
            &data->buffers[i]
        );

        if (status != noErr) {
            lws_log_error(0, "[DEV_MACOS] Failed to allocate buffer %d: %d\n", i, (int)status);
            /* 清理已分配的缓冲区 */
            for (int j = 0; j < i; j++) {
                AudioQueueFreeBuffer(data->audio_queue, data->buffers[j]);
            }
            AudioQueueDispose(data->audio_queue, true);
            pthread_mutex_destroy(&data->mutex);
            pthread_cond_destroy(&data->cond);
            free(data);
            return -1;
        }

        /* 对于采集，需要立即入队缓冲区 */
        if (data->is_capture) {
            AudioQueueEnqueueBuffer(data->audio_queue, data->buffers[i], 0, NULL);
        } else {
            /* 对于播放，填充静音并入队 */
            memset(data->buffers[i]->mAudioData, 0, buffer_size);
            data->buffers[i]->mAudioDataByteSize = buffer_size;
            AudioQueueEnqueueBuffer(data->audio_queue, data->buffers[i], 0, NULL);
        }
    }

    /* 创建环形缓冲区（1秒数据） */
    data->ring_buffer_size = data->sample_rate * bytes_per_sample;
    data->ring_buffer = (uint8_t*)malloc(data->ring_buffer_size);
    if (!data->ring_buffer) {
        lws_log_error(0, "[DEV_MACOS] Failed to allocate ring buffer\n");
        for (int i = 0; i < NUM_BUFFERS; i++) {
            AudioQueueFreeBuffer(data->audio_queue, data->buffers[i]);
        }
        AudioQueueDispose(data->audio_queue, true);
        pthread_mutex_destroy(&data->mutex);
        pthread_cond_destroy(&data->cond);
        free(data);
        return -1;
    }

    data->ring_buffer_read_pos = 0;
    data->ring_buffer_write_pos = 0;
    data->ring_buffer_used = 0;

    dev->platform_data = data;

    lws_log_info("[DEV_MACOS] Opened audio device (capture=%d, rate=%d, channels=%d)\n",
                 data->is_capture, data->sample_rate, data->channels);

    return 0;
}

static void macos_close(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return;
    }

    lws_dev_macos_data_t* data = (lws_dev_macos_data_t*)dev->platform_data;

    lws_log_info("[DEV_MACOS] Closing audio device\n");

    /* 停止AudioQueue */
    if (data->audio_queue) {
        data->is_running = 0;
        AudioQueueStop(data->audio_queue, true);

        /* 释放缓冲区 */
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (data->buffers[i]) {
                AudioQueueFreeBuffer(data->audio_queue, data->buffers[i]);
            }
        }

        AudioQueueDispose(data->audio_queue, true);
        data->audio_queue = NULL;
    }

    /* 释放环形缓冲区 */
    if (data->ring_buffer) {
        free(data->ring_buffer);
        data->ring_buffer = NULL;
    }

    /* 销毁同步对象 */
    pthread_mutex_destroy(&data->mutex);
    pthread_cond_destroy(&data->cond);

    free(data);
    dev->platform_data = NULL;
}

static int macos_start(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    lws_dev_macos_data_t* data = (lws_dev_macos_data_t*)dev->platform_data;

    data->is_running = 1;

    /* 启动AudioQueue */
    OSStatus status = AudioQueueStart(data->audio_queue, NULL);
    if (status != noErr) {
        lws_log_error(0, "[DEV_MACOS] Failed to start AudioQueue: %d\n", (int)status);
        data->is_running = 0;
        return -1;
    }

    lws_log_info("[DEV_MACOS] Started audio device\n");

    return 0;
}

static void macos_stop(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return;
    }

    lws_dev_macos_data_t* data = (lws_dev_macos_data_t*)dev->platform_data;

    data->is_running = 0;

    /* 停止AudioQueue */
    AudioQueueStop(data->audio_queue, true);

    /* 清空环形缓冲区 */
    pthread_mutex_lock(&data->mutex);
    data->ring_buffer_read_pos = 0;
    data->ring_buffer_write_pos = 0;
    data->ring_buffer_used = 0;
    pthread_mutex_unlock(&data->mutex);

    lws_log_info("[DEV_MACOS] Stopped audio device\n");
}

static int macos_read_audio(lws_dev_t* dev, void* buf, int samples) {
    if (!dev || !dev->platform_data || !buf || samples <= 0) {
        return -1;
    }

    lws_dev_macos_data_t* data = (lws_dev_macos_data_t*)dev->platform_data;

    int bytes_per_sample = data->format.mBytesPerFrame;
    size_t bytes_to_read = samples * bytes_per_sample;

    pthread_mutex_lock(&data->mutex);

    /* 等待数据可用（最多等待100ms） */
    struct timespec ts;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec;
    ts.tv_nsec = tv.tv_usec * 1000 + 100000000;  /* current time + 100ms */
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    while (data->ring_buffer_used < bytes_to_read && data->is_running) {
        int ret = pthread_cond_timedwait(&data->cond, &data->mutex, &ts);
        if (ret != 0) {
            /* 超时或错误 */
            break;
        }
    }

    /* 读取数据 */
    size_t bytes_read = 0;
    if (data->ring_buffer_used >= bytes_to_read) {
        size_t read_pos = data->ring_buffer_read_pos;
        size_t to_end = data->ring_buffer_size - read_pos;

        if (bytes_to_read <= to_end) {
            /* 一次读取 */
            memcpy(buf, data->ring_buffer + read_pos, bytes_to_read);
            data->ring_buffer_read_pos = (read_pos + bytes_to_read) % data->ring_buffer_size;
        } else {
            /* 分两次读取 */
            memcpy(buf, data->ring_buffer + read_pos, to_end);
            memcpy((uint8_t*)buf + to_end, data->ring_buffer, bytes_to_read - to_end);
            data->ring_buffer_read_pos = bytes_to_read - to_end;
        }

        data->ring_buffer_used -= bytes_to_read;
        bytes_read = bytes_to_read;
    }

    pthread_mutex_unlock(&data->mutex);

    return bytes_read / bytes_per_sample;
}

static int macos_write_audio(lws_dev_t* dev, const void* pcm_data, int samples) {
    if (!dev || !dev->platform_data || !pcm_data || samples <= 0) {
        return -1;
    }

    lws_dev_macos_data_t* data = (lws_dev_macos_data_t*)dev->platform_data;

    int bytes_per_sample = data->format.mBytesPerFrame;
    size_t bytes_to_write = samples * bytes_per_sample;

    pthread_mutex_lock(&data->mutex);

    /* 检查缓冲区空间 */
    size_t available = data->ring_buffer_size - data->ring_buffer_used;
    if (bytes_to_write > available) {
        lws_log_warn(0, "[DEV_MACOS] Ring buffer full, dropping audio\n");
        pthread_mutex_unlock(&data->mutex);
        return 0;
    }

    /* 写入数据 */
    size_t write_pos = data->ring_buffer_write_pos;
    size_t to_end = data->ring_buffer_size - write_pos;

    if (bytes_to_write <= to_end) {
        /* 一次写入 */
        memcpy(data->ring_buffer + write_pos, pcm_data, bytes_to_write);
        data->ring_buffer_write_pos = (write_pos + bytes_to_write) % data->ring_buffer_size;
    } else {
        /* 分两次写入 */
        memcpy(data->ring_buffer + write_pos, pcm_data, to_end);
        memcpy(data->ring_buffer, (const uint8_t*)pcm_data + to_end, bytes_to_write - to_end);
        data->ring_buffer_write_pos = bytes_to_write - to_end;
    }

    data->ring_buffer_used += bytes_to_write;

    pthread_mutex_unlock(&data->mutex);

    return samples;
}

static int macos_get_audio_avail(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    lws_dev_macos_data_t* data = (lws_dev_macos_data_t*)dev->platform_data;

    pthread_mutex_lock(&data->mutex);
    size_t available = data->ring_buffer_size - data->ring_buffer_used;
    pthread_mutex_unlock(&data->mutex);

    int bytes_per_sample = data->format.mBytesPerFrame;
    return available / bytes_per_sample;
}

static int macos_flush_audio(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    lws_dev_macos_data_t* data = (lws_dev_macos_data_t*)dev->platform_data;

    pthread_mutex_lock(&data->mutex);
    data->ring_buffer_read_pos = 0;
    data->ring_buffer_write_pos = 0;
    data->ring_buffer_used = 0;
    pthread_mutex_unlock(&data->mutex);

    return 0;
}

static int macos_read_video(lws_dev_t* dev, void* buf, int size) {
    /* macOS视频支持暂未实现 */
    (void)dev;
    (void)buf;
    (void)size;
    return -1;
}

static int macos_write_video(lws_dev_t* dev, const void* data, int size) {
    /* macOS视频支持暂未实现 */
    (void)dev;
    (void)data;
    (void)size;
    return -1;
}

/* ========================================
 * macOS后端ops表
 * ======================================== */

const lws_dev_ops_t lws_dev_macos_ops = {
    .open = macos_open,
    .close = macos_close,
    .start = macos_start,
    .stop = macos_stop,
    .read_audio = macos_read_audio,
    .write_audio = macos_write_audio,
    .get_audio_avail = macos_get_audio_avail,
    .flush_audio = macos_flush_audio,
    .read_video = macos_read_video,
    .write_video = macos_write_video
};

#endif /* __APPLE__ */
