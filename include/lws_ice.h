/**
 * @file lws_ice.h
 * @brief lwsip ICE protocol wrapper
 *
 * lws ICE层基于libice实现，保持协议处理的纯粹性：
 * - 被动输入模型（与libice一致）
 * - Host/STUN/TURN candidate收集
 * - ICE connectivity checks
 * - 选择最优candidate pair
 * - 通过回调传递接收的数据
 */

#ifndef __LWS_ICE_H__
#define __LWS_ICE_H__

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
 * @brief ICE状态
 */
typedef enum {
    LWS_ICE_STATE_IDLE,         /**< 空闲 */
    LWS_ICE_STATE_GATHERING,    /**< 正在收集candidates */
    LWS_ICE_STATE_GATHERED,     /**< Candidates收集完成 */
    LWS_ICE_STATE_CHECKING,     /**< 正在进行连接性检查 */
    LWS_ICE_STATE_CONNECTED,    /**< 已连接 */
    LWS_ICE_STATE_COMPLETED,    /**< 完成 */
    LWS_ICE_STATE_FAILED,       /**< 失败 */
    LWS_ICE_STATE_CLOSED        /**< 已关闭 */
} lws_ice_state_t;

/**
 * @brief ICE candidate类型
 */
typedef enum {
    LWS_ICE_CAND_TYPE_HOST,     /**< Host candidate */
    LWS_ICE_CAND_TYPE_SRFLX,    /**< Server reflexive */
    LWS_ICE_CAND_TYPE_PRFLX,    /**< Peer reflexive */
    LWS_ICE_CAND_TYPE_RELAY     /**< Relayed */
} lws_ice_cand_type_t;

/**
 * @brief ICE transport协议
 */
typedef enum {
    LWS_ICE_TRANS_UDP,          /**< UDP */
    LWS_ICE_TRANS_TCP_ACTIVE,   /**< TCP active */
    LWS_ICE_TRANS_TCP_PASSIVE,  /**< TCP passive */
    LWS_ICE_TRANS_TCP_SO        /**< TCP simultaneous-open */
} lws_ice_trans_t;

/**
 * @brief ICE component ID
 */
typedef enum {
    LWS_ICE_COMP_RTP = 1,       /**< RTP component */
    LWS_ICE_COMP_RTCP = 2       /**< RTCP component */
} lws_ice_comp_id_t;

/* 前向声明 */
typedef struct lws_ice_t lws_ice_t;

/**
 * @brief ICE candidate信息
 */
typedef struct {
    lws_ice_cand_type_t type;   /**< Candidate类型 */
    lws_ice_trans_t transport;  /**< 传输协议 */
    char foundation[LWS_MAX_FOUNDATION_LEN];  /**< Foundation */
    int component_id;           /**< Component ID (1=RTP, 2=RTCP) */
    uint32_t priority;          /**< 优先级 */
    char ip[LWS_MAX_IP_LEN];    /**< IP地址 */
    uint16_t port;              /**< 端口 */
    char rel_ip[LWS_MAX_IP_LEN];/**< Related address IP */
    uint16_t rel_port;          /**< Related address port */
} lws_ice_candidate_t;

/**
 * @brief ICE candidate pair信息
 */
typedef struct {
    lws_ice_candidate_t local;  /**< 本地candidate */
    lws_ice_candidate_t remote; /**< 远端candidate */
    uint64_t priority;          /**< Pair优先级 */
    int nominated;              /**< 是否被提名 */
} lws_ice_candidate_pair_t;

/* ========================================
 * 回调函数
 * ======================================== */

/**
 * @brief ICE状态变化回调
 * @param ice ICE实例
 * @param old_state 旧状态
 * @param new_state 新状态
 * @param userdata 用户数据
 */
typedef void (*lws_ice_on_state_changed_f)(
    lws_ice_t* ice,
    lws_ice_state_t old_state,
    lws_ice_state_t new_state,
    void* userdata
);

/**
 * @brief Candidate收集完成回调
 * @param ice ICE实例
 * @param candidates Candidate数组
 * @param count Candidate数量
 * @param userdata 用户数据
 */
typedef void (*lws_ice_on_gathering_done_f)(
    lws_ice_t* ice,
    const lws_ice_candidate_t* candidates,
    int count,
    void* userdata
);

/**
 * @brief 新candidate发现回调（trickle ICE）
 * @param ice ICE实例
 * @param candidate 新candidate
 * @param userdata 用户数据
 */
typedef void (*lws_ice_on_candidate_f)(
    lws_ice_t* ice,
    const lws_ice_candidate_t* candidate,
    void* userdata
);

/**
 * @brief ICE连接建立回调
 * @param ice ICE实例
 * @param pair 选中的candidate pair
 * @param userdata 用户数据
 */
typedef void (*lws_ice_on_connected_f)(
    lws_ice_t* ice,
    const lws_ice_candidate_pair_t* pair,
    void* userdata
);

/**
 * @brief 接收到数据回调（RTP/RTCP）
 * @param ice ICE实例
 * @param component_id Component ID
 * @param data 数据指针
 * @param len 数据长度
 * @param userdata 用户数据
 */
typedef void (*lws_ice_on_data_f)(
    lws_ice_t* ice,
    int component_id,
    const void* data,
    int len,
    void* userdata
);

/**
 * @brief ICE错误回调
 * @param ice ICE实例
 * @param error_code 错误码
 * @param error_msg 错误消息
 * @param userdata 用户数据
 */
typedef void (*lws_ice_on_error_f)(
    lws_ice_t* ice,
    int error_code,
    const char* error_msg,
    void* userdata
);

/**
 * @brief 需要发送数据回调（STUN/候选数据）
 * @param ice ICE实例
 * @param dst_ip 目标IP
 * @param dst_port 目标端口
 * @param data 数据指针
 * @param len 数据长度
 * @param userdata 用户数据
 * @return 0成功，-1失败
 */
typedef int (*lws_ice_on_send_f)(
    lws_ice_t* ice,
    const char* dst_ip,
    uint16_t dst_port,
    const void* data,
    int len,
    void* userdata
);

/**
 * @brief ICE事件回调集合
 */
typedef struct {
    lws_ice_on_state_changed_f on_state_changed;   /**< 状态变化回调 */
    lws_ice_on_gathering_done_f on_gathering_done; /**< 收集完成回调 */
    lws_ice_on_candidate_f on_candidate;           /**< 新candidate回调 */
    lws_ice_on_connected_f on_connected;           /**< 连接建立回调 */
    lws_ice_on_data_f on_data;                     /**< 接收数据回调 */
    lws_ice_on_error_f on_error;                   /**< 错误回调 */
    lws_ice_on_send_f on_send;                     /**< 发送数据回调 */
    void* userdata;                                 /**< 用户数据 */
} lws_ice_handler_t;

/* ========================================
 * 配置
 * ======================================== */

/**
 * @brief ICE role
 */
typedef enum {
    LWS_ICE_ROLE_CONTROLLING,   /**< Controlling */
    LWS_ICE_ROLE_CONTROLLED     /**< Controlled */
} lws_ice_role_t;

/**
 * @brief ICE配置
 */
typedef struct {
    lws_ice_role_t role;        /**< ICE角色 */

    /* STUN服务器 */
    const char* stun_server;    /**< STUN服务器地址 */
    uint16_t stun_port;         /**< STUN端口（默认LWS_DEFAULT_STUN_PORT） */

    /* TURN服务器（可选） */
    const char* turn_server;    /**< TURN服务器地址 */
    uint16_t turn_port;         /**< TURN端口（默认LWS_DEFAULT_TURN_PORT） */
    const char* turn_username;  /**< TURN用户名 */
    const char* turn_password;  /**< TURN密码 */

    /* ICE参数 */
    int component_count;        /**< Component数量（1=RTP only, 2=RTP+RTCP） */
    int enable_ipv6;            /**< 启用IPv6 */
    int trickle_ice;            /**< 启用trickle ICE */
    int aggressive_nomination;  /**< 激进提名模式 */

    /* 超时设置 */
    int gathering_timeout_ms;   /**< Gathering超时（毫秒） */
    int connectivity_timeout_ms;/**< Connectivity检查超时（毫秒） */
    int keepalive_interval_ms;  /**< Keepalive间隔（毫秒） */
} lws_ice_config_t;

/* ========================================
 * 核心API
 * ======================================== */

/**
 * @brief 创建ICE实例
 * @param config 配置
 * @param handler 回调函数
 * @return ICE实例，失败返回NULL
 */
lws_ice_t* lws_ice_create(
    const lws_ice_config_t* config,
    const lws_ice_handler_t* handler
);

/**
 * @brief 销毁ICE实例
 * @param ice ICE实例
 */
void lws_ice_destroy(lws_ice_t* ice);

/**
 * @brief 开始收集candidates
 * @param ice ICE实例
 * @return 0成功，-1失败
 */
int lws_ice_gather_candidates(lws_ice_t* ice);

/**
 * @brief 设置远端ICE参数（ufrag和pwd）
 * @param ice ICE实例
 * @param ufrag 远端username fragment
 * @param pwd 远端password
 * @return 0成功，-1失败
 */
int lws_ice_set_remote_credentials(
    lws_ice_t* ice,
    const char* ufrag,
    const char* pwd
);

/**
 * @brief 添加远端candidate
 * @param ice ICE实例
 * @param candidate 远端candidate
 * @return 0成功，-1失败
 */
int lws_ice_add_remote_candidate(
    lws_ice_t* ice,
    const lws_ice_candidate_t* candidate
);

/**
 * @brief 开始ICE连接性检查
 * @param ice ICE实例
 * @return 0成功，-1失败
 */
int lws_ice_start_check(lws_ice_t* ice);

/**
 * @brief 输入接收的数据（STUN或RTP/RTCP）
 *
 * 被动输入模型：应用层接收到网络数据后，通过此函数输入到ICE层
 *
 * @param ice ICE实例
 * @param src_ip 源IP地址
 * @param src_port 源端口
 * @param data 数据指针
 * @param len 数据长度
 * @return 0成功，-1失败
 */
int lws_ice_input(
    lws_ice_t* ice,
    const char* src_ip,
    uint16_t src_port,
    const void* data,
    int len
);

/**
 * @brief 发送数据（通过ICE通道）
 * @param ice ICE实例
 * @param component_id Component ID (1=RTP, 2=RTCP)
 * @param data 数据指针
 * @param len 数据长度
 * @return 实际发送字节数，-1失败
 */
int lws_ice_send(
    lws_ice_t* ice,
    int component_id,
    const void* data,
    int len
);

/**
 * @brief 停止ICE
 * @param ice ICE实例
 */
void lws_ice_stop(lws_ice_t* ice);

/* ========================================
 * 状态查询
 * ======================================== */

/**
 * @brief 获取ICE状态
 * @param ice ICE实例
 * @return ICE状态
 */
lws_ice_state_t lws_ice_get_state(lws_ice_t* ice);

/**
 * @brief 获取本地ICE参数（ufrag和pwd）
 * @param ice ICE实例
 * @param ufrag 输出username fragment
 * @param pwd 输出password
 * @return 0成功，-1失败
 */
int lws_ice_get_local_credentials(
    lws_ice_t* ice,
    char* ufrag,
    char* pwd
);

/**
 * @brief 获取本地candidates
 * @param ice ICE实例
 * @param candidates 输出candidate数组
 * @param max_count 最大数量
 * @return 实际数量，-1失败
 */
int lws_ice_get_local_candidates(
    lws_ice_t* ice,
    lws_ice_candidate_t* candidates,
    int max_count
);

/**
 * @brief 获取选中的candidate pair
 * @param ice ICE实例
 * @param pair 输出candidate pair
 * @return 0成功，-1失败
 */
int lws_ice_get_selected_pair(
    lws_ice_t* ice,
    lws_ice_candidate_pair_t* pair
);

/**
 * @brief 获取下次keepalive时间（毫秒）
 * @param ice ICE实例
 * @return 剩余毫秒数，0表示需要立即发送
 */
int lws_ice_get_keepalive_interval(lws_ice_t* ice);

/**
 * @brief 发送keepalive（STUN Binding Indication）
 * @param ice ICE实例
 * @return 0成功，-1失败
 */
int lws_ice_send_keepalive(lws_ice_t* ice);

/* ========================================
 * 辅助函数
 * ======================================== */

/**
 * @brief 初始化默认ICE配置
 * @param config 配置结构体
 * @param stun_server STUN服务器地址
 */
void lws_ice_init_default_config(
    lws_ice_config_t* config,
    const char* stun_server
);

/**
 * @brief Candidate转SDP字符串
 * @param candidate Candidate
 * @param buf 输出缓冲区
 * @param size 缓冲区大小
 * @return 字符串长度
 */
int lws_ice_candidate_to_sdp(
    const lws_ice_candidate_t* candidate,
    char* buf,
    size_t size
);

/**
 * @brief SDP字符串转candidate
 * @param sdp SDP candidate字符串
 * @param candidate 输出candidate
 * @return 0成功，-1失败
 */
int lws_ice_candidate_from_sdp(
    const char* sdp,
    lws_ice_candidate_t* candidate
);

/**
 * @brief 获取candidate类型名称
 * @param type Candidate类型
 * @return 类型名称字符串
 */
const char* lws_ice_candidate_type_name(lws_ice_cand_type_t type);

/**
 * @brief 获取ICE状态名称
 * @param state ICE状态
 * @return 状态名称字符串
 */
const char* lws_ice_state_name(lws_ice_state_t state);

#ifdef __cplusplus
}
#endif

#endif // __LWS_ICE_H__
