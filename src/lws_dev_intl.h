/**
 * @file lws_dev_intl.h
 * @brief lwsip device internal definitions (shared among backend implementations)
 */

#ifndef __LWS_DEV_INTL_H__
#define __LWS_DEV_INTL_H__

#include "lws_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================
 * 操作函数表（虚函数表）
 * ======================================== */

/**
 * @brief 设备操作函数表
 */
typedef struct lws_dev_ops_t {
    int (*open)(lws_dev_t* dev);
    void (*close)(lws_dev_t* dev);
    int (*start)(lws_dev_t* dev);
    void (*stop)(lws_dev_t* dev);

    /* 音频操作 */
    int (*read_audio)(lws_dev_t* dev, void* buf, int samples);
    int (*write_audio)(lws_dev_t* dev, const void* data, int samples);
    int (*get_audio_avail)(lws_dev_t* dev);
    int (*flush_audio)(lws_dev_t* dev);

    /* 视频操作 */
    int (*read_video)(lws_dev_t* dev, void* buf, int size);
    int (*write_video)(lws_dev_t* dev, const void* data, int size);
} lws_dev_ops_t;

/* ========================================
 * 设备实例结构体
 * ======================================== */

struct lws_dev_t {
    lws_dev_type_t type;
    lws_dev_state_t state;
    lws_dev_config_t config;
    lws_dev_handler_t handler;

    /* 操作函数表 */
    const lws_dev_ops_t* ops;

    /* 平台特定数据 */
    void* platform_data;

    /* 设备名称 */
    char device_name[128];

    /* 时间戳基准 */
    uint64_t start_timestamp_us;
};

#ifdef __cplusplus
}
#endif

#endif /* __LWS_DEV_INTL_H__ */
