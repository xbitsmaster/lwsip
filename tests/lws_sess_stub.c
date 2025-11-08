/**
 * @file lws_sess_stub.c
 * @brief Stub implementation of lws_sess API for DEBUG_SIP builds
 *
 * 这个文件提供lws_sess API的桩实现，用于在没有实际媒体会话层的情况下测试SIP层。
 * 仅在定义了DEBUG_SIP时编译此文件。
 */

#ifdef DEBUG_SIP

#include "../include/lws_sess.h"
#include "../include/lws_defs.h"
#include "../osal/include/lws_log.h"
#include "../osal/include/lws_mem.h"
#include <string.h>

/* Stub session structure */
typedef struct {
    lws_sess_config_t config;
    lws_sess_handler_t handler;
    lws_sess_state_t state;
    char local_sdp[2048];
} lws_sess_stub_t;

lws_sess_t* lws_sess_create(const lws_sess_config_t* config,
                            const lws_sess_handler_t* handler)
{
    lws_log_debug("[STUB] lws_sess_create called\n");

    lws_sess_stub_t* sess = (lws_sess_stub_t*)lws_calloc(1, sizeof(lws_sess_stub_t));
    if (!sess) {
        return NULL;
    }

    if (config) {
        sess->config = *config;
    }
    if (handler) {
        sess->handler = *handler;
    }

    sess->state = LWS_SESS_STATE_IDLE;

    return (lws_sess_t*)sess;
}

void lws_sess_destroy(lws_sess_t* sess)
{
    lws_log_debug("[STUB] lws_sess_destroy called\n");

    if (sess) {
        lws_free(sess);
    }
}

int lws_sess_gather_candidates(lws_sess_t* sess)
{
    lws_log_debug("[STUB] lws_sess_gather_candidates called\n");

    if (!sess) {
        return -1;
    }

    lws_sess_stub_t* s = (lws_sess_stub_t*)sess;
    s->state = LWS_SESS_STATE_GATHERING;

    /* Simulate immediate gathering completion */
    s->state = LWS_SESS_STATE_GATHERED;

    /* Generate fake SDP */
    snprintf(s->local_sdp, sizeof(s->local_sdp),
            "v=0\r\n"
            "o=- 0 0 IN IP4 127.0.0.1\r\n"
            "s=lwsip stub\r\n"
            "c=IN IP4 127.0.0.1\r\n"
            "t=0 0\r\n"
            "m=audio 5004 RTP/AVP 0\r\n"
            "a=rtpmap:0 PCMU/8000\r\n");

    /* Trigger callback */
    if (s->handler.on_sdp_ready) {
        s->handler.on_sdp_ready(sess, s->local_sdp, s->handler.userdata);
    }

    return 0;
}

int lws_sess_set_remote_sdp(lws_sess_t* sess, const char* sdp)
{
    lws_log_debug("[STUB] lws_sess_set_remote_sdp called\n");
    return 0;
}

int lws_sess_add_remote_candidate(lws_sess_t* sess, const char* candidate)
{
    lws_log_debug("[STUB] lws_sess_add_remote_candidate called\n");
    return 0;
}

int lws_sess_start_ice(lws_sess_t* sess)
{
    lws_log_debug("[STUB] lws_sess_start_ice called\n");

    if (!sess) {
        return -1;
    }

    lws_sess_stub_t* s = (lws_sess_stub_t*)sess;
    s->state = LWS_SESS_STATE_CONNECTING;

    /* Simulate immediate connection */
    s->state = LWS_SESS_STATE_CONNECTED;

    if (s->handler.on_connected) {
        s->handler.on_connected(sess, s->handler.userdata);
    }

    return 0;
}

int lws_sess_loop(lws_sess_t* sess, int timeout_ms)
{
    /* Stub: do nothing */
    return 0;
}

void lws_sess_stop(lws_sess_t* sess)
{
    lws_log_debug("[STUB] lws_sess_stop called\n");

    if (sess) {
        lws_sess_stub_t* s = (lws_sess_stub_t*)sess;
        s->state = LWS_SESS_STATE_DISCONNECTED;
    }
}

lws_sess_state_t lws_sess_get_state(lws_sess_t* sess)
{
    if (!sess) {
        return LWS_SESS_STATE_IDLE;
    }

    lws_sess_stub_t* s = (lws_sess_stub_t*)sess;
    return s->state;
}

const char* lws_sess_get_local_sdp(lws_sess_t* sess)
{
    if (!sess) {
        return NULL;
    }

    lws_sess_stub_t* s = (lws_sess_stub_t*)sess;
    return s->local_sdp;
}

int lws_sess_get_stats(lws_sess_t* sess, lws_sess_stats_t* stats)
{
    if (!sess || !stats) {
        return -1;
    }

    memset(stats, 0, sizeof(*stats));
    lws_sess_stub_t* s = (lws_sess_stub_t*)sess;
    stats->state = s->state;

    return 0;
}

lws_rtp_t* lws_sess_get_audio_rtp(lws_sess_t* sess)
{
    return NULL;
}

lws_rtp_t* lws_sess_get_video_rtp(lws_sess_t* sess)
{
    return NULL;
}

lws_ice_t* lws_sess_get_ice(lws_sess_t* sess)
{
    return NULL;
}

void lws_sess_init_audio_config(lws_sess_config_t* config,
                                const char* stun_server,
                                lws_rtp_payload_t codec)
{
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->enable_audio = 1;
    config->audio_codec = codec;
    config->audio_sample_rate = LWS_DEFAULT_SAMPLE_RATE;
    config->audio_channels = LWS_DEFAULT_CHANNELS;
    config->media_dir = LWS_MEDIA_DIR_SENDRECV;

    if (stun_server) {
        config->stun_server = stun_server;
        config->stun_port = LWS_DEFAULT_STUN_PORT;
    }
}

void lws_sess_init_video_config(lws_sess_config_t* config,
                                const char* stun_server,
                                lws_rtp_payload_t codec)
{
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->enable_video = 1;
    config->video_codec = codec;
    config->video_width = LWS_DEFAULT_VIDEO_WIDTH;
    config->video_height = LWS_DEFAULT_VIDEO_HEIGHT;
    config->video_fps = LWS_DEFAULT_VIDEO_FPS;
    config->media_dir = LWS_MEDIA_DIR_SENDRECV;

    if (stun_server) {
        config->stun_server = stun_server;
        config->stun_port = LWS_DEFAULT_STUN_PORT;
    }
}

void lws_sess_init_av_config(lws_sess_config_t* config,
                             const char* stun_server,
                             lws_rtp_payload_t audio_codec,
                             lws_rtp_payload_t video_codec)
{
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->enable_audio = 1;
    config->audio_codec = audio_codec;
    config->audio_sample_rate = LWS_DEFAULT_SAMPLE_RATE;
    config->audio_channels = LWS_DEFAULT_CHANNELS;

    config->enable_video = 1;
    config->video_codec = video_codec;
    config->video_width = LWS_DEFAULT_VIDEO_WIDTH;
    config->video_height = LWS_DEFAULT_VIDEO_HEIGHT;
    config->video_fps = LWS_DEFAULT_VIDEO_FPS;

    config->media_dir = LWS_MEDIA_DIR_SENDRECV;

    if (stun_server) {
        config->stun_server = stun_server;
        config->stun_port = LWS_DEFAULT_STUN_PORT;
    }
}

const char* lws_sess_state_name(lws_sess_state_t state)
{
    switch (state) {
        case LWS_SESS_STATE_IDLE: return "IDLE";
        case LWS_SESS_STATE_GATHERING: return "GATHERING";
        case LWS_SESS_STATE_GATHERED: return "GATHERED";
        case LWS_SESS_STATE_CONNECTING: return "CONNECTING";
        case LWS_SESS_STATE_CONNECTED: return "CONNECTED";
        case LWS_SESS_STATE_DISCONNECTED: return "DISCONNECTED";
        case LWS_SESS_STATE_CLOSED: return "CLOSED";
        default: return "UNKNOWN";
    }
}

#endif /* DEBUG_SIP */
