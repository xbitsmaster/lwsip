/**
 * @file lws_rtp.h
 * @brief lwsip RTP/RTCP protocol wrapper
 *
 * lws RTP层基于librtp实现，保持协议处理的纯粹性：
 * - 被动输入模型（与librtp一致）
 * - RTP打包/解包
 * - RTCP报告生成（SR/RR）
 * - 统计信息维护（丢包率、抖动等）
 * - 通过回调传递解包后的媒体帧
 */

#ifndef __LWS_RTP_H__
#define __LWS_RTP_H__

#include <stdint.h>
#include <stddef.h>
#include "lws_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================
 * 类型定义
 * ======================================== */

/**
 * @brief RTP编解码类型
 */
typedef enum {
    LWS_RTP_PAYLOAD_PCMU = 0,       /**< G.711 μ-law */
    LWS_RTP_PAYLOAD_PCMA = 8,       /**< G.711 A-law */
    LWS_RTP_PAYLOAD_G722 = 9,       /**< G.722 */
    LWS_RTP_PAYLOAD_L16_2 = 10,     /**< L16 stereo */
    LWS_RTP_PAYLOAD_L16_1 = 11,     /**< L16 mono */
    LWS_RTP_PAYLOAD_OPUS = 96,      /**< Opus (dynamic) */
    LWS_RTP_PAYLOAD_H264 = 97,      /**< H.264 (dynamic) */
    LWS_RTP_PAYLOAD_H265 = 98,      /**< H.265 (dynamic) */
    LWS_RTP_PAYLOAD_VP8 = 99,       /**< VP8 (dynamic) */
    LWS_RTP_PAYLOAD_VP9 = 100       /**< VP9 (dynamic) */
} lws_rtp_payload_t;

/**
 * @brief RTP媒体类型
 */
typedef enum {
    LWS_RTP_MEDIA_AUDIO,            /**< 音频 */
    LWS_RTP_MEDIA_VIDEO             /**< 视频 */
} lws_rtp_media_type_t;

/* 前向声明 */
typedef struct lws_rtp_t lws_rtp_t;

/**
 * @brief RTP统计信息
 */
typedef struct {
    /* 发送统计 */
    uint64_t sent_packets;          /**< 发送的包数 */
    uint64_t sent_bytes;            /**< 发送的字节数 */
    uint32_t sent_timestamp;        /**< 最后发送的RTP时间戳 */

    /* 接收统计 */
    uint64_t recv_packets;          /**< 接收的包数 */
    uint64_t recv_bytes;            /**< 接收的字节数 */
    uint32_t recv_timestamp;        /**< 最后接收的RTP时间戳 */

    /* 丢包统计 */
    uint64_t lost_packets;          /**< 丢失的包数 */
    double loss_rate;               /**< 丢包率（0.0-1.0） */

    /* 抖动统计 */
    uint32_t jitter;                /**< 抖动（RTP时间戳单位） */

    /* RTCP统计 */
    uint64_t rtcp_sent;             /**< 发送的RTCP包数 */
    uint64_t rtcp_recv;             /**< 接收的RTCP包数 */
} lws_rtp_stats_t;

/**
 * @brief RTCP接收报告
 */
typedef struct {
    uint32_t ssrc;                  /**< 源SSRC */
    uint8_t fraction_lost;          /**< 丢包率 */
    uint32_t packets_lost;          /**< 累计丢包数 */
    uint32_t highest_seq;           /**< 最高序列号 */
    uint32_t jitter;                /**< 抖动 */
    uint32_t lsr;                   /**< Last SR timestamp */
    uint32_t dlsr;                  /**< Delay since last SR */
} lws_rtcp_rr_t;

/* ========================================
 * 回调函数
 * ======================================== */

/**
 * @brief 接收到音频帧回调
 * @param rtp RTP实例
 * @param data 音频数据
 * @param samples 采样数
 * @param timestamp RTP时间戳
 * @param userdata 用户数据
 */
typedef void (*lws_rtp_on_audio_frame_f)(
    lws_rtp_t* rtp,
    const void* data,
    int samples,
    uint32_t timestamp,
    void* userdata
);

/**
 * @brief 接收到视频帧回调
 * @param rtp RTP实例
 * @param data 视频数据
 * @param size 数据大小
 * @param timestamp RTP时间戳
 * @param is_keyframe 是否关键帧
 * @param userdata 用户数据
 */
typedef void (*lws_rtp_on_video_frame_f)(
    lws_rtp_t* rtp,
    const void* data,
    int size,
    uint32_t timestamp,
    int is_keyframe,
    void* userdata
);

/**
 * @brief 接收到RTCP报告回调
 * @param rtp RTP实例
 * @param rr 接收报告
 * @param userdata 用户数据
 */
typedef void (*lws_rtp_on_rtcp_report_f)(
    lws_rtp_t* rtp,
    const lws_rtcp_rr_t* rr,
    void* userdata
);

/**
 * @brief RTP错误回调
 * @param rtp RTP实例
 * @param error_code 错误码
 * @param error_msg 错误消息
 * @param userdata 用户数据
 */
typedef void (*lws_rtp_on_error_f)(
    lws_rtp_t* rtp,
    int error_code,
    const char* error_msg,
    void* userdata
);

/**
 * @brief RTP事件回调集合
 */
typedef struct {
    lws_rtp_on_audio_frame_f on_audio_frame;       /**< 音频帧回调 */
    lws_rtp_on_video_frame_f on_video_frame;       /**< 视频帧回调 */
    lws_rtp_on_rtcp_report_f on_rtcp_report;       /**< RTCP报告回调 */
    lws_rtp_on_error_f on_error;                   /**< 错误回调 */
    void* userdata;                                 /**< 用户数据 */
} lws_rtp_handler_t;

/* ========================================
 * 配置
 * ======================================== */

/**
 * @brief RTP配置
 */
typedef struct {
    lws_rtp_media_type_t media_type;    /**< 媒体类型 */
    lws_rtp_payload_t payload_type;     /**< 编解码类型 */

    /* 音频参数 */
    int sample_rate;                    /**< 采样率（音频） */
    int channels;                       /**< 声道数（音频） */
    int frame_duration_ms;              /**< 帧时长（音频，毫秒） */

    /* 视频参数 */
    int clock_rate;                     /**< 时钟频率（视频） */
    int max_packet_size;                /**< 最大包大小 */

    /* SSRC */
    uint32_t ssrc;                      /**< 本地SSRC（0=自动生成） */

    /* RTCP */
    int enable_rtcp;                    /**< 启用RTCP */
    int rtcp_interval_ms;               /**< RTCP发送间隔（毫秒） */

    /* 抖动缓冲区（接收端） */
    int jitter_buffer_ms;               /**< 抖动缓冲区大小（毫秒） */
    int jitter_buffer_max_packets;      /**< 抖动缓冲区最大包数 */
} lws_rtp_config_t;

/* ========================================
 * 核心API
 * ======================================== */

/**
 * @brief 创建RTP实例
 * @param config 配置
 * @param handler 回调函数
 * @return RTP实例，失败返回NULL
 */
lws_rtp_t* lws_rtp_create(
    const lws_rtp_config_t* config,
    const lws_rtp_handler_t* handler
);

/**
 * @brief 销毁RTP实例
 * @param rtp RTP实例
 */
void lws_rtp_destroy(lws_rtp_t* rtp);

/**
 * @brief 设置远端SSRC
 * @param rtp RTP实例
 * @param ssrc 远端SSRC
 * @return 0成功，-1失败
 */
int lws_rtp_set_remote_ssrc(lws_rtp_t* rtp, uint32_t ssrc);

/* ========================================
 * 发送API
 * ======================================== */

/**
 * @brief 发送音频数据
 * @param rtp RTP实例
 * @param data 音频数据
 * @param samples 采样数
 * @param timestamp RTP时间戳
 * @return 0成功，-1失败
 */
int lws_rtp_send_audio(
    lws_rtp_t* rtp,
    const void* data,
    int samples,
    uint32_t timestamp
);

/**
 * @brief 发送视频数据
 * @param rtp RTP实例
 * @param data 视频数据
 * @param size 数据大小
 * @param timestamp RTP时间戳
 * @param is_keyframe 是否关键帧
 * @return 0成功，-1失败
 */
int lws_rtp_send_video(
    lws_rtp_t* rtp,
    const void* data,
    int size,
    uint32_t timestamp,
    int is_keyframe
);

/**
 * @brief 获取打包后的RTP包（由应用层发送）
 *
 * 被动输出模型：RTP层打包完成后，应用层调用此函数获取包并发送
 *
 * @param rtp RTP实例
 * @param buf 输出缓冲区
 * @param size 缓冲区大小
 * @return 实际大小，0表示无包，-1失败
 */
int lws_rtp_get_packet(lws_rtp_t* rtp, void* buf, int size);

/* ========================================
 * 接收API
 * ======================================== */

/**
 * @brief 输入接收的RTP/RTCP包
 *
 * 被动输入模型：应用层接收到RTP/RTCP包后，通过此函数输入到RTP层
 *
 * @param rtp RTP实例
 * @param data 数据指针
 * @param len 数据长度
 * @return 0成功，-1失败
 */
int lws_rtp_input(lws_rtp_t* rtp, const void* data, int len);

/* ========================================
 * RTCP API
 * ======================================== */

/**
 * @brief 获取下次RTCP发送间隔（毫秒）
 * @param rtp RTP实例
 * @return 剩余毫秒数，0表示需要立即发送
 */
int lws_rtp_get_rtcp_interval(lws_rtp_t* rtp);

/**
 * @brief 生成RTCP报告（SR/RR）
 * @param rtp RTP实例
 * @param buf 输出缓冲区
 * @param size 缓冲区大小
 * @return 实际大小，0表示无报告，-1失败
 */
int lws_rtp_generate_rtcp(lws_rtp_t* rtp, void* buf, int size);

/* ========================================
 * 统计API
 * ======================================== */

/**
 * @brief 获取RTP统计信息
 * @param rtp RTP实例
 * @param stats 输出统计信息
 * @return 0成功，-1失败
 */
int lws_rtp_get_stats(lws_rtp_t* rtp, lws_rtp_stats_t* stats);

/**
 * @brief 重置统计信息
 * @param rtp RTP实例
 * @return 0成功，-1失败
 */
int lws_rtp_reset_stats(lws_rtp_t* rtp);

/**
 * @brief 获取本地SSRC
 * @param rtp RTP实例
 * @return 本地SSRC
 */
uint32_t lws_rtp_get_local_ssrc(lws_rtp_t* rtp);

/**
 * @brief 获取远端SSRC
 * @param rtp RTP实例
 * @return 远端SSRC
 */
uint32_t lws_rtp_get_remote_ssrc(lws_rtp_t* rtp);

/**
 * @brief 获取当前RTP序列号
 * @param rtp RTP实例
 * @return RTP序列号
 */
uint16_t lws_rtp_get_sequence(lws_rtp_t* rtp);

/**
 * @brief 获取当前RTP时间戳
 * @param rtp RTP实例
 * @return RTP时间戳
 */
uint32_t lws_rtp_get_timestamp(lws_rtp_t* rtp);

/* ========================================
 * 辅助函数
 * ======================================== */

/**
 * @brief 初始化音频RTP配置（默认值）
 * @param config 配置结构体
 * @param payload_type 编解码类型
 * @param sample_rate 采样率
 */
void lws_rtp_init_audio_config(
    lws_rtp_config_t* config,
    lws_rtp_payload_t payload_type,
    int sample_rate
);

/**
 * @brief 初始化视频RTP配置（默认值）
 * @param config 配置结构体
 * @param payload_type 编解码类型
 * @param clock_rate 时钟频率
 */
void lws_rtp_init_video_config(
    lws_rtp_config_t* config,
    lws_rtp_payload_t payload_type,
    int clock_rate
);

/**
 * @brief 获取payload类型名称
 * @param payload_type Payload类型
 * @return 类型名称字符串
 */
const char* lws_rtp_payload_name(lws_rtp_payload_t payload_type);

/**
 * @brief 字符串转payload类型
 * @param name 类型名称
 * @return Payload类型，-1表示无效
 */
lws_rtp_payload_t lws_rtp_payload_from_name(const char* name);

/**
 * @brief 计算音频采样时长（毫秒）
 * @param samples 采样数
 * @param sample_rate 采样率
 * @return 时长（毫秒）
 */
int lws_rtp_calc_audio_duration(int samples, int sample_rate);

/**
 * @brief 计算RTP时间戳增量
 * @param duration_ms 时长（毫秒）
 * @param clock_rate 时钟频率
 * @return 时间戳增量
 */
uint32_t lws_rtp_calc_timestamp_delta(int duration_ms, int clock_rate);

/**
 * @brief 检测RTP包类型（RTP或RTCP）
 * @param data 数据指针
 * @param len 数据长度
 * @return 1=RTP, 2=RTCP, 0=无效
 */
int lws_rtp_detect_packet_type(const void* data, int len);

#ifdef __cplusplus
}
#endif

#endif // __LWS_RTP_H__
