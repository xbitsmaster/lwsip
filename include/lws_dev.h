/**
 * @file lws_dev.h
 * @brief lwsip device abstraction layer
 *
 * lwsip设备抽象层，补齐基础库之外的设备管理功能：
 * - 统一的音视频设备抽象接口
 * - 支持真实设备（ALSA、PortAudio、V4L2等）
 * - 支持文件设备（WAV、MP4等，用于测试）
 * - 虚函数表设计，易于扩展新的设备驱动
 * - 时间戳同步机制（设备时间 → RTP时间戳）
 */

#ifndef __LWS_DEV_H__
#define __LWS_DEV_H__

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
 * @brief 设备类型
 */
typedef enum {
    LWS_DEV_AUDIO_CAPTURE,      /**< 音频采集（麦克风） */
    LWS_DEV_AUDIO_PLAYBACK,     /**< 音频播放（扬声器） */
    LWS_DEV_VIDEO_CAPTURE,      /**< 视频采集（摄像头） */
    LWS_DEV_VIDEO_DISPLAY,      /**< 视频显示 */
    LWS_DEV_FILE_READER,        /**< 文件读取（用于测试或离线处理） */
    LWS_DEV_FILE_WRITER         /**< 文件写入（录音/录像） */
} lws_dev_type_t;

/**
 * @brief 设备状态
 */
typedef enum {
    LWS_DEV_STATE_IDLE,         /**< 空闲 */
    LWS_DEV_STATE_OPENING,      /**< 正在打开 */
    LWS_DEV_STATE_OPENED,       /**< 已打开 */
    LWS_DEV_STATE_STARTED,      /**< 已启动 */
    LWS_DEV_STATE_STOPPED,      /**< 已停止 */
    LWS_DEV_STATE_CLOSED,       /**< 已关闭 */
    LWS_DEV_STATE_ERROR         /**< 错误 */
} lws_dev_state_t;

/**
 * @brief 音频格式
 */
typedef enum {
    LWS_AUDIO_FMT_PCM_S16LE,    /**< PCM signed 16-bit little-endian */
    LWS_AUDIO_FMT_PCM_S16BE,    /**< PCM signed 16-bit big-endian */
    LWS_AUDIO_FMT_PCMU,         /**< G.711 μ-law */
    LWS_AUDIO_FMT_PCMA          /**< G.711 A-law */
} lws_audio_format_t;

/**
 * @brief 视频格式
 */
typedef enum {
    LWS_VIDEO_FMT_YUV420P,      /**< YUV 4:2:0 planar */
    LWS_VIDEO_FMT_NV12,         /**< YUV 4:2:0 NV12 */
    LWS_VIDEO_FMT_H264,         /**< H.264 编码数据 */
    LWS_VIDEO_FMT_H265,         /**< H.265 编码数据 */
    LWS_VIDEO_FMT_MJPEG         /**< Motion JPEG */
} lws_video_format_t;

/* 前向声明 */
typedef struct lws_dev_t lws_dev_t;

/* ========================================
 * 回调函数
 * ======================================== */

/**
 * @brief 设备状态变化回调
 * @param dev 设备实例
 * @param old_state 旧状态
 * @param new_state 新状态
 * @param userdata 用户数据
 */
typedef void (*lws_dev_on_state_changed_f)(
    lws_dev_t* dev,
    lws_dev_state_t old_state,
    lws_dev_state_t new_state,
    void* userdata
);

/**
 * @brief 设备错误回调
 * @param dev 设备实例
 * @param error_code 错误码
 * @param error_msg 错误消息
 * @param userdata 用户数据
 */
typedef void (*lws_dev_on_error_f)(
    lws_dev_t* dev,
    int error_code,
    const char* error_msg,
    void* userdata
);

/**
 * @brief 音频数据就绪回调（用于异步模式）
 * @param dev 设备实例
 * @param data 音频数据
 * @param samples 采样数
 * @param timestamp 时间戳
 * @param userdata 用户数据
 */
typedef void (*lws_dev_on_audio_data_f)(
    lws_dev_t* dev,
    const void* data,
    int samples,
    uint64_t timestamp,
    void* userdata
);

/**
 * @brief 视频帧就绪回调（用于异步模式）
 * @param dev 设备实例
 * @param data 视频数据
 * @param size 数据大小
 * @param timestamp 时间戳
 * @param userdata 用户数据
 */
typedef void (*lws_dev_on_video_frame_f)(
    lws_dev_t* dev,
    const void* data,
    int size,
    uint64_t timestamp,
    void* userdata
);

/**
 * @brief 设备事件回调集合
 */
typedef struct {
    lws_dev_on_state_changed_f on_state_changed; /**< 状态变化回调 */
    lws_dev_on_error_f on_error;                 /**< 错误回调 */
    lws_dev_on_audio_data_f on_audio_data;       /**< 音频数据回调 */
    lws_dev_on_video_frame_f on_video_frame;     /**< 视频帧回调 */
    void* userdata;                               /**< 用户数据 */
} lws_dev_handler_t;

/* ========================================
 * 配置
 * ======================================== */

/**
 * @brief 音频设备配置
 */
typedef struct {
    lws_audio_format_t format;  /**< 音频格式 */
    int sample_rate;            /**< 采样率（Hz） */
    int channels;               /**< 声道数（1=单声道，2=立体声） */
    int frame_duration_ms;      /**< 帧时长（毫秒） */
    int latency_ms;             /**< 期望延迟（毫秒） */
} lws_audio_config_t;

/**
 * @brief 视频设备配置
 */
typedef struct {
    lws_video_format_t format;  /**< 视频格式 */
    int width;                  /**< 宽度（像素） */
    int height;                 /**< 高度（像素） */
    int fps;                    /**< 帧率（帧/秒） */
    int bitrate;                /**< 码率（bps，用于编码格式） */
} lws_video_config_t;

/**
 * @brief 文件设备配置
 */
typedef struct {
    const char* file_path;      /**< 文件路径 */
    int loop;                   /**< 循环播放（仅读取） */
} lws_file_config_t;

/**
 * @brief 设备配置
 */
typedef struct {
    lws_dev_type_t type;        /**< 设备类型 */
    const char* device_name;    /**< 设备名称（如"hw:0,0"，NULL=默认设备） */
    int async_mode;             /**< 异步模式（1=使用回调，0=同步读写） */

    /* 类型特定配置 */
    union {
        lws_audio_config_t audio;   /**< 音频配置 */
        lws_video_config_t video;   /**< 视频配置 */
        lws_file_config_t file;     /**< 文件配置 */
    };
} lws_dev_config_t;

/* ========================================
 * 核心API
 * ======================================== */

/**
 * @brief 创建设备实例
 * @param config 配置
 * @param handler 回调函数
 * @return 设备实例，失败返回NULL
 */
lws_dev_t* lws_dev_create(
    const lws_dev_config_t* config,
    const lws_dev_handler_t* handler
);

/**
 * @brief 销毁设备实例
 * @param dev 设备实例
 */
void lws_dev_destroy(lws_dev_t* dev);

/**
 * @brief 打开设备
 * @param dev 设备实例
 * @return 0成功，-1失败
 */
int lws_dev_open(lws_dev_t* dev);

/**
 * @brief 关闭设备
 * @param dev 设备实例
 */
void lws_dev_close(lws_dev_t* dev);

/**
 * @brief 启动设备（开始采集或播放）
 * @param dev 设备实例
 * @return 0成功，-1失败
 */
int lws_dev_start(lws_dev_t* dev);

/**
 * @brief 停止设备
 * @param dev 设备实例
 */
void lws_dev_stop(lws_dev_t* dev);

/* ========================================
 * 音频API
 * ======================================== */

/**
 * @brief 读取音频数据（采集设备）
 * @param dev 设备实例
 * @param buf 输出缓冲区
 * @param samples 期望读取的采样数
 * @return 实际读取的采样数，-1失败
 */
int lws_dev_read_audio(lws_dev_t* dev, void* buf, int samples);

/**
 * @brief 写入音频数据（播放设备）
 * @param dev 设备实例
 * @param data 音频数据
 * @param samples 采样数
 * @return 实际写入的采样数，-1失败
 */
int lws_dev_write_audio(lws_dev_t* dev, const void* data, int samples);

/**
 * @brief 获取音频缓冲区可用空间（播放设备）
 * @param dev 设备实例
 * @return 可用采样数，-1失败
 */
int lws_dev_get_audio_avail(lws_dev_t* dev);

/**
 * @brief 清空音频缓冲区
 * @param dev 设备实例
 * @return 0成功，-1失败
 */
int lws_dev_flush_audio(lws_dev_t* dev);

/* ========================================
 * 视频API
 * ======================================== */

/**
 * @brief 读取视频帧（采集设备）
 * @param dev 设备实例
 * @param buf 输出缓冲区
 * @param size 缓冲区大小
 * @return 实际读取的字节数，-1失败
 */
int lws_dev_read_video(lws_dev_t* dev, void* buf, int size);

/**
 * @brief 写入视频帧（显示设备）
 * @param dev 设备实例
 * @param data 视频数据
 * @param size 数据大小
 * @return 实际写入的字节数，-1失败
 */
int lws_dev_write_video(lws_dev_t* dev, const void* data, int size);

/* ========================================
 * 时间戳API
 * ======================================== */

/**
 * @brief 获取设备当前时间戳（微秒）
 * @param dev 设备实例
 * @return 时间戳（微秒）
 */
uint64_t lws_dev_get_timestamp(lws_dev_t* dev);

/**
 * @brief 设备时间戳转RTP时间戳
 * @param dev 设备实例
 * @param dev_timestamp 设备时间戳（微秒）
 * @param sample_rate RTP采样率
 * @return RTP时间戳
 */
uint32_t lws_dev_to_rtp_timestamp(
    lws_dev_t* dev,
    uint64_t dev_timestamp,
    int sample_rate
);

/* ========================================
 * 状态查询
 * ======================================== */

/**
 * @brief 获取设备状态
 * @param dev 设备实例
 * @return 设备状态
 */
lws_dev_state_t lws_dev_get_state(lws_dev_t* dev);

/**
 * @brief 获取设备类型
 * @param dev 设备实例
 * @return 设备类型
 */
lws_dev_type_t lws_dev_get_type(lws_dev_t* dev);

/**
 * @brief 获取设备名称
 * @param dev 设备实例
 * @return 设备名称
 */
const char* lws_dev_get_name(lws_dev_t* dev);

/* ========================================
 * 辅助函数
 * ======================================== */

/**
 * @brief 初始化音频采集配置（默认值）
 * @param config 配置结构体
 */
void lws_dev_init_audio_capture_config(lws_dev_config_t* config);

/**
 * @brief 初始化音频播放配置（默认值）
 * @param config 配置结构体
 */
void lws_dev_init_audio_playback_config(lws_dev_config_t* config);

/**
 * @brief 初始化视频采集配置（默认值）
 * @param config 配置结构体
 */
void lws_dev_init_video_capture_config(lws_dev_config_t* config);

/**
 * @brief 初始化视频显示配置（默认值）
 * @param config 配置结构体
 */
void lws_dev_init_video_display_config(lws_dev_config_t* config);

/**
 * @brief 初始化文件读取配置
 * @param config 配置结构体
 * @param file_path 文件路径
 */
void lws_dev_init_file_reader_config(lws_dev_config_t* config, const char* file_path);

/**
 * @brief 初始化文件写入配置
 * @param config 配置结构体
 * @param file_path 文件路径
 */
void lws_dev_init_file_writer_config(lws_dev_config_t* config, const char* file_path);

/**
 * @brief 计算音频帧大小（字节）
 * @param format 音频格式
 * @param channels 声道数
 * @param samples 采样数
 * @return 帧大小（字节）
 */
int lws_audio_calc_frame_size(
    lws_audio_format_t format,
    int channels,
    int samples
);

/**
 * @brief 计算音频帧采样数
 * @param sample_rate 采样率
 * @param frame_duration_ms 帧时长（毫秒）
 * @return 采样数
 */
int lws_audio_calc_frame_samples(int sample_rate, int frame_duration_ms);

/**
 * @brief 枚举系统可用的音频设备
 * @param type 设备类型（采集或播放）
 * @param devices 输出设备列表（指针数组，由调用者释放）
 * @param max_devices 最多返回设备数
 * @return 实际设备数，-1失败
 */
int lws_dev_enum_audio_devices(
    lws_dev_type_t type,
    char** devices,
    int max_devices
);

/**
 * @brief 枚举系统可用的视频设备
 * @param devices 输出设备列表（指针数组，由调用者释放）
 * @param max_devices 最多返回设备数
 * @return 实际设备数，-1失败
 */
int lws_dev_enum_video_devices(char** devices, int max_devices);

#ifdef __cplusplus
}
#endif

#endif // __LWS_DEV_H__
