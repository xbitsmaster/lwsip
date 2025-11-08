/**
 * @file lws_defs.h
 * @brief lwsip definitions and constants
 *
 * 定义所有lwsip模块共享的常量、类型和宏
 */

#ifndef __LWS_DEFS_H__
#define __LWS_DEFS_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================
 * 版本信息
 * ======================================== */

#define LWS_VERSION_MAJOR       3
#define LWS_VERSION_MINOR       0
#define LWS_VERSION_PATCH       0
#define LWS_VERSION_STRING      "3.0.0"

/* ========================================
 * 网络相关常量
 * ======================================== */

#define LWS_MAX_IP_LEN          64      /**< IP地址最大长度 (支持IPv6) */
#define LWS_MAX_HOSTNAME_LEN    256     /**< 主机名最大长度 */
#define LWS_MAX_DOMAIN_LEN      256     /**< 域名最大长度 */
#define LWS_DEFAULT_PORT        5060    /**< SIP默认端口 */

/* ========================================
 * SIP相关常量
 * ======================================== */

#define LWS_MAX_USERNAME_LEN    64      /**< 用户名最大长度 */
#define LWS_MAX_PASSWORD_LEN    64     /**< 密码最大长度 */
#define LWS_MAX_REALM_LEN       256     /**< Realm最大长度 */
#define LWS_MAX_DISPLAY_NAME_LEN 64     /**< 显示名称最大长度 */
#define LWS_MAX_NICKNAME_LEN    64    /**< 昵称最大长度 */
#define LWS_MAX_USER_AGENT_LEN  128     /**< User-Agent最大长度 */
#define LWS_MAX_CALL_ID_LEN     128     /**< Call-ID最大长度 */
#define LWS_MAX_TAG_LEN         64      /**< Tag最大长度 */
#define LWS_MAX_URI_LEN         512     /**< SIP URI最大长度 */

#define LWS_DEFAULT_REGISTER_EXPIRES  3600  /**< 默认注册过期时间(秒) */

/* ========================================
 * ICE相关常量
 * ======================================== */

#define LWS_MAX_FOUNDATION_LEN  33      /**< ICE foundation最大长度 */
#define LWS_MAX_UFRAG_LEN       256     /**< ICE ufrag最大长度 */
#define LWS_MAX_PWD_LEN         256     /**< ICE pwd最大长度 */
#define LWS_MAX_CANDIDATES      16      /**< 最大candidate数量 */

#define LWS_ICE_UFRAG_LEN       8       /**< ICE ufrag生成长度 */
#define LWS_ICE_PWD_LEN         24      /**< ICE pwd生成长度 */

#define LWS_DEFAULT_STUN_PORT   3478    /**< STUN默认端口 */
#define LWS_DEFAULT_TURN_PORT   3478    /**< TURN默认端口 */

/* ========================================
 * MQTT相关常量
 * ======================================== */

#define LWS_MAX_CLIENT_ID_LEN   128     /**< MQTT client ID最大长度 */
#define LWS_MAX_TOPIC_LEN       256     /**< MQTT topic最大长度 */
#define LWS_DEFAULT_MQTT_PORT   1883    /**< MQTT默认端口 */

/* ========================================
 * 设备相关常量
 * ======================================== */

#define LWS_MAX_DEV_NAME_LEN    64      /**< 设备名称最大长度 */
#define LWS_MAX_DEV_ID_LEN      128     /**< 设备ID最大长度 */
#define LWS_MAX_CODEC_NAME_LEN  32      /**< 编解码名称最大长度 */

/* 音频相关 */
#define LWS_DEFAULT_SAMPLE_RATE     8000    /**< 默认采样率 */
#define LWS_DEFAULT_CHANNELS        1       /**< 默认声道数 */
#define LWS_DEFAULT_FRAME_DURATION  20      /**< 默认帧时长(毫秒) */
#define LWS_MAX_AUDIO_FRAME_SIZE    1024    /**< 最大音频帧大小(采样数) */
#define LWS_MAX_AUDIO_SAMPLES_20MS  960     /**< 20ms最大采样数 (48kHz) */

/* 视频相关 */
#define LWS_DEFAULT_VIDEO_WIDTH     640     /**< 默认视频宽度 */
#define LWS_DEFAULT_VIDEO_HEIGHT    480     /**< 默认视频高度 */
#define LWS_DEFAULT_VIDEO_FPS       30      /**< 默认视频帧率 */
#define LWS_MAX_VIDEO_FRAME_SIZE    (1024*1024)  /**< 最大视频帧大小(字节) */

/* ========================================
 * RTP相关常量
 * ======================================== */

#define LWS_DEFAULT_RTP_CLOCK_RATE      90000   /**< 默认RTP时钟频率(视频) */
#define LWS_DEFAULT_RTCP_INTERVAL       5000    /**< 默认RTCP间隔(毫秒) */
#define LWS_DEFAULT_JITTER_BUFFER_MS    50      /**< 默认抖动缓冲区(毫秒) */
#define LWS_MAX_JITTER_PACKETS          100     /**< 最大抖动缓冲区包数 */
#define LWS_MAX_RTP_PACKET_SIZE         1500    /**< 最大RTP包大小 */

/* ========================================
 * 缓冲区大小
 * ======================================== */

#define LWS_SMALL_BUF_SIZE      128     /**< 小缓冲区 */
#define LWS_MEDIUM_BUF_SIZE     512     /**< 中等缓冲区 */
#define LWS_LARGE_BUF_SIZE      2048    /**< 大缓冲区 */
#define LWS_HUGE_BUF_SIZE       8192    /**< 超大缓冲区 */

#define LWS_DEFAULT_RECV_BUF    (64*1024)   /**< 默认接收缓冲区 */
#define LWS_DEFAULT_SEND_BUF    (64*1024)   /**< 默认发送缓冲区 */

/* ========================================
 * 超时设置
 * ======================================== */

#define LWS_DEFAULT_TIMEOUT_MS          2000    /**< 默认超时(毫秒) */
#define LWS_DEFAULT_CONNECT_TIMEOUT_MS  5000    /**< 默认连接超时(毫秒) */
#define LWS_DEFAULT_GATHERING_TIMEOUT_MS 5000   /**< 默认gathering超时(毫秒) */
#define LWS_DEFAULT_CONNECTIVITY_TIMEOUT_MS 30000  /**< 默认connectivity超时(毫秒) */
#define LWS_DEFAULT_KEEPALIVE_INTERVAL_MS  15000   /**< 默认keepalive间隔(毫秒) */

/* ========================================
 * 错误码
 * ======================================== */

#define LWS_OK                  0       /**< 成功 */
#define LWS_ERROR              -1       /**< 一般错误 */
#define LWS_EINVAL             -2       /**< 无效参数 */
#define LWS_ENOMEM             -3       /**< 内存不足 */
#define LWS_ETIMEOUT           -4       /**< 超时 */
#define LWS_ENOTCONN           -5       /**< 未连接 */
#define LWS_ECONNREFUSED       -6       /**< 连接被拒绝 */
#define LWS_ECONNRESET         -7       /**< 连接重置 */
#define LWS_EAGAIN             -8       /**< 需要重试 */
#define LWS_ENOTSUP            -9       /**< 不支持 */
#define LWS_EBUSY              -10      /**< 设备忙 */
#define LWS_ENODEV             -11      /**< 设备不存在 */

/* ========================================
 * 通用宏
 * ======================================== */

/** 最小值 */
#ifndef LWS_MIN
#define LWS_MIN(a, b)           ((a) < (b) ? (a) : (b))
#endif

/** 最大值 */
#ifndef LWS_MAX
#define LWS_MAX(a, b)           ((a) > (b) ? (a) : (b))
#endif

/** 数组元素个数 */
#ifndef LWS_ARRAY_SIZE
#define LWS_ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))
#endif

/** 未使用的参数 */
#ifndef LWS_UNUSED
#define LWS_UNUSED(x)           (void)(x)
#endif

/** 字符串安全复制 */
#ifndef LWS_STRNCPY
#define LWS_STRNCPY(dst, src, size) do { \
    strncpy((dst), (src), (size) - 1); \
    (dst)[(size) - 1] = '\0'; \
} while (0)
#endif

/* ========================================
 * 通用类型定义
 * ======================================== */

/**
 * @brief 通用回调函数
 */
typedef void (*lws_callback_f)(void* userdata);

/**
 * @brief 布尔类型（如果没有stdbool.h）
 */
#ifndef __cplusplus
#ifndef bool
typedef int bool;
#define true  1
#define false 0
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif // __LWS_DEFS_H__
