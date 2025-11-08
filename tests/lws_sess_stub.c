/**
 * @file sess_stub.c
 * @brief Media session stub implementation for testing
 *
 * This file provides stub implementations of all lws_sess_* functions
 * for testing purposes. No actual ICE/RTP/media operations are performed.
 * Callbacks are triggered immediately to simulate successful operation.
 */

#include "../include/lws_sess.h"
#include "../osal/include/lws_mem.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ========================================
 * Internal Structures
 * ======================================== */

/**
 * @brief Fake session structure
 */
struct lws_sess_t {
    lws_sess_config_t config;
    lws_sess_handler_t handler;
    lws_sess_state_t state;
    char local_sdp[2048];
};

/* Fake SDP template */
static const char* FAKE_SDP_TEMPLATE =
    "v=0\r\n"
    "o=lwsip-stub %lld %lld IN IP4 127.0.0.1\r\n"
    "s=lwsip stub session\r\n"
    "c=IN IP4 127.0.0.1\r\n"
    "t=0 0\r\n"
    "m=audio 9000 RTP/AVP 0 8\r\n"
    "a=rtpmap:0 PCMU/8000\r\n"
    "a=rtpmap:8 PCMA/8000\r\n"
    "a=sendrecv\r\n"
    "a=ice-ufrag:stub-ufrag\r\n"
    "a=ice-pwd:stub-password-123456\r\n"
    "a=candidate:1 1 UDP 2130706431 127.0.0.1 9000 typ host\r\n";

/* ========================================
 * Helper Functions
 * ======================================== */

/**
 * @brief Generate fake SDP
 */
static void generate_fake_sdp(lws_sess_t* sess)
{
    long long session_id = (long long)time(NULL);
    long long version = session_id;

    snprintf(sess->local_sdp, sizeof(sess->local_sdp),
             FAKE_SDP_TEMPLATE, session_id, version);

    printf("[SESS_STUB] Generated fake SDP (%zu bytes)\n", strlen(sess->local_sdp));
}

/**
 * @brief Change session state and notify
 */
static void change_state(lws_sess_t* sess, lws_sess_state_t new_state)
{
    lws_sess_state_t old_state = sess->state;
    sess->state = new_state;

    printf("[SESS_STUB] State change: %s -> %s\n",
           lws_sess_state_name(old_state),
           lws_sess_state_name(new_state));

    if (sess->handler.on_state_changed) {
        sess->handler.on_state_changed(sess, old_state, new_state, sess->handler.userdata);
    }
}

/* ========================================
 * Core API Implementation
 * ======================================== */

lws_sess_t* lws_sess_create(
    const lws_sess_config_t* config,
    const lws_sess_handler_t* handler)
{
    printf("[SESS_STUB] lws_sess_create\n");

    if (!config || !handler) {
        printf("[SESS_STUB]   ERROR: Invalid parameters\n");
        return NULL;
    }

    // Allocate fake session object
    lws_sess_t* sess = (lws_sess_t*)lws_calloc(1, sizeof(lws_sess_t));
    if (!sess) {
        printf("[SESS_STUB]   ERROR: Failed to allocate session\n");
        return NULL;
    }

    // Copy config and handler
    sess->config = *config;
    sess->handler = *handler;
    sess->state = LWS_SESS_STATE_IDLE;

    printf("[SESS_STUB]   Session created successfully\n");
    return sess;
}

void lws_sess_destroy(lws_sess_t* sess)
{
    printf("[SESS_STUB] lws_sess_destroy\n");

    if (sess) {
        printf("[MEDIA_SESSION] Media session destroyed (resources released)\n");
        change_state(sess, LWS_SESS_STATE_CLOSED);
        lws_free(sess);
        printf("[SESS_STUB]   Session destroyed\n");
    }
}

int lws_sess_gather_candidates(lws_sess_t* sess)
{
    printf("[SESS_STUB] lws_sess_gather_candidates\n");

    if (!sess) {
        printf("[SESS_STUB]   ERROR: Invalid session\n");
        return -1;
    }

    // Simulate immediate candidate gathering
    change_state(sess, LWS_SESS_STATE_GATHERING);

    // Generate fake SDP
    generate_fake_sdp(sess);

    // Immediately transition to GATHERED state
    change_state(sess, LWS_SESS_STATE_GATHERED);

    // Trigger on_sdp_ready callback
    if (sess->handler.on_sdp_ready) {
        printf("[SESS_STUB]   Triggering on_sdp_ready callback\n");
        printf("[MEDIA_SESSION] Media session preparing (local SDP generated)\n");
        sess->handler.on_sdp_ready(sess, sess->local_sdp, sess->handler.userdata);
    }

    printf("[SESS_STUB]   Candidate gathering complete (simulated)\n");
    return 0;
}

int lws_sess_set_remote_sdp(lws_sess_t* sess, const char* sdp)
{
    printf("[SESS_STUB] lws_sess_set_remote_sdp\n");

    if (!sess || !sdp) {
        printf("[SESS_STUB]   ERROR: Invalid parameters\n");
        return -1;
    }

    printf("[SESS_STUB]   Remote SDP received (%zu bytes):\n", strlen(sdp));
    printf("[SESS_STUB]   --- BEGIN SDP ---\n");

    // Print first 5 lines of SDP
    const char* p = sdp;
    int line_count = 0;
    while (*p && line_count < 5) {
        const char* line_end = strchr(p, '\n');
        if (!line_end) line_end = p + strlen(p);

        printf("[SESS_STUB]   %.*s\n", (int)(line_end - p), p);

        if (*line_end == '\0') break;
        p = line_end + 1;
        line_count++;
    }
    printf("[SESS_STUB]   --- END SDP (truncated) ---\n");

    return 0;
}

int lws_sess_add_remote_candidate(lws_sess_t* sess, const char* candidate)
{
    printf("[SESS_STUB] lws_sess_add_remote_candidate\n");

    if (!sess || !candidate) {
        printf("[SESS_STUB]   ERROR: Invalid parameters\n");
        return -1;
    }

    printf("[SESS_STUB]   Remote candidate: %s\n", candidate);
    return 0;
}

int lws_sess_start_ice(lws_sess_t* sess)
{
    printf("[SESS_STUB] lws_sess_start_ice\n");

    if (!sess) {
        printf("[SESS_STUB]   ERROR: Invalid session\n");
        return -1;
    }

    // Simulate immediate ICE connection
    change_state(sess, LWS_SESS_STATE_CONNECTING);

    // Immediately transition to CONNECTED state
    change_state(sess, LWS_SESS_STATE_CONNECTED);

    printf("[MEDIA_SESSION] Media session established (ICE connected, media channel ready)\n");

    // Trigger on_connected callback
    if (sess->handler.on_connected) {
        printf("[SESS_STUB]   Triggering on_connected callback\n");
        sess->handler.on_connected(sess, sess->handler.userdata);
    }

    printf("[SESS_STUB]   ICE connection established (simulated)\n");
    return 0;
}

int lws_sess_loop(lws_sess_t* sess, int timeout_ms)
{
    // Don't print anything to avoid log spam
    (void)sess;
    (void)timeout_ms;
    return 0;
}

void lws_sess_stop(lws_sess_t* sess)
{
    printf("[SESS_STUB] lws_sess_stop\n");

    if (sess) {
        if (sess->state == LWS_SESS_STATE_CONNECTED) {
            printf("[MEDIA_SESSION] Media session terminated (session stopped, media channel closed)\n");
            change_state(sess, LWS_SESS_STATE_DISCONNECTED);

            // Trigger on_disconnected callback
            if (sess->handler.on_disconnected) {
                printf("[SESS_STUB]   Triggering on_disconnected callback\n");
                sess->handler.on_disconnected(sess, "User requested", sess->handler.userdata);
            }
        }
    }
}

/* ========================================
 * State Query APIs
 * ======================================== */

lws_sess_state_t lws_sess_get_state(lws_sess_t* sess)
{
    if (!sess) {
        return LWS_SESS_STATE_IDLE;
    }
    return sess->state;
}

const char* lws_sess_get_local_sdp(lws_sess_t* sess)
{
    if (!sess) {
        return NULL;
    }

    if (sess->state < LWS_SESS_STATE_GATHERED) {
        printf("[SESS_STUB] lws_sess_get_local_sdp: SDP not ready yet (state=%s)\n",
               lws_sess_state_name(sess->state));
        return NULL;
    }

    return sess->local_sdp;
}

int lws_sess_get_stats(lws_sess_t* sess, lws_sess_stats_t* stats)
{
    printf("[SESS_STUB] lws_sess_get_stats (stub implementation)\n");

    if (!sess || !stats) {
        return -1;
    }

    memset(stats, 0, sizeof(*stats));
    stats->state = sess->state;

    return 0;
}

lws_rtp_t* lws_sess_get_audio_rtp(lws_sess_t* sess)
{
    (void)sess;
    return NULL;  // No real RTP instance
}

lws_rtp_t* lws_sess_get_video_rtp(lws_sess_t* sess)
{
    (void)sess;
    return NULL;  // No real RTP instance
}

lws_ice_t* lws_sess_get_ice(lws_sess_t* sess)
{
    (void)sess;
    return NULL;  // No real ICE instance
}

/* ========================================
 * Helper APIs
 * ======================================== */

void lws_sess_init_audio_config(
    lws_sess_config_t* config,
    const char* stun_server,
    lws_rtp_payload_t codec)
{
    printf("[SESS_STUB] lws_sess_init_audio_config: stun=%s, codec=%d\n",
           stun_server ? stun_server : "NULL", codec);

    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));

    config->stun_server = stun_server;
    config->stun_port = 3478;
    config->enable_audio = 1;
    config->audio_codec = codec;
    config->audio_sample_rate = 8000;
    config->audio_channels = 1;
    config->media_dir = LWS_MEDIA_DIR_SENDRECV;
    config->enable_rtcp = 1;
    config->jitter_buffer_ms = 50;
}

void lws_sess_init_video_config(
    lws_sess_config_t* config,
    const char* stun_server,
    lws_rtp_payload_t codec)
{
    printf("[SESS_STUB] lws_sess_init_video_config: stun=%s, codec=%d\n",
           stun_server ? stun_server : "NULL", codec);

    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));

    config->stun_server = stun_server;
    config->stun_port = 3478;
    config->enable_video = 1;
    config->video_codec = codec;
    config->video_width = 640;
    config->video_height = 480;
    config->video_fps = 30;
    config->media_dir = LWS_MEDIA_DIR_SENDRECV;
    config->enable_rtcp = 1;
}

void lws_sess_init_av_config(
    lws_sess_config_t* config,
    const char* stun_server,
    lws_rtp_payload_t audio_codec,
    lws_rtp_payload_t video_codec)
{
    printf("[SESS_STUB] lws_sess_init_av_config: stun=%s, audio_codec=%d, video_codec=%d\n",
           stun_server ? stun_server : "NULL", audio_codec, video_codec);

    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));

    config->stun_server = stun_server;
    config->stun_port = 3478;
    config->enable_audio = 1;
    config->audio_codec = audio_codec;
    config->audio_sample_rate = 8000;
    config->audio_channels = 1;
    config->enable_video = 1;
    config->video_codec = video_codec;
    config->video_width = 640;
    config->video_height = 480;
    config->video_fps = 30;
    config->media_dir = LWS_MEDIA_DIR_SENDRECV;
    config->enable_rtcp = 1;
    config->jitter_buffer_ms = 50;
}

const char* lws_sess_state_name(lws_sess_state_t state)
{
    switch (state) {
        case LWS_SESS_STATE_IDLE:         return "IDLE";
        case LWS_SESS_STATE_GATHERING:    return "GATHERING";
        case LWS_SESS_STATE_GATHERED:     return "GATHERED";
        case LWS_SESS_STATE_CONNECTING:   return "CONNECTING";
        case LWS_SESS_STATE_CONNECTED:    return "CONNECTED";
        case LWS_SESS_STATE_DISCONNECTED: return "DISCONNECTED";
        case LWS_SESS_STATE_CLOSED:       return "CLOSED";
        default:                          return "UNKNOWN";
    }
}
