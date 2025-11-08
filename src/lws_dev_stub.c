/**
 * @file lws_dev_stub.c
 * @brief lwsip embedded device stub implementation
 *
 * 这是一个设备后端的桩实现，用于嵌入式系统。
 * 用户可以根据具体的嵌入式平台修改此文件，实现实际的设备操作。
 *
 * 实现步骤：
 * 1. 在 stub_open() 中初始化你的设备硬件
 * 2. 在 stub_start() 中启动设备（开始采集或播放）
 * 3. 在 stub_read_audio() 中从设备读取音频数据
 * 4. 在 stub_write_audio() 中向设备写入音频数据
 * 5. 在 stub_stop() 中停止设备
 * 6. 在 stub_close() 中关闭并释放设备资源
 *
 * 示例平台：
 * - FreeRTOS + I2S
 * - Zephyr + SAI
 * - RT-Thread + Audio Framework
 */

#ifdef LWS_ENABLE_DEV_STUB

#include <stdlib.h>
#include <string.h>

#include "../include/lws_dev.h"
#include "../include/lws_err.h"
#include "../osal/include/lws_log.h"
#include "lws_dev_intl.h"

/* ========================================
 * 桩后端数据结构
 * ======================================== */

typedef struct {
    /* TODO: 添加你的平台特定数据 */
    /* 例如：
     * - 设备句柄
     * - 缓冲区指针
     * - DMA配置
     * - 中断标志
     */

    int sample_rate;
    int channels;
    int is_capture;

    /* 示例：简单的缓冲区 */
    uint8_t* buffer;
    size_t buffer_size;
} lws_dev_stub_data_t;

/* ========================================
 * 桩后端操作函数实现
 * ======================================== */

/**
 * @brief 打开设备
 *
 * 在此函数中：
 * 1. 初始化设备硬件（I2S、SAI等）
 * 2. 配置采样率、声道数、位深度
 * 3. 分配缓冲区
 * 4. 配置DMA（如果使用）
 */
static int stub_open(lws_dev_t* dev) {
    if (!dev) {
        return -1;
    }

    lws_log_warn("[DEV_STUB] Using stub device backend - implement platform-specific code!\n");

    lws_dev_stub_data_t* data = (lws_dev_stub_data_t*)malloc(sizeof(lws_dev_stub_data_t));
    if (!data) {
        lws_log_error(0, "[DEV_STUB] Failed to allocate stub data\n");
        return -1;
    }

    memset(data, 0, sizeof(lws_dev_stub_data_t));

    /* 保存参数 */
    data->sample_rate = dev->config.audio.sample_rate;
    data->channels = dev->config.audio.channels;
    data->is_capture = (dev->type == LWS_DEV_AUDIO_CAPTURE) ? 1 : 0;

    /* TODO: 初始化你的设备硬件 */
    /* 例如：
     * - i2s_init(&i2s_config);
     * - codec_init(&codec_config);
     * - dma_init(&dma_config);
     */

    /* 分配缓冲区（示例） */
    data->buffer_size = data->sample_rate * 2 * data->channels;  /* 1秒 16-bit */
    data->buffer = (uint8_t*)malloc(data->buffer_size);
    if (!data->buffer) {
        lws_log_error(0, "[DEV_STUB] Failed to allocate buffer\n");
        free(data);
        return -1;
    }

    dev->platform_data = data;

    lws_log_info("[DEV_STUB] Opened stub device (capture=%d, rate=%d, channels=%d)\n",
                 data->is_capture, data->sample_rate, data->channels);

    return 0;
}

/**
 * @brief 关闭设备
 *
 * 在此函数中：
 * 1. 停止设备（如果还在运行）
 * 2. 释放缓冲区
 * 3. 关闭设备硬件
 */
static void stub_close(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return;
    }

    lws_dev_stub_data_t* data = (lws_dev_stub_data_t*)dev->platform_data;

    lws_log_info("[DEV_STUB] Closing stub device\n");

    /* TODO: 关闭你的设备硬件 */
    /* 例如：
     * - i2s_deinit();
     * - codec_deinit();
     * - dma_deinit();
     */

    /* 释放缓冲区 */
    if (data->buffer) {
        free(data->buffer);
        data->buffer = NULL;
    }

    free(data);
    dev->platform_data = NULL;
}

/**
 * @brief 启动设备
 *
 * 在此函数中：
 * 1. 启动设备（开始采集或播放）
 * 2. 启动DMA传输（如果使用）
 * 3. 使能中断（如果使用）
 */
static int stub_start(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    lws_dev_stub_data_t* data = (lws_dev_stub_data_t*)dev->platform_data;

    /* TODO: 启动你的设备 */
    /* 例如：
     * if (data->is_capture) {
     *     i2s_start_rx();
     * } else {
     *     i2s_start_tx();
     * }
     */

    lws_log_info("[DEV_STUB] Started stub device\n");

    (void)data;  /* 避免未使用警告 */

    return 0;
}

/**
 * @brief 停止设备
 *
 * 在此函数中：
 * 1. 停止设备
 * 2. 停止DMA传输（如果使用）
 * 3. 禁用中断（如果使用）
 */
static void stub_stop(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return;
    }

    lws_dev_stub_data_t* data = (lws_dev_stub_data_t*)dev->platform_data;

    /* TODO: 停止你的设备 */
    /* 例如：
     * i2s_stop();
     * dma_stop();
     */

    lws_log_info("[DEV_STUB] Stopped stub device\n");

    (void)data;  /* 避免未使用警告 */
}

/**
 * @brief 读取音频数据（采集）
 *
 * 在此函数中：
 * 1. 从设备读取指定数量的采样
 * 2. 返回实际读取的采样数
 *
 * @param dev 设备实例
 * @param buf 输出缓冲区
 * @param samples 期望读取的采样数
 * @return 实际读取的采样数，-1表示失败
 */
static int stub_read_audio(lws_dev_t* dev, void* buf, int samples) {
    if (!dev || !dev->platform_data || !buf || samples <= 0) {
        return -1;
    }

    lws_dev_stub_data_t* data = (lws_dev_stub_data_t*)dev->platform_data;

    /* TODO: 从你的设备读取音频数据 */
    /* 例如：
     * int bytes_per_sample = 2 * data->channels;  // 假设16-bit
     * int bytes_to_read = samples * bytes_per_sample;
     * int bytes_read = i2s_read(buf, bytes_to_read, timeout_ms);
     * return bytes_read / bytes_per_sample;
     */

    /* 桩实现：填充静音 */
    int bytes_per_sample = 2 * data->channels;
    int bytes = samples * bytes_per_sample;
    memset(buf, 0, bytes);

    return samples;
}

/**
 * @brief 写入音频数据（播放）
 *
 * 在此函数中：
 * 1. 向设备写入指定数量的采样
 * 2. 返回实际写入的采样数
 *
 * @param dev 设备实例
 * @param data 音频数据
 * @param samples 采样数
 * @return 实际写入的采样数，-1表示失败
 */
static int stub_write_audio(lws_dev_t* dev, const void* pcm_data, int samples) {
    if (!dev || !dev->platform_data || !pcm_data || samples <= 0) {
        return -1;
    }

    lws_dev_stub_data_t* data = (lws_dev_stub_data_t*)dev->platform_data;

    /* TODO: 向你的设备写入音频数据 */
    /* 例如：
     * int bytes_per_sample = 2 * data->channels;  // 假设16-bit
     * int bytes_to_write = samples * bytes_per_sample;
     * int bytes_written = i2s_write(pcm_data, bytes_to_write, timeout_ms);
     * return bytes_written / bytes_per_sample;
     */

    /* 桩实现：丢弃数据 */
    (void)data;
    (void)pcm_data;

    return samples;
}

/**
 * @brief 获取音频缓冲区可用空间
 *
 * @param dev 设备实例
 * @return 可用采样数，-1表示失败
 */
static int stub_get_audio_avail(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    /* TODO: 返回实际的缓冲区可用空间 */
    /* 例如：
     * lws_dev_stub_data_t* data = (lws_dev_stub_data_t*)dev->platform_data;
     * int bytes_avail = i2s_get_tx_buffer_available();
     * return bytes_avail / (2 * data->channels);
     */

    /* 桩实现：返回较大值 */
    return 4096;
}

/**
 * @brief 清空音频缓冲区
 *
 * @param dev 设备实例
 * @return 0成功，-1失败
 */
static int stub_flush_audio(lws_dev_t* dev) {
    if (!dev || !dev->platform_data) {
        return -1;
    }

    /* TODO: 清空你的设备缓冲区 */
    /* 例如：
     * i2s_flush();
     */

    return 0;
}

/**
 * @brief 读取视频帧（暂未实现）
 */
static int stub_read_video(lws_dev_t* dev, void* buf, int size) {
    (void)dev;
    (void)buf;
    (void)size;
    lws_log_error(0, "[DEV_STUB] Video not supported\n");
    return -1;
}

/**
 * @brief 写入视频帧（暂未实现）
 */
static int stub_write_video(lws_dev_t* dev, const void* data, int size) {
    (void)dev;
    (void)data;
    (void)size;
    lws_log_error(0, "[DEV_STUB] Video not supported\n");
    return -1;
}

/* ========================================
 * 桩后端ops表
 * ======================================== */

const lws_dev_ops_t lws_dev_stub_ops = {
    .open = stub_open,
    .close = stub_close,
    .start = stub_start,
    .stop = stub_stop,
    .read_audio = stub_read_audio,
    .write_audio = stub_write_audio,
    .get_audio_avail = stub_get_audio_avail,
    .flush_audio = stub_flush_audio,
    .read_video = stub_read_video,
    .write_video = stub_write_video
};

#endif /* LWS_ENABLE_DEV_STUB */
