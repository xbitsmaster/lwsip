/**
 * @file lws_transport_socket.c
 * @brief Socket-Based Transport Implementation (TCP/UDP)
 *
 * This is a BSD socket-based transport implementation that supports both
 * TCP and UDP protocols for SIP signaling.
 *
 * Features:
 * - TCP: Reliable, connection-oriented transport
 * - UDP: Connectionless, best-effort delivery transport
 * - Non-blocking I/O with poll()-based event loop
 * - Automatic protocol selection via configuration
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
 * Socket Transport Structure
 *
 * Note: base doesn't need to be the first member!
 * We use lws_container_of for type-safe conversions
 * ============================================================ */

typedef struct lws_transport_socket {
    // Socket handle
    int sockfd;

    // Network addresses
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;

    // Receive buffer
    char recv_buffer[8192];

    // Protocol type: TCP or UDP
    int is_tcp;  // 1 for TCP, 0 for UDP

    // Base transport (can be at any position in the struct)
    lws_transport_t base;
} lws_transport_socket_t;

/**
 * @brief Type-safe conversion: from lws_transport_t* to lws_transport_socket_t*
 * Uses container_of instead of simple cast
 */
static inline lws_transport_socket_t* to_socket_transport(lws_transport_t* transport)
{
    if (!transport) {
        return NULL;
    }
    return lws_container_of(transport, lws_transport_socket_t, base);
}

/* ============================================================
 * Forward Declarations
 * ============================================================ */

static int socket_connect(lws_transport_t* transport);
static void socket_disconnect(lws_transport_t* transport);
static int socket_send(lws_transport_t* transport, const void* data, int len);
static lws_transport_state_t socket_get_state(lws_transport_t* transport);
static int socket_get_local_addr(lws_transport_t* transport, char* ip, uint16_t* port);
static int socket_poll(lws_transport_t* transport, int timeout_ms);
static void socket_destroy(lws_transport_t* transport);

/* ============================================================
 * Operations Table
 * ============================================================ */

static const lws_transport_ops_t socket_ops = {
    .connect = socket_connect,
    .disconnect = socket_disconnect,
    .send = socket_send,
    .get_state = socket_get_state,
    .get_local_addr = socket_get_local_addr,
    .poll = socket_poll,
    .destroy = socket_destroy,
};

/* ============================================================
 * Helper Functions
 * ============================================================ */

static int set_nonblocking(int sockfd)
{
    lws_log_trace("set_nonblocking: sockfd=%d\n", sockfd);
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

static int create_socket(lws_transport_socket_t* sock)
{
    lws_log_trace("create_socket: protocol=%s, local_port=%d\n",
                  sock->is_tcp ? "TCP" : "UDP",
                  sock->base.config.local_port);
    int sockfd;
    int reuse = 1;
    int sock_type;

    // Determine socket type based on protocol
    sock_type = sock->is_tcp ? SOCK_STREAM : SOCK_DGRAM;

    // Create socket
    sockfd = socket(AF_INET, sock_type, 0);
    if (sockfd < 0) {
        lws_log_error(LWS_ERR_SOCKET_CREATE, "failed to create socket: %s\n",
                      strerror(errno));
        return -1;
    }

    // Set socket options
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Set non-blocking mode
    if (set_nonblocking(sockfd) < 0) {
        lws_log_warn(0, "failed to set non-blocking mode\n");
    }

    // Bind to local port if specified
    if (sock->base.config.local_port > 0) {
        memset(&sock->local_addr, 0, sizeof(sock->local_addr));
        sock->local_addr.sin_family = AF_INET;
        sock->local_addr.sin_addr.s_addr = INADDR_ANY;
        sock->local_addr.sin_port = htons(sock->base.config.local_port);

        if (bind(sockfd, (struct sockaddr*)&sock->local_addr,
                 sizeof(sock->local_addr)) < 0) {
            lws_log_error(LWS_ERR_SOCKET_BIND,
                          "failed to bind to port %d: %s\n",
                          sock->base.config.local_port, strerror(errno));
            close(sockfd);
            return -1;
        }

        lws_log_info("bound to local port %d\n", sock->base.config.local_port);
    }

    sock->sockfd = sockfd;
    return 0;
}

/* ============================================================
 * Operations Implementation
 * ============================================================ */

static int socket_connect(lws_transport_t* transport)
{
    lws_log_trace("socket_connect: transport=%p\n", (void*)transport);
    lws_transport_socket_t* sock;
    int ret;

    if (!transport) {
        return LWS_ERR_INVALID_PARAM;
    }

    sock = to_socket_transport(transport);

    if (sock->base.state == LWS_TRANSPORT_STATE_CONNECTED) {
        return LWS_OK;
    }

    lws_log_info("connecting to %s:%d via %s\n",
                 sock->base.config.remote_host,
                 sock->base.config.remote_port,
                 sock->is_tcp ? "TCP" : "UDP");

    // Create socket
    if (create_socket(sock) < 0) {
        return LWS_ERR_SOCKET_CREATE;
    }

    // Setup remote address
    memset(&sock->remote_addr, 0, sizeof(sock->remote_addr));
    sock->remote_addr.sin_family = AF_INET;
    sock->remote_addr.sin_port = htons(sock->base.config.remote_port);

    if (inet_pton(AF_INET, sock->base.config.remote_host,
                  &sock->remote_addr.sin_addr) <= 0) {
        lws_log_error(LWS_ERR_SOCKET_CONNECT,
                      "invalid address: %s\n",
                      sock->base.config.remote_host);
        close(sock->sockfd);
        sock->sockfd = -1;
        return LWS_ERR_SOCKET_CONNECT;
    }

    // Connect socket
    // For TCP: establishes connection
    // For UDP: records destination address and lets kernel assign local port/IP
    ret = connect(sock->sockfd,
                  (struct sockaddr*)&sock->remote_addr,
                  sizeof(sock->remote_addr));

    if (ret < 0 && errno != EINPROGRESS) {
        lws_log_error(LWS_ERR_SOCKET_CONNECT,
                      "connect failed: %s\n", strerror(errno));
        close(sock->sockfd);
        sock->sockfd = -1;
        sock->base.state = LWS_TRANSPORT_STATE_ERROR;

        if (sock->base.handler.on_state) {
            sock->base.handler.on_state(transport,
                                       LWS_TRANSPORT_STATE_ERROR,
                                       sock->base.handler.userdata);
        }
        return LWS_ERR_SOCKET_CONNECT;
    }

    if (sock->is_tcp && errno == EINPROGRESS) {
        sock->base.state = LWS_TRANSPORT_STATE_CONNECTING;
        lws_log_info("TCP connection in progress...\n");
        return LWS_OK;
    }

    sock->base.state = LWS_TRANSPORT_STATE_CONNECTED;
    lws_log_info("transport connected\n");

    if (sock->base.handler.on_state) {
        sock->base.handler.on_state(transport,
                                   LWS_TRANSPORT_STATE_CONNECTED,
                                   sock->base.handler.userdata);
    }

    return LWS_OK;
}

static void socket_disconnect(lws_transport_t* transport)
{
    lws_log_trace("socket_disconnect: transport=%p\n", (void*)transport);
    lws_transport_socket_t* sock;

    if (!transport) {
        return;
    }

    sock = to_socket_transport(transport);

    if (sock->sockfd >= 0) {
        close(sock->sockfd);
        sock->sockfd = -1;
    }

    sock->base.state = LWS_TRANSPORT_STATE_DISCONNECTED;
    lws_log_info("transport disconnected\n");

    if (sock->base.handler.on_state) {
        sock->base.handler.on_state(transport,
                                   LWS_TRANSPORT_STATE_DISCONNECTED,
                                   sock->base.handler.userdata);
    }
}

static int socket_send(lws_transport_t* transport, const void* data, int len)
{
    lws_log_trace("socket_send: transport=%p, len=%d\n", (void*)transport, len);
    lws_transport_socket_t* sock;
    int ret;

    if (!transport || !data || len <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    sock = to_socket_transport(transport);

    if (sock->sockfd < 0) {
        lws_log_error(LWS_ERR_SOCKET_SEND, "socket not connected\n");
        return LWS_ERR_SOCKET_SEND;
    }

    // Log what we're about to send
    lws_log_info("========== SENDING SIP MESSAGE (%d bytes, %s) ==========\n",
                 len, sock->is_tcp ? "TCP" : "UDP");
    if (len > 0 && data) {
        // Print first 200 bytes to see what's being sent
        char preview[256];
        int preview_len = (len < 200) ? len : 200;
        memcpy(preview, data, preview_len);
        preview[preview_len] = '\0';
        lws_log_info("%s%s\n", preview, (len > 200) ? "..." : "");
    }
    lws_log_info("===================================================\n");

    // Use send() for both TCP and UDP (since we connect() both socket types)
    ret = send(sock->sockfd, data, len, 0);

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

static lws_transport_state_t socket_get_state(lws_transport_t* transport)
{
    lws_log_trace("socket_get_state: transport=%p\n", (void*)transport);
    if (!transport) {
        return LWS_TRANSPORT_STATE_DISCONNECTED;
    }

    return transport->state;
}

static int socket_get_local_addr(lws_transport_t* transport,
    char* ip,
    uint16_t* port)
{
    lws_log_trace("socket_get_local_addr: transport=%p\n", (void*)transport);
    lws_transport_socket_t* sock;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    if (!transport) {
        return LWS_ERR_INVALID_PARAM;
    }

    sock = to_socket_transport(transport);

    if (sock->sockfd < 0) {
        return LWS_ERR_SOCKET_CREATE;
    }

    if (getsockname(sock->sockfd, (struct sockaddr*)&addr, &addr_len) < 0) {
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

static int socket_poll(lws_transport_t* transport, int timeout_ms)
{
    lws_transport_socket_t* sock;
    struct pollfd pfd;
    int ret;
    int events = 0;

    if (!transport) {
        lws_log_error(LWS_ERR_INVALID_PARAM,
                      "socket_poll: transport is NULL\n");
        return LWS_ERR_INVALID_PARAM;
    }

    sock = to_socket_transport(transport);

    if (sock->sockfd < 0) {
        lws_log_error(LWS_ERR_SOCKET_RECV,
                      "socket_poll: sockfd < 0\n");
        return 0;
    }

    // Setup poll
    pfd.fd = sock->sockfd;
    pfd.events = POLLIN;

    // Check if still connecting (TCP only)
    if (sock->is_tcp && sock->base.state == LWS_TRANSPORT_STATE_CONNECTING) {
        pfd.events |= POLLOUT;
    }

    pfd.revents = 0;

    lws_log_trace("socket_poll: calling poll() on sockfd=%d, timeout_ms=%d, protocol=%s\n",
                  sock->sockfd, timeout_ms, sock->is_tcp ? "TCP" : "UDP");
    ret = poll(&pfd, 1, timeout_ms);
    lws_log_trace("socket_poll: poll() returned %d, revents=0x%04x\n", ret, pfd.revents);

    if (ret < 0) {
        if (errno == EINTR) {
            lws_log_trace("socket_poll: poll interrupted by signal\n");
            return 0;
        }
        lws_log_error(LWS_ERR_SOCKET_RECV,
                      "poll failed: %s\n", strerror(errno));
        return LWS_ERR_SOCKET_RECV;
    }

    if (ret == 0) {
        lws_log_trace("socket_poll: poll timeout\n");
        return 0;  // Timeout
    }

    // Check connection completion (TCP only)
    if (sock->is_tcp &&
        sock->base.state == LWS_TRANSPORT_STATE_CONNECTING &&
        (pfd.revents & POLLOUT)) {
        int error = 0;
        socklen_t len = sizeof(error);

        if (getsockopt(sock->sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            error = errno;
        }

        if (error) {
            lws_log_error(LWS_ERR_SOCKET_CONNECT,
                          "TCP connection failed: %s\n", strerror(error));
            sock->base.state = LWS_TRANSPORT_STATE_ERROR;
            if (sock->base.handler.on_state) {
                sock->base.handler.on_state(transport,
                                           LWS_TRANSPORT_STATE_ERROR,
                                           sock->base.handler.userdata);
            }
            return LWS_ERR_SOCKET_CONNECT;
        }

        sock->base.state = LWS_TRANSPORT_STATE_CONNECTED;
        lws_log_info("TCP connection established\n");

        if (sock->base.handler.on_state) {
            sock->base.handler.on_state(transport,
                                       LWS_TRANSPORT_STATE_CONNECTED,
                                       sock->base.handler.userdata);
        }

        events++;
    }

    // Check for incoming data
    if (pfd.revents & POLLIN) {
        ssize_t n;

        lws_log_trace("socket_poll: POLLIN detected, reading data via %s...\n",
                     sock->is_tcp ? "recv()" : "recvfrom()");

        if (sock->is_tcp) {
            n = recv(sock->sockfd, sock->recv_buffer,
                     sizeof(sock->recv_buffer), 0);
        } else {
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            n = recvfrom(sock->sockfd, sock->recv_buffer,
                         sizeof(sock->recv_buffer), 0,
                         (struct sockaddr*)&from, &fromlen);
        }

        lws_log_trace("socket_poll: read returned %zd bytes\n", n);

        if (n > 0) {
            // Print received SIP message
            lws_log_info("========== RECEIVED SIP MESSAGE (%zd bytes, %s) ==========\n",
                        n, sock->is_tcp ? "TCP" : "UDP");
            // Ensure null-terminated for printing
            if (n < (ssize_t)sizeof(sock->recv_buffer)) {
                sock->recv_buffer[n] = '\0';
                lws_log_info("%s\n", sock->recv_buffer);
            } else {
                // Buffer full, print anyway
                char temp = sock->recv_buffer[sizeof(sock->recv_buffer) - 1];
                sock->recv_buffer[sizeof(sock->recv_buffer) - 1] = '\0';
                lws_log_info("%s...(truncated)\n", sock->recv_buffer);
                sock->recv_buffer[sizeof(sock->recv_buffer) - 1] = temp;
            }
            lws_log_info("================= END SIP MESSAGE ====================\n");

            // Call receive callback
            if (sock->base.handler.on_recv) {
                lws_log_trace("socket_poll: calling on_recv callback\n");
                sock->base.handler.on_recv(transport,
                                          sock->recv_buffer,
                                          n,
                                          sock->base.handler.userdata);
            }
            events++;
        } else if (n == 0) {
            // Connection closed (TCP only)
            if (sock->is_tcp) {
                lws_log_info("TCP connection closed by peer\n");
                socket_disconnect(transport);
            } else {
                // UDP: n==0 is unusual but not an error
                lws_log_trace("UDP recvfrom returned 0 bytes\n");
            }
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

static void socket_destroy(lws_transport_t* transport)
{
    lws_log_trace("socket_destroy: transport=%p\n", (void*)transport);
    lws_transport_socket_t* sock;

    if (!transport) {
        return;
    }

    sock = to_socket_transport(transport);

    socket_disconnect(transport);
    lws_free(sock);
    lws_log_info("socket transport destroyed\n");
}

/* ============================================================
 * Factory Function
 *
 * Creates a socket-based transport that supports both TCP and UDP.
 * ============================================================ */

lws_transport_t* lws_transport_socket_create(
    const lws_transport_config_t* config,
    const lws_transport_handler_t* handler)
{
    lws_log_trace("lws_transport_socket_create: config=%p, handler=%p\n",
                  (void*)config, (void*)handler);
    lws_transport_socket_t* sock;

    if (!config || !handler) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    sock = (lws_transport_socket_t*)lws_malloc(sizeof(lws_transport_socket_t));
    if (!sock) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate socket transport\n");
        return NULL;
    }

    memset(sock, 0, sizeof(lws_transport_socket_t));

    // Setup base
    sock->base.ops = &socket_ops;
    memcpy(&sock->base.config, config, sizeof(lws_transport_config_t));
    memcpy(&sock->base.handler, handler, sizeof(lws_transport_handler_t));
    sock->base.state = LWS_TRANSPORT_STATE_DISCONNECTED;

    // Initialize socket
    sock->sockfd = -1;

    // Determine transport type from config->transport_type field
    // Fall back to userdata for backward compatibility
    if (config->transport_type != 0) {
        sock->is_tcp = (config->transport_type == LWS_TRANSPORT_TCP) ? 1 : 0;
    } else if (config->userdata) {
        // Legacy: check userdata (will be deprecated)
        lws_transport_type_t transport_type = (lws_transport_type_t)(intptr_t)config->userdata;
        sock->is_tcp = (transport_type == LWS_TRANSPORT_TCP) ? 1 : 0;
    } else {
        // Default to UDP for standard SIP
        sock->is_tcp = 0;
    }

    lws_log_info("socket transport created: %s:%d (%s)\n",
                 config->remote_host,
                 config->remote_port,
                 sock->is_tcp ? "TCP" : "UDP");

    return &sock->base;
}
