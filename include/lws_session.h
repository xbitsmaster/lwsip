/**
 * @file lws_session.h
 * @brief LwSIP RTP Session Management
 *
 * This layer manages RTP/RTCP sessions:
 * - RTP packet sending/receiving
 * - RTCP reporting
 * - SDP generation/parsing
 * - Media stream coordination
 */

#ifndef __LWS_SESSION_H__
#define __LWS_SESSION_H__

#include "lws_types.h"
#include "lws_media.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Opaque Session Handle
 * ============================================================ */

typedef struct lws_session lws_session_t;

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
 * Session API
 * ============================================================ */

/**
 * @brief Create RTP session
 * @param config Configuration
 * @param handler Event callbacks
 * @return Session handle or NULL on error
 */
lws_session_t* lws_session_create(
    const lws_config_t* config,
    const lws_session_handler_t* handler);

/**
 * @brief Destroy RTP session
 * @param session Session handle
 */
void lws_session_destroy(lws_session_t* session);

/**
 * @brief Set media source (for sending)
 * @param session Session handle
 * @param media Media handle
 * @return 0 on success, <0 on error
 */
int lws_session_set_media_source(lws_session_t* session, lws_media_t* media);

/**
 * @brief Set media sink (for receiving)
 * @param session Session handle
 * @param media Media handle
 * @return 0 on success, <0 on error
 */
int lws_session_set_media_sink(lws_session_t* session, lws_media_t* media);

/**
 * @brief Generate local SDP offer
 * @param session Session handle
 * @param local_ip Local IP address
 * @param sdp SDP buffer [out]
 * @param len Buffer size
 * @return SDP length on success, <0 on error
 */
int lws_session_generate_sdp_offer(lws_session_t* session,
    const char* local_ip,
    char* sdp,
    int len);

/**
 * @brief Generate SDP answer
 * @param session Session handle
 * @param remote_sdp Remote SDP offer
 * @param remote_len Remote SDP length
 * @param local_ip Local IP address
 * @param sdp SDP buffer [out]
 * @param len Buffer size
 * @return SDP length on success, <0 on error
 */
int lws_session_generate_sdp_answer(lws_session_t* session,
    const char* remote_sdp,
    int remote_len,
    const char* local_ip,
    char* sdp,
    int len);

/**
 * @brief Process remote SDP
 * @param session Session handle
 * @param sdp Remote SDP
 * @param len SDP length
 * @return 0 on success, <0 on error
 */
int lws_session_process_sdp(lws_session_t* session,
    const char* sdp,
    int len);

/**
 * @brief Start RTP session
 * @param session Session handle
 * @return 0 on success, <0 on error
 */
int lws_session_start(lws_session_t* session);

/**
 * @brief Stop RTP session
 * @param session Session handle
 */
void lws_session_stop(lws_session_t* session);

/**
 * @brief Poll for RTP/RTCP packets
 * Call this in your event loop to receive and process RTP/RTCP packets
 * @param session Session handle
 * @param timeout_ms Timeout in milliseconds (0=non-blocking, -1=infinite)
 * @return 0 on timeout, >0 on packets processed, <0 on error
 */
int lws_session_poll(lws_session_t* session, int timeout_ms);

/**
 * @brief Send audio frame
 * @param session Session handle
 * @param data Audio frame data
 * @param bytes Frame size
 * @param timestamp RTP timestamp
 * @return 0 on success, <0 on error
 */
int lws_session_send_audio(lws_session_t* session,
    const void* data,
    int bytes,
    uint32_t timestamp);

/**
 * @brief Send video frame
 * @param session Session handle
 * @param data Video frame data
 * @param bytes Frame size
 * @param timestamp RTP timestamp
 * @return 0 on success, <0 on error
 */
int lws_session_send_video(lws_session_t* session,
    const void* data,
    int bytes,
    uint32_t timestamp);

/**
 * @brief Get local RTP port
 * @param session Session handle
 * @param audio_port Audio RTP port [out]
 * @param video_port Video RTP port [out]
 * @return 0 on success, <0 on error
 */
int lws_session_get_local_port(lws_session_t* session,
    uint16_t* audio_port,
    uint16_t* video_port);

/**
 * @brief Get media count
 * @param session Session handle
 * @return Number of media streams
 */
int lws_session_get_media_count(lws_session_t* session);

/**
 * @brief Get local SDP
 * @param session Session handle
 * @param len SDP length [out]
 * @return SDP string or NULL
 */
const char* lws_session_get_local_sdp(lws_session_t* session, int* len);

/**
 * @brief Set SIP dialog (internal use)
 * @param session Session handle
 * @param dialog SIP dialog handle (struct sip_dialog_t*)
 */
void lws_session_set_dialog(lws_session_t* session, void* dialog);

/**
 * @brief Get SIP dialog (internal use)
 * @param session Session handle
 * @return SIP dialog handle or NULL
 */
void* lws_session_get_dialog(lws_session_t* session);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_SESSION_H__ */
