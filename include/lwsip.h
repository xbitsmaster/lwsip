/**
 * @file lwsip.h
 * @brief lwsip main header
 *
 * lwsip = Light-Weight SIP stack for RTOS
 *
 * lwsip是一个拿来即用的完整SIP客户端框架，专为嵌入式系统和RTOS环境设计。
 */

#ifndef __LWSIP_H__
#define __LWSIP_H__

#include <stdint.h>
#include <stddef.h>

/* ========================================
 * 版本信息
 * ======================================== */

#define LWSIP_VERSION_MAJOR     3
#define LWSIP_VERSION_MINOR     0
#define LWSIP_VERSION_PATCH     0

#define LWSIP_VERSION_STRING    "3.0.0"

/* ========================================
 * 核心头文件
 * ======================================== */

/* 通用定义 */
#include "lws_defs.h"

/* 传输层 */
#include "lws_trans.h"

/* 设备层 */
#include "lws_dev.h"

/* 协调层 (直接使用 librtp/libice，不需要抽象层) */
#include "lws_sess.h"
#include "lws_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================
 * 库初始化和清理
 * ======================================== */

/**
 * @brief 初始化lwsip库
 *
 * 在使用lwsip任何功能之前必须调用此函数
 *
 * @return 0成功，-1失败
 */
int lwsip_init(void);

/**
 * @brief 清理lwsip库
 *
 * 释放lwsip使用的所有资源
 */
void lwsip_cleanup(void);

/**
 * @brief 获取lwsip版本字符串
 * @return 版本字符串
 */
const char* lwsip_version(void);

/**
 * @brief 获取lwsip版本号
 * @param major 输出主版本号
 * @param minor 输出次版本号
 * @param patch 输出补丁版本号
 */
void lwsip_version_number(int* major, int* minor, int* patch);

/* ========================================
 * 错误码
 *
 * Note: 日志和内存管理已移至 OSAL 层
 *       请使用 osal/include/lws_log.h 和 lws_mem.h
 * ======================================== */

#define LWSIP_OK                0       /**< 成功 */
#define LWSIP_ERROR            -1       /**< 一般错误 */
#define LWSIP_EINVAL           -2       /**< 无效参数 */
#define LWSIP_ENOMEM           -3       /**< 内存不足 */
#define LWSIP_ETIMEOUT         -4       /**< 超时 */
#define LWSIP_ENOTCONN         -5       /**< 未连接 */
#define LWSIP_ECONNREFUSED     -6       /**< 连接被拒绝 */
#define LWSIP_ECONNRESET       -7       /**< 连接重置 */
#define LWSIP_EAGAIN           -8       /**< 需要重试 */
#define LWSIP_ENOTSUP          -9       /**< 不支持 */
#define LWSIP_EBUSY            -10      /**< 设备忙 */
#define LWSIP_ENODEV           -11      /**< 设备不存在 */

/**
 * @brief 获取错误信息
 * @param error_code 错误码
 * @return 错误信息字符串
 */
const char* lwsip_strerror(int error_code);

/* ========================================
 * 实用工具
 * ======================================== */

/**
 * @brief 生成随机数
 * @param min 最小值
 * @param max 最大值
 * @return 随机数
 */
uint32_t lwsip_random(uint32_t min, uint32_t max);

/**
 * @brief 生成UUID
 * @param buf 输出缓冲区
 * @param size 缓冲区大小（至少37字节）
 * @return 0成功，-1失败
 */
int lwsip_generate_uuid(char* buf, size_t size);

/* ========================================
 * 示例代码说明
 * ======================================== */

/**
 * @example basic_call.c
 * 基本呼叫示例（单线程模式）
 *
 * 展示如何使用lwsip发起基本的音频呼叫：
 * - 创建传输层、设备层、会话层、Agent层
 * - 收集ICE candidates并生成SDP
 * - 发起呼叫
 * - 在单线程中驱动三个loop函数
 * - 接收媒体并播放
 * - 挂断呼叫并清理资源
 */

/**
 * @example multi_thread_call.c
 * 多线程呼叫示例（双线程模式）
 *
 * 展示如何使用双线程模式：
 * - 线程1：SIP信令线程（lws_agent_loop）
 * - 线程2：媒体线程（lws_sess_loop + lws_trans_loop）
 * - 线程间通过无锁队列通信
 */

/**
 * @example incoming_call.c
 * 来电处理示例（UAS）
 *
 * 展示如何处理来电：
 * - 注册到SIP服务器
 * - 等待来电
 * - 应答来电或拒绝来电
 * - 媒体通话
 * - 挂断
 */

/**
 * @example video_call.c
 * 视频呼叫示例
 *
 * 展示如何进行视频呼叫：
 * - 配置音视频编解码
 * - 创建视频采集和显示设备
 * - 发送和接收视频流
 */

/**
 * @example mqtt_transport.c
 * MQTT传输示例
 *
 * 展示如何使用MQTT作为传输层：
 * - 配置MQTT传输
 * - SIP信令通过MQTT传输
 * - ICE/RTP通过MQTT传输
 * - 适用于NAT穿透困难的IoT场景
 */

#ifdef __cplusplus
}
#endif

#endif // __LWSIP_H__
