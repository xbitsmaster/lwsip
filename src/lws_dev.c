/**
 * @file lws_dev.c
 * @brief lwsip device abstraction layer implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#include "lws_dev.h"
#include "lws_err.h"
#include "lws_log.h"
#include "lws_dev_intl.h"

/* ========================================
 * 前向声明 - 各后端的ops表
 * ======================================== */

/* 文件后端 */
extern const lws_dev_ops_t lws_dev_file_ops;

/* macOS设备后端 */
#ifdef __APPLE__
extern const lws_dev_ops_t lws_dev_macos_ops;
#endif

/* Linux设备后端 */
#ifdef __linux__
extern const lws_dev_ops_t lws_dev_linux_ops;
#endif

/* 嵌入式设备桩 */
#ifdef LWS_ENABLE_DEV_STUB
extern const lws_dev_ops_t lws_dev_stub_ops;
#endif

/* ========================================
 * 内部辅助函数
 * ======================================== */

/**
 * @brief 获取当前时间戳（微秒）
 */
static uint64_t get_current_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

/**
 * @brief 改变设备状态并触发回调
 */
static void change_state(lws_dev_t* dev, lws_dev_state_t new_state) {
    if (!dev || dev->state == new_state) {
        return;
    }

    lws_dev_state_t old_state = dev->state;
    dev->state = new_state;

    if (dev->handler.on_state_changed) {
        dev->handler.on_state_changed(dev, old_state, new_state, dev->handler.userdata);
    }
}

/**
 * @brief 触发错误回调
 */
static void trigger_error(lws_dev_t* dev, int error_code, const char* error_msg) {
    if (!dev) {
        return;
    }

    change_state(dev, LWS_DEV_STATE_ERROR);

    if (dev->handler.on_error) {
        dev->handler.on_error(dev, error_code, error_msg, dev->handler.userdata);
    }
}

/**
 * @brief 根据配置选择合适的ops
 */
static const lws_dev_ops_t* select_ops(const lws_dev_config_t* config) {
    assert(config != NULL);

    /* 文件设备 */
#ifdef DEV_FILE
    if (config->type == LWS_DEV_FILE_READER ||
        config->type == LWS_DEV_FILE_WRITER) {
        return &lws_dev_file_ops;
    }
#endif

    /* 真实设备 - 根据平台选择 */
#ifdef __APPLE__
    if (config->type == LWS_DEV_AUDIO_CAPTURE ||
        config->type == LWS_DEV_AUDIO_PLAYBACK) {
        return &lws_dev_macos_ops;
    }
#endif

#ifdef __linux__
    if (config->type == LWS_DEV_AUDIO_CAPTURE ||
        config->type == LWS_DEV_AUDIO_PLAYBACK) {
        return &lws_dev_linux_ops;
    }
#endif

#ifdef LWS_ENABLE_DEV_STUB
    /* 嵌入式设备桩 */
    return &lws_dev_stub_ops;
#else
    /* 不支持的设备类型 */
    lws_log_error(0, "[DEV] Unsupported device type: %d\n", config->type);
    return NULL;
#endif
}

/* ========================================
 * 核心API实现
 * ======================================== */

lws_dev_t* lws_dev_create(
    const lws_dev_config_t* config,
    const lws_dev_handler_t* handler
) {
    if (!config) {
        lws_log_error(0, "[DEV] Invalid config parameter\n");
        return NULL;
    }

    /* 选择ops */
    const lws_dev_ops_t* ops = select_ops(config);
    if (!ops) {
        lws_log_error(0, "[DEV] Failed to select ops for device type %d\n", config->type);
        return NULL;
    }

    /* 分配设备实例 */
    lws_dev_t* dev = (lws_dev_t*)malloc(sizeof(lws_dev_t));
    if (!dev) {
        lws_log_error(0, "[DEV] Failed to allocate device instance\n");
        return NULL;
    }

    memset(dev, 0, sizeof(lws_dev_t));

    /* 初始化 */
    dev->type = config->type;
    dev->state = LWS_DEV_STATE_IDLE;
    dev->config = *config;
    dev->ops = ops;

    if (handler) {
        dev->handler = *handler;
    }

    /* 设备名称 */
    if (config->device_name) {
        strncpy(dev->device_name, config->device_name, sizeof(dev->device_name) - 1);
    } else {
        snprintf(dev->device_name, sizeof(dev->device_name), "default_%d", config->type);
    }

    lws_log_info("[DEV] Created device: %s (type=%d)\n", dev->device_name, dev->type);

    return dev;
}

void lws_dev_destroy(lws_dev_t* dev) {
    if (!dev) {
        return;
    }

    lws_log_info("[DEV] Destroying device: %s\n", dev->device_name);

    /* 确保设备已关闭 */
    if (dev->state != LWS_DEV_STATE_CLOSED &&
        dev->state != LWS_DEV_STATE_IDLE) {
        lws_dev_close(dev);
    }

    free(dev);
}

int lws_dev_open(lws_dev_t* dev) {
    if (!dev) {
        lws_log_error(0, "[DEV] Invalid device parameter\n");
        return -1;
    }

    if (dev->state != LWS_DEV_STATE_IDLE &&
        dev->state != LWS_DEV_STATE_CLOSED) {
        lws_log_error(0, "[DEV] Device already opened\n");
        return -1;
    }

    if (!dev->ops || !dev->ops->open) {
        lws_log_error(0, "[DEV] No open operation for this device\n");
        return -1;
    }

    lws_log_info("[DEV] Opening device: %s\n", dev->device_name);

    change_state(dev, LWS_DEV_STATE_OPENING);

    int ret = dev->ops->open(dev);
    if (ret < 0) {
        lws_log_error(0, "[DEV] Failed to open device: %s\n", dev->device_name);
        trigger_error(dev, ret, "Failed to open device");
        return ret;
    }

    /* 记录开始时间戳 */
    dev->start_timestamp_us = get_current_time_us();

    change_state(dev, LWS_DEV_STATE_OPENED);

    return 0;
}

void lws_dev_close(lws_dev_t* dev) {
    if (!dev) {
        return;
    }

    if (dev->state == LWS_DEV_STATE_IDLE ||
        dev->state == LWS_DEV_STATE_CLOSED) {
        return;
    }

    lws_log_info("[DEV] Closing device: %s\n", dev->device_name);

    /* 先停止 */
    if (dev->state == LWS_DEV_STATE_STARTED) {
        lws_dev_stop(dev);
    }

    if (dev->ops && dev->ops->close) {
        dev->ops->close(dev);
    }

    change_state(dev, LWS_DEV_STATE_CLOSED);
}

int lws_dev_start(lws_dev_t* dev) {
    if (!dev) {
        lws_log_error(0, "[DEV] Invalid device parameter\n");
        return -1;
    }

    if (dev->state != LWS_DEV_STATE_OPENED &&
        dev->state != LWS_DEV_STATE_STOPPED) {
        lws_log_error(0, "[DEV] Device not in opened/stopped state\n");
        return -1;
    }

    if (!dev->ops || !dev->ops->start) {
        lws_log_error(0, "[DEV] No start operation for this device\n");
        return -1;
    }

    lws_log_info("[DEV] Starting device: %s\n", dev->device_name);

    int ret = dev->ops->start(dev);
    if (ret < 0) {
        lws_log_error(0, "[DEV] Failed to start device: %s\n", dev->device_name);
        trigger_error(dev, ret, "Failed to start device");
        return ret;
    }

    change_state(dev, LWS_DEV_STATE_STARTED);

    return 0;
}

void lws_dev_stop(lws_dev_t* dev) {
    if (!dev) {
        return;
    }

    if (dev->state != LWS_DEV_STATE_STARTED) {
        return;
    }

    lws_log_info("[DEV] Stopping device: %s\n", dev->device_name);

    if (dev->ops && dev->ops->stop) {
        dev->ops->stop(dev);
    }

    change_state(dev, LWS_DEV_STATE_STOPPED);
}

/* ========================================
 * 音频API实现
 * ======================================== */

int lws_dev_read_audio(lws_dev_t* dev, void* buf, int samples) {
    if (!dev || !buf || samples <= 0) {
        return -1;
    }

    if (dev->state != LWS_DEV_STATE_STARTED) {
        return -1;
    }

    if (!dev->ops || !dev->ops->read_audio) {
        return -1;
    }

    return dev->ops->read_audio(dev, buf, samples);
}

int lws_dev_write_audio(lws_dev_t* dev, const void* data, int samples) {
    if (!dev || !data || samples <= 0) {
        return -1;
    }

    if (dev->state != LWS_DEV_STATE_STARTED) {
        return -1;
    }

    if (!dev->ops || !dev->ops->write_audio) {
        return -1;
    }

    return dev->ops->write_audio(dev, data, samples);
}

int lws_dev_get_audio_avail(lws_dev_t* dev) {
    if (!dev) {
        return -1;
    }

    if (!dev->ops || !dev->ops->get_audio_avail) {
        return -1;
    }

    return dev->ops->get_audio_avail(dev);
}

int lws_dev_flush_audio(lws_dev_t* dev) {
    if (!dev) {
        return -1;
    }

    if (!dev->ops || !dev->ops->flush_audio) {
        return -1;
    }

    return dev->ops->flush_audio(dev);
}

/* ========================================
 * 视频API实现
 * ======================================== */

int lws_dev_read_video(lws_dev_t* dev, void* buf, int size) {
    if (!dev || !buf || size <= 0) {
        return -1;
    }

    if (dev->state != LWS_DEV_STATE_STARTED) {
        return -1;
    }

    if (!dev->ops || !dev->ops->read_video) {
        return -1;
    }

    return dev->ops->read_video(dev, buf, size);
}

int lws_dev_write_video(lws_dev_t* dev, const void* data, int size) {
    if (!dev || !data || size <= 0) {
        return -1;
    }

    if (dev->state != LWS_DEV_STATE_STARTED) {
        return -1;
    }

    if (!dev->ops || !dev->ops->write_video) {
        return -1;
    }

    return dev->ops->write_video(dev, data, size);
}

/* ========================================
 * 时间戳API实现
 * ======================================== */

uint64_t lws_dev_get_timestamp(lws_dev_t* dev) {
    if (!dev) {
        return 0;
    }

    uint64_t current_us = get_current_time_us();
    return current_us - dev->start_timestamp_us;
}

uint32_t lws_dev_to_rtp_timestamp(
    lws_dev_t* dev,
    uint64_t dev_timestamp,
    int sample_rate
) {
    if (!dev || sample_rate <= 0) {
        return 0;
    }

    /* 设备时间戳（微秒）转RTP时间戳 */
    /* RTP timestamp = (dev_timestamp_us / 1000000) * sample_rate */
    return (uint32_t)((dev_timestamp * sample_rate) / 1000000);
}

/* ========================================
 * 状态查询API实现
 * ======================================== */

lws_dev_state_t lws_dev_get_state(lws_dev_t* dev) {
    return dev ? dev->state : LWS_DEV_STATE_IDLE;
}

lws_dev_type_t lws_dev_get_type(lws_dev_t* dev) {
    return dev ? dev->type : LWS_DEV_AUDIO_CAPTURE;
}

const char* lws_dev_get_name(lws_dev_t* dev) {
    return dev ? dev->device_name : "unknown";
}

/* ========================================
 * 辅助函数实现
 * ======================================== */

void lws_dev_init_audio_capture_config(lws_dev_config_t* config) {
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(lws_dev_config_t));
    config->type = LWS_DEV_AUDIO_CAPTURE;
    config->audio.format = LWS_AUDIO_FMT_PCM_S16LE;
    config->audio.sample_rate = 8000;
    config->audio.channels = 1;
    config->audio.frame_duration_ms = 20;
    config->audio.latency_ms = 100;
}

void lws_dev_init_audio_playback_config(lws_dev_config_t* config) {
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(lws_dev_config_t));
    config->type = LWS_DEV_AUDIO_PLAYBACK;
    config->audio.format = LWS_AUDIO_FMT_PCM_S16LE;
    config->audio.sample_rate = 8000;
    config->audio.channels = 1;
    config->audio.frame_duration_ms = 20;
    config->audio.latency_ms = 100;
}

void lws_dev_init_video_capture_config(lws_dev_config_t* config) {
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(lws_dev_config_t));
    config->type = LWS_DEV_VIDEO_CAPTURE;
    config->video.format = LWS_VIDEO_FMT_YUV420P;
    config->video.width = 640;
    config->video.height = 480;
    config->video.fps = 30;
}

void lws_dev_init_video_display_config(lws_dev_config_t* config) {
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(lws_dev_config_t));
    config->type = LWS_DEV_VIDEO_DISPLAY;
    config->video.format = LWS_VIDEO_FMT_YUV420P;
    config->video.width = 640;
    config->video.height = 480;
    config->video.fps = 30;
}

#ifdef DEV_FILE
void lws_dev_init_file_reader_config(lws_dev_config_t* config, const char* file_path) {
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(lws_dev_config_t));
    config->type = LWS_DEV_FILE_READER;
    config->file.file_path = file_path;
    config->file.loop = 0;
}

void lws_dev_init_file_writer_config(lws_dev_config_t* config, const char* file_path) {
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(lws_dev_config_t));
    config->type = LWS_DEV_FILE_WRITER;
    config->file.file_path = file_path;
}
#endif /* DEV_FILE */

int lws_audio_calc_frame_size(
    lws_audio_format_t format,
    int channels,
    int samples
) {
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

int lws_audio_calc_frame_samples(int sample_rate, int frame_duration_ms) {
    if (sample_rate <= 0 || frame_duration_ms <= 0) {
        return -1;
    }

    return (sample_rate * frame_duration_ms) / 1000;
}

int lws_dev_enum_audio_devices(
    lws_dev_type_t type,
    char** devices,
    int max_devices
) {
    /* TODO: 平台特定实现 */
    (void)type;
    (void)devices;
    (void)max_devices;
    return 0;
}

int lws_dev_enum_video_devices(char** devices, int max_devices) {
    /* TODO: 平台特定实现 */
    (void)devices;
    (void)max_devices;
    return 0;
}
