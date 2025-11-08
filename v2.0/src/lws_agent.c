/**
 * @file lws_agent.c
 * @brief SIP agent core implementation
 */

#include "lws_agent.h"
#include "lws_session.h"
#include "lws_transport.h"
#include "lws_error.h"
#include "lws_mem.h"
#include "lws_log.h"
#include "lws_mutex.h"
#include "lws_thread.h"
#include "lws_intl.h"  // Internal structures

// libsip headers
#include "sip-agent.h"
#include "sip-message.h"
#include "sip-uac.h"
#include "sip-uas.h"
#include "sip-transport.h"
#include "http-parser.h"

#include <string.h>
#include <stdio.h>

/* ============================================================
 * Forward Declarations for Static UAC/UAS Functions
 * ============================================================ */

// UAC functions (internal only)
static int lws_uac_register(lws_agent_t* agent);
static int lws_uac_unregister(lws_agent_t* agent);
static lws_reg_state_t lws_uac_get_reg_state(lws_agent_t* agent);
static int lws_uac_invite(lws_agent_t* agent, lws_session_t *session, const char* peer_uri);
static int lws_uac_cancel(lws_agent_t* agent, lws_session_t* session);
static int lws_uac_bye(lws_agent_t* agent, lws_session_t* session);

// UAS functions (internal only)
static int lws_uas_answer(lws_agent_t* agent, lws_session_t* session, const char* peer_uri);
static int lws_uas_reject(lws_agent_t* agent, const char* peer_uri, int code);
static int lws_uas_ringing(lws_agent_t* agent, const char* peer_uri);

/* ============================================================
 * UAC Internal Structures and Callbacks
 * ============================================================ */

/* Transaction context for UAC callbacks */
typedef struct {
    lws_agent_t* agent;
    lws_session_t* session;
    char peer_uri[256];
} uac_context_t;

// REGISTER callback
static int on_register_reply(void* param, const struct sip_message_t* reply,
    struct sip_uac_transaction_t* t, int code)
{
    lws_agent_t* agent = (lws_agent_t*)param;

    lws_log_info("REGISTER reply: code=%d\n", code);

    if (code >= 200 && code < 300) {
        agent->reg_state = LWS_REG_REGISTERED;
    } else if (code >= 300) {
        agent->reg_state = LWS_REG_FAILED;
    }

    // Notify application
    if (agent->handler->on_reg_state) {
        agent->handler->on_reg_state(agent->handler->param,
                                     agent->reg_state, code);
    }

    (void)reply;
    (void)t;
    return 0;
}

// INVITE callback
static int on_invite_reply(void* param, const struct sip_message_t* reply,
    struct sip_uac_transaction_t* t, struct sip_dialog_t* dialog,
    const struct cstring_t* id, int code)
{
    uac_context_t* ctx = (uac_context_t*)param;
    lws_agent_t* agent = ctx->agent;

    lws_log_info("INVITE reply: code=%d\n", code);

    // Process SDP in 200 OK
    if (code == 200 && reply->payload && reply->size > 0) {
        lws_log_info("processing SDP answer (%d bytes)\n", reply->size);

        // Save dialog to session (for sending BYE later)
        if (ctx->session && dialog) {
            lws_session_set_dialog(ctx->session, dialog);
            lws_log_info("dialog saved to session\n");
        }

        // Process SDP answer and update session
        if (ctx->session) {
            lws_session_process_sdp(ctx->session,
                                   (const char*)reply->payload,
                                   reply->size);
        }

        // Send ACK for 2xx response
        sip_uac_ack(t, NULL, 0, NULL);
    }

    // Notify application about call state
    if (agent->handler->on_call_state) {
        lws_call_state_t state;

        if (code < 200) {
            state = LWS_CALL_RINGING;
        } else if (code >= 200 && code < 300) {
            state = LWS_CALL_ESTABLISHED;
        } else {
            state = LWS_CALL_FAILED;
        }

        agent->handler->on_call_state(agent->handler->param,
                                     ctx->peer_uri, state);
    }

    // Free context on final response
    if (code >= 200) {
        lws_free(ctx);
    }

    (void)id;
    return 0;
}

// BYE callback
static int on_bye_reply(void* param, const struct sip_message_t* reply,
    struct sip_uac_transaction_t* t, int code)
{
    lws_agent_t* agent = (lws_agent_t*)param;

    lws_log_info("BYE reply: code=%d\n", code);

    if (agent->handler->on_call_state) {
        agent->handler->on_call_state(agent->handler->param, NULL,
                                     LWS_CALL_TERMINATED);
    }

    (void)reply;
    (void)t;
    return 0;
}

// MESSAGE callback
static int on_message_reply(void* param, const struct sip_message_t* reply,
    struct sip_uac_transaction_t* t, int code)
{
    lws_agent_t* agent = (lws_agent_t*)param;

    lws_log_info("MESSAGE reply: code=%d\n", code);

    // MESSAGE is a simple request-response transaction
    // We just log the result for now
    // Applications can add error handling if needed via on_error callback

    if (code >= 300 && agent->handler->on_error) {
        char desc[128];
        snprintf(desc, sizeof(desc), "MESSAGE failed with code %d", code);
        agent->handler->on_error(agent->handler->param, code, desc);
    }

    (void)reply;
    (void)t;
    return 0;
}

// Via callback implementation
static int lws_uac_via_callback(void* transport, const char* destination,
                                 char protocol[16], char local[128], char dns[128])
{
    lws_agent_t* agent = (lws_agent_t*)transport;
    char local_ip[64];
    uint16_t local_port;

    lws_log_info("lws_uac_via_callback: %s\n", destination);

    if (!agent || !agent->transport) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid transport in via callback\n");
        return -1;
    }

    // Get local address from transport
    lws_transport_get_local_addr((void*)agent->transport, local_ip, &local_port);

    lws_log_info("lws_uac_via_callback: %s %s:%d\n", protocol, local_ip, local_port);

    // Set protocol (for now, only TCP is supported)
    snprintf(protocol, 16, "TCP");

    // Set local address with port
    snprintf(local, 128, "%s:%d", local_ip, local_port);

    // Set DNS (use IP address if no DNS name available)
    snprintf(dns, 128, "%s", local_ip);

    (void)destination;
    return 0;
}

// Send callback implementation
static int lws_uac_send_callback(void* transport, const void* data, size_t bytes)
{
    lws_agent_t* agent = (lws_agent_t*)transport;

    lws_log_trace("lws_uac_send_callback: transport=%p, bytes=%zu\n", transport, bytes);
    lws_log_info("lws_uac_send_callback send %zu bytes\n", bytes);

    if (!agent || !agent->transport) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid transport in send callback\n");
        return -1;
    }

    lws_log_trace("lws_uac_send_callback: calling lws_transport_send\n");

    // Send data via the transport layer
    int ret = lws_transport_send((lws_transport_t*)agent->transport, data, (int)bytes);
    if (ret < 0) {
        lws_log_error(LWS_ERR_SOCKET_SEND, "failed to send SIP message\n");
        return -1;
    }

    lws_log_trace("lws_uac_send_callback: send successful\n");
    return 0;
}

// libsip transport for UAC
static struct sip_transport_t s_uac_transport = {
    .via = lws_uac_via_callback,
    .send = lws_uac_send_callback,
};

/* ============================================================
 * libsip Transport Callbacks
 * ============================================================ */

// libsip transport send callback
static int sip_transport_send(void* param, const struct cstring_t* protocol,
    const struct cstring_t* url, const struct cstring_t *received,
    int rport, const void* data, int bytes)
{
    lws_agent_t* agent = (lws_agent_t*)param;
    int ret;

    (void)protocol;
    (void)url;
    (void)received;
    (void)rport;

    lws_log_info("sip transport sending %d bytes\n", bytes);

    // Send via our transport abstraction
    ret = lws_transport_send(agent->transport, data, bytes);
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
    lws_agent_t* agent = (lws_agent_t*)userdata;
    struct http_parser_t* parser;
    struct sip_message_t* msg;
    size_t bytes;
    int ret;

    (void)transport;

    lws_log_info("received %d bytes from transport\n", len);

    // Determine if this is a request or response
    // SIP responses start with "SIP/2.0", requests start with method (INVITE, REGISTER, etc.)
    int parser_type = HTTP_PARSER_REQUEST;
    int msg_type = SIP_MESSAGE_REQUEST;
    if (len >= 7 && memcmp(data, "SIP/2.0", 7) == 0) {
        parser_type = HTTP_PARSER_RESPONSE;
        msg_type = SIP_MESSAGE_REPLY;
    }

    // Create HTTP parser for SIP message
    parser = http_parser_create(parser_type, NULL, NULL);
    if (!parser) {
        lws_log_error(LWS_ERR_NOMEM, "failed to create http parser\n");
        return -1;
    }

    // Parse SIP message
    bytes = len;
    ret = http_parser_input(parser, data, &bytes);
    if (ret < 0) {
        lws_log_error(LWS_ERR_SIP_PARSE, "failed to parse SIP message:%d \n%s \n---------\n", len, (const char *)data);
        http_parser_destroy(parser);
        return -1;
    }

    // Create SIP message structure
    msg = sip_message_create(msg_type);
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
    if (agent->sip_agent) {
        ret = sip_agent_input((struct sip_agent_t*)agent->sip_agent, msg, agent);
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
    lws_agent_t* agent = (lws_agent_t*)userdata;

    lws_log_info("transport state changed: %d\n", state);

    if (state == LWS_TRANSPORT_STATE_ERROR && agent->handler->on_error) {
        agent->handler->on_error(agent->handler->param,
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
    lws_agent_t* agent = (lws_agent_t*)param;

    lws_log_info("register response: user=%s, expires=%d\n", user, expires);

    if (expires > 0) {
        agent->reg_state = LWS_REG_REGISTERED;
    } else {
        agent->reg_state = LWS_REG_UNREGISTERED;
    }

    // Notify application
    if (agent->handler->on_reg_state) {
        agent->handler->on_reg_state(agent->handler->param,
                                     agent->reg_state, 200);
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
    lws_agent_t* agent = (lws_agent_t*)param;
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
    agent->current_invite_transaction = t;
    strncpy(agent->current_peer_uri, from, sizeof(agent->current_peer_uri) - 1);

    // Add reference to transaction so it won't be destroyed until we reply
    sip_uas_transaction_addref(t);

    // Notify application
    if (agent->handler->on_incoming_call) {
        agent->handler->on_incoming_call(agent->handler->param,
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
    lws_agent_t* agent = (lws_agent_t*)param;

    lws_log_info("received ACK: code=%d, dialog=%p\n", code, dialog);

    // Save dialog to current session for UAS to send BYE later
    if (agent->current_session && dialog) {
        lws_session_set_dialog(agent->current_session, dialog);
        lws_log_info("dialog saved to UAS session\n");
    } else {
        lws_log_warn(LWS_ERR_INVALID_PARAM, "no current session or dialog to save\n");
    }

    (void)req;
    (void)t;
    (void)id;
    (void)data;
    (void)bytes;
    return 0;
}

static int sip_uas_onbye(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct cstring_t* id)
{
    lws_agent_t* agent = (lws_agent_t*)param;

    lws_log_info("received BYE\n");

    // Notify application
    if (agent->handler->on_call_state) {
        agent->handler->on_call_state(agent->handler->param, NULL,
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
    lws_agent_t* agent = (lws_agent_t*)param;

    lws_log_info("received CANCEL from %s\n",
                 req->from.uri.host.p ? req->from.uri.host.p : "unknown");

    // Notify application that call was cancelled
    if (agent->handler->on_call_state) {
        agent->handler->on_call_state(agent->handler->param,
                                     req->from.uri.host.p,
                                     LWS_CALL_TERMINATED);
    }

    // Send 200 OK response to CANCEL per RFC 3261
    sip_uas_reply(t, 200, NULL, 0, agent);

    (void)id;
    return 0;
}

// Stub handlers for other methods
static int sip_uas_onprack(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct cstring_t* id,
    const void* data, int bytes)
{
    lws_log_info("received PRACK\n");
    (void)param; (void)req; (void)t; (void)id; (void)data; (void)bytes;
    return 0;
}

static int sip_uas_onupdate(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct cstring_t* id,
    const void* data, int bytes)
{
    lws_log_info("received UPDATE\n");
    (void)param; (void)req; (void)t; (void)id; (void)data; (void)bytes;
    return 0;
}

static int sip_uas_oninfo(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct cstring_t* id,
    const struct cstring_t* package, const void* data, int bytes)
{
    lws_log_info("received INFO\n");
    (void)param; (void)req; (void)t; (void)id; (void)package;
    (void)data; (void)bytes;
    return 0;
}

static int sip_uas_onsubscribe(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, struct sip_subscribe_t* subscribe,
    const struct cstring_t* id)
{
    lws_log_info("received SUBSCRIBE\n");
    (void)param; (void)req; (void)t; (void)subscribe; (void)id;
    return 0;
}

static int sip_uas_onnotify(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct sip_event_t* event)
{
    lws_log_info("received NOTIFY\n");
    (void)param; (void)req; (void)t; (void)event;
    return 0;
}

static int sip_uas_onpublish(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const struct sip_event_t* event)
{
    lws_log_info("received PUBLISH\n");
    (void)param; (void)req; (void)t; (void)event;
    return 0;
}

static int sip_uas_onrefer(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t)
{
    lws_log_info("received REFER\n");

    (void)param; (void)req; (void)t;
    return 0;
}

static int sip_uas_onmessage(void* param, const struct sip_message_t* req,
    struct sip_uas_transaction_t* t, const void* data, int bytes)
{
    lws_agent_t* agent = (lws_agent_t*)param;
    char from[256] = {0};
    char to[256] = {0};

    lws_log_info("incoming MESSAGE: content_len=%d\n", bytes);

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

    // Notify application
    if (agent->handler->on_msg) {
        agent->handler->on_msg(agent->handler->param,
                               from, to,
                               (const char*)data, bytes);
    }

    // Send 200 OK response
    sip_uas_reply(t, 200, NULL, 0, agent);

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
 * UAC Implementation (Static Functions)
 * ============================================================ */

static int lws_uac_register(lws_agent_t* agent)
{
    struct sip_uac_transaction_t* t;
    char from[256];
    char registrar[256];
    int ret;

    if (!agent) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "agent is NULL\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!agent->sip_agent) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "sip agent not created\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("sending REGISTER to %s:%d as %s\n",
                 agent->config->server_host,
                 agent->config->server_port,
                 agent->config->username);

    agent->reg_state = LWS_REG_REGISTERING;

    if (agent->handler->on_reg_state) {
        agent->handler->on_reg_state(agent->handler->param,
                                     LWS_REG_REGISTERING, 0);
    }

    // Build From URI: sip:username@server
    snprintf(from, sizeof(from), "sip:%s@%s",
             agent->config->username,
             agent->config->server_host);

    // Build registrar URI
    snprintf(registrar, sizeof(registrar), "sip:%s:%d",
             agent->config->server_host,
             agent->config->server_port);

    // Create REGISTER transaction
    t = sip_uac_register((struct sip_agent_t*)agent->sip_agent,
                         from, registrar,
                         agent->config->expires,
                         on_register_reply, agent);
    if (!t) {
        lws_log_error(LWS_ERR_SIP_REGISTER, "failed to create REGISTER transaction\n");
        agent->reg_state = LWS_REG_FAILED;

        if (agent->handler->on_reg_state) {
            agent->handler->on_reg_state(agent->handler->param,
                                        LWS_REG_FAILED, 0);
        }

        return LWS_ERR_SIP_REGISTER;
    }

    // Send REGISTER
    lws_log_trace("lws_uac_register: calling sip_uac_send with transport callbacks\n");
    ret = sip_uac_send(t, NULL, 0, &s_uac_transport, agent);
    lws_log_trace("lws_uac_register: sip_uac_send returned %d\n", ret);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send REGISTER\n");
        agent->reg_state = LWS_REG_FAILED;

        if (agent->handler->on_reg_state) {
            agent->handler->on_reg_state(agent->handler->param,
                                        LWS_REG_FAILED, 0);
        }

        return LWS_ERR_SIP_SEND;
    }

    return LWS_OK;
}

static int lws_uac_unregister(lws_agent_t* agent)
{
    struct sip_uac_transaction_t* t;
    char from[256];
    char registrar[256];
    int ret;

    if (!agent) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "agent is NULL\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!agent->sip_agent) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "sip agent not created\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("sending UNREGISTER to %s:%d\n",
                 agent->config->server_host,
                 agent->config->server_port);

    agent->reg_state = LWS_REG_UNREGISTERING;

    if (agent->handler->on_reg_state) {
        agent->handler->on_reg_state(agent->handler->param,
                                     LWS_REG_UNREGISTERING, 0);
    }

    // Build From URI: sip:username@server
    snprintf(from, sizeof(from), "sip:%s@%s",
             agent->config->username,
             agent->config->server_host);

    // Build registrar URI
    snprintf(registrar, sizeof(registrar), "sip:%s:%d",
             agent->config->server_host,
             agent->config->server_port);

    // Create REGISTER transaction with Expires: 0
    t = sip_uac_register((struct sip_agent_t*)agent->sip_agent,
                         from, registrar,
                         0,  // Expires: 0 = unregister
                         on_register_reply, agent);
    if (!t) {
        lws_log_error(LWS_ERR_SIP_REGISTER, "failed to create UNREGISTER transaction\n");
        agent->reg_state = LWS_REG_FAILED;

        if (agent->handler->on_reg_state) {
            agent->handler->on_reg_state(agent->handler->param,
                                        LWS_REG_FAILED, 0);
        }

        return LWS_ERR_SIP_REGISTER;
    }

    // Send UNREGISTER
    ret = sip_uac_send(t, NULL, 0, &s_uac_transport, agent);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send UNREGISTER\n");
        agent->reg_state = LWS_REG_FAILED;

        if (agent->handler->on_reg_state) {
            agent->handler->on_reg_state(agent->handler->param,
                                        LWS_REG_FAILED, 0);
        }

        return LWS_ERR_SIP_SEND;
    }

    return LWS_OK;
}

static lws_reg_state_t lws_uac_get_reg_state(lws_agent_t* agent)
{
    if (!agent) {
        return LWS_REG_NONE;
    }

    return agent->reg_state;
}

static int lws_uac_invite(lws_agent_t* agent, lws_session_t* session, const char* peer_uri)
{
    struct sip_uac_transaction_t* t;
    uac_context_t* ctx;
    char from[256];
    char sdp[4096];
    int sdp_len;
    int ret;

    if (!agent || !peer_uri || !session) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!agent->sip_agent) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "sip agent not created\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("sending INVITE to %s\n", peer_uri);

    // Create context for callback
    ctx = (uac_context_t*)lws_malloc(sizeof(uac_context_t));
    if (!ctx) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate context\n");
        return LWS_ERR_NOMEM;
    }

    memset(ctx, 0, sizeof(uac_context_t));
    ctx->agent = agent;
    ctx->session = session;
    strncpy(ctx->peer_uri, peer_uri, sizeof(ctx->peer_uri) - 1);

    // Build From URI
    snprintf(from, sizeof(from), "sip:%s@%s",
             agent->config->username,
             agent->config->server_host);

    // Generate SDP offer
    char local_ip[64];
    lws_transport_get_local_addr((void*)agent->transport, local_ip, NULL);
    sdp_len = lws_session_generate_sdp_offer(session, local_ip, sdp, sizeof(sdp));
    if (sdp_len <= 0) {
        lws_log_error(LWS_ERR_SDP_GENERATE, "failed to generate SDP\n");
        lws_free(ctx);
        return LWS_ERR_SDP_GENERATE;
    }

    lws_log_info("generated SDP offer (%d bytes)\n", sdp_len);

    // Create INVITE transaction
    t = sip_uac_invite((struct sip_agent_t*)agent->sip_agent,
                       from, peer_uri,
                       on_invite_reply, ctx);
    if (!t) {
        lws_log_error(LWS_ERR_SIP_INVITE, "failed to create INVITE transaction\n");
        lws_free(ctx);
        return LWS_ERR_SIP_INVITE;
    }

    // Add Content-Type header
    sip_uac_add_header(t, "Content-Type", "application/sdp");

    // Send INVITE with SDP
    ret = sip_uac_send(t, sdp, sdp_len, &s_uac_transport, agent);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send INVITE\n");
        lws_free(ctx);
        return LWS_ERR_SIP_SEND;
    }

    // Save transaction to session for CANCEL
    lws_session_set_invite_transaction(session, t);

    // Notify application
    if (agent->handler->on_call_state) {
        agent->handler->on_call_state(agent->handler->param,
                                      peer_uri,
                                      LWS_CALL_CALLING);
    }

    return LWS_OK;
}

static int lws_uac_cancel(lws_agent_t* agent, lws_session_t* session)
{
    struct sip_uac_transaction_t* invite_t;
    struct sip_uac_transaction_t* cancel_t;

    if (!agent || !session) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!agent->sip_agent) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "sip agent not created\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    // Get INVITE transaction from session
    invite_t = (struct sip_uac_transaction_t*)lws_session_get_invite_transaction(session);
    if (!invite_t) {
        lws_log_error(LWS_ERR_INTERNAL, "no INVITE transaction found (already answered or timed out?)\n");
        return LWS_ERR_INTERNAL;
    }

    // Send CANCEL request using libsip
    // sip_uac_cancel() creates and sends a CANCEL request for the given INVITE transaction
    // The CANCEL transaction is managed by libsip, we don't need to store it
    cancel_t = sip_uac_cancel(agent->sip_agent, invite_t, NULL, NULL);
    if (!cancel_t) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send CANCEL\n");
        return LWS_ERR_SIP_SEND;
    }

    lws_log_info("CANCEL sent successfully\n");

    // Clear the saved transaction pointer (it's no longer valid for CANCEL)
    lws_session_set_invite_transaction(session, NULL);

    // Notify application
    if (agent->handler->on_call_state) {
        agent->handler->on_call_state(agent->handler->param,
                                      NULL,  // peer_uri not needed for cancel
                                      LWS_CALL_TERMINATED);
    }

    return LWS_OK;
}

static int lws_uac_bye(lws_agent_t* agent, lws_session_t* session)
{
    struct sip_uac_transaction_t* t;
    struct sip_dialog_t* dialog;
    int ret;

    if (!agent || !session) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!agent->sip_agent) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "sip agent not created\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    // Get dialog from session
    dialog = (struct sip_dialog_t*)lws_session_get_dialog(session);
    if (!dialog) {
        lws_log_error(LWS_ERR_SIP_NO_DIALOG, "no dialog in session\n");
        return LWS_ERR_SIP_NO_DIALOG;
    }

    lws_log_info("sending BYE\n");

    // Create BYE transaction
    t = sip_uac_bye((struct sip_agent_t*)agent->sip_agent,
                    dialog,
                    on_bye_reply,
                    agent);
    if (!t) {
        lws_log_error(LWS_ERR_SIP_BYE, "failed to create BYE transaction\n");
        return LWS_ERR_SIP_BYE;
    }

    // Send BYE
    ret = sip_uac_send(t, NULL, 0, &s_uac_transport, agent);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send BYE\n");
        return LWS_ERR_SIP_SEND;
    }

    if (agent->handler->on_call_state) {
        agent->handler->on_call_state(agent->handler->param,
                                      NULL,
                                      LWS_CALL_HANGUP);
    }

    return LWS_OK;
}

/* ============================================================
 * UAS Implementation (Static Functions)
 * ============================================================ */

static int lws_uas_answer(lws_agent_t* agent, lws_session_t* session, const char* peer_uri)
{
    struct sip_uas_transaction_t* t;
    char sdp[4096];
    char local_ip[64];
    int sdp_len;
    int ret;

    if (!agent || !peer_uri || !session) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    // Get current INVITE transaction
    t = (struct sip_uas_transaction_t*)agent->current_invite_transaction;
    if (!t) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "no current INVITE transaction\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("answering call from %s\n", peer_uri);

    // Generate SDP answer
    lws_transport_get_local_addr(agent->transport, local_ip, NULL);
    sdp_len = lws_session_generate_sdp_offer(session, local_ip, sdp, sizeof(sdp));
    if (sdp_len <= 0) {
        lws_log_error(LWS_ERR_SDP_GENERATE, "failed to generate SDP answer\n");
        return LWS_ERR_SDP_GENERATE;
    }

    lws_log_info("generated SDP answer (%d bytes)\n", sdp_len);

    // Add Content-Type header
    sip_uas_add_header(t, "Content-Type", "application/sdp");

    // Send 200 OK with SDP
    ret = sip_uas_reply(t, 200, sdp, sdp_len, agent);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send 200 OK\n");
        return LWS_ERR_SIP_SEND;
    }

    // Notify application
    if (agent->handler->on_call_state) {
        agent->handler->on_call_state(agent->handler->param,
                                      peer_uri,
                                      LWS_CALL_ESTABLISHED);
    }

    // Release and clear transaction
    if (agent->current_invite_transaction) {
        sip_uas_transaction_release((struct sip_uas_transaction_t*)agent->current_invite_transaction);
        agent->current_invite_transaction = NULL;
    }

    return LWS_OK;
}

static int lws_uas_reject(lws_agent_t* agent,
    const char* peer_uri,
    int code)
{
    struct sip_uas_transaction_t* t;
    int ret;

    if (!agent || !peer_uri) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    // Get current INVITE transaction
    t = (struct sip_uas_transaction_t*)agent->current_invite_transaction;
    if (!t) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "no current INVITE transaction\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("rejecting call from %s with code %d\n", peer_uri, code);

    // Common rejection codes:
    // 486 Busy Here
    // 603 Decline
    // 480 Temporarily Unavailable
    if (code <= 0 || code < 400) {
        code = 486;  // Default: Busy Here
    }

    // Send rejection response
    ret = sip_uas_reply(t, code, NULL, 0, agent);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send rejection\n");
        return LWS_ERR_SIP_SEND;
    }

    // Notify application
    if (agent->handler->on_call_state) {
        agent->handler->on_call_state(agent->handler->param,
                                      peer_uri,
                                      LWS_CALL_TERMINATED);
    }

    // Release and clear transaction
    if (agent->current_invite_transaction) {
        sip_uas_transaction_release((struct sip_uas_transaction_t*)agent->current_invite_transaction);
        agent->current_invite_transaction = NULL;
    }

    return LWS_OK;
}

static int lws_uas_ringing(lws_agent_t* agent, const char* peer_uri)
{
    struct sip_uas_transaction_t* t;
    int ret;

    if (!agent || !peer_uri) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    // Get current INVITE transaction
    t = (struct sip_uas_transaction_t*)agent->current_invite_transaction;
    if (!t) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "no current INVITE transaction\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("sending ringing to %s\n", peer_uri);

    // Send 180 Ringing
    ret = sip_uas_reply(t, 180, NULL, 0, agent);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send ringing\n");
        return LWS_ERR_SIP_SEND;
    }

    // Notify application
    if (agent->handler->on_call_state) {
        agent->handler->on_call_state(agent->handler->param,
                                      peer_uri,
                                      LWS_CALL_RINGING);
    }

    return LWS_OK;
}

/* ============================================================
 * Public API Implementation
 * ============================================================ */

lws_agent_t* lws_agent_create(lws_config_t* config, lws_agent_handler_t* handler, lws_session_handler_t* session_handler)
{
    lws_agent_t* agent;
    lws_transport_config_t transport_config;
    lws_transport_handler_t transport_handler;

    if (!config || !handler) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    agent = (lws_agent_t*)lws_malloc(sizeof(lws_agent_t));
    if (!agent) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate agent\n");
        return NULL;
    }

    memset(agent, 0, sizeof(lws_agent_t));

    agent->config = config;
    agent->handler = handler;
    agent->session_handler = session_handler;

    agent->mutex = lws_mutex_create();
    if (!agent->mutex) {
        lws_log_error(LWS_ERR_NOMEM, "failed to create mutex\n");
        lws_free(agent);
        return NULL;
    }

    // Create transport
    memset(&transport_config, 0, sizeof(transport_config));
    transport_config.remote_host = config->server_host;
    transport_config.remote_port = config->server_port;
    transport_config.local_port = config->local_port;
    transport_config.transport_type = config->transport_type;
    transport_config.userdata = agent;

    transport_config.enable_tls = config->enable_tls;
    if(config->enable_tls) {
        transport_config.tls_ca = config->tls_ca;
        transport_config.tls_ca_size = config->tls_ca_size;
        transport_config.tls_cert = config->tls_cert;
        transport_config.tls_cert_size = config->tls_cert_size;
        transport_config.tls_key = config->tls_key;
        transport_config.tls_key_size = config->tls_key_size;
    }

#if defined(LWS_ENABLE_TRANSPORT_MQTT)
    if (config->transport_type == LWS_TRANSPORT_MQTT) {
        transport_config.mqtt_client_id = config->mqtt_client_id;
        transport_config.mqtt_pub_topic = config->mqtt_pub_topic;
        transport_config.mqtt_sub_topic = config->mqtt_sub_topic;
    }
#endif

    transport_handler.on_recv = on_transport_recv;
    transport_handler.on_state = on_transport_state;
    transport_handler.userdata = agent;

    // Create transport based on type
    switch(config->transport_type) {
        case LWS_TRANSPORT_UDP:
        case LWS_TRANSPORT_TCP:
            // Socket transport handles both TCP and UDP sockets
            agent->transport = lws_transport_socket_create(&transport_config,
                                                         &transport_handler);
            break;
        case LWS_TRANSPORT_MQTT:
#if defined(LWS_ENABLE_TRANSPORT_MQTT)
            agent->transport = lws_transport_mqtt_create(&transport_config,
                                                      &transport_handler);
#else
            lws_log_error(LWS_ERR_INVALID_PARAM, "MQTT transport not enabled\n");
            lws_mutex_destroy(agent->mutex);
            lws_free(agent);
            return NULL;
#endif
            break;

        default:
            lws_log_error(LWS_ERR_INVALID_PARAM, "invalid transport type\n");
            lws_mutex_destroy(agent->mutex);
            lws_free(agent);
            return NULL;
    }
    if (!agent->transport) {
        lws_log_error(LWS_ERR_SIP_TRANSPORT, "failed to create transport\n");
        lws_mutex_destroy(agent->mutex);
        lws_free(agent);
        return NULL;
    }

    agent->reg_state = LWS_REG_NONE;

    lws_log_info("agent created: server=%s:%d, user=%s\n",
                 config->server_host,
                 config->server_port,
                 config->username);

    return agent;
}

void lws_agent_destroy(lws_agent_t* agent)
{
    if (!agent) {
        return;
    }

    if (agent->is_started) {
        lws_agent_stop(agent);
    }

    if (agent->transport) {
        lws_transport_destroy(agent->transport);
        agent->transport = NULL;
    }

    if (agent->mutex) {
        lws_mutex_destroy(agent->mutex);
    }

    lws_free(agent);
    lws_log_info("agent destroyed\n");
}

int lws_agent_start(lws_agent_t* agent)
{
    int ret;

    lws_log_trace("lws_agent_start: entered, agent=%p\n", (void*)agent);

    if (!agent) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "agent is NULL\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (agent->is_started) {
        lws_log_warn(LWS_ERR_ALREADY_INITIALIZED, "agent already started\n");
        return LWS_OK;
    }

    // Create SIP agent from libsip
    lws_log_trace("lws_agent_start: creating sip agent from libsip\n");
    agent->sip_agent = sip_agent_create(&s_uas_handler);
    if (!agent->sip_agent) {
        lws_log_error(LWS_ERR_SIP_CREATE, "failed to create sip agent\n");
        return LWS_ERR_SIP_CREATE;
    }
    lws_log_trace("lws_agent_start: sip agent created successfully\n");

    // Connect transport
    lws_log_trace("lws_agent_start: connecting transport\n");
    ret = lws_transport_connect(agent->transport);
    if (ret != LWS_OK) {
        lws_log_error(ret, "failed to connect transport\n");
        sip_agent_destroy((struct sip_agent_t*)agent->sip_agent);
        agent->sip_agent = NULL;
        return ret;
    }
    lws_log_trace("lws_agent_start: transport connected successfully\n");

    // Start SIP registration
    lws_log_trace("lws_agent_start: starting registration\n");
    ret = lws_uac_register(agent);
    if (ret != LWS_OK) {
        lws_log_warn(ret, "failed to start registration (will retry later)\n");
        // Don't fail agent start - registration can be retried
    }

    agent->is_started = 1;
    lws_log_info("agent started\n");

    return LWS_OK;
}

void lws_agent_stop(lws_agent_t* agent)
{
    if (!agent || !agent->is_started) {
        return;
    }

    // unregistration
    lws_uac_unregister(agent);

    // Disconnect transport
    if (agent->transport) {
        lws_transport_disconnect(agent->transport);
    }

    // Destroy SIP agent
    if (agent->sip_agent) {
        sip_agent_destroy((struct sip_agent_t*)agent->sip_agent);
        agent->sip_agent = NULL;
        lws_log_info("sip agent destroyed\n");
    }

    agent->is_started = 0;
    lws_log_info("agent stopped\n");
}

int lws_agent_loop(lws_agent_t* agent, int timeout_ms)
{
    int ret;

    if (!agent) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (!agent->is_started) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "agent not started\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_trace("lws_agent_loop: entered, agent=%p\n", (void*)agent);

    // Poll transport for SIP messages
    ret = lws_transport_poll(agent->transport, timeout_ms);
    if (ret < 0) {
        return ret;
    }

    // Note: RTP/RTCP packet processing is done at session level
    // Call lws_session_poll(session, timeout_ms) in your event loop
    // for each active session to receive and process media packets

    return ret;
}

const lws_config_t* lws_agent_get_config(lws_agent_t* agent)
{
    if (!agent) {
        return NULL;
    }

    return agent->config;
}

/* ============================================================
 * Simplified Call API
 * ============================================================ */

lws_session_t* lws_call(lws_agent_t* agent, const char* peer_uri, int enable_video)
{
    lws_session_t* session;
    int ret;

    if (!agent || !peer_uri) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    lws_log_info("making call to %s (video=%d)\n", peer_uri, enable_video);

    session = lws_session_create(agent->config, agent->session_handler, enable_video);
    if (!session) {
        lws_log_error(LWS_ERR_SESSION_CREATE, "failed to create session\n");
        return NULL;
    }

    // Send INVITE
    ret = lws_uac_invite(agent, session, peer_uri);
    if (ret != LWS_OK) {
        lws_log_error(LWS_ERR_SIP_INVITE, "failed to send INVITE\n");
        lws_session_destroy(session);
        return NULL;
    }

    return session;
}

lws_session_t* lws_answer(lws_agent_t* agent, const char* peer_uri)
{
    lws_session_t* session;
    int ret;

    if (!agent || !peer_uri) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    lws_log_info("answering call from %s\n", peer_uri);

    session = lws_session_create(agent->config, agent->session_handler, agent->config->enable_video);
    if (!session) {
        lws_log_error(LWS_ERR_SESSION_CREATE, "failed to create session\n");
        return NULL;
    }

    // Save session to agent for dialog binding in onack callback
    agent->current_session = session;

    // Answer call
    ret = lws_uas_answer(agent, session, peer_uri);
    if (ret != LWS_OK) {
        lws_log_error(ret, "failed to answer call\n");
        lws_session_destroy(session);
        return NULL;
    }

    return session;
}

void lws_hangup(lws_agent_t* agent, lws_session_t* session)
{
    if (!agent || !session) {
        return;
    }

    lws_log_info("hanging up call\n");

    // Send BYE message
    lws_uac_bye(agent, session);

    // Stop session
    lws_session_stop(session);

    // Destroy session
    lws_session_destroy(session);
}

int lws_cancel(lws_agent_t* agent, lws_session_t* session)
{
    if (!agent || !session) {
        return LWS_ERR_INVALID_PARAM;
    }

    lws_log_info("canceling outgoing call\n");

    // Send CANCEL request
    return lws_uac_cancel(agent, session);
}

/* ============================================================
 * Simplified Message API (Public)
 * ============================================================ */

int lws_send_msg(lws_agent_t* agent, const char* peer_uri, const char* content, int content_len)
{
    struct sip_uac_transaction_t* t;
    char from[256];
    int ret;

    if (!agent || !peer_uri || !content || content_len <= 0) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!agent->sip_agent) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "sip agent not created\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("sending MESSAGE to %s (%d bytes)\n", peer_uri, content_len);

    // Build From URI
    snprintf(from, sizeof(from), "sip:%s@%s",
             agent->config->username,
             agent->config->server_host);

    // Create MESSAGE transaction
    t = sip_uac_message((struct sip_agent_t*)agent->sip_agent,
                        from, peer_uri,
                        on_message_reply, agent);
    if (!t) {
        lws_log_error(LWS_ERR_SIP_MESSAGE, "failed to create MESSAGE transaction\n");
        return LWS_ERR_SIP_MESSAGE;
    }

    // Add Content-Type header for text messages
    sip_uac_add_header(t, "Content-Type", "text/plain");

    // Send MESSAGE with content
    ret = sip_uac_send(t, content, content_len, &s_uac_transport, agent);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send MESSAGE\n");
        return LWS_ERR_SIP_SEND;
    }

    return LWS_OK;
}
