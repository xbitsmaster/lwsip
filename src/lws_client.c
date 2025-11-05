/**
 * @file lws_client.c
 * @brief SIP client core implementation
 */

#include "lws_client.h"
#include "lws_uac.h"
#include "lws_uas.h"
#include "lws_transport.h"
#include "lws_error.h"
#include "lws_mem.h"
#include "lws_log.h"
#include "lws_mutex.h"
#include "lws_thread.h"

// libsip headers
#include "sip-agent.h"
#include "sip-message.h"
#include "sip-uas.h"
#include "http-parser.h"

#include <string.h>
#include <stdio.h>

/* ============================================================
 * Client Structure
 * ============================================================ */

struct lws_client {
    lws_config_t config;
    lws_client_handler_t handler;

    // SIP layer
    void* sip_agent;  // sip_agent_t from libsip
    lws_reg_state_t reg_state;

    // Transport layer (abstraction)
    lws_transport_t* transport;

    // UAS: current incoming INVITE transaction
    void* current_invite_transaction;  // sip_uas_transaction_t*
    char current_peer_uri[256];

    // State
    int is_started;
    int worker_running;  // Worker thread running flag
    void* mutex;         // lws_mutex_t* from osal
    void* worker_thread; // lws_thread_t* from osal
};

/* ============================================================
 * libsip Transport Callbacks
 * ============================================================ */

// libsip transport send callback
static int sip_transport_send(void* param, const struct cstring_t* protocol,
    const struct cstring_t* url, const struct cstring_t *received,
    int rport, const void* data, int bytes)
{
    lws_client_t* client = (lws_client_t*)param;
    int ret;

    (void)protocol;
    (void)url;
    (void)received;
    (void)rport;

    lws_log_info("sip transport sending %d bytes\n", bytes);

    // Send via our transport abstraction
    ret = lws_transport_send(client->transport, data, bytes);
    if (ret < 0) {
        lws_log_error(ret, "failed to send via transport\n");
        return -1;
    }

    return 0;
}

/* ============================================================
 * Transport Callbacks
 * ============================================================ */

static int on_transport_recv(lws_transport_t* transport,
    const void* data,
    int len,
    void* userdata)
{
    lws_client_t* client = (lws_client_t*)userdata;
    struct http_parser_t* parser;
    struct sip_message_t* msg;
    size_t bytes;
    int ret;

    (void)transport;

    lws_log_info("received %d bytes from transport\n", len);

    // Create HTTP parser for SIP message
    parser = http_parser_create(HTTP_PARSER_REQUEST, NULL, NULL);
    if (!parser) {
        lws_log_error(LWS_ERR_NOMEM, "failed to create http parser\n");
        return -1;
    }

    // Parse SIP message
    bytes = len;
    ret = http_parser_input(parser, data, &bytes);
    if (ret < 0) {
        lws_log_error(LWS_ERR_SIP_PARSE, "failed to parse SIP message\n");
        http_parser_destroy(parser);
        return -1;
    }

    // Create SIP message structure
    msg = sip_message_create(SIP_MESSAGE_REQUEST);
    if (!msg) {
        lws_log_error(LWS_ERR_NOMEM, "failed to create sip message\n");
        http_parser_destroy(parser);
        return -1;
    }

    // Load from parser
    ret = sip_message_load(msg, parser);
    http_parser_destroy(parser);

    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_PARSE, "failed to load sip message\n");
        sip_message_destroy(msg);
        return -1;
    }

    // Input to SIP agent
    if (client->sip_agent) {
        ret = sip_agent_input((struct sip_agent_t*)client->sip_agent, msg, client);
        if (ret != 0) {
            lws_log_error(LWS_ERR_SIP_INPUT, "sip_agent_input failed\n");
        }
    }

    sip_message_destroy(msg);
    return 0;
}

static void on_transport_state(lws_transport_t* transport,
    lws_transport_state_t state,
    void* userdata)
{
    lws_client_t* client = (lws_client_t*)userdata;

    lws_log_info("transport state changed: %d\n", state);

    if (state == LWS_TRANSPORT_STATE_ERROR && client->handler.on_error) {
        client->handler.on_error(client->handler.param,
                                 LWS_ERR_SIP_TRANSPORT,
                                 "Transport error");
    }
}

/* ============================================================
 * libsip UAS Handler Callbacks
 * ============================================================ */

static int sip_uas_onregister(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const char* user,
    const char* location, int expires)
{
    lws_client_t* client = (lws_client_t*)param;

    lws_log_info("register response: user=%s, expires=%d\n", user, expires);

    if (expires > 0) {
        client->reg_state = LWS_REG_REGISTERED;
    } else {
        client->reg_state = LWS_REG_UNREGISTERED;
    }

    // Notify application
    if (client->handler.on_reg_state) {
        client->handler.on_reg_state(client->handler.param,
                                     client->reg_state, 200);
    }

    (void)req;
    (void)t;
    (void)location;
    return 0;
}

static int sip_uas_oninvite(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, struct sip_dialog_t* redialog,
    const struct cstring_t* id, const void* data, int bytes)
{
    lws_client_t* client = (lws_client_t*)param;
    char from[256] = {0};
    char to[256] = {0};

    lws_log_info("incoming INVITE: dialog=%p, sdp_len=%d\n", redialog, bytes);

    // Extract From/To URIs
    if (req->from.uri.host.p && req->from.uri.host.n > 0) {
        snprintf(from, sizeof(from), "sip:%.*s@%.*s",
                 (int)req->from.uri.user.n, req->from.uri.user.p,
                 (int)req->from.uri.host.n, req->from.uri.host.p);
    }

    if (req->to.uri.host.p && req->to.uri.host.n > 0) {
        snprintf(to, sizeof(to), "sip:%.*s@%.*s",
                 (int)req->to.uri.user.n, req->to.uri.user.p,
                 (int)req->to.uri.host.n, req->to.uri.host.p);
    }

    // Save transaction for later use in lws_uas_answer/reject
    // Note: This is simplified - real implementation needs dialog management
    client->current_invite_transaction = t;
    strncpy(client->current_peer_uri, from, sizeof(client->current_peer_uri) - 1);

    // Add reference to transaction so it won't be destroyed until we reply
    sip_uas_transaction_addref(t);

    // Notify application
    if (client->handler.on_incoming_call) {
        client->handler.on_incoming_call(client->handler.param,
                                         from, to,
                                         (const char*)data, bytes);
    }

    (void)redialog;
    (void)id;
    return 0;
}

static int sip_uas_onack(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, struct sip_dialog_t* dialog,
    const struct cstring_t* id, int code, const void* data, int bytes)
{
    lws_log_info("received ACK: code=%d\n", code);

    (void)param;
    (void)req;
    (void)t;
    (void)dialog;
    (void)id;
    (void)data;
    (void)bytes;
    return 0;
}

static int sip_uas_onbye(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct cstring_t* id)
{
    lws_client_t* client = (lws_client_t*)param;

    lws_log_info("received BYE\n");

    // Notify application
    if (client->handler.on_call_state) {
        client->handler.on_call_state(client->handler.param, NULL,
                                     LWS_CALL_TERMINATED);
    }

    (void)req;
    (void)t;
    (void)id;
    return 0;
}

static int sip_uas_oncancel(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct cstring_t* id)
{
    lws_client_t* client = (lws_client_t*)param;

    lws_log_info("received CANCEL\n");

    // Notify application
    if (client->handler.on_call_state) {
        client->handler.on_call_state(client->handler.param, NULL,
                                     LWS_CALL_TERMINATED);
    }

    (void)req;
    (void)t;
    (void)id;
    return 0;
}

// Stub handlers for other methods
static int sip_uas_onprack(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct cstring_t* id,
    const void* data, int bytes)
{
    (void)param; (void)req; (void)t; (void)id; (void)data; (void)bytes;
    return 0;
}

static int sip_uas_onupdate(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct cstring_t* id,
    const void* data, int bytes)
{
    (void)param; (void)req; (void)t; (void)id; (void)data; (void)bytes;
    return 0;
}

static int sip_uas_oninfo(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct cstring_t* id,
    const struct cstring_t* package, const void* data, int bytes)
{
    (void)param; (void)req; (void)t; (void)id; (void)package;
    (void)data; (void)bytes;
    return 0;
}

static int sip_uas_onsubscribe(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, struct sip_subscribe_t* subscribe,
    const struct cstring_t* id)
{
    (void)param; (void)req; (void)t; (void)subscribe; (void)id;
    return 0;
}

static int sip_uas_onnotify(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct sip_event_t* event)
{
    (void)param; (void)req; (void)t; (void)event;
    return 0;
}

static int sip_uas_onpublish(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct sip_event_t* event)
{
    (void)param; (void)req; (void)t; (void)event;
    return 0;
}

static int sip_uas_onrefer(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t)
{
    (void)param; (void)req; (void)t;
    return 0;
}

static int sip_uas_onmessage(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const void* data, int bytes)
{
    (void)param; (void)req; (void)t; (void)data; (void)bytes;
    return 0;
}

// UAS handler structure
static struct sip_uas_handler_t s_uas_handler = {
    .send = sip_transport_send,
    .onregister = sip_uas_onregister,
    .oninvite = sip_uas_oninvite,
    .onack = sip_uas_onack,
    .onbye = sip_uas_onbye,
    .oncancel = sip_uas_oncancel,
    .onprack = sip_uas_onprack,
    .onupdate = sip_uas_onupdate,
    .oninfo = sip_uas_oninfo,
    .onsubscribe = sip_uas_onsubscribe,
    .onnotify = sip_uas_onnotify,
    .onpublish = sip_uas_onpublish,
    .onrefer = sip_uas_onrefer,
    .onmessage = sip_uas_onmessage,
};

/* ============================================================
 * Worker Thread
 * ============================================================ */

static void* worker_thread_func(void* arg)
{
    lws_client_t* client = (lws_client_t*)arg;
    int ret;

    lws_log_info("worker thread started\n");

    while (client->worker_running) {
        // Poll for SIP messages (100ms timeout)
        ret = lws_client_loop(client, 100);
        if (ret < 0) {
            lws_log_error(ret, "worker thread loop error\n");
            break;
        }
    }

    lws_log_info("worker thread stopped\n");
    return NULL;
}

/* ============================================================
 * Public API Implementation
 * ============================================================ */

lws_client_t* lws_client_create(
    const lws_config_t* config,
    const lws_client_handler_t* handler)
{
    lws_client_t* client;
    lws_transport_config_t transport_config;
    lws_transport_handler_t transport_handler;

    if (!config || !handler) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    client = (lws_client_t*)lws_malloc(sizeof(lws_client_t));
    if (!client) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate client\n");
        return NULL;
    }

    memset(client, 0, sizeof(lws_client_t));
    memcpy(&client->config, config, sizeof(lws_config_t));
    memcpy(&client->handler, handler, sizeof(lws_client_handler_t));
    client->worker_running = 0;

    client->mutex = lws_mutex_create();
    if (!client->mutex) {
        lws_log_error(LWS_ERR_NOMEM, "failed to create mutex\n");
        lws_free(client);
        return NULL;
    }

    // Create transport
    memset(&transport_config, 0, sizeof(transport_config));
    transport_config.remote_host = config->server_host;
    transport_config.remote_port = config->server_port;
    transport_config.local_port = config->local_port;
    transport_config.use_tcp = config->use_tcp;

    transport_handler.on_recv = on_transport_recv;
    transport_handler.on_state = on_transport_state;
    transport_handler.userdata = client;

    client->transport = lws_transport_tcp_create(&transport_config,
                                                  &transport_handler);
    if (!client->transport) {
        lws_log_error(LWS_ERR_SIP_TRANSPORT, "failed to create transport\n");
        lws_mutex_destroy(client->mutex);
        lws_free(client);
        return NULL;
    }

    client->reg_state = LWS_REG_NONE;

    lws_log_info("client created: server=%s:%d, user=%s\n",
                 config->server_host,
                 config->server_port,
                 config->username);

    return client;
}

void lws_client_destroy(lws_client_t* client)
{
    if (!client) {
        return;
    }

    if (client->is_started) {
        lws_client_stop(client);
    }

    if (client->transport) {
        lws_transport_destroy(client->transport);
        client->transport = NULL;
    }

    if (client->mutex) {
        lws_mutex_destroy(client->mutex);
    }

    lws_free(client);
    lws_log_info("client destroyed\n");
}

int lws_client_start(lws_client_t* client)
{
    int ret;

    if (!client) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "client is NULL\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (client->is_started) {
        lws_log_warn(LWS_ERR_ALREADY_INITIALIZED, "client already started\n");
        return LWS_OK;
    }

    // Connect transport
    ret = lws_transport_connect(client->transport);
    if (ret != LWS_OK) {
        lws_log_error(ret, "failed to connect transport\n");
        return ret;
    }

    // Create SIP agent from libsip
    client->sip_agent = sip_agent_create(&s_uas_handler);
    if (!client->sip_agent) {
        lws_log_error(LWS_ERR_SIP_CREATE, "failed to create sip agent\n");
        lws_transport_disconnect(client->transport);
        return LWS_ERR_SIP_CREATE;
    }

    lws_log_info("sip agent created\n");

    // Start worker thread if configured
    if (client->config.use_worker_thread) {
        client->worker_running = 1;
        client->worker_thread = lws_thread_create(worker_thread_func, client);
        if (!client->worker_thread) {
            lws_log_error(LWS_ERR_INTERNAL, "failed to create worker thread\n");
            client->worker_running = 0;
            sip_agent_destroy((struct sip_agent_t*)client->sip_agent);
            client->sip_agent = NULL;
            lws_transport_disconnect(client->transport);
            return LWS_ERR_INTERNAL;
        }
        lws_log_info("worker thread created\n");
    }

    client->is_started = 1;
    lws_log_info("client started\n");

    return LWS_OK;
}

void lws_client_stop(lws_client_t* client)
{
    if (!client || !client->is_started) {
        return;
    }

    // Stop worker thread if running
    if (client->worker_thread) {
        lws_log_info("stopping worker thread\n");
        client->worker_running = 0;
        lws_thread_join(client->worker_thread, NULL);
        lws_thread_destroy(client->worker_thread);
        client->worker_thread = NULL;
        lws_log_info("worker thread stopped\n");
    }

    // Disconnect transport
    if (client->transport) {
        lws_transport_disconnect(client->transport);
    }

    // Destroy SIP agent
    if (client->sip_agent) {
        sip_agent_destroy((struct sip_agent_t*)client->sip_agent);
        client->sip_agent = NULL;
        lws_log_info("sip agent destroyed\n");
    }

    client->is_started = 0;
    lws_log_info("client stopped\n");
}

int lws_client_loop(lws_client_t* client, int timeout_ms)
{
    int ret;

    if (!client) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (!client->is_started) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "client not started\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    // Poll transport for SIP messages
    ret = lws_transport_poll(client->transport, timeout_ms);
    if (ret < 0) {
        return ret;
    }

    // Note: RTP/RTCP packet processing is done at session level
    // Call lws_session_poll(session, timeout_ms) in your event loop
    // for each active session to receive and process media packets

    return ret;
}

const lws_config_t* lws_client_get_config(lws_client_t* client)
{
    if (!client) {
        return NULL;
    }

    return &client->config;
}

/* ============================================================
 * Simplified Call API
 * ============================================================ */

lws_session_t* lws_call(lws_client_t* client, const char* peer_uri)
{
    lws_session_t* session;
    int ret;

    if (!client || !peer_uri) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    lws_log_info("making call to %s\n", peer_uri);

    // Create RTP session
    lws_session_handler_t session_handler = {
        .on_media_ready = NULL,  // TODO: Set proper callbacks
        .on_audio_frame = NULL,
        .on_video_frame = NULL,
        .on_bye = NULL,
        .on_error = NULL,
        .param = NULL,
    };

    session = lws_session_create(&client->config, &session_handler);
    if (!session) {
        lws_log_error(LWS_ERR_SESSION_CREATE, "failed to create session\n");
        return NULL;
    }

    // Send INVITE
    ret = lws_uac_invite(client, peer_uri, session);
    if (ret != LWS_OK) {
        lws_log_error(LWS_ERR_SIP_INVITE, "failed to send INVITE\n");
        lws_session_destroy(session);
        return NULL;
    }

    return session;
}

lws_session_t* lws_answer(lws_client_t* client, const char* peer_uri)
{
    lws_session_t* session;
    int ret;

    if (!client || !peer_uri) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    lws_log_info("answering call from %s\n", peer_uri);

    // Create RTP session
    lws_session_handler_t session_handler = {
        .on_media_ready = NULL,
        .on_audio_frame = NULL,
        .on_video_frame = NULL,
        .on_bye = NULL,
        .on_error = NULL,
        .param = NULL,
    };

    session = lws_session_create(&client->config, &session_handler);
    if (!session) {
        lws_log_error(LWS_ERR_SESSION_CREATE, "failed to create session\n");
        return NULL;
    }

    // Answer call
    ret = lws_uas_answer(client, peer_uri, session);
    if (ret != LWS_OK) {
        lws_log_error(ret, "failed to answer call\n");
        lws_session_destroy(session);
        return NULL;
    }

    return session;
}

void lws_hangup(lws_client_t* client, lws_session_t* session)
{
    if (!client || !session) {
        return;
    }

    lws_log_info("hanging up call\n");

    // Send BYE message
    lws_uac_bye(client, session);

    // Stop session
    lws_session_stop(session);

    // Destroy session
    lws_session_destroy(session);
}
