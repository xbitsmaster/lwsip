#ifndef __LWS_INTL_H__
#define __LWS_INTL_H__

#include "lws_agent.h"
#include "lws_session.h"
#include "lws_osal.h"

/**
 * Internal structure definitions for lwsip implementation
 * This header consolidates internal structures to ensure consistent
 * layout across all compilation units and prevent memory corruption
 * due to structure layout mismatches.
 */

/**
 * Internal representation of lws_agent
 * This structure is used internally by lws_agent.c, lws_uac.c, and lws_uas.c
 *
 * IMPORTANT: Do not duplicate this definition in source files!
 * Always include this header to get the consistent definition.
 */
struct lws_agent {
    lws_config_t *config;
    lws_agent_handler_t *handler;
    lws_session_handler_t *session_handler;

    // SIP layer
    void* sip_agent;  // sip_agent_t from libsip
    lws_reg_state_t reg_state;

    // Transport layer (abstraction)
    lws_transport_t* transport;

    // UAS: current incoming INVITE transaction and session
    void* current_invite_transaction;  // sip_uas_transaction_t*
    lws_session_t* current_session;    // Current UAS session for dialog binding
    char current_peer_uri[256];

    // State
    int is_started;
    lws_mutex_t* mutex;         // lws_mutex_t* from osal
};

/**
 * Note: struct lws_session is defined in lws_session.c as an implementation detail.
 * Other files should only use lws_session_t* pointers without knowing the internal structure.
 */

#endif // __LWS_INTL_H__
