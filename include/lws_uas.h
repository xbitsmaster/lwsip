/**
 * @file lws_uas.h
 * @brief LwSIP User Agent Server (UAS)
 *
 * This layer handles incoming SIP requests:
 * - INVITE (incoming call)
 * - BYE (call termination)
 * - CANCEL (call cancellation)
 */

#ifndef __LWS_UAS_H__
#define __LWS_UAS_H__

#include "lws_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct lws_client lws_client_t;
typedef struct lws_session lws_session_t;

/* ============================================================
 * UAS API
 * ============================================================ */

/**
 * @brief Answer incoming call (send 200 OK)
 * @param client Client handle
 * @param peer_uri Peer URI (from on_incoming_call callback)
 * @param session RTP session handle
 * @return 0 on success, <0 on error
 */
int lws_uas_answer(lws_client_t* client,
    const char* peer_uri,
    lws_session_t* session);

/**
 * @brief Reject incoming call
 * @param client Client handle
 * @param peer_uri Peer URI
 * @param code SIP status code (486=Busy, 603=Decline, etc.)
 * @return 0 on success, <0 on error
 */
int lws_uas_reject(lws_client_t* client,
    const char* peer_uri,
    int code);

/**
 * @brief Send ringing indication (180 Ringing)
 * @param client Client handle
 * @param peer_uri Peer URI
 * @return 0 on success, <0 on error
 */
int lws_uas_ringing(lws_client_t* client, const char* peer_uri);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_UAS_H__ */
