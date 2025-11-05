/**
 * @file lws_uac.c
 * @brief User Agent Client implementation
 */

#include "lws_uac.h"
#include "lws_client.h"
#include "lws_session.h"
#include "lws_transport.h"
#include "lws_error.h"
#include "lws_log.h"
#include "lws_mem.h"

// libsip headers
#include "sip-agent.h"
#include "sip-uac.h"
#include "sip-message.h"
#include "sip-transport.h"

#include <stdio.h>
#include <string.h>

/* Forward declaration of internal structure */
struct lws_client {
    lws_config_t config;
    lws_client_handler_t handler;

    void* sip_agent;  // SIP agent handle
    lws_reg_state_t reg_state;
    void* transport;  // lws_transport_t

    // Add more fields as needed
};

/* Transaction context for UAC callbacks */
typedef struct {
    lws_client_t* client;
    lws_session_t* session;
    char peer_uri[256];
} uac_context_t;

/* ============================================================
 * UAC Callbacks
 * ============================================================ */

// REGISTER callback
static int on_register_reply(void* param, const struct sip_message_t* reply,
    struct sip_uac_transaction_t* t, int code)
{
    lws_client_t* client = (lws_client_t*)param;

    lws_log_info("REGISTER reply: code=%d\n", code);

    if (code >= 200 && code < 300) {
        client->reg_state = LWS_REG_REGISTERED;
    } else if (code >= 300) {
        client->reg_state = LWS_REG_FAILED;
    }

    // Notify application
    if (client->handler.on_reg_state) {
        client->handler.on_reg_state(client->handler.param,
                                     client->reg_state, code);
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
    lws_client_t* client = ctx->client;

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
    if (client->handler.on_call_state) {
        lws_call_state_t state;

        if (code < 200) {
            state = LWS_CALL_RINGING;
        } else if (code >= 200 && code < 300) {
            state = LWS_CALL_ESTABLISHED;
        } else {
            state = LWS_CALL_FAILED;
        }

        client->handler.on_call_state(client->handler.param,
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
    lws_client_t* client = (lws_client_t*)param;

    lws_log_info("BYE reply: code=%d\n", code);

    if (client->handler.on_call_state) {
        client->handler.on_call_state(client->handler.param, NULL,
                                     LWS_CALL_TERMINATED);
    }

    (void)reply;
    (void)t;
    return 0;
}

// libsip transport for UAC
static struct sip_transport_t s_uac_transport = {
    .via = NULL,  // Not needed for client
    .send = NULL,  // Will be set in lws_client
};

/* ============================================================
 * UAC API Implementation
 * ============================================================ */

int lws_uac_register(lws_client_t* client)
{
    struct sip_uac_transaction_t* t;
    char from[256];
    char registrar[256];
    int ret;

    if (!client) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "client is NULL\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!client->sip_agent) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "sip agent not created\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("sending REGISTER to %s:%d as %s\n",
                 client->config.server_host,
                 client->config.server_port,
                 client->config.username);

    client->reg_state = LWS_REG_REGISTERING;

    if (client->handler.on_reg_state) {
        client->handler.on_reg_state(client->handler.param,
                                     LWS_REG_REGISTERING, 0);
    }

    // Build From URI: sip:username@server
    snprintf(from, sizeof(from), "sip:%s@%s",
             client->config.username,
             client->config.server_host);

    // Build registrar URI
    snprintf(registrar, sizeof(registrar), "sip:%s:%d",
             client->config.server_host,
             client->config.server_port);

    // Create REGISTER transaction
    t = sip_uac_register((struct sip_agent_t*)client->sip_agent,
                         from, registrar,
                         client->config.expires,
                         on_register_reply, client);
    if (!t) {
        lws_log_error(LWS_ERR_SIP_REGISTER, "failed to create REGISTER transaction\n");
        client->reg_state = LWS_REG_FAILED;

        if (client->handler.on_reg_state) {
            client->handler.on_reg_state(client->handler.param,
                                        LWS_REG_FAILED, 0);
        }

        return LWS_ERR_SIP_REGISTER;
    }

    // Send REGISTER
    ret = sip_uac_send(t, NULL, 0, &s_uac_transport, client);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send REGISTER\n");
        client->reg_state = LWS_REG_FAILED;

        if (client->handler.on_reg_state) {
            client->handler.on_reg_state(client->handler.param,
                                        LWS_REG_FAILED, 0);
        }

        return LWS_ERR_SIP_SEND;
    }

    return LWS_OK;
}

int lws_uac_unregister(lws_client_t* client)
{
    struct sip_uac_transaction_t* t;
    char from[256];
    char registrar[256];
    int ret;

    if (!client) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "client is NULL\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!client->sip_agent) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "sip agent not created\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("sending UNREGISTER to %s:%d\n",
                 client->config.server_host,
                 client->config.server_port);

    client->reg_state = LWS_REG_UNREGISTERING;

    if (client->handler.on_reg_state) {
        client->handler.on_reg_state(client->handler.param,
                                     LWS_REG_UNREGISTERING, 0);
    }

    // Build From URI: sip:username@server
    snprintf(from, sizeof(from), "sip:%s@%s",
             client->config.username,
             client->config.server_host);

    // Build registrar URI
    snprintf(registrar, sizeof(registrar), "sip:%s:%d",
             client->config.server_host,
             client->config.server_port);

    // Create REGISTER transaction with Expires: 0
    t = sip_uac_register((struct sip_agent_t*)client->sip_agent,
                         from, registrar,
                         0,  // Expires: 0 = unregister
                         on_register_reply, client);
    if (!t) {
        lws_log_error(LWS_ERR_SIP_REGISTER, "failed to create UNREGISTER transaction\n");
        client->reg_state = LWS_REG_FAILED;

        if (client->handler.on_reg_state) {
            client->handler.on_reg_state(client->handler.param,
                                        LWS_REG_FAILED, 0);
        }

        return LWS_ERR_SIP_REGISTER;
    }

    // Send UNREGISTER
    ret = sip_uac_send(t, NULL, 0, &s_uac_transport, client);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send UNREGISTER\n");
        client->reg_state = LWS_REG_FAILED;

        if (client->handler.on_reg_state) {
            client->handler.on_reg_state(client->handler.param,
                                        LWS_REG_FAILED, 0);
        }

        return LWS_ERR_SIP_SEND;
    }

    return LWS_OK;
}

lws_reg_state_t lws_uac_get_reg_state(lws_client_t* client)
{
    if (!client) {
        return LWS_REG_NONE;
    }

    return client->reg_state;
}

int lws_uac_invite(lws_client_t* client,
    const char* peer_uri,
    lws_session_t* session)
{
    struct sip_uac_transaction_t* t;
    uac_context_t* ctx;
    char from[256];
    char sdp[4096];
    int sdp_len;
    int ret;

    if (!client || !peer_uri || !session) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!client->sip_agent) {
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
    ctx->client = client;
    ctx->session = session;
    strncpy(ctx->peer_uri, peer_uri, sizeof(ctx->peer_uri) - 1);

    // Build From URI
    snprintf(from, sizeof(from), "sip:%s@%s",
             client->config.username,
             client->config.server_host);

    // Generate SDP offer
    char local_ip[64];
    lws_transport_get_local_addr((void*)client->transport, local_ip, NULL);
    sdp_len = lws_session_generate_sdp_offer(session, local_ip, sdp, sizeof(sdp));
    if (sdp_len <= 0) {
        lws_log_error(LWS_ERR_SDP_GENERATE, "failed to generate SDP\n");
        lws_free(ctx);
        return LWS_ERR_SDP_GENERATE;
    }

    lws_log_info("generated SDP offer (%d bytes)\n", sdp_len);

    // Create INVITE transaction
    t = sip_uac_invite((struct sip_agent_t*)client->sip_agent,
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
    ret = sip_uac_send(t, sdp, sdp_len, &s_uac_transport, client);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send INVITE\n");
        lws_free(ctx);
        return LWS_ERR_SIP_SEND;
    }

    // Notify application
    if (client->handler.on_call_state) {
        client->handler.on_call_state(client->handler.param,
                                      peer_uri,
                                      LWS_CALL_CALLING);
    }

    return LWS_OK;
}

int lws_uac_cancel(lws_client_t* client, lws_session_t* session)
{
    struct sip_uac_transaction_t* t;
    int ret;

    if (!client || !session) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!client->sip_agent) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "sip agent not created\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    // TODO: Need to save invite transaction in session to call sip_uac_cancel
    // For now, just log a warning
    lws_log_warn(LWS_ERR_NOT_IMPLEMENTED, "CANCEL not fully implemented (need invite transaction)\n");

    return LWS_OK;
}

int lws_uac_bye(lws_client_t* client, lws_session_t* session)
{
    struct sip_uac_transaction_t* t;
    struct sip_dialog_t* dialog;
    int ret;

    if (!client || !session) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    if (!client->sip_agent) {
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
    t = sip_uac_bye((struct sip_agent_t*)client->sip_agent,
                    dialog,
                    on_bye_reply,
                    client);
    if (!t) {
        lws_log_error(LWS_ERR_SIP_BYE, "failed to create BYE transaction\n");
        return LWS_ERR_SIP_BYE;
    }

    // Send BYE
    ret = sip_uac_send(t, NULL, 0, &s_uac_transport, client);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send BYE\n");
        return LWS_ERR_SIP_SEND;
    }

    if (client->handler.on_call_state) {
        client->handler.on_call_state(client->handler.param,
                                      NULL,
                                      LWS_CALL_HANGUP);
    }

    return LWS_OK;
}
