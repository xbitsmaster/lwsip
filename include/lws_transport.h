/**
 * @file lws_transport.h
 * @brief LwSIP Transport Layer Abstraction
 *
 * This provides a transport-agnostic interface for SIP signaling.
 * Implementations can use TCP/UDP sockets, MQTT, serial port, or any
 * custom protocol, making lwsip suitable for various embedded scenarios.
 *
 * Examples:
 * - lws_transport_tcp.c    : TCP/UDP socket (standard SIP)
 * - lws_transport_mqtt.c   : MQTT publish/subscribe (IoT)
 * - lws_transport_serial.c : RS232/RS485 (industrial)
 * - lws_transport_custom.c : Proprietary protocol
 */

#ifndef __LWS_TRANSPORT_H__
#define __LWS_TRANSPORT_H__

#include "lws_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Transport Handle
 * ============================================================ */

typedef struct lws_transport lws_transport_t;

/* ============================================================
 * Transport Configuration
 * ============================================================ */

typedef struct {
    // Common settings
    const char* remote_host;  // Server address (IP or hostname)
    uint16_t remote_port;     // Server port
    uint16_t local_port;      // Local bind port (0 for auto)
    int use_tcp;              // 0=UDP, 1=TCP

    // For MQTT transport
    const char* mqtt_client_id;
    const char* mqtt_pub_topic;   // Topic for sending
    const char* mqtt_sub_topic;   // Topic for receiving

    // For serial transport
    const char* serial_device;    // e.g., "/dev/ttyS0"
    int serial_baudrate;          // e.g., 115200

    // Generic user data
    void* userdata;
} lws_transport_config_t;

/* ============================================================
 * Transport Callbacks
 * ============================================================ */

/**
 * @brief Data received callback
 * @param transport Transport handle
 * @param data Received data
 * @param len Data length
 * @param userdata User data from config
 * @return 0 on success, <0 on error
 */
typedef int (*lws_transport_recv_cb)(lws_transport_t* transport,
    const void* data,
    int len,
    void* userdata);

/**
 * @brief Connection state changed callback
 * @param transport Transport handle
 * @param state New state
 * @param userdata User data from config
 */
typedef void (*lws_transport_state_cb)(lws_transport_t* transport,
    lws_transport_state_t state,
    void* userdata);

typedef struct {
    lws_transport_recv_cb on_recv;
    lws_transport_state_cb on_state;
    void* userdata;
} lws_transport_handler_t;

/* ============================================================
 * Transport Operations (Virtual Function Table)
 * ============================================================ */

typedef struct {
    /**
     * @brief Connect to remote peer
     * @param transport Transport handle
     * @return 0 on success, <0 on error
     */
    int (*connect)(lws_transport_t* transport);

    /**
     * @brief Disconnect from remote peer
     * @param transport Transport handle
     */
    void (*disconnect)(lws_transport_t* transport);

    /**
     * @brief Send data
     * @param transport Transport handle
     * @param data Data to send
     * @param len Data length
     * @return Number of bytes sent, <0 on error
     */
    int (*send)(lws_transport_t* transport,
        const void* data,
        int len);

    /**
     * @brief Get transport state
     * @param transport Transport handle
     * @return Current state
     */
    lws_transport_state_t (*get_state)(lws_transport_t* transport);

    /**
     * @brief Get local address (for SDP generation)
     * @param transport Transport handle
     * @param ip IP address buffer [out]
     * @param port Port number [out]
     * @return 0 on success, <0 on error
     */
    int (*get_local_addr)(lws_transport_t* transport,
        char* ip,
        uint16_t* port);

    /**
     * @brief Poll/process events
     * @param transport Transport handle
     * @param timeout_ms Timeout in milliseconds
     * @return >0 events processed, 0 timeout, <0 error
     */
    int (*poll)(lws_transport_t* transport, int timeout_ms);

    /**
     * @brief Destroy transport
     * @param transport Transport handle
     */
    void (*destroy)(lws_transport_t* transport);
} lws_transport_ops_t;

/* ============================================================
 * Base Transport Structure
 *
 * Concrete implementations should embed this as first member:
 *
 * struct lws_transport_tcp {
 *     lws_transport_t base;  // Must be first!
 *     int sockfd;
 *     // ... other fields
 * };
 * ============================================================ */

struct lws_transport {
    const lws_transport_ops_t* ops;  // Virtual function table
    lws_transport_config_t config;
    lws_transport_handler_t handler;
    lws_transport_state_t state;
};

/* ============================================================
 * Generic Transport API
 * ============================================================ */

/**
 * @brief Connect transport
 * @param transport Transport handle
 * @return 0 on success, <0 on error
 */
static inline int lws_transport_connect(lws_transport_t* transport)
{
    if (!transport || !transport->ops || !transport->ops->connect) {
        return -1;
    }
    return transport->ops->connect(transport);
}

/**
 * @brief Disconnect transport
 * @param transport Transport handle
 */
static inline void lws_transport_disconnect(lws_transport_t* transport)
{
    if (transport && transport->ops && transport->ops->disconnect) {
        transport->ops->disconnect(transport);
    }
}

/**
 * @brief Send data
 * @param transport Transport handle
 * @param data Data to send
 * @param len Data length
 * @return Number of bytes sent, <0 on error
 */
static inline int lws_transport_send(lws_transport_t* transport,
    const void* data,
    int len)
{
    if (!transport || !transport->ops || !transport->ops->send) {
        return -1;
    }
    return transport->ops->send(transport, data, len);
}

/**
 * @brief Get transport state
 * @param transport Transport handle
 * @return Current state
 */
static inline lws_transport_state_t lws_transport_get_state(lws_transport_t* transport)
{
    if (!transport || !transport->ops || !transport->ops->get_state) {
        return LWS_TRANSPORT_STATE_DISCONNECTED;
    }
    return transport->ops->get_state(transport);
}

/**
 * @brief Get local address
 * @param transport Transport handle
 * @param ip IP address buffer [out]
 * @param port Port number [out]
 * @return 0 on success, <0 on error
 */
static inline int lws_transport_get_local_addr(lws_transport_t* transport,
    char* ip,
    uint16_t* port)
{
    if (!transport || !transport->ops || !transport->ops->get_local_addr) {
        return -1;
    }
    return transport->ops->get_local_addr(transport, ip, port);
}

/**
 * @brief Poll events
 * @param transport Transport handle
 * @param timeout_ms Timeout in milliseconds
 * @return >0 events processed, 0 timeout, <0 error
 */
static inline int lws_transport_poll(lws_transport_t* transport, int timeout_ms)
{
    if (!transport || !transport->ops || !transport->ops->poll) {
        return -1;
    }
    return transport->ops->poll(transport, timeout_ms);
}

/**
 * @brief Destroy transport
 * @param transport Transport handle
 */
static inline void lws_transport_destroy(lws_transport_t* transport)
{
    if (transport && transport->ops && transport->ops->destroy) {
        transport->ops->destroy(transport);
    }
}

/* ============================================================
 * Transport Factory Functions
 * (Implemented by concrete transports)
 * ============================================================ */

/**
 * @brief Create TCP/UDP transport
 * @param config Configuration
 * @param handler Callbacks
 * @return Transport handle or NULL on error
 */
lws_transport_t* lws_transport_tcp_create(
    const lws_transport_config_t* config,
    const lws_transport_handler_t* handler);

/**
 * @brief Create MQTT transport (optional)
 * @param config Configuration
 * @param handler Callbacks
 * @return Transport handle or NULL on error
 */
lws_transport_t* lws_transport_mqtt_create(
    const lws_transport_config_t* config,
    const lws_transport_handler_t* handler);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_TRANSPORT_H__ */
