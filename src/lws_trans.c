/**
 * @file lws_trans.c
 * @brief Transport layer common code
 *
 * 传输层公共代码，提供：
 * - 虚函数表架构
 * - 传输层工厂函数
 * - 公共辅助函数
 */

#include "lws_intl.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ========================================
 * 公共辅助函数
 * ======================================== */

/**
 * @brief 设置socket为非阻塞模式
 */
int lws_trans_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return LWS_ERROR;
    }

    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        return LWS_ERROR;
    }

    return LWS_OK;
}

/**
 * @brief 设置socket重用地址
 */
int lws_trans_set_reuseaddr(int fd, int reuse)
{
    int opt = reuse ? 1 : 0;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        return LWS_ERROR;
    }
    return LWS_OK;
}

/**
 * @brief 解析地址字符串
 */
int lws_trans_parse_addr(const char* addr_str, struct sockaddr_in* addr)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;

    if (!addr_str || strlen(addr_str) == 0 || strcmp(addr_str, "0.0.0.0") == 0) {
        addr->sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, addr_str, &addr->sin_addr) <= 0) {
            return LWS_EINVAL;
        }
    }

    return LWS_OK;
}

/* ========================================
 * 公共API实现（工厂模式）
 * ======================================== */

lws_trans_t* lws_trans_create(const lws_trans_config_t* config,
                               const lws_trans_handler_t* handler)
{
    if (!config) {
        return NULL;
    }

    switch (config->type) {
        case LWS_TRANS_TYPE_UDP:
            return lws_trans_udp_create(config, handler);

#ifdef TRANS_MQTT
        case LWS_TRANS_TYPE_MQTT:
            return lws_trans_mqtt_create(config, handler);
#endif

        case LWS_TRANS_TYPE_TCP_SERVER:
        case LWS_TRANS_TYPE_TCP_CLIENT:
        case LWS_TRANS_TYPE_TLS:
            /* 未实现的传输类型 */
            return NULL;

        default:
            return NULL;
    }
}

void lws_trans_destroy(lws_trans_t* trans)
{
    if (!trans) {
        return;
    }

    if (trans->ops && trans->ops->destroy) {
        trans->ops->destroy(trans);
    } else {
        free(trans);
    }
}

int lws_trans_connect(lws_trans_t* trans, const char* addr, uint16_t port)
{
    if (!trans || !trans->ops || !trans->ops->connect) {
        return LWS_ENOTSUP;
    }

    return trans->ops->connect(trans, addr, port);
}

int lws_trans_send(lws_trans_t* trans, const void* data, int len,
                   const lws_addr_t* to)
{
    if (!trans || !trans->ops || !trans->ops->send) {
        return LWS_EINVAL;
    }

    return trans->ops->send(trans, data, len, to);
}

int lws_trans_loop(lws_trans_t* trans, int timeout_ms)
{
    if (!trans || !trans->ops || !trans->ops->loop) {
        return LWS_EINVAL;
    }

    return trans->ops->loop(trans, timeout_ms);
}

int lws_trans_get_fd(lws_trans_t* trans)
{
    if (!trans || !trans->ops || !trans->ops->get_fd) {
        return -1;
    }

    return trans->ops->get_fd(trans);
}

int lws_trans_get_local_addr(lws_trans_t* trans, lws_addr_t* addr)
{
    if (!trans || !trans->ops || !trans->ops->get_local_addr) {
        return LWS_EINVAL;
    }

    return trans->ops->get_local_addr(trans, addr);
}

/* ========================================
 * 辅助函数实现
 * ======================================== */

void lws_trans_init_udp_config(lws_trans_config_t* config, uint16_t port)
{
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->type = LWS_TRANS_TYPE_UDP;
    config->sock.bind_port = port;
    config->sock.reuse_addr = 1;
    config->nonblock = 1;
}

int lws_addr_to_string(const lws_addr_t* addr, char* buf, size_t size)
{
    if (!addr || !buf || size == 0) {
        return 0;
    }

    return snprintf(buf, size, "%s:%d", addr->ip, addr->port);
}

int lws_addr_from_string(const char* str, lws_addr_t* addr)
{
    if (!str || !addr) {
        return LWS_EINVAL;
    }

    memset(addr, 0, sizeof(*addr));

    /* 解析 "ip:port" 格式 */
    char ip[LWS_MAX_IP_LEN];
    int port = 0;

    if (sscanf(str, "%[^:]:%d", ip, &port) == 2) {
        strncpy(addr->ip, ip, LWS_MAX_IP_LEN - 1);
        addr->port = (uint16_t)port;
        addr->family = AF_INET;
        return LWS_OK;
    }

    /* 只有IP，没有端口 */
    if (sscanf(str, "%s", ip) == 1) {
        strncpy(addr->ip, ip, LWS_MAX_IP_LEN - 1);
        addr->port = 0;
        addr->family = AF_INET;
        return LWS_OK;
    }

    return LWS_EINVAL;
}

int lws_addr_equals(const lws_addr_t* addr1, const lws_addr_t* addr2)
{
    if (!addr1 || !addr2) {
        return 0;
    }

    return (strcmp(addr1->ip, addr2->ip) == 0) && (addr1->port == addr2->port);
}

/* ========================================
 * Config初始化辅助函数
 * ======================================== */

void lws_trans_init_mqtt_config(
    lws_trans_config_t* config,
    const char* broker,
    uint16_t port,
    const char* client_id)
{
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));

    config->type = LWS_TRANS_TYPE_MQTT;

    /* MQTT配置 */
    if (broker) {
        strncpy(config->mqtt.broker, broker, sizeof(config->mqtt.broker) - 1);
    }
    config->mqtt.port = port > 0 ? port : LWS_DEFAULT_MQTT_PORT;

    if (client_id) {
        strncpy(config->mqtt.client_id, client_id, sizeof(config->mqtt.client_id) - 1);
    } else {
        /* 生成默认client ID */
        snprintf(config->mqtt.client_id, sizeof(config->mqtt.client_id),
                "lwsip_%lu", (unsigned long)time(NULL));
    }

    /* 默认topic前缀 */
    strncpy(config->mqtt.topic_prefix, "lwsip", sizeof(config->mqtt.topic_prefix) - 1);

    /* 通用配置 */
    config->nonblock = 1;
}
