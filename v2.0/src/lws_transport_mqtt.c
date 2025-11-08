/**
 * @file lws_transport_mqtt.c
 * @brief MQTT Transport Implementation (Example)
 *
 * This demonstrates how to implement a custom transport using MQTT
 * publish/subscribe for SIP signaling. This is useful for:
 * - IoT scenarios where MQTT is already deployed
 * - Low-bandwidth environments
 * - Devices behind NAT/firewall that can't accept incoming connections
 *
 * Usage:
 * - Publish SIP messages to mqtt_pub_topic
 * - Subscribe to mqtt_sub_topic for responses
 *
 * Requirements:
 * - MQTT client library (e.g., Eclipse Paho, mosquitto)
 */

#include "lws_transport.h"
#include "lws_error.h"
#include "lws_mem.h"
#include "lws_log.h"
#include <string.h>
#include <stdio.h>

// TODO: Include MQTT client library
// #include <mosquitto.h>

/* ============================================================
 * MQTT Transport Structure
 *
 * 注意：base 不需要是第一个成员！
 * 使用 lws_container_of 进行类型安全的转换
 * ============================================================ */

typedef struct lws_transport_mqtt {
    // MQTT client handle
    void* mqtt_client;  // struct mosquitto*

    // Topics
    char pub_topic[128];
    char sub_topic[128];

    // Buffer for receiving
    char recv_buffer[8192];

    // Base transport (可以在任何位置)
    lws_transport_t base;
} lws_transport_mqtt_t;

/**
 * @brief 类型安全的转换：从 lws_transport_t* 到 lws_transport_mqtt_t*
 * 使用 container_of 而不是简单的强制类型转换
 */
static inline lws_transport_mqtt_t* to_mqtt_transport(lws_transport_t* transport)
{
    if (!transport) {
        return NULL;
    }
    return lws_container_of(transport, lws_transport_mqtt_t, base);
}

/* ============================================================
 * Forward Declarations
 * ============================================================ */

static int mqtt_connect(lws_transport_t* transport);
static void mqtt_disconnect(lws_transport_t* transport);
static int mqtt_send(lws_transport_t* transport, const void* data, int len);
static lws_transport_state_t mqtt_get_state(lws_transport_t* transport);
static int mqtt_get_local_addr(lws_transport_t* transport, char* ip, uint16_t* port);
static int mqtt_poll(lws_transport_t* transport, int timeout_ms);
static void mqtt_destroy(lws_transport_t* transport);

/* ============================================================
 * Operations Table
 * ============================================================ */

static const lws_transport_ops_t mqtt_ops = {
    .connect = mqtt_connect,
    .disconnect = mqtt_disconnect,
    .send = mqtt_send,
    .get_state = mqtt_get_state,
    .get_local_addr = mqtt_get_local_addr,
    .poll = mqtt_poll,
    .destroy = mqtt_destroy,
};

/* ============================================================
 * MQTT Callbacks (from MQTT library)
 * ============================================================ */

#if 0  // TODO: Uncomment when MQTT library is available

static void on_mqtt_connect(struct mosquitto *mosq, void *obj, int result)
{
    lws_transport_mqtt_t* mqtt = (lws_transport_mqtt_t*)obj;

    if (result == 0) {
        lws_log_info("mqtt connected, subscribing to %s\n", mqtt->sub_topic);

        // Subscribe to topic
        mosquitto_subscribe(mosq, NULL, mqtt->sub_topic, 0);

        mqtt->base.state = LWS_TRANSPORT_STATE_CONNECTED;

        if (mqtt->base.handler.on_state) {
            mqtt->base.handler.on_state(&mqtt->base,
                                        LWS_TRANSPORT_STATE_CONNECTED,
                                        mqtt->base.handler.userdata);
        }
    } else {
        lws_log_error(LWS_ERR_SOCKET_CONNECT, "mqtt connection failed: %d\n", result);
        mqtt->base.state = LWS_TRANSPORT_STATE_ERROR;

        if (mqtt->base.handler.on_state) {
            mqtt->base.handler.on_state(&mqtt->base,
                                        LWS_TRANSPORT_STATE_ERROR,
                                        mqtt->base.handler.userdata);
        }
    }
}

static void on_mqtt_message(struct mosquitto *mosq,
    void *obj,
    const struct mosquitto_message *message)
{
    lws_transport_mqtt_t* mqtt = (lws_transport_mqtt_t*)obj;

    lws_log_info("mqtt message received: topic=%s, len=%d\n",
                 message->topic, message->payloadlen);

    // Call receive callback
    if (mqtt->base.handler.on_recv) {
        mqtt->base.handler.on_recv(&mqtt->base,
                                   message->payload,
                                   message->payloadlen,
                                   mqtt->base.handler.userdata);
    }
}

static void on_mqtt_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
    lws_transport_mqtt_t* mqtt = (lws_transport_mqtt_t*)obj;

    lws_log_info("mqtt disconnected: %d\n", rc);
    mqtt->base.state = LWS_TRANSPORT_STATE_DISCONNECTED;

    if (mqtt->base.handler.on_state) {
        mqtt->base.handler.on_state(&mqtt->base,
                                    LWS_TRANSPORT_STATE_DISCONNECTED,
                                    mqtt->base.handler.userdata);
    }
}

#endif

/* ============================================================
 * Operations Implementation
 * ============================================================ */

static int mqtt_connect(lws_transport_t* transport)
{
    lws_transport_mqtt_t* mqtt;

    if (!transport) {
        return LWS_ERR_INVALID_PARAM;
    }

    mqtt = to_mqtt_transport(transport);

    lws_log_info("connecting to MQTT broker %s:%d\n",
                 mqtt->base.config.remote_host,
                 mqtt->base.config.remote_port);

    // TODO: Initialize MQTT client library
    /*
    mosquitto_lib_init();

    mqtt->mqtt_client = mosquitto_new(mqtt->base.config.mqtt_client_id,
                                      true, mqtt);
    if (!mqtt->mqtt_client) {
        lws_log_error(LWS_ERR_NOMEM, "failed to create mqtt client\n");
        return LWS_ERR_NOMEM;
    }

    // Set callbacks
    mosquitto_connect_callback_set(mqtt->mqtt_client, on_mqtt_connect);
    mosquitto_message_callback_set(mqtt->mqtt_client, on_mqtt_message);
    mosquitto_disconnect_callback_set(mqtt->mqtt_client, on_mqtt_disconnect);

    // Connect
    int rc = mosquitto_connect_async(mqtt->mqtt_client,
                                     mqtt->base.config.remote_host,
                                     mqtt->base.config.remote_port,
                                     60);
    if (rc != MOSQ_ERR_SUCCESS) {
        lws_log_error(LWS_ERR_SOCKET_CONNECT, "mqtt connect failed: %d\n", rc);
        mosquitto_destroy(mqtt->mqtt_client);
        mqtt->mqtt_client = NULL;
        return LWS_ERR_SOCKET_CONNECT;
    }

    // Start network loop
    mosquitto_loop_start(mqtt->mqtt_client);
    */

    mqtt->base.state = LWS_TRANSPORT_STATE_CONNECTING;

    lws_log_warn(0, "MQTT transport not implemented yet (requires MQTT library)\n");
    return LWS_ERR_NOT_FOUND;
}

static void mqtt_disconnect(lws_transport_t* transport)
{
    lws_transport_mqtt_t* mqtt;

    if (!transport) {
        return;
    }

    // TODO: Disconnect MQTT
    /*
    if (mqtt->mqtt_client) {
        mosquitto_disconnect(mqtt->mqtt_client);
        mosquitto_loop_stop(mqtt->mqtt_client, true);
        mosquitto_destroy(mqtt->mqtt_client);
        mqtt->mqtt_client = NULL;
    }

    mosquitto_lib_cleanup();
    */

    mqtt->base.state = LWS_TRANSPORT_STATE_DISCONNECTED;
    lws_log_info("mqtt transport disconnected\n");
}

static int mqtt_send(lws_transport_t* transport, const void* data, int len)
{
    lws_transport_mqtt_t* mqtt;

    if (!transport || !data || len <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    mqtt = to_mqtt_transport(transport);

    // TODO: Publish to MQTT topic
    /*
    int rc = mosquitto_publish(mqtt->mqtt_client,
                              NULL,
                              mqtt->pub_topic,
                              len,
                              data,
                              0,  // qos
                              false);  // retain

    if (rc != MOSQ_ERR_SUCCESS) {
        lws_log_error(LWS_ERR_SOCKET_SEND, "mqtt publish failed: %d\n", rc);
        return LWS_ERR_SOCKET_SEND;
    }
    */

    lws_log_info("would publish %d bytes to topic: %s\n", len, mqtt->pub_topic);
    return len;
}

static lws_transport_state_t mqtt_get_state(lws_transport_t* transport)
{
    if (!transport) {
        return LWS_TRANSPORT_STATE_DISCONNECTED;
    }

    return transport->state;
}

static int mqtt_get_local_addr(lws_transport_t* transport,
    char* ip,
    uint16_t* port)
{
    // MQTT is a client-only protocol, so "local address" is the broker
    // For SDP generation, we might use the device's IP or a placeholder

    if (ip) {
        // TODO: Get actual device IP
        strncpy(ip, "0.0.0.0", 64);
    }

    if (port) {
        *port = 0;  // No local port in MQTT
    }

    return LWS_OK;
}

static int mqtt_poll(lws_transport_t* transport, int timeout_ms)
{
    // MQTT library typically handles polling internally via background thread
    // (mosquitto_loop_start), so we don't need to do anything here

    (void)transport;
    (void)timeout_ms;

    return 0;
}

static void mqtt_destroy(lws_transport_t* transport)
{
    lws_transport_mqtt_t* mqtt;

    if (!transport) {
        return;
    }

    mqtt_disconnect(transport);
    lws_free(mqtt);
    lws_log_info("mqtt transport destroyed\n");
}

/* ============================================================
 * Factory Function
 * ============================================================ */

lws_transport_t* lws_transport_mqtt_create(
    const lws_transport_config_t* config,
    const lws_transport_handler_t* handler)
{
    lws_transport_mqtt_t* mqtt;

    if (!config || !handler) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

#if defined(LWS_ENABLE_TRANSPORT_MQTT)
    if (!config->mqtt_client_id || !config->mqtt_pub_topic || !config->mqtt_sub_topic) {
        lws_log_error(LWS_ERR_INVALID_PARAM,
                      "mqtt_client_id, mqtt_pub_topic, and mqtt_sub_topic are required\n");
        return NULL;
    }
#else
    lws_log_error(LWS_ERR_INVALID_PARAM, "MQTT transport not enabled\n");
    return NULL;
#endif

    mqtt = (lws_transport_mqtt_t*)lws_malloc(sizeof(lws_transport_mqtt_t));
    if (!mqtt) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate mqtt transport\n");
        return NULL;
    }

    memset(mqtt, 0, sizeof(lws_transport_mqtt_t));

    // Setup base
    mqtt->base.ops = &mqtt_ops;
    memcpy(&mqtt->base.config, config, sizeof(lws_transport_config_t));
    memcpy(&mqtt->base.handler, handler, sizeof(lws_transport_handler_t));
    mqtt->base.state = LWS_TRANSPORT_STATE_DISCONNECTED;

#if defined(LWS_ENABLE_TRANSPORT_MQTT)
    // Copy topics
    strncpy(mqtt->pub_topic, config->mqtt_pub_topic, sizeof(mqtt->pub_topic) - 1);
    strncpy(mqtt->sub_topic, config->mqtt_sub_topic, sizeof(mqtt->sub_topic) - 1);
#else
    // Set empty topics when MQTT is not enabled
    mqtt->pub_topic[0] = '\0';
    mqtt->sub_topic[0] = '\0';
#endif

    lws_log_info("mqtt transport created:\n");
    lws_log_info("  broker: %s:%d\n", config->remote_host, config->remote_port);
    // lws_log_info("  client_id: %s\n", config->mqtt_client_id);  // MQTT config field not available
    lws_log_info("  pub_topic: %s\n", mqtt->pub_topic);
    lws_log_info("  sub_topic: %s\n", mqtt->sub_topic);

    return &mqtt->base;
}
