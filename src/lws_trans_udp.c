/**
 * @file lws_trans_udp.c
 * @brief UDP transport implementation
 *
 * UDP传输层实现，支持：
 * - 非阻塞UDP socket
 * - 发送/接收数据
 * - 事件循环
 */

#include "lws_intl.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

/* ========================================
 * UDP Transport 结构体
 * ======================================== */

typedef struct {
    int fd;                          /**< Socket文件描述符 */

    /* 绑定信息 */
    char bind_addr[LWS_MAX_IP_LEN];
    uint16_t bind_port;

    /* 回调函数 */
    lws_trans_handler_t handler;

    /* 接收缓冲区 */
    char recv_buffer[LWS_LARGE_BUF_SIZE];
} lws_trans_udp_t;

/* ========================================
 * 虚函数实现
 * ======================================== */

static void udp_destroy(lws_trans_t* trans)
{
    if (!trans) {
        return;
    }

    lws_trans_udp_t* udp = (lws_trans_udp_t*)trans->impl;
    if (udp) {
        if (udp->fd >= 0) {
            close(udp->fd);
        }
        lws_free(udp);
    }

    lws_free(trans);
}

static int udp_connect(lws_trans_t* trans, const char* addr, uint16_t port)
{
    /* UDP不支持connect操作 */
    (void)trans;
    (void)addr;
    (void)port;
    return LWS_ENOTSUP;
}

static int udp_send(lws_trans_t* trans, const void* data, int len,
                    const lws_addr_t* to)
{
    if (!trans || !data || len <= 0 || !to) {
        printf("[UDP_SEND] ERROR: Invalid parameters (trans=%p, data=%p, len=%d, to=%p)\n",
               trans, data, len, to);
        fflush(stdout);
        return LWS_EINVAL;
    }

    lws_trans_udp_t* udp = (lws_trans_udp_t*)trans->impl;

    /* 解析目标地址 */
    struct sockaddr_in addr;
    if (lws_trans_parse_addr(to->ip, &addr) != LWS_OK) {
        printf("[UDP_SEND] ERROR: Failed to parse address: %s\n", to->ip);
        fflush(stdout);
        return LWS_EINVAL;
    }
    addr.sin_port = htons(to->port);

    printf("[UDP_SEND] Sending %d bytes to %s:%d\n", len, to->ip, to->port);
    printf("[UDP_SEND] First 100 bytes: %.100s\n", (char*)data);
    fflush(stdout);

    /* 发送数据 */
    int sent = sendto(udp->fd, data, len, 0,
                     (struct sockaddr*)&addr, sizeof(addr));

    printf("[UDP_SEND] sendto() returned: %d (errno=%d: %s)\n", sent, errno, sent < 0 ? strerror(errno) : "OK");
    fflush(stdout);

    return sent;
}

static int udp_loop(lws_trans_t* trans, int timeout_ms)
{
    if (!trans) {
        return LWS_EINVAL;
    }

    lws_trans_udp_t* udp = (lws_trans_udp_t*)trans->impl;

    if (udp->fd < 0) {
        return LWS_ERROR;
    }

    /* 使用poll等待事件 */
    struct pollfd pfd;
    pfd.fd = udp->fd;
    pfd.events = POLLIN;

    printf("[UDP_LOOP] Calling poll(fd=%d, timeout=%d)...\n", udp->fd, timeout_ms);
    fflush(stdout);

    int ret = poll(&pfd, 1, timeout_ms);

    printf("[UDP_LOOP] poll() returned: %d (errno=%d)\n", ret, errno);
    fflush(stdout);

    if (ret < 0) {
        if (errno == EINTR) {
            return LWS_OK;
        }
        return LWS_ERROR;
    }

    if (ret == 0) {
        /* 超时 */
        return LWS_OK;
    }

    /* 处理可读事件 */
    if (pfd.revents & POLLIN) {
        printf("[UDP_LOOP] POLLIN detected! Calling recvfrom()...\n");
        fflush(stdout);

        struct sockaddr_in from_addr;
        socklen_t addr_len = sizeof(from_addr);

        int n = recvfrom(udp->fd, udp->recv_buffer, sizeof(udp->recv_buffer), 0,
                       (struct sockaddr*)&from_addr, &addr_len);

        printf("[UDP_LOOP] recvfrom() returned: %d bytes (errno=%d)\n", n, errno);
        fflush(stdout);

        if (n > 0) {
            lws_addr_t from;
            inet_ntop(AF_INET, &from_addr.sin_addr, from.ip, sizeof(from.ip));
            from.port = ntohs(from_addr.sin_port);

            printf("[UDP_LOOP] Received %d bytes from %s:%d\n", n, from.ip, from.port);
            printf("[UDP_LOOP] First 100 bytes: %.100s\n", udp->recv_buffer);
            fflush(stdout);

            if (udp->handler.on_data) {
                printf("[UDP_LOOP] Calling on_data callback...\n");
                fflush(stdout);

                udp->handler.on_data(trans, udp->recv_buffer, n, &from,
                                   udp->handler.userdata);

                printf("[UDP_LOOP] on_data callback returned\n");
                fflush(stdout);
            } else {
                printf("[UDP_LOOP] WARNING: on_data callback is NULL!\n");
                fflush(stdout);
            }
        } else if (n < 0) {
            printf("[UDP_LOOP] recvfrom error: %s\n", strerror(errno));
            fflush(stdout);

            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                if (udp->handler.on_error) {
                    udp->handler.on_error(trans, LWS_ERROR, "recvfrom failed",
                                        udp->handler.userdata);
                }
                return LWS_ERROR;
            }
        }
    } else {
        printf("[UDP_LOOP] poll() returned but no POLLIN (revents=0x%x)\n", pfd.revents);
        fflush(stdout);
    }

    return LWS_OK;
}

static int udp_get_fd(lws_trans_t* trans)
{
    if (!trans) {
        return -1;
    }

    lws_trans_udp_t* udp = (lws_trans_udp_t*)trans->impl;
    return udp->fd;
}

static int udp_get_local_addr(lws_trans_t* trans, lws_addr_t* addr)
{
    if (!trans || !addr) {
        return LWS_EINVAL;
    }

    lws_trans_udp_t* udp = (lws_trans_udp_t*)trans->impl;

    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);

    if (getsockname(udp->fd, (struct sockaddr*)&local_addr, &addr_len) < 0) {
        return LWS_ERROR;
    }

    inet_ntop(AF_INET, &local_addr.sin_addr, addr->ip, sizeof(addr->ip));
    addr->port = ntohs(local_addr.sin_port);

    return LWS_OK;
}

/* ========================================
 * 虚函数表
 * ======================================== */

static const lws_trans_ops_t udp_ops = {
    .destroy = udp_destroy,
    .connect = udp_connect,
    .send = udp_send,
    .loop = udp_loop,
    .get_fd = udp_get_fd,
    .get_local_addr = udp_get_local_addr,
};

/* ========================================
 * UDP Transport创建函数
 * ======================================== */

lws_trans_t* lws_trans_udp_create(const lws_trans_config_t* config,
                                   const lws_trans_handler_t* handler)
{
    if (!config) {
        return NULL;
    }

    /* 创建transport基础结构 */
    lws_trans_t* trans = (lws_trans_t*)lws_calloc(1, sizeof(lws_trans_t));
    if (!trans) {
        lws_log_error(LWS_ENOMEM, "Failed to allocate lws_trans_t\n");
        return NULL;
    }

    /* 创建UDP实现结构 */
    lws_trans_udp_t* udp = (lws_trans_udp_t*)lws_calloc(1, sizeof(lws_trans_udp_t));
    if (!udp) {
        lws_log_error(LWS_ENOMEM, "Failed to allocate lws_trans_udp_t\n");
        lws_free(trans);
        return NULL;
    }

    /* 设置transport基础信息 */
    trans->type = LWS_TRANS_TYPE_UDP;
    trans->ops = &udp_ops;
    trans->impl = udp;

    /* 初始化UDP */
    udp->fd = -1;

    /* 保存配置 */
    if (config->sock.bind_addr[0]) {
        strncpy(udp->bind_addr, config->sock.bind_addr, LWS_MAX_IP_LEN - 1);
    }
    udp->bind_port = config->sock.bind_port;

    /* 保存回调 */
    if (handler) {
        udp->handler = *handler;
    }

    /* 创建UDP socket */
    udp->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp->fd < 0) {
        lws_log_error(LWS_ERR_SOCK_CREATE, "Failed to create UDP socket: %d\n", errno);
        lws_free(udp);
        lws_free(trans);
        return NULL;
    }

    /* 设置非阻塞 */
    if (lws_trans_set_nonblocking(udp->fd) != LWS_OK) {
        lws_log_error(LWS_ERR_SOCK_SETOPT, "Failed to set socket non-blocking\n");
        close(udp->fd);
        lws_free(udp);
        lws_free(trans);
        return NULL;
    }

    /* 设置重用地址 */
    if (config->sock.reuse_addr) {
        lws_trans_set_reuseaddr(udp->fd, 1);
    }

    /* 绑定地址 */
    struct sockaddr_in addr;
    if (lws_trans_parse_addr(udp->bind_addr, &addr) != LWS_OK) {
        lws_log_error(LWS_ERR_INVALID_ADDR, "Failed to parse bind address: %s\n", udp->bind_addr);
        close(udp->fd);
        lws_free(udp);
        lws_free(trans);
        return NULL;
    }
    addr.sin_port = htons(udp->bind_port);

    if (bind(udp->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        lws_log_error(LWS_ERR_SOCK_BIND, "Failed to bind to %s:%d: %d\n",
                     udp->bind_addr, udp->bind_port, errno);
        close(udp->fd);
        lws_free(udp);
        lws_free(trans);
        return NULL;
    }

    return trans;
}
