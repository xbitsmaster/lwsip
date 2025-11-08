/**
 * @file lws_dev_linux.c
 * @brief lwsip Linux device backend implementation (ALSA API)
 */

#ifdef __linux__

#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>

#include "../include/lws_dev.h"
#include "../include/lws_err.h"
#include "../osal/include/lws_log.h"
#include "lws_dev_intl.h"

/* ========================================
 * Linux后端数据结构
 * ======================================== */

typedef struct {
    /* ALSA句柄 */
    snd_pcm_t* pcm_handle;
    snd_pcm_hw_params_t* hw_params;

    /* 参数 */
    unsigned int sample_rate;
    unsigned int channels;
    snd_pcm_format_t format;
    snd_pcm_uframes_t period_size;

    /* 设备名称 */
    char device_name[64];

    /* 是否采集 */
    int is_capture;
} lws_dev_linux_data_t;

/* ========================================
 * 内部辅助函数
 * ======================================== */

/**
 * @brief 将lws_audio_format_t转换为ALSA格式
 */
static snd_pcm_format_t audio_format_to_alsa(lws_audio_format_t format) {
    switch (format) {
        case LWS_AUDIO_FMT_PCM_S16LE:
            return SND_PCM_FORMAT_S16_LE;
        case LWS_AUDIO_FMT_PCM_S16BE:
            return SND_PCM_FORMAT_S16_BE;
        case LWS_AUDIO_FMT_PCMU:
            return SND_PCM_FORMAT_MU_LAW;
        case LWS_AUDIO_FMT_PCMA:
            return SND_PCM_FORMAT_A_LAW;
        default:
            return SND_PCM_FORMAT_UNKNOWN;
    }
}

/* ========================================
 * Linux后端操作函数实现
 * ======================================== */

static int linux_open(lws_dev_t* dev) {
    if (!dev) {
        return -1;
    }

    lws_dev_linux_data_t* data = (lws_dev_linux_data_t*)malloc(sizeof(lws_dev_linux_data_t));
    if (!data) {
        lws_log_error(0, "[DEV_LINUX] Failed to allocate Linux data\n");
        return -1;
    }

    memset(data, 0, sizeof(lws_dev_linux_data_t));

    /* 初始化参数 */
    data->sample_rate = dev->config.audio.sample_rate;
    data->channels = dev->config.audio.channels;
    data->format = audio_format_to_alsa(dev->config.audio.format);
    data->is_capture = (dev->type == LWS_DEV_AUDIO_CAPTURE) ? 1 : 0;

    /* 设备名称 */
    if (dev->config.device_name) {
        strncpy(data->device_name, dev->config.device_name, sizeof(data->device_name) - 1);
    } else {
        strncpy(data->device_name, "default", sizeof(data->device_name) - 1);
    }

    /* 打开PCM设备 */
    snd_pcm_stream_t stream = data->is_capture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;
    int err = snd_pcm_open(&data->pcm_handle, data->device_name, stream, 0);
    if (err < 0) {
        lws_log_error(0, "[DEV_LINUX] Cannot open audio device %s: %s\n",
                      data->device_name, snd_strerror(err));
        free(data);
        return -1;
    }

    /* 分配硬件参数结构 */
    snd_pcm_hw_params_malloc(&data->hw_params);

    /* 初始化硬件参数 */
    err = snd_pcm_hw_params_any(data->pcm_handle, data->hw_params);
    if (err < 0) {
        lws_log_error(0, "[DEV_LINUX] Cannot initialize hardware parameters: %s\n",
                      snd_strerror(err));
        snd_pcm_hw_params_free(data->hw_params);
        snd_pcm_close(data->pcm_handle);
        free(data);
        return -1;
    }

    /* 设置访问类型 */
    err = snd_pcm_hw_params_set_access(data->pcm_handle, data->hw_params,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        lws_log_error(0, "[DEV_LINUX] Cannot set access type: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(data->hw_params);
        snd_pcm_close(data->pcm_handle);
        free(data);
        return -1;
    }

    /* 设置格式 */
    err = snd_pcm_hw_params_set_format(data->pcm_handle, data->hw_params, data->format);
    if (err < 0) {
        lws_log_error(0, "[DEV_LINUX] Cannot set format: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(data->hw_params);
        snd_pcm_close(data->pcm_handle);
        free(data);
        return -1;
    }

    /* 设置采样率 */
    err = snd_pcm_hw_params_set_rate_near(data->pcm_handle, data->hw_params,
                                           &data->sample_rate, 0);
    if (err < 0) {
        lws_log_error(0, "[DEV_LINUX] Cannot set sample rate: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(data->hw_params);
        snd_pcm_close(data->pcm_handle);
        free(data);
        return -1;
    }

    /* 设置声道数 */
    err = snd_pcm_hw_params_set_channels(data->pcm_handle, data->hw_params, data->channels);
    if (err < 0) {
        lws_log_error(0, "[DEV_LINUX] Cannot set channel count: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(data->hw_params);
        snd_pcm_close(data->pcm_handle);
        free(data);
        return -1;
    }

    /* 计算period size */
    data->period_size = (data->sample_rate * dev->config.audio.frame_duration_ms) / 1000;

    /* 应用硬件参数 */
    err = snd_pcm_hw_params(data->pcm_handle, data->hw_params);
    if (err < 0) {
        lws_log_error(0, "[DEV_LINUX] Cannot set hardware parameters: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(data->hw_params);
        snd_pcm_close(data->pcm_handle);
        free(data);
        return -1;
    }

    dev->platform_data = data;

    lws_log_info("[DEV_LINUX] Opened audio device: %s (capture=%d, rate=%u, channels=%u)\n",
                 data->device_name, data->is_capture, data->sample_rate, data->channels);

    return 0;
}

static void linux_close(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return;
    }

    lws_dev_linux_data_t* data = (lws_dev_linux_data_t*)dev->platform_data;

    lws_log_info("[DEV_LINUX] Closing audio device: %s\n", data->device_name);

    if (data->hw_params) {
        snd_pcm_hw_params_free(data->hw_params);
        data->hw_params = NULL;
    }

    if (data->pcm_handle) {
        snd_pcm_drain(data->pcm_handle);
        snd_pcm_close(data->pcm_handle);
        data->pcm_handle = NULL;
    }

    free(data);
    dev->platform_data = NULL;
}

static int linux_start(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    lws_dev_linux_data_t* data = (lws_dev_linux_data_t*)dev->platform_data;

    /* 准备PCM */
    int err = snd_pcm_prepare(data->pcm_handle);
    if (err < 0) {
        lws_log_error(0, "[DEV_LINUX] Cannot prepare audio interface: %s\n", snd_strerror(err));
        return -1;
    }

    lws_log_info("[DEV_LINUX] Started audio device: %s\n", data->device_name);

    return 0;
}

static void linux_stop(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return;
    }

    lws_dev_linux_data_t* data = (lws_dev_linux_data_t*)dev->platform_data;

    /* Drop pending frames */
    snd_pcm_drop(data->pcm_handle);

    lws_log_info("[DEV_LINUX] Stopped audio device: %s\n", data->device_name);
}

static int linux_read_audio(lws_dev_t* dev, void* buf, int samples) {
    if (!dev || !dev->platform_data || !buf || samples <= 0) {
        return -1;
    }

    lws_dev_linux_data_t* data = (lws_dev_linux_data_t*)dev->platform_data;

    snd_pcm_sframes_t frames = snd_pcm_readi(data->pcm_handle, buf, samples);
    if (frames < 0) {
        /* 尝试恢复 */
        frames = snd_pcm_recover(data->pcm_handle, frames, 0);
        if (frames < 0) {
            lws_log_error(0, "[DEV_LINUX] Read error: %s\n", snd_strerror(frames));
            return -1;
        }
        return 0;
    }

    return (int)frames;
}

static int linux_write_audio(lws_dev_t* dev, const void* pcm_data, int samples) {
    if (!dev || !dev->platform_data || !pcm_data || samples <= 0) {
        return -1;
    }

    lws_dev_linux_data_t* data = (lws_dev_linux_data_t*)dev->platform_data;

    snd_pcm_sframes_t frames = snd_pcm_writei(data->pcm_handle, pcm_data, samples);
    if (frames < 0) {
        /* 尝试恢复 */
        frames = snd_pcm_recover(data->pcm_handle, frames, 0);
        if (frames < 0) {
            lws_log_error(0, "[DEV_LINUX] Write error: %s\n", snd_strerror(frames));
            return -1;
        }
        return 0;
    }

    return (int)frames;
}

static int linux_get_audio_avail(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    lws_dev_linux_data_t* data = (lws_dev_linux_data_t*)dev->platform_data;

    snd_pcm_sframes_t avail = snd_pcm_avail(data->pcm_handle);
    if (avail < 0) {
        return 0;
    }

    return (int)avail;
}

static int linux_flush_audio(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    lws_dev_linux_data_t* data = (lws_dev_linux_data_t*)dev->platform_data;

    snd_pcm_drop(data->pcm_handle);
    snd_pcm_prepare(data->pcm_handle);

    return 0;
}

static int linux_read_video(lws_dev_t* dev, void* buf, int size) {
    /* Linux视频支持暂未实现 */
    (void)dev;
    (void)buf;
    (void)size;
    return -1;
}

static int linux_write_video(lws_dev_t* dev, const void* data, int size) {
    /* Linux视频支持暂未实现 */
    (void)dev;
    (void)data;
    (void)size;
    return -1;
}

/* ========================================
 * Linux后端ops表
 * ======================================== */

const lws_dev_ops_t lws_dev_linux_ops = {
    .open = linux_open,
    .close = linux_close,
    .start = linux_start,
    .stop = linux_stop,
    .read_audio = linux_read_audio,
    .write_audio = linux_write_audio,
    .get_audio_avail = linux_get_audio_avail,
    .flush_audio = linux_flush_audio,
    .read_video = linux_read_video,
    .write_video = linux_write_video
};

#endif /* __linux__ */
