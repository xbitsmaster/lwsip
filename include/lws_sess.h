/**
 * @file lws_sess.h
 * @brief lwsip media session coordination layer
 *
 * lws_sess是lwsip的核心创新层，协调ICE、RTP、Dev三层：
 * - ICE流程协调（candidate收集 → 连接性检查 → 选择最优路径）
 * - RTP会话管理（RTP打包/解包、RTCP定时发送）
 * - 设备协调（从Dev层采集数据 → 发送；接收数据 → 送Dev播放）
 * - 会话状态管理（IDLE → GATHERING → CONNECTING → CONNECTED）
 * - SDP自动生成（包含ICE candidates和RTP编解码信息）
 */

#ifndef __LWS_SESS_H__
#define __LWS_SESS_H__

#include <stdint.h>
#include <stddef.h>
#include "lws_defs.h"
#include "lws_dev.h"

/* Forward declarations for types (we use librtp/libice directly) */
typedef struct lws_rtp_t lws_rtp_t;
typedef struct lws_ice_t lws_ice_t;

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
 * @brief 会话状态
 */
typedef enum {
    LWS_SESS_STATE_IDLE,            /**< 空闲 */
    LWS_SESS_STATE_GATHERING,       /**< 正在收集ICE candidates */
    LWS_SESS_STATE_GATHERED,        /**< Candidates收集完成，SDP就绪 */
    LWS_SESS_STATE_CONNECTING,      /**< 正在建立ICE连接 */
    LWS_SESS_STATE_CONNECTED,       /**< 媒体已连接 */
    LWS_SESS_STATE_DISCONNECTED,    /**< 媒体已断开 */
    LWS_SESS_STATE_CLOSED           /**< 已关闭 */
} lws_sess_state_t;

/**
 * @brief 媒体方向
 */
typedef enum {
    LWS_MEDIA_DIR_SENDONLY,         /**< 只发送 */
    LWS_MEDIA_DIR_RECVONLY,         /**< 只接收 */
    LWS_MEDIA_DIR_SENDRECV,         /**< 收发 */
    LWS_MEDIA_DIR_INACTIVE          /**< 不活动 */
} lws_media_dir_t;

/* 前向声明 */
typedef struct lws_sess_t lws_sess_t;

/**
 * @brief 会话统计信息
 */
typedef struct {
    lws_sess_state_t state;         /**< 会话状态 */
    lws_rtp_stats_t audio_stats;    /**< 音频RTP统计 */
    lws_rtp_stats_t video_stats;    /**< 视频RTP统计 */
    uint64_t start_time;            /**< 会话开始时间（微秒） */
    uint64_t duration;              /**< 会话时长（微秒） */
} lws_sess_stats_t;

/* ========================================
 * 回调函数
 * ======================================== */

/**
 * @brief 会话状态变化回调
 * @param sess 会话实例
 * @param old_state 旧状态
 * @param new_state 新状态
 * @param userdata 用户数据
 */
typedef void (*lws_sess_on_state_changed_f)(
    lws_sess_t* sess,
    lws_sess_state_t old_state,
    lws_sess_state_t new_state,
    void* userdata
);

/**
 * @brief SDP就绪回调（candidates收集完成）
 * @param sess 会话实例
 * @param sdp SDP字符串
 * @param userdata 用户数据
 */
typedef void (*lws_sess_on_sdp_ready_f)(
    lws_sess_t* sess,
    const char* sdp,
    void* userdata
);

/**
 * @brief 新candidate发现回调（trickle ICE）
 * @param sess 会话实例
 * @param candidate Candidate字符串（SDP格式）
 * @param userdata 用户数据
 */
typedef void (*lws_sess_on_candidate_f)(
    lws_sess_t* sess,
    const char* candidate,
    void* userdata
);

/**
 * @brief 媒体连接建立回调
 * @param sess 会话实例
 * @param userdata 用户数据
 */
typedef void (*lws_sess_on_connected_f)(
    lws_sess_t* sess,
    void* userdata
);

/**
 * @brief 媒体连接断开回调
 * @param sess 会话实例
 * @param reason 断开原因
 * @param userdata 用户数据
 */
typedef void (*lws_sess_on_disconnected_f)(
    lws_sess_t* sess,
    const char* reason,
    void* userdata
);

/**
 * @brief 会话错误回调
 * @param sess 会话实例
 * @param error_code 错误码
 * @param error_msg 错误消息
 * @param userdata 用户数据
 */
typedef void (*lws_sess_on_error_f)(
    lws_sess_t* sess,
    int error_code,
    const char* error_msg,
    void* userdata
);

/**
 * @brief 会话事件回调集合
 */
typedef struct {
    lws_sess_on_state_changed_f on_state_changed;   /**< 状态变化回调 */
    lws_sess_on_sdp_ready_f on_sdp_ready;           /**< SDP就绪回调 */
    lws_sess_on_candidate_f on_candidate;           /**< 新candidate回调 */
    lws_sess_on_connected_f on_connected;           /**< 连接建立回调 */
    lws_sess_on_disconnected_f on_disconnected;     /**< 连接断开回调 */
    lws_sess_on_error_f on_error;                   /**< 错误回调 */
    void* userdata;                                  /**< 用户数据 */
} lws_sess_handler_t;

/* ========================================
 * 配置
 * ======================================== */

/**
 * @brief 会话配置
 */
typedef struct {
    /* ICE配置 */
    const char* stun_server;        /**< STUN服务器地址 */
    uint16_t stun_port;             /**< STUN端口 */
    const char* turn_server;        /**< TURN服务器地址（可选） */
    uint16_t turn_port;             /**< TURN端口 */
    const char* turn_username;      /**< TURN用户名 */
    const char* turn_password;      /**< TURN密码 */
    int trickle_ice;                /**< 启用trickle ICE */

    /* 音频配置 */
    int enable_audio;               /**< 启用音频 */
    lws_rtp_payload_t audio_codec;  /**< 音频编解码 */
    int audio_sample_rate;          /**< 音频采样率 */
    int audio_channels;             /**< 音频声道数 */
    lws_dev_t* audio_capture_dev;   /**< 音频采集设备 */
    lws_dev_t* audio_playback_dev;  /**< 音频播放设备 */

    /* 视频配置 */
    int enable_video;               /**< 启用视频 */
    lws_rtp_payload_t video_codec;  /**< 视频编解码 */
    int video_width;                /**< 视频宽度 */
    int video_height;               /**< 视频高度 */
    int video_fps;                  /**< 视频帧率 */
    lws_dev_t* video_capture_dev;   /**< 视频采集设备 */
    lws_dev_t* video_display_dev;   /**< 视频显示设备 */

    /* 媒体方向 */
    lws_media_dir_t media_dir;      /**< 媒体方向 */

    /* RTCP */
    int enable_rtcp;                /**< 启用RTCP */

    /* 抖动缓冲区 */
    int jitter_buffer_ms;           /**< 抖动缓冲区大小（毫秒） */
} lws_sess_config_t;

/* ========================================
 * 核心API
 * ======================================== */

/**
 * @brief 创建会话实例
 * @param config 配置
 * @param handler 回调函数
 * @return 会话实例，失败返回NULL
 */
lws_sess_t* lws_sess_create(
    const lws_sess_config_t* config,
    const lws_sess_handler_t* handler
);

/**
 * @brief 销毁会话实例
 * @param sess 会话实例
 */
void lws_sess_destroy(lws_sess_t* sess);

/**
 * @brief 开始收集ICE candidates
 * @param sess 会话实例
 * @return 0成功，-1失败
 */
int lws_sess_gather_candidates(lws_sess_t* sess);

/**
 * @brief 设置远端SDP
 * @param sess 会话实例
 * @param sdp 远端SDP字符串
 * @return 0成功，-1失败
 */
int lws_sess_set_remote_sdp(lws_sess_t* sess, const char* sdp);

/**
 * @brief 添加远端ICE candidate（trickle ICE）
 * @param sess 会话实例
 * @param candidate Candidate字符串（SDP格式）
 * @return 0成功，-1失败
 */
int lws_sess_add_remote_candidate(lws_sess_t* sess, const char* candidate);

/**
 * @brief 开始ICE连接
 * @param sess 会话实例
 * @return 0成功，-1失败
 */
int lws_sess_start_ice(lws_sess_t* sess);

/**
 * @brief 会话事件循环（驱动媒体发送）
 *
 * 功能：
 * 1. 从Dev层采集音视频数据
 * 2. RTP打包
 * 3. 通过ICE发送到网络
 * 4. 处理RTCP定时器（到期则发送RTCP）
 * 5. 更新媒体统计信息
 *
 * @param sess 会话实例
 * @param timeout_ms 超时时间（毫秒）
 * @return 0成功，-1失败
 */
int lws_sess_loop(lws_sess_t* sess, int timeout_ms);

/**
 * @brief 停止会话
 * @param sess 会话实例
 */
void lws_sess_stop(lws_sess_t* sess);

/* ========================================
 * 状态查询
 * ======================================== */

/**
 * @brief 获取会话状态
 * @param sess 会话实例
 * @return 会话状态
 */
lws_sess_state_t lws_sess_get_state(lws_sess_t* sess);

/**
 * @brief 获取本地SDP
 * @param sess 会话实例
 * @return 本地SDP字符串（需要在GATHERED状态后调用）
 */
const char* lws_sess_get_local_sdp(lws_sess_t* sess);

/**
 * @brief 获取会话统计信息
 * @param sess 会话实例
 * @param stats 输出统计信息
 * @return 0成功，-1失败
 */
int lws_sess_get_stats(lws_sess_t* sess, lws_sess_stats_t* stats);

/**
 * @brief 获取音频RTP实例（用于高级操作）
 * @param sess 会话实例
 * @return 音频RTP实例
 */
lws_rtp_t* lws_sess_get_audio_rtp(lws_sess_t* sess);

/**
 * @brief 获取视频RTP实例（用于高级操作）
 * @param sess 会话实例
 * @return 视频RTP实例
 */
lws_rtp_t* lws_sess_get_video_rtp(lws_sess_t* sess);

/**
 * @brief 获取ICE实例（用于高级操作）
 * @param sess 会话实例
 * @return ICE实例
 */
lws_ice_t* lws_sess_get_ice(lws_sess_t* sess);

/* ========================================
 * 辅助函数
 * ======================================== */

/**
 * @brief 初始化音频会话配置（默认值）
 * @param config 配置结构体
 * @param stun_server STUN服务器地址
 * @param codec 音频编解码
 */
void lws_sess_init_audio_config(
    lws_sess_config_t* config,
    const char* stun_server,
    lws_rtp_payload_t codec
);

/**
 * @brief 初始化视频会话配置（默认值）
 * @param config 配置结构体
 * @param stun_server STUN服务器地址
 * @param codec 视频编解码
 */
void lws_sess_init_video_config(
    lws_sess_config_t* config,
    const char* stun_server,
    lws_rtp_payload_t codec
);

/**
 * @brief 初始化音视频会话配置（默认值）
 * @param config 配置结构体
 * @param stun_server STUN服务器地址
 * @param audio_codec 音频编解码
 * @param video_codec 视频编解码
 */
void lws_sess_init_av_config(
    lws_sess_config_t* config,
    const char* stun_server,
    lws_rtp_payload_t audio_codec,
    lws_rtp_payload_t video_codec
);

/**
 * @brief 获取会话状态名称
 * @param state 会话状态
 * @return 状态名称字符串
 */
const char* lws_sess_state_name(lws_sess_state_t state);

#ifdef __cplusplus
}
#endif

#endif // __LWS_SESS_H__
