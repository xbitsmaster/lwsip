/**
 * @file lws_dev_file.c
 * @brief lwsip file device backend implementation (MP4 via libmov)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "lws_dev.h"
#include "lws_err.h"
#include "lws_log.h"
#include "lws_dev_intl.h"

/* libmov headers */
#include "mov-buffer.h"
#include "mov-format.h"
#include "mp4-writer.h"
#include "mov-reader.h"

/* ========================================
 * 文件后端数据结构
 * ======================================== */

typedef struct {
    /* 文件信息 */
    char filepath[512];
    FILE* fp;
    int is_writing;  /* 0=reading, 1=writing */

    /* MP4 writer */
    struct mp4_writer_t* writer;
    int audio_track_id;
    int video_track_id;

    /* MP4 reader */
    mov_reader_t* reader;
    uint32_t audio_track;
    uint32_t video_track;
    uint8_t audio_object;
    int audio_channels;
    int audio_sample_rate;
    int audio_bits_per_sample;

    /* 时间戳管理 */
    uint64_t start_time_ms;
    int64_t current_pts_ms;
    uint32_t samples_written;
    uint32_t samples_read;

    /* 缓冲区 */
    uint8_t* read_buffer;
    size_t read_buffer_size;
    size_t read_buffer_used;
} lws_dev_file_data_t;

/* ========================================
 * libmov buffer接口实现
 * ======================================== */

static int file_buffer_read(void* param, void* data, uint64_t bytes) {
    FILE* fp = (FILE*)param;
    if (!fp) {
        return -1;
    }
    size_t n = fread(data, 1, (size_t)bytes, fp);
    return (n == (size_t)bytes) ? 0 : -1;
}

static int file_buffer_write(void* param, const void* data, uint64_t bytes) {
    FILE* fp = (FILE*)param;
    if (!fp) {
        return -1;
    }
    size_t n = fwrite(data, 1, (size_t)bytes, fp);
    return (n == (size_t)bytes) ? 0 : -1;
}

static int file_buffer_seek(void* param, int64_t offset) {
    FILE* fp = (FILE*)param;
    if (!fp) {
        return -1;
    }
    return fseek(fp, (long)offset, offset >= 0 ? SEEK_SET : SEEK_END);
}

static int64_t file_buffer_tell(void* param) {
    FILE* fp = (FILE*)param;
    if (!fp) {
        return -1;
    }
    return (int64_t)ftell(fp);
}

static const struct mov_buffer_t s_file_buffer = {
    file_buffer_read,
    file_buffer_write,
    file_buffer_seek,
    file_buffer_tell
};

/* ========================================
 * libmov reader回调
 * ======================================== */

static void on_audio_track(void* param, uint32_t track, uint8_t object,
                           int channel_count, int bit_per_sample, int sample_rate,
                           const void* extra, size_t bytes) {
    lws_dev_file_data_t* data = (lws_dev_file_data_t*)param;

    lws_log_info("[DEV_FILE] Found audio track %u: object=0x%02x, channels=%d, "
                 "sample_rate=%d, bits=%d\n",
                 track, object, channel_count, sample_rate, bit_per_sample);

    data->audio_track = track;
    data->audio_object = object;
    data->audio_channels = channel_count;
    data->audio_sample_rate = sample_rate;
    data->audio_bits_per_sample = bit_per_sample;

    (void)extra;
    (void)bytes;
}

static void on_video_track(void* param, uint32_t track, uint8_t object,
                           int width, int height, const void* extra, size_t bytes) {
    lws_dev_file_data_t* data = (lws_dev_file_data_t*)param;

    lws_log_info("[DEV_FILE] Found video track %u: object=0x%02x, %dx%d\n",
                 track, object, width, height);

    data->video_track = track;

    (void)extra;
    (void)bytes;
}

static void on_subtitle_track(void* param, uint32_t track, uint8_t object,
                              const void* extra, size_t bytes) {
    lws_log_info("[DEV_FILE] Found subtitle track %u: object=0x%02x\n",
                 track, object);

    (void)param;
    (void)extra;
    (void)bytes;
}

/* ========================================
 * 内部辅助函数
 * ======================================== */

/**
 * @brief 将lws_audio_format_t转换为MOV object type
 */
static uint8_t audio_format_to_mov_object(lws_audio_format_t format) {
    switch (format) {
        case LWS_AUDIO_FMT_PCMA:
            return MOV_OBJECT_G711a;
        case LWS_AUDIO_FMT_PCMU:
            return MOV_OBJECT_G711u;
        case LWS_AUDIO_FMT_PCM_S16LE:
        case LWS_AUDIO_FMT_PCM_S16BE:
            /* libmov doesn't directly support PCM16, use G.711 as fallback */
            lws_log_warn(0, "[DEV_FILE] PCM16 not supported by libmov, using G.711a\n");
            return MOV_OBJECT_G711a;
        default:
            lws_log_error(0, "[DEV_FILE] Unsupported audio format: %d\n", format);
            return MOV_OBJECT_NONE;
    }
}

/**
 * @brief 获取音频帧大小（字节）
 */
static int get_audio_frame_size(lws_audio_format_t format, int channels, int samples) {
    int bytes_per_sample = 0;

    switch (format) {
        case LWS_AUDIO_FMT_PCM_S16LE:
        case LWS_AUDIO_FMT_PCM_S16BE:
            bytes_per_sample = 2;
            break;
        case LWS_AUDIO_FMT_PCMU:
        case LWS_AUDIO_FMT_PCMA:
            bytes_per_sample = 1;
            break;
        default:
            return -1;
    }

    return bytes_per_sample * channels * samples;
}

/* ========================================
 * 文件后端操作函数实现
 * ======================================== */

static int file_open(lws_dev_t* dev) {
    if (!dev) {
        return -1;
    }

    lws_dev_file_data_t* data = (lws_dev_file_data_t*)malloc(sizeof(lws_dev_file_data_t));
    if (!data) {
        lws_log_error(0, "[DEV_FILE] Failed to allocate file data\n");
        return -1;
    }

    memset(data, 0, sizeof(lws_dev_file_data_t));

    /* 复制文件路径 */
    strncpy(data->filepath, dev->config.file.file_path, sizeof(data->filepath) - 1);

    /* 确定读写模式 */
    data->is_writing = (dev->type == LWS_DEV_FILE_WRITER) ? 1 : 0;

    /* 打开文件 */
    const char* mode = data->is_writing ? "wb" : "rb";
    data->fp = fopen(data->filepath, mode);
    if (!data->fp) {
        lws_log_error(0, "[DEV_FILE] Failed to open file: %s\n", data->filepath);
        free(data);
        return -1;
    }

    lws_log_info("[DEV_FILE] Opened file: %s (mode=%s)\n", data->filepath, mode);

    /* 写入模式：创建MP4 writer */
    if (data->is_writing) {
        data->writer = mp4_writer_create(0, &s_file_buffer, data->fp, MOV_FLAG_FASTSTART);
        if (!data->writer) {
            lws_log_error(0, "[DEV_FILE] Failed to create MP4 writer\n");
            fclose(data->fp);
            free(data);
            return -1;
        }

        /* 添加音频轨道 */
        uint8_t object = audio_format_to_mov_object(dev->config.audio.format);
        if (object == MOV_OBJECT_NONE) {
            mp4_writer_destroy(data->writer);
            fclose(data->fp);
            free(data);
            return -1;
        }

        int track_id = mp4_writer_add_audio(
            data->writer,
            object,
            dev->config.audio.channels,
            dev->config.audio.format == LWS_AUDIO_FMT_PCMA ||
            dev->config.audio.format == LWS_AUDIO_FMT_PCMU ? 8 : 16,
            dev->config.audio.sample_rate,
            NULL,  /* extra_data */
            0      /* extra_data_size */
        );

        if (track_id < 0) {
            lws_log_error(0, "[DEV_FILE] Failed to add audio track\n");
            mp4_writer_destroy(data->writer);
            fclose(data->fp);
            free(data);
            return -1;
        }

        data->audio_track_id = track_id;
        lws_log_info("[DEV_FILE] Added audio track %d (object=0x%02x, rate=%d, channels=%d)\n",
                     track_id, object, dev->config.audio.sample_rate, dev->config.audio.channels);
    }
    /* 读取模式：创建MP4 reader */
    else {
        data->reader = mov_reader_create(&s_file_buffer, data->fp);
        if (!data->reader) {
            lws_log_error(0, "[DEV_FILE] Failed to create MP4 reader\n");
            fclose(data->fp);
            free(data);
            return -1;
        }

        /* 获取轨道信息 */
        struct mov_reader_trackinfo_t track_info = {
            .onaudio = on_audio_track,
            .onvideo = on_video_track,
            .onsubtitle = on_subtitle_track
        };

        int ret = mov_reader_getinfo(data->reader, &track_info, data);
        if (ret < 0) {
            lws_log_error(0, "[DEV_FILE] Failed to get track info\n");
            mov_reader_destroy(data->reader);
            fclose(data->fp);
            free(data);
            return -1;
        }

        /* 分配读取缓冲区 */
        data->read_buffer_size = 64 * 1024;  /* 64KB */
        data->read_buffer = (uint8_t*)malloc(data->read_buffer_size);
        if (!data->read_buffer) {
            lws_log_error(0, "[DEV_FILE] Failed to allocate read buffer\n");
            mov_reader_destroy(data->reader);
            fclose(data->fp);
            free(data);
            return -1;
        }

        lws_log_info("[DEV_FILE] Created MP4 reader (audio: track=%u, rate=%d)\n",
                     data->audio_track, data->audio_sample_rate);
    }

    dev->platform_data = data;

    return 0;
}

static void file_close(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return;
    }

    lws_dev_file_data_t* data = (lws_dev_file_data_t*)dev->platform_data;

    lws_log_info("[DEV_FILE] Closing file: %s\n", data->filepath);

    /* 关闭writer */
    if (data->writer) {
        mp4_writer_destroy(data->writer);
        data->writer = NULL;
    }

    /* 关闭reader */
    if (data->reader) {
        mov_reader_destroy(data->reader);
        data->reader = NULL;
    }

    /* 关闭文件 */
    if (data->fp) {
        fclose(data->fp);
        data->fp = NULL;
    }

    /* 释放缓冲区 */
    if (data->read_buffer) {
        free(data->read_buffer);
        data->read_buffer = NULL;
    }

    free(data);
    dev->platform_data = NULL;
}

static int file_start(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    lws_dev_file_data_t* data = (lws_dev_file_data_t*)dev->platform_data;

    /* 记录开始时间 */
    data->start_time_ms = 0;
    data->current_pts_ms = 0;
    data->samples_written = 0;
    data->samples_read = 0;

    lws_log_info("[DEV_FILE] Started file device: %s\n", data->filepath);

    return 0;
}

static void file_stop(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return;
    }

    lws_dev_file_data_t* data = (lws_dev_file_data_t*)dev->platform_data;

    lws_log_info("[DEV_FILE] Stopped file device: %s (samples_written=%u, samples_read=%u)\n",
                 data->filepath, data->samples_written, data->samples_read);
}

/* MP4 reader回调：存储读取的数据 */
static void on_read_frame(void* param, uint32_t track, const void* buffer,
                          size_t bytes, int64_t pts, int64_t dts, int flags) {
    lws_dev_file_data_t* data = (lws_dev_file_data_t*)param;

    /* 只处理音频轨道 */
    if (track != data->audio_track) {
        return;
    }

    /* 存储到缓冲区 */
    if (data->read_buffer_used + bytes <= data->read_buffer_size) {
        memcpy(data->read_buffer + data->read_buffer_used, buffer, bytes);
        data->read_buffer_used += bytes;
        data->current_pts_ms = pts;
    } else {
        lws_log_warn(0, "[DEV_FILE] Read buffer overflow, dropping frame\n");
    }

    (void)dts;
    (void)flags;
}

static int file_read_audio(lws_dev_t* dev, void* buf, int samples) {
    if (!dev || !dev->platform_data || !buf || samples <= 0) {
        return -1;
    }

    lws_dev_file_data_t* data = (lws_dev_file_data_t*)dev->platform_data;

    if (!data->reader) {
        return -1;
    }

    /* 计算需要读取的字节数 */
    int frame_size = get_audio_frame_size(dev->config.audio.format,
                                          dev->config.audio.channels,
                                          samples);
    if (frame_size < 0) {
        return -1;
    }

    /* 如果缓冲区数据不足，从文件读取 */
    while (data->read_buffer_used < (size_t)frame_size) {
        int ret = mov_reader_read(data->reader,
                                  data->read_buffer + data->read_buffer_used,
                                  data->read_buffer_size - data->read_buffer_used,
                                  on_read_frame,
                                  data);
        if (ret == 0) {
            /* EOF */
            lws_log_info("[DEV_FILE] Reached end of file\n");
            return 0;
        } else if (ret < 0) {
            lws_log_error(0, "[DEV_FILE] Failed to read from MP4 file\n");
            return -1;
        }
    }

    /* 从缓冲区复制数据 */
    memcpy(buf, data->read_buffer, frame_size);

    /* 移动缓冲区数据 */
    if (data->read_buffer_used > (size_t)frame_size) {
        memmove(data->read_buffer,
                data->read_buffer + frame_size,
                data->read_buffer_used - frame_size);
        data->read_buffer_used -= frame_size;
    } else {
        data->read_buffer_used = 0;
    }

    data->samples_read += samples;

    return samples;
}

static int file_write_audio(lws_dev_t* dev, const void* pcm_data, int samples) {
    if (!dev || !dev->platform_data || !pcm_data || samples <= 0) {
        return -1;
    }

    lws_dev_file_data_t* data = (lws_dev_file_data_t*)dev->platform_data;

    if (!data->writer) {
        return -1;
    }

    /* 计算帧大小 */
    int frame_size = get_audio_frame_size(dev->config.audio.format,
                                          dev->config.audio.channels,
                                          samples);
    if (frame_size < 0) {
        return -1;
    }

    /* 计算PTS（毫秒） */
    int64_t pts = (data->samples_written * 1000LL) / dev->config.audio.sample_rate;
    int64_t dts = pts;

    /* 写入MP4文件 */
    int ret = mp4_writer_write(data->writer,
                               data->audio_track_id,
                               pcm_data,
                               frame_size,
                               pts,
                               dts,
                               MOV_AV_FLAG_KEYFREAME);  /* Audio frames are always keyframes */

    if (ret < 0) {
        lws_log_error(0, "[DEV_FILE] Failed to write audio frame to MP4\n");
        return -1;
    }

    data->samples_written += samples;
    data->current_pts_ms = pts;

    return samples;
}

static int file_get_audio_avail(lws_dev_t* dev) {
    /* 文件设备不限制可用空间 */
    (void)dev;
    return INT32_MAX;
}

static int file_flush_audio(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    lws_dev_file_data_t* data = (lws_dev_file_data_t*)dev->platform_data;

    /* 清空读取缓冲区 */
    data->read_buffer_used = 0;

    return 0;
}

static int file_read_video(lws_dev_t* dev, void* buf, int size) {
    /* TODO: 视频读取支持 */
    (void)dev;
    (void)buf;
    (void)size;
    return -1;
}

static int file_write_video(lws_dev_t* dev, const void* data, int size) {
    /* TODO: 视频写入支持 */
    (void)dev;
    (void)data;
    (void)size;
    return -1;
}

/* ========================================
 * 文件后端ops表
 * ======================================== */

const lws_dev_ops_t lws_dev_file_ops = {
    .open = file_open,
    .close = file_close,
    .start = file_start,
    .stop = file_stop,
    .read_audio = file_read_audio,
    .write_audio = file_write_audio,
    .get_audio_avail = file_get_audio_avail,
    .flush_audio = file_flush_audio,
    .read_video = file_read_video,
    .write_video = file_write_video
};
