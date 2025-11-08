/**
 * @file lws_trans_mqtt.c
 * @brief MQTT transport implementation using lwIP MQTT client
 *
 * This file implements MQTT-based transport layer using lwIP's MQTT client.
 * MQTT provides reliable publish/subscribe messaging suitable for IoT scenarios
 * where traditional UDP/TCP may face NAT traversal challenges.
 *
 * Design:
 * - Uses lwIP mqtt_client API
 * - Publish data to <topic_prefix>/send
 * - Subscribe to <topic_prefix>/recv
 * - Automatic reconnection on disconnect
 */

#ifdef TRANS_MQTT

#include "lws_intl.h"
#include "lws_mem.h"
#include "lws_log.h"

#include <string.h>
#include <stdlib.h>

/* lwIP MQTT headers */
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"

/* ========================================
 * Data Structures
 * ======================================== */

/**
 * @brief MQTT transport implementation data
 */
typedef struct {
    /* MQTT client */
    mqtt_client_t* client;

    /* Configuration */
    char broker[LWS_MAX_HOSTNAME_LEN];
    uint16_t port;
    char client_id[LWS_MAX_CLIENT_ID_LEN];
    char username[LWS_MAX_USERNAME_LEN];
    char password[LWS_MAX_PASSWORD_LEN];

    /* Topics */
    char topic_prefix[LWS_MAX_TOPIC_LEN];
    char topic_send[LWS_MAX_TOPIC_LEN + 16];    /* topic_prefix/send */
    char topic_recv[LWS_MAX_TOPIC_LEN + 16];    /* topic_prefix/recv */

    /* Connection state */
    int connected;
    int subscribed;

    /* Receive buffer for incoming MQTT data */
    uint8_t recv_buf[LWS_TRANS_RECV_BUF_SIZE];
    int recv_len;
    int recv_complete;

    /* Event handler */
    lws_trans_handler_t handler;

    /* Broker IP address (resolved) */
    ip_addr_t broker_ip;
} lws_trans_mqtt_impl_t;

/* ========================================
 * Forward Declarations
 * ======================================== */

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status);
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len);
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags);
static void mqtt_subscribe_cb(void *arg, err_t err);

/* ========================================
 * MQTT Callback Implementations
 * ======================================== */

/**
 * @brief MQTT connection status callback
 */
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)arg;
    (void)client;

    if (!impl) {
        return;
    }

    if (status == MQTT_CONNECT_ACCEPTED) {
        lws_log_info("[MQTT] Connected to broker");
        impl->connected = 1;

        /* Subscribe to receive topic */
        err_t err = mqtt_subscribe(impl->client, impl->topic_recv, 0,
                                   mqtt_subscribe_cb, impl);
        if (err != ERR_OK) {
            lws_log_error("[MQTT] Failed to subscribe: %d", err);
        }

        /* Notify application */
        if (impl->handler.on_connected) {
            impl->handler.on_connected((lws_trans_t*)impl, 1, impl->handler.userdata);
        }

    } else {
        lws_log_warn("[MQTT] Disconnected: status=%d", status);
        impl->connected = 0;
        impl->subscribed = 0;

        /* Notify application */
        if (impl->handler.on_connected) {
            impl->handler.on_connected((lws_trans_t*)impl, 0, impl->handler.userdata);
        }

        if (impl->handler.on_error) {
            impl->handler.on_error((lws_trans_t*)impl, status,
                                  "MQTT connection lost", impl->handler.userdata);
        }
    }
}

/**
 * @brief MQTT incoming publish callback
 */
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)arg;

    if (!impl) {
        return;
    }

    lws_log_debug("[MQTT] Incoming publish on topic '%s', length=%u", topic, tot_len);

    /* Reset receive buffer for new message */
    impl->recv_len = 0;
    impl->recv_complete = 0;

    /* Check if buffer is large enough */
    if (tot_len > sizeof(impl->recv_buf)) {
        lws_log_warn("[MQTT] Message too large: %u > %zu", tot_len, sizeof(impl->recv_buf));
    }
}

/**
 * @brief MQTT incoming data callback
 */
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)arg;

    if (!impl) {
        return;
    }

    /* Accumulate data fragments */
    if (impl->recv_len + len <= sizeof(impl->recv_buf)) {
        memcpy(impl->recv_buf + impl->recv_len, data, len);
        impl->recv_len += len;
    } else {
        lws_log_warn("[MQTT] Receive buffer overflow");
        impl->recv_len = 0;
        return;
    }

    /* Check if this is the last fragment */
    if (flags & MQTT_DATA_FLAG_LAST) {
        impl->recv_complete = 1;

        lws_log_debug("[MQTT] Received complete message: %d bytes", impl->recv_len);

        /* Deliver to application */
        if (impl->handler.on_data && impl->recv_len > 0) {
            lws_addr_t from;
            memset(&from, 0, sizeof(from));
            strncpy(from.ip, impl->broker, sizeof(from.ip) - 1);
            from.port = impl->port;

            impl->handler.on_data((lws_trans_t*)impl, impl->recv_buf,
                                 impl->recv_len, &from, impl->handler.userdata);
        }

        /* Reset for next message */
        impl->recv_len = 0;
        impl->recv_complete = 0;
    }
}

/**
 * @brief MQTT subscribe callback
 */
static void mqtt_subscribe_cb(void *arg, err_t err)
{
    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)arg;

    if (!impl) {
        return;
    }

    if (err == ERR_OK) {
        lws_log_info("[MQTT] Successfully subscribed to '%s'", impl->topic_recv);
        impl->subscribed = 1;
    } else {
        lws_log_error("[MQTT] Subscribe failed: %d", err);
        impl->subscribed = 0;
    }
}

/* ========================================
 * Transport Operations Implementation
 * ======================================== */

/**
 * @brief Destroy MQTT transport
 */
static void mqtt_destroy(lws_trans_t* trans)
{
    if (!trans || !trans->impl) {
        return;
    }

    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)trans->impl;

    /* Disconnect and free MQTT client */
    if (impl->client) {
        mqtt_disconnect(impl->client);
        mqtt_client_free(impl->client);
        impl->client = NULL;
    }

    /* Free implementation structure */
    lws_free(impl);
    trans->impl = NULL;

    /* Free transport structure */
    lws_free(trans);
}

/**
 * @brief Connect to MQTT broker
 */
static int mqtt_connect(lws_trans_t* trans, const char* addr, uint16_t port)
{
    if (!trans || !trans->impl) {
        return LWS_EINVAL;
    }

    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)trans->impl;

    /* Update broker address if provided */
    if (addr) {
        strncpy(impl->broker, addr, sizeof(impl->broker) - 1);
        impl->broker[sizeof(impl->broker) - 1] = '\0';
    }
    if (port > 0) {
        impl->port = port;
    }

    /* Resolve broker address */
    if (!ipaddr_aton(impl->broker, &impl->broker_ip)) {
        lws_log_error("[MQTT] Invalid broker address: %s", impl->broker);
        return LWS_EINVAL;
    }

    /* Prepare client info */
    struct mqtt_connect_client_info_t client_info;
    memset(&client_info, 0, sizeof(client_info));

    client_info.client_id = impl->client_id;
    client_info.client_user = impl->username[0] ? impl->username : NULL;
    client_info.client_pass = impl->password[0] ? impl->password : NULL;
    client_info.keep_alive = 60;  /* 60 seconds keep-alive */

    /* Connect to broker */
    err_t err = mqtt_client_connect(impl->client, &impl->broker_ip, impl->port,
                                    mqtt_connection_cb, impl, &client_info);

    if (err != ERR_OK) {
        lws_log_error("[MQTT] Failed to connect: %d", err);
        return LWS_ERROR;
    }

    lws_log_info("[MQTT] Connecting to %s:%u...", impl->broker, impl->port);
    return LWS_OK;
}

/**
 * @brief Send data via MQTT publish
 */
static int mqtt_send(lws_trans_t* trans, const void* data, int len, const lws_addr_t* to)
{
    if (!trans || !trans->impl || !data || len <= 0) {
        return LWS_EINVAL;
    }

    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)trans->impl;
    (void)to;  /* MQTT doesn't use destination address */

    /* Check if connected */
    if (!impl->connected) {
        lws_log_warn("[MQTT] Not connected, cannot send");
        return LWS_ENOTCONN;
    }

    /* Publish to send topic */
    err_t err = mqtt_publish(impl->client, impl->topic_send, data, len,
                            0, 0, NULL, NULL);

    if (err != ERR_OK) {
        lws_log_error("[MQTT] Publish failed: %d", err);
        return LWS_ERROR;
    }

    lws_log_debug("[MQTT] Published %d bytes to '%s'", len, impl->topic_send);
    return len;
}

/**
 * @brief MQTT event loop (currently no-op, polling handled by lwIP)
 */
static int mqtt_loop(lws_trans_t* trans, int timeout_ms)
{
    (void)trans;
    (void)timeout_ms;

    /* lwIP's MQTT client uses its own event loop via tcpip thread,
     * so we don't need to do anything here. Data is delivered via callbacks. */
    return 0;
}

/**
 * @brief Get file descriptor (not applicable for MQTT)
 */
static int mqtt_get_fd(lws_trans_t* trans)
{
    (void)trans;
    return -1;  /* MQTT doesn't expose a file descriptor */
}

/**
 * @brief Get local address
 */
static int mqtt_get_local_addr(lws_trans_t* trans, lws_addr_t* addr)
{
    if (!trans || !trans->impl || !addr) {
        return LWS_EINVAL;
    }

    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)trans->impl;

    /* Return broker address as "local" address for MQTT */
    memset(addr, 0, sizeof(*addr));
    strncpy(addr->ip, impl->broker, sizeof(addr->ip) - 1);
    addr->port = impl->port;

    return LWS_OK;
}

/* ========================================
 * Operation Table
 * ======================================== */

static const lws_trans_ops_t mqtt_ops = {
    .destroy = mqtt_destroy,
    .connect = mqtt_connect,
    .send = mqtt_send,
    .loop = mqtt_loop,
    .get_fd = mqtt_get_fd,
    .get_local_addr = mqtt_get_local_addr,
};

/* ========================================
 * Public API
 * ======================================== */

/**
 * @brief Create MQTT transport instance
 */
lws_trans_t* lws_trans_mqtt_create(const lws_trans_config_t* config,
                                    const lws_trans_handler_t* handler)
{
    if (!config || config->type != LWS_TRANS_TYPE_MQTT) {
        lws_log_error("[MQTT] Invalid config");
        return NULL;
    }

    /* Allocate transport structure */
    lws_trans_t* trans = (lws_trans_t*)lws_malloc(sizeof(lws_trans_t));
    if (!trans) {
        lws_log_error("[MQTT] Failed to allocate transport");
        return NULL;
    }
    memset(trans, 0, sizeof(*trans));

    /* Allocate implementation structure */
    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)lws_malloc(sizeof(lws_trans_mqtt_impl_t));
    if (!impl) {
        lws_log_error("[MQTT] Failed to allocate impl");
        lws_free(trans);
        return NULL;
    }
    memset(impl, 0, sizeof(*impl));

    /* Initialize implementation */
    impl->client = mqtt_client_new();
    if (!impl->client) {
        lws_log_error("[MQTT] Failed to create MQTT client");
        lws_free(impl);
        lws_free(trans);
        return NULL;
    }

    /* Copy configuration */
    strncpy(impl->broker, config->mqtt.broker, sizeof(impl->broker) - 1);
    impl->port = config->mqtt.port > 0 ? config->mqtt.port : MQTT_PORT;
    strncpy(impl->client_id, config->mqtt.client_id, sizeof(impl->client_id) - 1);

    if (config->mqtt.username[0]) {
        strncpy(impl->username, config->mqtt.username, sizeof(impl->username) - 1);
    }
    if (config->mqtt.password[0]) {
        strncpy(impl->password, config->mqtt.password, sizeof(impl->password) - 1);
    }

    /* Set up topics */
    strncpy(impl->topic_prefix, config->mqtt.topic_prefix, sizeof(impl->topic_prefix) - 1);
    snprintf(impl->topic_send, sizeof(impl->topic_send), "%s/send", impl->topic_prefix);
    snprintf(impl->topic_recv, sizeof(impl->topic_recv), "%s/recv", impl->topic_prefix);

    /* Copy handler */
    if (handler) {
        impl->handler = *handler;
    }

    /* Set MQTT incoming callbacks */
    mqtt_set_inpub_callback(impl->client, mqtt_incoming_publish_cb,
                           mqtt_incoming_data_cb, impl);

    /* Initialize transport structure */
    trans->type = LWS_TRANS_TYPE_MQTT;
    trans->ops = &mqtt_ops;
    trans->impl = impl;

    lws_log_info("[MQTT] Transport created: broker=%s:%u, client_id=%s, topic=%s",
                 impl->broker, impl->port, impl->client_id, impl->topic_prefix);

    /* Auto-connect */
    if (mqtt_connect(trans, NULL, 0) != LWS_OK) {
        mqtt_destroy(trans);
        return NULL;
    }

    return trans;
}

/* ========================================
 * MQTT-specific API
 * ======================================== */

/**
 * @brief Set MQTT topic prefix
 */
int lws_trans_mqtt_set_topic(lws_trans_t* trans, const char* topic)
{
    if (!trans || !trans->impl || !topic || trans->type != LWS_TRANS_TYPE_MQTT) {
        return LWS_EINVAL;
    }

    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)trans->impl;

    strncpy(impl->topic_prefix, topic, sizeof(impl->topic_prefix) - 1);
    snprintf(impl->topic_send, sizeof(impl->topic_send), "%s/send", topic);
    snprintf(impl->topic_recv, sizeof(impl->topic_recv), "%s/recv", topic);

    lws_log_info("[MQTT] Topic updated: %s", topic);
    return LWS_OK;
}

/**
 * @brief Subscribe to additional MQTT topic
 */
int lws_trans_mqtt_subscribe(lws_trans_t* trans, const char* topic)
{
    if (!trans || !trans->impl || !topic || trans->type != LWS_TRANS_TYPE_MQTT) {
        return LWS_EINVAL;
    }

    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)trans->impl;

    if (!impl->connected) {
        lws_log_warn("[MQTT] Not connected, cannot subscribe");
        return LWS_ENOTCONN;
    }

    err_t err = mqtt_subscribe(impl->client, topic, 0, mqtt_subscribe_cb, impl);
    if (err != ERR_OK) {
        lws_log_error("[MQTT] Subscribe failed: %d", err);
        return LWS_ERROR;
    }

    lws_log_info("[MQTT] Subscribing to '%s'", topic);
    return LWS_OK;
}

/**
 * @brief Unsubscribe from MQTT topic
 */
int lws_trans_mqtt_unsubscribe(lws_trans_t* trans, const char* topic)
{
    if (!trans || !trans->impl || !topic || trans->type != LWS_TRANS_TYPE_MQTT) {
        return LWS_EINVAL;
    }

    lws_trans_mqtt_impl_t* impl = (lws_trans_mqtt_impl_t*)trans->impl;

    if (!impl->connected) {
        lws_log_warn("[MQTT] Not connected, cannot unsubscribe");
        return LWS_ENOTCONN;
    }

    err_t err = mqtt_unsubscribe(impl->client, topic, NULL, impl);
    if (err != ERR_OK) {
        lws_log_error("[MQTT] Unsubscribe failed: %d", err);
        return LWS_ERROR;
    }

    lws_log_info("[MQTT] Unsubscribing from '%s'", topic);
    return LWS_OK;
}

#endif /* TRANS_MQTT */
