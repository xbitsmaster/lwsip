/**
 * @file lws_uac.h
 * @brief LwSIP User Agent Client (UAC)
 *
 * This layer handles outgoing SIP requests:
 * - REGISTER
 * - INVITE
 * - BYE
 * - CANCEL
 */

#ifndef __LWS_UAC_H__
#define __LWS_UAC_H__

#include "lws_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct lws_client lws_client_t;
typedef struct lws_session lws_session_t;

/* ============================================================
 * UAC API
 * ============================================================ */

/**
 * @brief Send REGISTER request
 * @param client Client handle
 * @return 0 on success, <0 on error
 */
int lws_uac_register(lws_client_t* client);

/**
 * @brief Send UNREGISTER request
 * @param client Client handle
 * @return 0 on success, <0 on error
 */
int lws_uac_unregister(lws_client_t* client);

/**
 * @brief Get registration state
 * @param client Client handle
 * @return Registration state
 */
lws_reg_state_t lws_uac_get_reg_state(lws_client_t* client);

/**
 * @brief Make call (send INVITE)
 * @param client Client handle
 * @param peer_uri Peer URI (e.g., "sip:1001@192.168.1.100")
 * @param session RTP session handle
 * @return 0 on success, <0 on error
 */
int lws_uac_invite(lws_client_t* client,
    const char* peer_uri,
    lws_session_t* session);

/**
 * @brief Cancel outgoing call
 * @param client Client handle
 * @param session Session handle
 * @return 0 on success, <0 on error
 */
int lws_uac_cancel(lws_client_t* client, lws_session_t* session);

/**
 * @brief Hang up call (send BYE)
 * @param client Client handle
 * @param session Session handle
 * @return 0 on success, <0 on error
 */
int lws_uac_bye(lws_client_t* client, lws_session_t* session);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_UAC_H__ */
