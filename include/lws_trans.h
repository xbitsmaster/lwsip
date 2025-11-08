/**
 * @file lws_trans.h
 * @brief lwsip unified transport layer
 *
 * lwsip统一传输层，提供纯粹的网络传输抽象：
 * - 支持多种传输类型（UDP/TCP/MQTT）
 * - 非阻塞I/O处理
 * - 虚函数表设计，易于扩展
 * - 不关心上层协议，只负责数据收发
 * - 可被多个上层模块（SIP/ICE等）独立使用
 */

#ifndef __LWS_TRANS_H__
#define __LWS_TRANS_H__

#include "lws_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================
 * 类型定义
 * ======================================== */

/**
 * @brief Transport类型
 */
typedef enum {
    LWS_TRANS_TYPE_UDP,         /**< UDP传输 */
    LWS_TRANS_TYPE_TCP,         /**< TCP传输（通用） */
    LWS_TRANS_TYPE_TCP_SERVER,  /**< TCP服务端 */
    LWS_TRANS_TYPE_TCP_CLIENT,  /**< TCP客户端 */
    LWS_TRANS_TYPE_TLS,         /**< TLS传输 */
    LWS_TRANS_TYPE_MQTT,        /**< MQTT传输 */
    LWS_TRANS_TYPE_CUSTOM       /**< 自定义传输 */
} lws_trans_type_t;

/* 前向声明 */
typedef struct lws_trans_t lws_trans_t;

/**
 * @brief 地址信息
 */
typedef struct {
    char ip[LWS_MAX_IP_LEN];    /**< IP地址 */
    uint16_t port;              /**< 端口号 */
    int family;                 /**< AF_INET 或 AF_INET6 */
} lws_addr_t;

/* ========================================
 * 回调函数
 * ======================================== */

/**
 * @brief 接收到数据回调
 * @param trans Transport实例
 * @param data 数据指针
 * @param len 数据长度
 * @param from 源地址
 * @param userdata 用户数据
 */
typedef void (*lws_trans_on_data_f)(
    lws_trans_t* trans,
    const void* data,
    int len,
    const lws_addr_t* from,
    void* userdata
);

/**
 * @brief Transport错误回调
 * @param trans Transport实例
 * @param error_code 错误码
 * @param error_msg 错误消息
 * @param userdata 用户数据
 */
typedef void (*lws_trans_on_error_f)(
    lws_trans_t* trans,
    int error_code,
    const char* error_msg,
    void* userdata
);

/**
 * @brief Transport连接状态变化回调（TCP/TLS/MQTT）
 * @param trans Transport实例
 * @param connected 是否已连接
 * @param userdata 用户数据
 */
typedef void (*lws_trans_on_connected_f)(
    lws_trans_t* trans,
    int connected,
    void* userdata
);

/**
 * @brief Transport事件回调集合
 */
typedef struct {
    lws_trans_on_data_f on_data;            /**< 数据接收回调 */
    lws_trans_on_connected_f on_connected;  /**< 连接状态回调 */
    lws_trans_on_error_f on_error;          /**< 错误回调 */
    void* userdata;                          /**< 用户数据 */
} lws_trans_handler_t;

/* ========================================
 * 配置
 * ======================================== */

/**
 * @brief Transport配置
 */
typedef struct {
    lws_trans_type_t type;      /**< 传输类型 */

    /* Socket配置（UDP/TCP/TLS） */
    struct {
        char bind_addr[LWS_MAX_IP_LEN];     /**< 绑定地址（空=INADDR_ANY） */
        uint16_t bind_port;                 /**< 绑定端口（0=自动分配） */
        int reuse_addr;                     /**< SO_REUSEADDR选项 */
        int reuse_port;                     /**< SO_REUSEPORT选项 */
        int enable_ipv6;                    /**< 启用IPv6 */
    } sock;

    /* MQTT配置 */
    struct {
        char broker[LWS_MAX_HOSTNAME_LEN];  /**< MQTT broker地址 */
        uint16_t port;                      /**< MQTT端口（0=默认1883） */
        char client_id[LWS_MAX_CLIENT_ID_LEN]; /**< 客户端ID */
        char username[LWS_MAX_USERNAME_LEN];   /**< 用户名（可选） */
        char password[LWS_MAX_PASSWORD_LEN];   /**< 密码（可选） */
        char topic_prefix[LWS_MAX_TOPIC_LEN];  /**< Topic前缀 */
    } mqtt;

    /* 通用配置 */
    int nonblock;               /**< 非阻塞模式（默认=1） */
    int recv_buf_size;          /**< 接收缓冲区大小（0=默认） */
    int send_buf_size;          /**< 发送缓冲区大小（0=默认） */
} lws_trans_config_t;

/* ========================================
 * 核心API
 * ======================================== */

/**
 * @brief 创建Transport实例
 * @param config 配置
 * @param handler 回调函数
 * @return Transport实例，失败返回NULL
 */
lws_trans_t* lws_trans_create(
    const lws_trans_config_t* config,
    const lws_trans_handler_t* handler
);

/**
 * @brief 销毁Transport实例
 * @param trans Transport实例
 */
void lws_trans_destroy(lws_trans_t* trans);

/**
 * @brief 打开Transport（建立连接或绑定端口）
 * @param trans Transport实例
 * @return 0成功，-1失败
 */
int lws_trans_open(lws_trans_t* trans);

/**
 * @brief 关闭Transport
 * @param trans Transport实例
 */
void lws_trans_close(lws_trans_t* trans);

/**
 * @brief 连接到远端地址（TCP client）
 * @param trans Transport实例
 * @param addr 远端地址
 * @param port 远端端口
 * @return 0成功，-1失败
 */
int lws_trans_connect(lws_trans_t* trans, const char* addr, uint16_t port);

/**
 * @brief 发送数据
 * @param trans Transport实例
 * @param data 数据指针
 * @param len 数据长度
 * @param to 目标地址（UDP需要，TCP/TLS/MQTT可为NULL）
 * @return 实际发送字节数，-1失败
 */
int lws_trans_send(
    lws_trans_t* trans,
    const void* data,
    int len,
    const lws_addr_t* to
);

/**
 * @brief Transport事件循环（接收数据）
 *
 * 功能：
 * 1. 接收网络数据
 * 2. 调用on_data回调传递给应用层
 * 3. 应用层负责协议解析和分发
 *
 * @param trans Transport实例
 * @param timeout_ms 超时时间（毫秒），-1表示阻塞
 * @return 处理的数据包数量，-1失败
 */
int lws_trans_loop(lws_trans_t* trans, int timeout_ms);

/**
 * @brief 获取本地地址
 * @param trans Transport实例
 * @param addr 输出本地地址
 * @return 0成功，-1失败
 */
int lws_trans_get_local_addr(lws_trans_t* trans, lws_addr_t* addr);

/**
 * @brief 获取远端地址（TCP/TLS）
 * @param trans Transport实例
 * @param addr 输出远端地址
 * @return 0成功，-1失败，UDP返回-1
 */
int lws_trans_get_remote_addr(lws_trans_t* trans, lws_addr_t* addr);

/**
 * @brief 获取传输类型
 * @param trans Transport实例
 * @return 传输类型
 */
lws_trans_type_t lws_trans_get_type(lws_trans_t* trans);

/**
 * @brief 检查Transport是否已连接
 * @param trans Transport实例
 * @return 1已连接，0未连接
 */
int lws_trans_is_connected(lws_trans_t* trans);

/**
 * @brief 获取文件描述符（socket fd）
 * @param trans Transport实例
 * @return 文件描述符，失败返回-1
 */
int lws_trans_get_fd(lws_trans_t* trans);

/* ========================================
 * 辅助函数
 * ======================================== */

/**
 * @brief 初始化UDP配置（默认值）
 * @param config 配置结构体
 * @param port 绑定端口（0=自动分配）
 */
void lws_trans_init_udp_config(lws_trans_config_t* config, uint16_t port);

/**
 * @brief 初始化TCP配置（默认值）
 * @param config 配置结构体
 * @param port 绑定端口
 */
void lws_trans_init_tcp_config(lws_trans_config_t* config, uint16_t port);

/**
 * @brief 初始化TLS配置（默认值）
 * @param config 配置结构体
 * @param port 绑定端口
 */
void lws_trans_init_tls_config(lws_trans_config_t* config, uint16_t port);

/**
 * @brief 初始化MQTT配置（默认值）
 * @param config 配置结构体
 * @param broker MQTT broker地址
 * @param port MQTT端口（0=默认1883）
 * @param client_id 客户端ID
 */
void lws_trans_init_mqtt_config(
    lws_trans_config_t* config,
    const char* broker,
    uint16_t port,
    const char* client_id
);

/**
 * @brief 地址转字符串
 * @param addr 地址
 * @param buf 输出缓冲区
 * @param size 缓冲区大小
 * @return 字符串长度
 */
int lws_addr_to_string(const lws_addr_t* addr, char* buf, size_t size);

/**
 * @brief 字符串转地址
 * @param str 字符串（格式："ip:port" 或 "ip"）
 * @param addr 输出地址
 * @return 0成功，-1失败
 */
int lws_addr_from_string(const char* str, lws_addr_t* addr);

/**
 * @brief 比较两个地址
 * @param addr1 地址1
 * @param addr2 地址2
 * @return 1相同，0不同
 */
int lws_addr_equals(const lws_addr_t* addr1, const lws_addr_t* addr2);

/* ========================================
 * MQTT特定API
 * ======================================== */

/**
 * @brief 设置MQTT topic（用于收发）
 * @param trans Transport实例
 * @param topic Topic字符串
 * @return 0成功，-1失败
 */
int lws_trans_mqtt_set_topic(lws_trans_t* trans, const char* topic);

/**
 * @brief 订阅MQTT topic
 * @param trans Transport实例
 * @param topic Topic字符串
 * @return 0成功，-1失败
 */
int lws_trans_mqtt_subscribe(lws_trans_t* trans, const char* topic);

/**
 * @brief 取消订阅MQTT topic
 * @param trans Transport实例
 * @param topic Topic字符串
 * @return 0成功，-1失败
 */
int lws_trans_mqtt_unsubscribe(lws_trans_t* trans, const char* topic);

#ifdef __cplusplus
}
#endif

#endif // __LWS_TRANS_H__
