/**
 * @file lws_agent.h
 * @brief LwSIP Agent Core Interface
 *
 * This is the main entry point for using lwsip.
 * It coordinates SIP signaling and RTP media sessions.
 */

#ifndef __LWS_AGENT_H__
#define __LWS_AGENT_H__

#include "lws_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Opaque Agent Handle
 * (Defined in lws_types.h)
 * ============================================================ */

/* ============================================================
 * Agent Callbacks
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

    /**
     * @brief Incoming SIP MESSAGE received
     * @param param User parameter
     * @param from Sender URI
     * @param to Receiver URI
     * @param content Message content
     * @param content_len Message content length
     */
    void (*on_msg)(void* param,
        const char* from,
        const char* to,
        const char* content,
        int content_len);

    void* param;  // User parameter
} lws_agent_handler_t;

/* ============================================================
 * Session Callbacks
 * ============================================================ */

 typedef struct {
    /**
     * @brief Media negotiation completed
     * Called after SDP negotiation completes
     * @param param User parameter
     * @param audio_codec Audio codec
     * @param audio_rate Audio sample rate
     * @param audio_channels Audio channels
     * @param video_codec Video codec
     * @param video_width Video width
     * @param video_height Video height
     * @param video_fps Video FPS
     * @return 0 on success, <0 on error
     */
    int (*on_media_ready)(void* param,
        lws_audio_codec_t audio_codec,
        int audio_rate,
        int audio_channels,
        lws_video_codec_t video_codec,
        int video_width,
        int video_height,
        int video_fps);

    /**
     * @brief Audio frame received
     * @param param User parameter
     * @param data Audio frame data
     * @param bytes Frame size
     * @param timestamp RTP timestamp
     * @return 0 on success, <0 on error
     */
    int (*on_audio_frame)(void* param,
        const void* data,
        int bytes,
        uint32_t timestamp);

    /**
     * @brief Video frame received
     * @param param User parameter
     * @param data Video frame data
     * @param bytes Frame size
     * @param timestamp RTP timestamp
     * @return 0 on success, <0 on error
     */
    int (*on_video_frame)(void* param,
        const void* data,
        int bytes,
        uint32_t timestamp);

    /**
     * @brief RTCP BYE received
     * @param param User parameter
     */
    void (*on_bye)(void* param);

    /**
     * @brief Error occurred
     * @param param User parameter
     * @param errcode Error code
     */
    void (*on_error)(void* param, int errcode);

    void* param;  // User parameter
} lws_session_handler_t;

/* ============================================================
 * Agent API
 * ============================================================ */

/**
 * @brief Create SIP agent
 * @param config Configuration pointer (agent retains ownership, caller must keep valid)
 * @param handler Event callbacks pointer (agent retains ownership, caller must keep valid)
 * @param session_handler Session callbacks pointer (agent retains ownership, caller must keep valid)
 * @return Agent handle or NULL on error
 *
 * @note IMPORTANT: The agent stores pointers to config and handler, NOT copies.
 *       The caller MUST ensure these structures remain valid for the entire
 *       lifetime of the agent. Do NOT free or modify these structures while
 *       the agent exists. The agent does NOT take ownership and will NOT free
 *       these pointers when destroyed.
 */
lws_agent_t* lws_agent_create(lws_config_t* config, lws_agent_handler_t* handler, lws_session_handler_t* session_handler);

/**
 * @brief Destroy SIP agent
 * @param agent Agent handle
 */
void lws_agent_destroy(lws_agent_t* agent);

/**
 * @brief Start SIP agent
 * @param agent Agent handle
 * @return 0 on success, <0 on error
 */
int lws_agent_start(lws_agent_t* agent);

/**
 * @brief Stop SIP agent
 * @param agent Agent handle
 */
void lws_agent_stop(lws_agent_t* agent);

/**
 * @brief Run agent event loop
 * This is a blocking call that processes SIP and RTP events.
 * Call this in your main loop or a dedicated thread.
 *
 * @param agent Agent handle
 * @param timeout_ms Timeout in milliseconds (0=non-blocking, -1=infinite)
 * @return 0 on timeout, >0 on events processed, <0 on error
 */
int lws_agent_loop(lws_agent_t* agent, int timeout_ms);

/**
 * @brief Get configuration
 * @param agent Agent handle
 * @return Configuration pointer
 */
const lws_config_t* lws_agent_get_config(lws_agent_t* agent);

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
lws_session_t* lws_call(lws_agent_t* agent, const char* peer_uri, int enable_video);

/**
 * @brief Answer a call (simplified API)
 * @param client Client handle
 * @param peer_uri Peer URI (from on_incoming_call callback)
 * @return Session handle or NULL on error
 */
lws_session_t* lws_answer(lws_agent_t* agent, const char* peer_uri);

/**
 * @brief Hang up a call
 * @param client Client handle
 * @param session Session handle (from lws_call or lws_answer)
 */
void lws_hangup(lws_agent_t* agent, lws_session_t* session);

/**
 * @brief Cancel an outgoing call (before it's answered)
 * Send SIP CANCEL for a pending INVITE request.
 * Can only be called after lws_call() but before receiving 200 OK.
 *
 * @param agent Agent handle
 * @param session Session handle (from lws_call)
 * @return 0 on success, <0 on error
 */
int lws_cancel(lws_agent_t* agent, lws_session_t* session);

/* ============================================================
 * Simplified Message API
 * ============================================================ */

/**
 * @brief Send a SIP MESSAGE (instant message)
 * @param agent Agent handle
 * @param peer_uri Peer URI (e.g., "sip:1001@192.168.1.100")
 * @param content Message content
 * @param content_len Content length
 * @return 0 on success, <0 on error
 */
int lws_send_msg(lws_agent_t* agent, const char* peer_uri, const char* content, int content_len);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_AGENT_H__ */
