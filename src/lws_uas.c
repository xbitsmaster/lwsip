/**
 * @file lws_uas.c
 * @brief User Agent Server implementation
 */

#include "lws_uas.h"
#include "lws_client.h"
#include "lws_session.h"
#include "lws_transport.h"
#include "lws_error.h"
#include "lws_log.h"
#include "lws_mem.h"

// libsip headers
#include "sip-agent.h"
#include "sip-uas.h"
#include "sip-message.h"

#include <stdio.h>
#include <string.h>

/* Forward declaration of internal structure */
struct lws_client {
    lws_config_t config;
    lws_client_handler_t handler;

    void* sip_agent;  // SIP agent handle
    lws_reg_state_t reg_state;
    void* transport;  // lws_transport_t

    // For UAS: save current incoming call transaction
    void* current_invite_transaction;  // sip_uas_transaction_t*
    char current_peer_uri[256];
};

/* ============================================================
 * UAS API Implementation
 * ============================================================ */

int lws_uas_answer(lws_client_t* client,
    const char* peer_uri,
    lws_session_t* session)
{
    struct sip_uas_transaction_t* t;
    char sdp[4096];
    char local_ip[64];
    int sdp_len;
    int ret;

    if (!client || !peer_uri || !session) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    // Get current INVITE transaction
    t = (struct sip_uas_transaction_t*)client->current_invite_transaction;
    if (!t) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "no current INVITE transaction\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("answering call from %s\n", peer_uri);

    // Generate SDP answer
    lws_transport_get_local_addr(client->transport, local_ip, NULL);
    sdp_len = lws_session_generate_sdp_offer(session, local_ip, sdp, sizeof(sdp));
    if (sdp_len <= 0) {
        lws_log_error(LWS_ERR_SDP_GENERATE, "failed to generate SDP answer\n");
        return LWS_ERR_SDP_GENERATE;
    }

    lws_log_info("generated SDP answer (%d bytes)\n", sdp_len);

    // Add Content-Type header
    sip_uas_add_header(t, "Content-Type", "application/sdp");

    // Send 200 OK with SDP
    ret = sip_uas_reply(t, 200, sdp, sdp_len, client);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send 200 OK\n");
        return LWS_ERR_SIP_SEND;
    }

    // Notify application
    if (client->handler.on_call_state) {
        client->handler.on_call_state(client->handler.param,
                                      peer_uri,
                                      LWS_CALL_ESTABLISHED);
    }

    // Clear transaction (it will be freed by libsip)
    client->current_invite_transaction = NULL;

    return LWS_OK;
}

int lws_uas_reject(lws_client_t* client,
    const char* peer_uri,
    int code)
{
    struct sip_uas_transaction_t* t;
    int ret;

    if (!client || !peer_uri) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    // Get current INVITE transaction
    t = (struct sip_uas_transaction_t*)client->current_invite_transaction;
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
    ret = sip_uas_reply(t, code, NULL, 0, client);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send rejection\n");
        return LWS_ERR_SIP_SEND;
    }

    // Notify application
    if (client->handler.on_call_state) {
        client->handler.on_call_state(client->handler.param,
                                      peer_uri,
                                      LWS_CALL_TERMINATED);
    }

    // Clear transaction
    client->current_invite_transaction = NULL;

    return LWS_OK;
}

int lws_uas_ringing(lws_client_t* client, const char* peer_uri)
{
    struct sip_uas_transaction_t* t;
    int ret;

    if (!client || !peer_uri) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return LWS_ERR_INVALID_PARAM;
    }

    // Get current INVITE transaction
    t = (struct sip_uas_transaction_t*)client->current_invite_transaction;
    if (!t) {
        lws_log_error(LWS_ERR_NOT_INITIALIZED, "no current INVITE transaction\n");
        return LWS_ERR_NOT_INITIALIZED;
    }

    lws_log_info("sending ringing to %s\n", peer_uri);

    // Send 180 Ringing
    ret = sip_uas_reply(t, 180, NULL, 0, client);
    if (ret != 0) {
        lws_log_error(LWS_ERR_SIP_SEND, "failed to send ringing\n");
        return LWS_ERR_SIP_SEND;
    }

    // Notify application
    if (client->handler.on_call_state) {
        client->handler.on_call_state(client->handler.param,
                                      peer_uri,
                                      LWS_CALL_RINGING);
    }

    return LWS_OK;
}
