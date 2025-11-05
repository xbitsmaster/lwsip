/**
 * @file lws_transport_tcp.c
 * @brief TCP/UDP Socket Transport Implementation
 *
 * This is a standard socket-based transport for SIP over TCP/UDP.
 */

#include "lws_transport.h"
#include "lws_error.h"
#include "lws_mem.h"
#include "lws_log.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

/* ============================================================
 * TCP Transport Structure
 * ============================================================ */

typedef struct lws_transport_tcp {
    lws_transport_t base;  // Must be first!

    // Socket handles
    int sockfd;

    // Addresses
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;

    // Buffer for receiving
    char recv_buffer[8192];
} lws_transport_tcp_t;

/* ============================================================
 * Forward Declarations
 * ============================================================ */

static int tcp_connect(lws_transport_t* transport);
static void tcp_disconnect(lws_transport_t* transport);
static int tcp_send(lws_transport_t* transport, const void* data, int len);
static lws_transport_state_t tcp_get_state(lws_transport_t* transport);
static int tcp_get_local_addr(lws_transport_t* transport, char* ip, uint16_t* port);
static int tcp_poll(lws_transport_t* transport, int timeout_ms);
static void tcp_destroy(lws_transport_t* transport);

/* ============================================================
 * Operations Table
 * ============================================================ */

static const lws_transport_ops_t tcp_ops = {
    .connect = tcp_connect,
    .disconnect = tcp_disconnect,
    .send = tcp_send,
    .get_state = tcp_get_state,
    .get_local_addr = tcp_get_local_addr,
    .poll = tcp_poll,
    .destroy = tcp_destroy,
};

/* ============================================================
 * Helper Functions
 * ============================================================ */

static int set_nonblocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

static int create_socket(lws_transport_tcp_t* tcp)
{
    int sockfd;
    int reuse = 1;

    // Create socket
    sockfd = socket(AF_INET,
                    tcp->base.config.use_tcp ? SOCK_STREAM : SOCK_DGRAM,
                    0);
    if (sockfd < 0) {
        lws_log_error(LWS_ERR_SOCKET_CREATE, "failed to create socket: %s\n",
                      strerror(errno));
        return -1;
    }

    // Set socket options
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Set non-blocking
    if (set_nonblocking(sockfd) < 0) {
        lws_log_warn(0, "failed to set non-blocking\n");
    }

    // Bind to local port if specified
    if (tcp->base.config.local_port > 0) {
        memset(&tcp->local_addr, 0, sizeof(tcp->local_addr));
        tcp->local_addr.sin_family = AF_INET;
        tcp->local_addr.sin_addr.s_addr = INADDR_ANY;
        tcp->local_addr.sin_port = htons(tcp->base.config.local_port);

        if (bind(sockfd, (struct sockaddr*)&tcp->local_addr,
                 sizeof(tcp->local_addr)) < 0) {
            lws_log_error(LWS_ERR_SOCKET_BIND,
                          "failed to bind to port %d: %s\n",
                          tcp->base.config.local_port, strerror(errno));
            close(sockfd);
            return -1;
        }

        lws_log_info("bound to local port %d\n", tcp->base.config.local_port);
    }

    tcp->sockfd = sockfd;
    return 0;
}

/* ============================================================
 * Operations Implementation
 * ============================================================ */

static int tcp_connect(lws_transport_t* transport)
{
    lws_transport_tcp_t* tcp = (lws_transport_tcp_t*)transport;
    int ret;

    if (!transport) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (tcp->base.state == LWS_TRANSPORT_STATE_CONNECTED) {
        return LWS_OK;
    }

    lws_log_info("connecting to %s:%d via %s\n",
                 tcp->base.config.remote_host,
                 tcp->base.config.remote_port,
                 tcp->base.config.use_tcp ? "TCP" : "UDP");

    // Create socket
    if (create_socket(tcp) < 0) {
        return LWS_ERR_SOCKET_CREATE;
    }

    // Setup remote address
    memset(&tcp->remote_addr, 0, sizeof(tcp->remote_addr));
    tcp->remote_addr.sin_family = AF_INET;
    tcp->remote_addr.sin_port = htons(tcp->base.config.remote_port);

    if (inet_pton(AF_INET, tcp->base.config.remote_host,
                  &tcp->remote_addr.sin_addr) <= 0) {
        lws_log_error(LWS_ERR_SOCKET_CONNECT,
                      "invalid address: %s\n",
                      tcp->base.config.remote_host);
        close(tcp->sockfd);
        tcp->sockfd = -1;
        return LWS_ERR_SOCKET_CONNECT;
    }

    // Connect (for TCP) or just mark as connected (for UDP)
    if (tcp->base.config.use_tcp) {
        ret = connect(tcp->sockfd,
                      (struct sockaddr*)&tcp->remote_addr,
                      sizeof(tcp->remote_addr));

        if (ret < 0 && errno != EINPROGRESS) {
            lws_log_error(LWS_ERR_SOCKET_CONNECT,
                          "connect failed: %s\n", strerror(errno));
            close(tcp->sockfd);
            tcp->sockfd = -1;
            tcp->base.state = LWS_TRANSPORT_STATE_ERROR;

            if (tcp->base.handler.on_state) {
                tcp->base.handler.on_state(transport,
                                           LWS_TRANSPORT_STATE_ERROR,
                                           tcp->base.handler.userdata);
            }
            return LWS_ERR_SOCKET_CONNECT;
        }

        if (errno == EINPROGRESS) {
            tcp->base.state = LWS_TRANSPORT_STATE_CONNECTING;
            lws_log_info("connection in progress...\n");
            return LWS_OK;
        }
    }

    tcp->base.state = LWS_TRANSPORT_STATE_CONNECTED;
    lws_log_info("transport connected\n");

    if (tcp->base.handler.on_state) {
        tcp->base.handler.on_state(transport,
                                   LWS_TRANSPORT_STATE_CONNECTED,
                                   tcp->base.handler.userdata);
    }

    return LWS_OK;
}

static void tcp_disconnect(lws_transport_t* transport)
{
    lws_transport_tcp_t* tcp = (lws_transport_tcp_t*)transport;

    if (!transport) {
        return;
    }

    if (tcp->sockfd >= 0) {
        close(tcp->sockfd);
        tcp->sockfd = -1;
    }

    tcp->base.state = LWS_TRANSPORT_STATE_DISCONNECTED;
    lws_log_info("transport disconnected\n");

    if (tcp->base.handler.on_state) {
        tcp->base.handler.on_state(transport,
                                   LWS_TRANSPORT_STATE_DISCONNECTED,
                                   tcp->base.handler.userdata);
    }
}

static int tcp_send(lws_transport_t* transport, const void* data, int len)
{
    lws_transport_tcp_t* tcp = (lws_transport_tcp_t*)transport;
    int ret;

    if (!transport || !data || len <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (tcp->sockfd < 0) {
        lws_log_error(LWS_ERR_SOCKET_SEND, "socket not connected\n");
        return LWS_ERR_SOCKET_SEND;
    }

    if (tcp->base.config.use_tcp) {
        // TCP: use send()
        ret = send(tcp->sockfd, data, len, 0);
    } else {
        // UDP: use sendto()
        ret = sendto(tcp->sockfd, data, len, 0,
                     (struct sockaddr*)&tcp->remote_addr,
                     sizeof(tcp->remote_addr));
    }

    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // Would block, try again later
        }

        lws_log_error(LWS_ERR_SOCKET_SEND,
                      "send failed: %s\n", strerror(errno));
        return LWS_ERR_SOCKET_SEND;
    }

    return ret;
}

static lws_transport_state_t tcp_get_state(lws_transport_t* transport)
{
    if (!transport) {
        return LWS_TRANSPORT_STATE_DISCONNECTED;
    }

    return transport->state;
}

static int tcp_get_local_addr(lws_transport_t* transport,
    char* ip,
    uint16_t* port)
{
    lws_transport_tcp_t* tcp = (lws_transport_tcp_t*)transport;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    if (!transport) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (tcp->sockfd < 0) {
        return LWS_ERR_SOCKET_CREATE;
    }

    if (getsockname(tcp->sockfd, (struct sockaddr*)&addr, &addr_len) < 0) {
        lws_log_error(LWS_ERR_SOCKET_CREATE,
                      "getsockname failed: %s\n", strerror(errno));
        return LWS_ERR_SOCKET_CREATE;
    }

    if (ip) {
        inet_ntop(AF_INET, &addr.sin_addr, ip, 64);
    }

    if (port) {
        *port = ntohs(addr.sin_port);
    }

    return LWS_OK;
}

static int tcp_poll(lws_transport_t* transport, int timeout_ms)
{
    lws_transport_tcp_t* tcp = (lws_transport_tcp_t*)transport;
    struct pollfd pfd;
    int ret;
    int events = 0;

    if (!transport) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (tcp->sockfd < 0) {
        return 0;
    }

    // Setup poll
    pfd.fd = tcp->sockfd;
    pfd.events = POLLIN;

    // Check if still connecting
    if (tcp->base.state == LWS_TRANSPORT_STATE_CONNECTING) {
        pfd.events |= POLLOUT;
    }

    pfd.revents = 0;

    ret = poll(&pfd, 1, timeout_ms);

    if (ret < 0) {
        if (errno == EINTR) {
            return 0;
        }
        lws_log_error(LWS_ERR_SOCKET_RECV,
                      "poll failed: %s\n", strerror(errno));
        return LWS_ERR_SOCKET_RECV;
    }

    if (ret == 0) {
        return 0;  // Timeout
    }

    // Check connection completion
    if (tcp->base.state == LWS_TRANSPORT_STATE_CONNECTING &&
        (pfd.revents & POLLOUT)) {
        int error = 0;
        socklen_t len = sizeof(error);

        if (getsockopt(tcp->sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            error = errno;
        }

        if (error) {
            lws_log_error(LWS_ERR_SOCKET_CONNECT,
                          "connection failed: %s\n", strerror(error));
            tcp->base.state = LWS_TRANSPORT_STATE_ERROR;
            if (tcp->base.handler.on_state) {
                tcp->base.handler.on_state(transport,
                                           LWS_TRANSPORT_STATE_ERROR,
                                           tcp->base.handler.userdata);
            }
            return LWS_ERR_SOCKET_CONNECT;
        }

        tcp->base.state = LWS_TRANSPORT_STATE_CONNECTED;
        lws_log_info("connection established\n");

        if (tcp->base.handler.on_state) {
            tcp->base.handler.on_state(transport,
                                       LWS_TRANSPORT_STATE_CONNECTED,
                                       tcp->base.handler.userdata);
        }

        events++;
    }

    // Check for incoming data
    if (pfd.revents & POLLIN) {
        ssize_t n;

        if (tcp->base.config.use_tcp) {
            n = recv(tcp->sockfd, tcp->recv_buffer,
                     sizeof(tcp->recv_buffer), 0);
        } else {
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            n = recvfrom(tcp->sockfd, tcp->recv_buffer,
                         sizeof(tcp->recv_buffer), 0,
                         (struct sockaddr*)&from, &fromlen);
        }

        if (n > 0) {
            // Call receive callback
            if (tcp->base.handler.on_recv) {
                tcp->base.handler.on_recv(transport,
                                          tcp->recv_buffer,
                                          n,
                                          tcp->base.handler.userdata);
            }
            events++;
        } else if (n == 0) {
            // Connection closed (TCP)
            lws_log_info("connection closed by peer\n");
            tcp_disconnect(transport);
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                lws_log_error(LWS_ERR_SOCKET_RECV,
                              "recv failed: %s\n", strerror(errno));
                return LWS_ERR_SOCKET_RECV;
            }
        }
    }

    return events;
}

static void tcp_destroy(lws_transport_t* transport)
{
    lws_transport_tcp_t* tcp = (lws_transport_tcp_t*)transport;

    if (!transport) {
        return;
    }

    tcp_disconnect(transport);
    lws_free(tcp);
    lws_log_info("tcp transport destroyed\n");
}

/* ============================================================
 * Factory Function
 * ============================================================ */

lws_transport_t* lws_transport_tcp_create(
    const lws_transport_config_t* config,
    const lws_transport_handler_t* handler)
{
    lws_transport_tcp_t* tcp;

    if (!config || !handler) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    tcp = (lws_transport_tcp_t*)lws_malloc(sizeof(lws_transport_tcp_t));
    if (!tcp) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate tcp transport\n");
        return NULL;
    }

    memset(tcp, 0, sizeof(lws_transport_tcp_t));

    // Setup base
    tcp->base.ops = &tcp_ops;
    memcpy(&tcp->base.config, config, sizeof(lws_transport_config_t));
    memcpy(&tcp->base.handler, handler, sizeof(lws_transport_handler_t));
    tcp->base.state = LWS_TRANSPORT_STATE_DISCONNECTED;

    // Initialize socket
    tcp->sockfd = -1;

    lws_log_info("tcp transport created: %s:%d (%s)\n",
                 config->remote_host,
                 config->remote_port,
                 config->use_tcp ? "TCP" : "UDP");

    return &tcp->base;
}
