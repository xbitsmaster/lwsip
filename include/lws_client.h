/**
 * @file lws_client.h
 * @brief LwSIP Client Core Interface
 *
 * This is the main entry point for using lwsip.
 * It coordinates SIP signaling and RTP media sessions.
 */

#ifndef __LWS_CLIENT_H__
#define __LWS_CLIENT_H__

#include "lws_types.h"
#include "lws_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Opaque Client Handle
 * ============================================================ */

typedef struct lws_client lws_client_t;

/* ============================================================
 * Client Callbacks
 * ============================================================ */

typedef struct {
    /**
     * @brief Registration state changed
     * @param param User parameter
     * @param state New registration state
     * @param code SIP status code
     */
    void (*on_reg_state)(void* param,
        lws_reg_state_t state,
        int code);

    /**
     * @brief Call state changed
     * @param param User parameter
     * @param peer Peer URI
     * @param state New call state
     */
    void (*on_call_state)(void* param,
        const char* peer,
        lws_call_state_t state);

    /**
     * @brief Incoming call
     * @param param User parameter
     * @param from Caller URI
     * @param to Callee URI
     * @param sdp Remote SDP offer
     * @param sdp_len SDP length
     */
    void (*on_incoming_call)(void* param,
        const char* from,
        const char* to,
        const char* sdp,
        int sdp_len);

    /**
     * @brief Error occurred
     * @param param User parameter
     * @param errcode Error code
     * @param description Error description
     */
    void (*on_error)(void* param,
        int errcode,
        const char* description);

    void* param;  // User parameter
} lws_client_handler_t;

/* ============================================================
 * Client API
 * ============================================================ */

/**
 * @brief Create SIP client
 * @param config Configuration
 * @param handler Event callbacks
 * @return Client handle or NULL on error
 */
lws_client_t* lws_client_create(
    const lws_config_t* config,
    const lws_client_handler_t* handler);

/**
 * @brief Destroy SIP client
 * @param client Client handle
 */
void lws_client_destroy(lws_client_t* client);

/**
 * @brief Start SIP client
 * @param client Client handle
 * @return 0 on success, <0 on error
 */
int lws_client_start(lws_client_t* client);

/**
 * @brief Stop SIP client
 * @param client Client handle
 */
void lws_client_stop(lws_client_t* client);

/**
 * @brief Run client event loop
 * This is a blocking call that processes SIP and RTP events.
 * Call this in your main loop or a dedicated thread.
 *
 * @param client Client handle
 * @param timeout_ms Timeout in milliseconds (0=non-blocking, -1=infinite)
 * @return 0 on timeout, >0 on events processed, <0 on error
 */
int lws_client_loop(lws_client_t* client, int timeout_ms);

/**
 * @brief Get configuration
 * @param client Client handle
 * @return Configuration pointer
 */
const lws_config_t* lws_client_get_config(lws_client_t* client);

/* ============================================================
 * Simplified Call API
 * ============================================================ */

/**
 * @brief Make a call (simplified API)
 * This creates an RTP session automatically and sends INVITE.
 *
 * @param client Client handle
 * @param peer_uri Peer URI (e.g., "sip:1001@192.168.1.100")
 * @return Session handle or NULL on error
 */
lws_session_t* lws_call(lws_client_t* client, const char* peer_uri);

/**
 * @brief Answer a call (simplified API)
 * @param client Client handle
 * @param peer_uri Peer URI (from on_incoming_call callback)
 * @return Session handle or NULL on error
 */
lws_session_t* lws_answer(lws_client_t* client, const char* peer_uri);

/**
 * @brief Hang up a call
 * @param client Client handle
 * @param session Session handle (from lws_call or lws_answer)
 */
void lws_hangup(lws_client_t* client, lws_session_t* session);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_CLIENT_H__ */
