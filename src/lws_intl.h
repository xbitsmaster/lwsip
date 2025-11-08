/**
 * @file lws_intl.h
 * @brief Transport layer internal definitions
 *
 * 传输层内部头文件，仅供传输层实现文件使用
 * 包含：
 * - 虚函数表定义
 * - 内部数据结构
 * - 辅助函数声明
 */

#ifndef __LWS_INTL_H__
#define __LWS_INTL_H__

#include "lws_trans.h"
#include "lws_defs.h"
#include "lws_err.h"
#include "lws_mem.h"
#include "lws_log.h"

/* ========================================
 * 虚函数表定义
 * ======================================== */

/**
 * @brief Transport操作虚函数表
 *
 * 定义了所有传输层类型必须实现的操作接口
 */
typedef struct {
    /** 销毁transport */
    void (*destroy)(lws_trans_t* trans);

    /** 连接到远端（TCP client） */
    int (*connect)(lws_trans_t* trans, const char* addr, uint16_t port);

    /** 发送数据 */
    int (*send)(lws_trans_t* trans, const void* data, int len, const lws_addr_t* to);

    /** 事件循环 */
    int (*loop)(lws_trans_t* trans, int timeout_ms);

    /** 获取文件描述符 */
    int (*get_fd)(lws_trans_t* trans);

    /** 获取本地地址 */
    int (*get_local_addr)(lws_trans_t* trans, lws_addr_t* addr);
} lws_trans_ops_t;

/* ========================================
 * Transport基础结构
 * ======================================== */

/**
 * @brief Transport基础结构
 *
 * 所有传输层类型的基础结构，采用虚函数表模式实现多态
 */
struct lws_trans_t {
    lws_trans_type_t type;          /**< 传输类型 */
    const lws_trans_ops_t* ops;     /**< 虚函数表指针 */
    void* impl;                      /**< 具体实现数据指针 */
};

/* ========================================
 * 公共辅助函数声明
 * ======================================== */

/**
 * @brief 设置socket为非阻塞模式
 * @param fd socket文件描述符
 * @return LWS_OK on success, LWS_ERROR on failure
 */
int lws_trans_set_nonblocking(int fd);

/**
 * @brief 设置socket重用地址选项
 * @param fd socket文件描述符
 * @param reuse 1=启用重用, 0=禁用重用
 * @return LWS_OK on success, LWS_ERROR on failure
 */
int lws_trans_set_reuseaddr(int fd, int reuse);

/**
 * @brief 解析IP地址字符串到sockaddr_in结构
 * @param addr_str IP地址字符串 (如 "127.0.0.1" 或 "0.0.0.0")
 * @param addr 输出的sockaddr_in结构
 * @return LWS_OK on success, LWS_EINVAL on failure
 */
struct sockaddr_in;  /* forward declaration */
int lws_trans_parse_addr(const char* addr_str, struct sockaddr_in* addr);

/* ========================================
 * 各传输类型的创建函数声明
 * ======================================== */

/**
 * @brief 创建UDP传输实例
 * @param config 传输配置
 * @param handler 事件处理器
 * @return Transport实例指针，失败返回NULL
 */
lws_trans_t* lws_trans_udp_create(const lws_trans_config_t* config,
                                   const lws_trans_handler_t* handler);

/* 未来扩展的传输类型：
 * lws_trans_t* lws_trans_tcp_create(...);
 * lws_trans_t* lws_trans_tls_create(...);
 * lws_trans_t* lws_trans_mqtt_create(...);
 */

#endif /* __LWS_INTL_H__ */
