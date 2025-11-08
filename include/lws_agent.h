/**
 * @file lws_agent.h
 * @brief lwsip SIP signaling layer
 *
 * lws_agent是SIP协议层的高层封装，提供开箱即用的SIP功能：
 * - SIP注册、呼叫建立、挂断等信令处理
 * - SDP Offer/Answer协商
 * - 定时器管理（SIP事务定时器Timer A-K）
 * - UAC/UAS状态机管理
 * - 通过回调通知应用层呼叫状态变化
 * - 统一的传输层抽象（通过lws_trans）
 */

#ifndef __LWS_AGENT_H__
#define __LWS_AGENT_H__

#include <stdint.h>
#include <stddef.h>
#include "lws_defs.h"
#include "lws_trans.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================
 * 类型定义
 * ======================================== */

/**
 * @brief Agent状态
 */
typedef enum {
    LWS_AGENT_STATE_IDLE,           /**< 空闲 */
    LWS_AGENT_STATE_REGISTERING,    /**< 正在注册 */
    LWS_AGENT_STATE_REGISTERED,     /**< 已注册 */
    LWS_AGENT_STATE_REGISTER_FAILED,/**< 注册失败 */
    LWS_AGENT_STATE_UNREGISTERING,  /**< 正在注销 */
    LWS_AGENT_STATE_UNREGISTERED    /**< 已注销 */
} lws_agent_state_t;

/**
 * @brief Dialog（呼叫）方向
 */
typedef enum {
    LWS_DIALOG_UNKNOWN,             /**< 未知方向 */
    LWS_DIALOG_OUTGOING,            /**< 出站呼叫（UAC） */
    LWS_DIALOG_INCOMING             /**< 入站呼叫（UAS） */
} lws_dialog_direction_t;

/**
 * @brief Dialog（呼叫）状态
 */
typedef enum {
    LWS_DIALOG_STATE_NULL,          /**< 初始状态 */
    LWS_DIALOG_STATE_CALLING,       /**< 正在呼叫（UAC） */
    LWS_DIALOG_STATE_INCOMING,      /**< 来电（UAS） */
    LWS_DIALOG_STATE_EARLY,         /**< 早期对话（收到18x） */
    LWS_DIALOG_STATE_CONFIRMED,     /**< 已确认（收到200） */
    LWS_DIALOG_STATE_TERMINATED,    /**< 已终止 */
    LWS_DIALOG_STATE_FAILED         /**< 失败 */
} lws_dialog_state_t;

/* 前向声明 */
typedef struct lws_agent_t lws_agent_t;

/**
 * @brief Dialog（呼叫）公共信息
 */
typedef struct lws_dialog_t {
    char call_id[LWS_MAX_CALL_ID_LEN];          /**< Call-ID */
    char local_uri[LWS_MAX_URI_LEN];            /**< 本地URI */
    char remote_uri[LWS_MAX_URI_LEN];           /**< 远端URI */
    lws_dialog_direction_t direction;            /**< 呼叫方向 */
} lws_dialog_t;

/**
 * @brief SIP地址
 */
typedef struct {
    char nickname[LWS_MAX_NICKNAME_LEN];   /**< 昵称 */
    char username[LWS_MAX_USERNAME_LEN];       /**< 用户名 */
    char domain[LWS_MAX_DOMAIN_LEN];           /**< 域名 */
    uint16_t port;                              /**< 端口（0=默认） */
} lws_sip_addr_t;

/**
 * @brief SIP凭证
 */
typedef struct {
    char username[LWS_MAX_USERNAME_LEN];   /**< 用户名 */
    char password[LWS_MAX_PASSWORD_LEN];   /**< 密码 */
    char realm[LWS_MAX_REALM_LEN];         /**< Realm */
} lws_sip_credential_t;

/* ========================================
 * 回调函数
 * ======================================== */

/**
 * @brief Agent状态变化回调
 * @param agent Agent实例
 * @param old_state 旧状态
 * @param new_state 新状态
 * @param userdata 用户数据
 */
typedef void (*lws_agent_on_state_changed_f)(
    lws_agent_t* agent,
    lws_agent_state_t old_state,
    lws_agent_state_t new_state,
    void* userdata
);

/**
 * @brief 注册结果回调
 * @param agent Agent实例
 * @param success 是否成功
 * @param status_code SIP状态码
 * @param reason_phrase 原因短语
 * @param userdata 用户数据
 */
typedef void (*lws_agent_on_register_result_f)(
    lws_agent_t* agent,
    int success,
    int status_code,
    const char* reason_phrase,
    void* userdata
);

/**
 * @brief 来电回调
 * @param agent Agent实例
 * @param dialog Dialog实例
 * @param from 来电方地址
 * @param userdata 用户数据
 */
typedef void (*lws_agent_on_incoming_call_f)(
    lws_agent_t* agent,
    lws_dialog_t* dialog,
    const lws_sip_addr_t* from,
    void* userdata
);

/**
 * @brief Dialog状态变化回调
 * @param agent Agent实例
 * @param dialog Dialog实例
 * @param old_state 旧状态
 * @param new_state 新状态
 * @param userdata 用户数据
 */
typedef void (*lws_agent_on_dialog_state_changed_f)(
    lws_agent_t* agent,
    lws_dialog_t* dialog,
    lws_dialog_state_t old_state,
    lws_dialog_state_t new_state,
    void* userdata
);

/**
 * @brief 收到远端SDP回调
 * @param agent Agent实例
 * @param dialog Dialog实例
 * @param sdp 远端SDP字符串
 * @param userdata 用户数据
 */
typedef void (*lws_agent_on_remote_sdp_f)(
    lws_agent_t* agent,
    lws_dialog_t* dialog,
    const char* sdp,
    void* userdata
);

/**
 * @brief Agent错误回调
 * @param agent Agent实例
 * @param error_code 错误码
 * @param error_msg 错误消息
 * @param userdata 用户数据
 */
typedef void (*lws_agent_on_error_f)(
    lws_agent_t* agent,
    int error_code,
    const char* error_msg,
    void* userdata
);

/**
 * @brief Agent事件回调集合
 */
typedef struct {
    lws_agent_on_state_changed_f on_state_changed;         /**< 状态变化回调 */
    lws_agent_on_register_result_f on_register_result;     /**< 注册结果回调 */
    lws_agent_on_incoming_call_f on_incoming_call;         /**< 来电回调 */
    lws_agent_on_dialog_state_changed_f on_dialog_state_changed; /**< Dialog状态变化回调 */
    lws_agent_on_remote_sdp_f on_remote_sdp;               /**< 远端SDP回调 */
    lws_agent_on_error_f on_error;                         /**< 错误回调 */
    void* userdata;                                         /**< 用户数据 */
} lws_agent_handler_t;

/* ========================================
 * 配置
 * ======================================== */

/**
 * @brief Agent配置
 */
typedef struct {
    /* SIP账号 */
    char username[LWS_MAX_USERNAME_LEN];        /**< 用户名 */
    char password[LWS_MAX_PASSWORD_LEN];        /**< 密码 */
    char nickname[LWS_MAX_NICKNAME_LEN];    /**< 昵称 */
    char domain[LWS_MAX_DOMAIN_LEN];            /**< 域名 */

    /* 服务器 */
    char registrar[LWS_MAX_HOSTNAME_LEN];       /**< 注册服务器地址 */
    uint16_t registrar_port;                    /**< 注册服务器端口（0=5060） */

    /* 注册 */
    int auto_register;                          /**< 自动注册 */
    int register_expires;                       /**< 注册过期时间（秒，默认3600） */

    /* User-Agent */
    char user_agent[LWS_MAX_USER_AGENT_LEN];    /**< User-Agent字符串 */
} lws_agent_config_t;

/* ========================================
 * 核心API
 * ======================================== */

/**
 * @brief 创建Agent实例
 * @param config 配置
 * @param handler 回调函数
 * @return Agent实例，失败返回NULL
 */
lws_agent_t* lws_agent_create(
    const lws_agent_config_t* config,
    const lws_agent_handler_t* handler
);

/**
 * @brief 销毁Agent实例
 * @param agent Agent实例
 */
void lws_agent_destroy(lws_agent_t* agent);

/**
 * @brief 开始注册
 * @param agent Agent实例
 * @return 0成功，-1失败
 */
int lws_agent_start(lws_agent_t* agent);

/**
 * @brief 注销
 * @param agent Agent实例
 * @return 0成功，-1失败
 */
int lws_agent_stop(lws_agent_t* agent);

/**
 * @brief Agent事件循环（驱动SIP信令处理）
 *
 * 功能：
 * 1. 处理SIP消息收发（通过lws_trans）
 * 2. 检查SIP定时器队列（Timer A-K）
 * 3. 触发到期的定时器回调
 * 4. 调用底层libsip的状态机
 * 5. 触发用户回调（注册结果、呼叫状态变化等）
 *
 * @param agent Agent实例
 * @param timeout_ms 超时时间（毫秒）
 * @return 0成功，-1失败
 */
int lws_agent_loop(lws_agent_t* agent, int timeout_ms);

/* ========================================
 * 呼叫控制API
 * ======================================== */

/**
 * @brief 发起呼叫（UAC）
 *
 * 创建媒体会话并开始异步收集ICE candidates。
 * INVITE消息将在媒体会话准备好SDP后自动发送（通过回调）。
 *
 * @param agent Agent实例
 * @param target_uri 目标URI（格式："sip:user@domain"）
 * @return Dialog实例，失败返回NULL
 */
lws_dialog_t* lws_agent_make_call(
    lws_agent_t* agent,
    const char* target_uri
);

/**
 * @brief 应答来电（UAS）
 *
 * 开始异步收集ICE candidates。
 * 200 OK响应将在媒体会话准备好SDP后自动发送（通过回调）。
 *
 * @param agent Agent实例
 * @param dialog Dialog实例
 * @return 0成功，-1失败
 */
int lws_agent_answer_call(
    lws_agent_t* agent,
    lws_dialog_t* dialog
);

/**
 * @brief 拒绝来电（UAS）
 * @param agent Agent实例
 * @param dialog Dialog实例
 * @param status_code SIP状态码（如486 Busy Here）
 * @param reason_phrase 原因短语（可选）
 * @return 0成功，-1失败
 */
int lws_agent_reject_call(
    lws_agent_t* agent,
    lws_dialog_t* dialog,
    int status_code,
    const char* reason_phrase
);

/**
 * @brief 挂断呼叫
 * @param agent Agent实例
 * @param dialog Dialog实例
 * @return 0成功，-1失败
 */
int lws_agent_hangup(
    lws_agent_t* agent,
    lws_dialog_t* dialog
);

/**
 * @brief 取消呼叫（CANCEL）
 * @param agent Agent实例
 * @param dialog Dialog实例
 * @return 0成功，-1失败
 */
int lws_agent_cancel_call(
    lws_agent_t* agent,
    lws_dialog_t* dialog
);

/* ========================================
 * Dialog查询API
 * ======================================== */

/**
 * @brief 获取Dialog状态
 * @param dialog Dialog实例
 * @return Dialog状态
 */
lws_dialog_state_t lws_dialog_get_state(lws_dialog_t* dialog);

/**
 * @brief 获取Dialog的Call-ID
 * @param dialog Dialog实例
 * @return Call-ID字符串
 */
const char* lws_dialog_get_call_id(lws_dialog_t* dialog);

/**
 * @brief 获取Dialog的远端地址
 * @param dialog Dialog实例
 * @param addr 输出远端地址
 * @return 0成功，-1失败
 */
int lws_dialog_get_remote_addr(lws_dialog_t* dialog, lws_sip_addr_t* addr);

/**
 * @brief 获取Dialog的本地地址
 * @param dialog Dialog实例
 * @param addr 输出本地地址
 * @return 0成功，-1失败
 */
int lws_dialog_get_local_addr(lws_dialog_t* dialog, lws_sip_addr_t* addr);

/**
 * @brief 获取Dialog的远端SDP
 * @param dialog Dialog实例
 * @return 远端SDP字符串
 */
const char* lws_dialog_get_remote_sdp(lws_dialog_t* dialog);

/* ========================================
 * Agent状态查询API
 * ======================================== */

/**
 * @brief 获取Agent状态
 * @param agent Agent实例
 * @return Agent状态
 */
lws_agent_state_t lws_agent_get_state(lws_agent_t* agent);

/**
 * @brief 获取Agent的SIP URI
 * @param agent Agent实例
 * @param buf 输出缓冲区
 * @param size 缓冲区大小
 * @return 字符串长度
 */
int lws_agent_get_uri(lws_agent_t* agent, char* buf, size_t size);

/**
 * @brief 获取所有活动的Dialog
 * @param agent Agent实例
 * @param dialogs 输出Dialog数组
 * @param max_count 最大数量
 * @return 实际数量
 */
int lws_agent_get_dialogs(
    lws_agent_t* agent,
    lws_dialog_t** dialogs,
    int max_count
);

/* ========================================
 * 辅助函数
 * ======================================== */

/**
 * @brief 初始化默认Agent配置
 * @param config 配置结构体
 * @param username 用户名
 * @param password 密码
 * @param domain 域名
 * @param trans 传输层实例
 */
void lws_agent_init_default_config(
    lws_agent_config_t* config,
    const char* username,
    const char* password,
    const char* domain,
    lws_trans_t* trans
);

/**
 * @brief 解析SIP URI
 * @param uri URI字符串
 * @param addr 输出地址
 * @return 0成功，-1失败
 */
int lws_sip_parse_uri(const char* uri, lws_sip_addr_t* addr);

/**
 * @brief SIP地址转字符串
 * @param addr 地址
 * @param buf 输出缓冲区
 * @param size 缓冲区大小
 * @return 字符串长度
 */
int lws_sip_addr_to_string(const lws_sip_addr_t* addr, char* buf, size_t size);

/**
 * @brief 获取Agent状态名称
 * @param state Agent状态
 * @return 状态名称字符串
 */
const char* lws_agent_state_name(lws_agent_state_t state);

/**
 * @brief 获取Dialog状态名称
 * @param state Dialog状态
 * @return 状态名称字符串
 */
const char* lws_dialog_state_name(lws_dialog_state_t state);

#ifdef __cplusplus
}
#endif

#endif // __LWS_AGENT_H__
