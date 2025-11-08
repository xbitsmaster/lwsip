/**
 * @file lws_sess.c
 * @brief lwsip media session coordination layer implementation
 *
 * 媒体会话协调层实现，直接使用 librtp 和 libice 库：
 * - ICE流程协调（candidate收集 → 连接性检查 → 选择最优路径）
 * - RTP会话管理（RTP打包/解包、RTCP定时发送）
 * - 设备协调（从Dev层采集数据 → 发送；接收数据 → 送Dev播放）
 * - 会话状态管理（IDLE → GATHERING → CONNECTING → CONNECTED）
 * - SDP自动生成（包含ICE candidates和RTP编解码信息）
 */

/* System headers first */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* lwsip headers */
#include "../include/lws_sess.h"
#include "../include/lws_err.h"
#include "../osal/include/lws_mem.h"
#include "../osal/include/lws_log.h"

/* librtp headers */
#include "rtp.h"
#include "rtp-payload.h"
#include "rtp-profile.h"

/* libice headers */
#include "ice-agent.h"
#include "stun-proto.h"

/* ========================================
 * Constants
 * ======================================== */

#define LWS_SESS_MAX_CANDIDATES     16
#define LWS_SESS_MAX_SDP_SIZE       4096
#define LWS_SESS_RTCP_INTERVAL_MS   5000    /* 5 seconds */
#define LWS_SESS_RTP_MTU            1200    /* RTP packet MTU */

/* ========================================
 * Internal Data Structures
 * ======================================== */

/**
 * @brief Media session internal structure
 */
struct lws_sess_t {
    /* Configuration and handlers */
    lws_sess_config_t config;
    lws_sess_handler_t handler;

    /* State */
    lws_sess_state_t state;

    /* ICE layer (from libice) */
    struct ice_agent_t* ice_agent;
    int ice_gathering_done;
    int ice_connected;

    /* RTP layer (from librtp) */
    void* rtp;                      /* RTP session for RTCP */
    void* audio_encoder;            /* RTP payload encoder */
    void* audio_decoder;            /* RTP payload decoder */
    uint32_t audio_ssrc;            /* Local audio SSRC */
    uint32_t audio_timestamp;       /* Current audio RTP timestamp */
    uint16_t audio_sequence;        /* Current audio RTP sequence */

    /* SDP */
    char local_sdp[LWS_SESS_MAX_SDP_SIZE];
    char local_ice_ufrag[64];
    char local_ice_pwd[64];
    char remote_ice_ufrag[64];
    char remote_ice_pwd[64];

    /* Timing */
    uint64_t last_rtcp_time;        /* Last RTCP send time (microseconds) */
    uint64_t session_start_time;    /* Session start time (microseconds) */

    /* Statistics */
    lws_rtp_stats_t audio_stats;
};

/* ========================================
 * Forward Declarations
 * ======================================== */

static void change_state(lws_sess_t* sess, lws_sess_state_t new_state);
static int generate_ice_credentials(lws_sess_t* sess);
static int generate_local_sdp(lws_sess_t* sess);

/* ========================================
 * Helper Functions
 * ======================================== */

/**
 * @brief Get current time in microseconds
 */
static uint64_t get_current_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

/**
 * @brief Change session state and trigger callback
 */
static void change_state(lws_sess_t* sess, lws_sess_state_t new_state)
{
    if (sess->state == new_state) {
        return;
    }

    lws_sess_state_t old_state = sess->state;
    sess->state = new_state;

    lws_log_info("[SESS] State change: %s -> %s",
                 lws_sess_state_name(old_state),
                 lws_sess_state_name(new_state));

    if (sess->handler.on_state_changed) {
        sess->handler.on_state_changed(sess, old_state, new_state,
                                       sess->handler.userdata);
    }
}

/**
 * @brief Generate random ICE credentials (ufrag and pwd)
 */
static int generate_ice_credentials(lws_sess_t* sess)
{
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "0123456789+/";

    /* Generate ufrag */
    int ufrag_len = LWS_ICE_UFRAG_LEN;
    for (int i = 0; i < ufrag_len; i++) {
        sess->local_ice_ufrag[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    sess->local_ice_ufrag[ufrag_len] = '\0';

    /* Generate pwd */
    int pwd_len = LWS_ICE_PWD_LEN;
    for (int i = 0; i < pwd_len; i++) {
        sess->local_ice_pwd[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    sess->local_ice_pwd[pwd_len] = '\0';

    lws_log_debug("[SESS] Generated ICE credentials: ufrag=%s, pwd=%s",
                  sess->local_ice_ufrag, sess->local_ice_pwd);

    return 0;
}

/**
 * @brief Convert payload type to RTP profile name
 */
static const char* payload_to_name(lws_rtp_payload_t payload)
{
    switch (payload) {
        case LWS_RTP_PAYLOAD_PCMU: return "PCMU";
        case LWS_RTP_PAYLOAD_PCMA: return "PCMA";
        case LWS_RTP_PAYLOAD_G722: return "G722";
        default: return "unknown";
    }
}

/**
 * @brief Get payload clock rate
 */
static int payload_clock_rate(lws_rtp_payload_t payload)
{
    switch (payload) {
        case LWS_RTP_PAYLOAD_PCMU:
        case LWS_RTP_PAYLOAD_PCMA:
            return 8000;
        case LWS_RTP_PAYLOAD_G722:
            return 8000; /* G.722 uses 8000 for RTP, but 16000 for sampling */
        default:
            return 8000;
    }
}

/* ========================================
 * ICE Callbacks
 * ======================================== */

/**
 * @brief ICE send callback - called when ICE needs to send data
 */
static int on_ice_send(void* param, int protocol, const struct sockaddr* local,
                       const struct sockaddr* remote, const void* data, int bytes)
{
    lws_sess_t* sess = (lws_sess_t*)param;
    (void)protocol;
    (void)local;
    (void)remote;
    (void)data;
    (void)bytes;

    /* TODO: Send data through transport layer (lws_trans) */
    /* For now, we just log it */
    lws_log_debug("[SESS] ICE send request: %d bytes", bytes);

    return 0;
}

/**
 * @brief ICE data callback - called when data is received through ICE
 */
static void on_ice_data(void* param, uint8_t stream, uint16_t component,
                        const void* data, int bytes)
{
    lws_sess_t* sess = (lws_sess_t*)param;
    (void)stream;

    if (component == 1) {
        /* RTP data */
        if (sess->audio_decoder) {
            rtp_payload_decode_input(sess->audio_decoder, data, bytes);
        }
    } else if (component == 2) {
        /* RTCP data */
        if (sess->rtp) {
            rtp_onreceived_rtcp(sess->rtp, data, bytes);
        }
    }
}

/**
 * @brief ICE gathering done callback
 */
static void on_ice_gather(void* param, int code)
{
    lws_sess_t* sess = (lws_sess_t*)param;

    lws_log_info("[SESS] ICE gathering done: code=%d", code);

    sess->ice_gathering_done = 1;
    change_state(sess, LWS_SESS_STATE_GATHERED);

    /* Generate SDP */
    if (generate_local_sdp(sess) == 0) {
        /* Trigger SDP ready callback */
        if (sess->handler.on_sdp_ready) {
            sess->handler.on_sdp_ready(sess, sess->local_sdp,
                                      sess->handler.userdata);
        }
    }
}

/**
 * @brief ICE connected callback
 */
static void on_ice_connected(void* param, uint64_t flags, uint64_t mask)
{
    lws_sess_t* sess = (lws_sess_t*)param;
    (void)mask;

    if (flags & 0x01) {
        lws_log_info("[SESS] ICE connected!");
        sess->ice_connected = 1;
        change_state(sess, LWS_SESS_STATE_CONNECTED);

        /* Trigger connected callback */
        if (sess->handler.on_connected) {
            sess->handler.on_connected(sess, sess->handler.userdata);
        }
    }
}

/* ========================================
 * RTP Callbacks
 * ======================================== */

/**
 * @brief RTCP message callback
 */
static void on_rtcp_message(void* param, const struct rtcp_msg_t* msg)
{
    lws_sess_t* sess = (lws_sess_t*)param;
    (void)msg;

    lws_log_debug("[SESS] RTCP message received: type=%d, ssrc=%u",
                  msg->type, msg->ssrc);
}

/**
 * @brief RTP payload allocator
 */
static void* rtp_alloc(void* param, int bytes)
{
    (void)param;
    return lws_malloc(bytes);
}

/**
 * @brief RTP payload free
 */
static void rtp_free(void* param, void *packet)
{
    (void)param;
    lws_free(packet);
}

/**
 * @brief RTP packet callback - called when a decoded packet is ready
 */
static int rtp_packet(void* param, const void *packet, int bytes,
                      uint32_t timestamp, int flags)
{
    lws_sess_t* sess = (lws_sess_t*)param;

    lws_log_debug("[SESS] RTP packet decoded: %d bytes, ts=%u, flags=0x%x",
                  bytes, timestamp, flags);

    /* Write to playback device */
    if (sess->config.audio_playback_dev) {
        int samples = bytes / 2; /* Assuming 16-bit PCM */
        lws_dev_write_audio(sess->config.audio_playback_dev, packet, samples);
    }

    /* Update statistics */
    sess->audio_stats.recv_packets++;
    sess->audio_stats.recv_bytes += bytes;
    sess->audio_stats.recv_timestamp = timestamp;

    return 0;
}

/* ========================================
 * SDP Generation
 * ======================================== */

/**
 * @brief Generate local SDP with ICE candidates
 */
static int generate_local_sdp(lws_sess_t* sess)
{
    char* p = sess->local_sdp;
    int remain = sizeof(sess->local_sdp);
    int n;

    uint64_t session_id = (uint64_t)time(NULL);

    /* SDP header */
    n = snprintf(p, remain,
        "v=0\r\n"
        "o=lwsip %llu %llu IN IP4 0.0.0.0\r\n"
        "s=lwsip media session\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "t=0 0\r\n",
        (unsigned long long)session_id,
        (unsigned long long)session_id);

    if (n < 0 || n >= remain) return -1;
    p += n; remain -= n;

    /* Audio media line */
    if (sess->config.enable_audio) {
        n = snprintf(p, remain,
            "m=audio 9 RTP/AVP %d\r\n"
            "a=rtpmap:%d %s/%d\r\n"
            "a=%s\r\n"
            "a=ice-ufrag:%s\r\n"
            "a=ice-pwd:%s\r\n",
            (int)sess->config.audio_codec,
            (int)sess->config.audio_codec,
            payload_to_name(sess->config.audio_codec),
            payload_clock_rate(sess->config.audio_codec),
            sess->config.media_dir == LWS_MEDIA_DIR_SENDRECV ? "sendrecv" :
            sess->config.media_dir == LWS_MEDIA_DIR_SENDONLY ? "sendonly" :
            sess->config.media_dir == LWS_MEDIA_DIR_RECVONLY ? "recvonly" : "inactive",
            sess->local_ice_ufrag,
            sess->local_ice_pwd);

        if (n < 0 || n >= remain) return -1;
        p += n; remain -= n;

        /* Add ICE candidates */
        /* TODO: Iterate through ice_agent candidates and add them */
        /* For now, add a dummy host candidate */
        n = snprintf(p, remain,
            "a=candidate:1 1 UDP 2130706431 127.0.0.1 9000 typ host\r\n");

        if (n < 0 || n >= remain) return -1;
        p += n; remain -= n;
    }

    lws_log_info("[SESS] Generated local SDP (%d bytes)",
                 (int)(p - sess->local_sdp));

    return 0;
}

/* ========================================
 * Core API Implementation
 * ======================================== */

/**
 * @brief Create media session instance
 */
lws_sess_t* lws_sess_create(const lws_sess_config_t* config,
                             const lws_sess_handler_t* handler)
{
    if (!config || !handler) {
        lws_log_error(0, "[SESS] Invalid parameters\n");
        return NULL;
    }

    lws_log_info("[SESS] Creating media session");

    /* Allocate session structure */
    lws_sess_t* sess = (lws_sess_t*)lws_calloc(1, sizeof(lws_sess_t));
    if (!sess) {
        lws_log_error(0, "[SESS] Failed to allocate session\n");
        return NULL;
    }

    /* Copy configuration and handler */
    sess->config = *config;
    sess->handler = *handler;
    sess->state = LWS_SESS_STATE_IDLE;

    /* Initialize random seed for ICE credentials */
    srand((unsigned int)time(NULL));

    /* Generate ICE credentials */
    generate_ice_credentials(sess);

    /* Create ICE agent */
    struct ice_agent_handler_t ice_handler;
    ice_handler.send = on_ice_send;
    ice_handler.ondata = on_ice_data;
    ice_handler.ongather = on_ice_gather;
    ice_handler.onconnected = on_ice_connected;

    sess->ice_agent = ice_agent_create(1, &ice_handler, sess);
    if (!sess->ice_agent) {
        lws_log_error(0, "[SESS] Failed to create ICE agent\n");
        lws_free(sess);
        return NULL;
    }

    /* Set local ICE credentials */
    ice_agent_set_local_auth(sess->ice_agent, sess->local_ice_ufrag,
                             sess->local_ice_pwd);

    /* Generate random SSRC */
    sess->audio_ssrc = (uint32_t)rand();
    sess->audio_timestamp = 0;
    sess->audio_sequence = (uint16_t)rand();

    /* Create RTP session for RTCP */
    if (sess->config.enable_audio && sess->config.enable_rtcp) {
        struct rtp_event_t rtp_event = {
            .on_rtcp = on_rtcp_message
        };

        sess->rtp = rtp_create(&rtp_event, sess, sess->audio_ssrc,
                               sess->audio_timestamp,
                               sess->config.audio_sample_rate,
                               0, /* bandwidth */
                               1  /* sender */);

        if (!sess->rtp) {
            lws_log_error(0, "[SESS] Failed to create RTP session\n");
            ice_agent_destroy(sess->ice_agent);
            lws_free(sess);
            return NULL;
        }
    }

    /* Create RTP payload encoder */
    if (sess->config.enable_audio && sess->config.audio_capture_dev) {
        struct rtp_payload_t payload_handler = {
            .alloc = rtp_alloc,
            .free = rtp_free,
            .packet = NULL /* Encoder doesn't need packet callback */
        };

        sess->audio_encoder = rtp_payload_encode_create(
            sess->config.audio_codec,
            payload_to_name(sess->config.audio_codec),
            sess->audio_sequence,
            sess->audio_ssrc,
            &payload_handler,
            sess);

        if (!sess->audio_encoder) {
            lws_log_error(0, "[SESS] Failed to create RTP encoder\n");
            if (sess->rtp) rtp_destroy(sess->rtp);
            ice_agent_destroy(sess->ice_agent);
            lws_free(sess);
            return NULL;
        }
    }

    /* Create RTP payload decoder */
    if (sess->config.enable_audio && sess->config.audio_playback_dev) {
        struct rtp_payload_t payload_handler = {
            .alloc = rtp_alloc,
            .free = rtp_free,
            .packet = rtp_packet
        };

        sess->audio_decoder = rtp_payload_decode_create(
            sess->config.audio_codec,
            payload_to_name(sess->config.audio_codec),
            &payload_handler,
            sess);

        if (!sess->audio_decoder) {
            lws_log_error(0, "[SESS] Failed to create RTP decoder\n");
            if (sess->audio_encoder) rtp_payload_encode_destroy(sess->audio_encoder);
            if (sess->rtp) rtp_destroy(sess->rtp);
            ice_agent_destroy(sess->ice_agent);
            lws_free(sess);
            return NULL;
        }
    }

    sess->session_start_time = get_current_time_us();

    lws_log_info("[SESS] Media session created successfully");
    return sess;
}

/**
 * @brief Destroy media session instance
 */
void lws_sess_destroy(lws_sess_t* sess)
{
    if (!sess) {
        return;
    }

    lws_log_info("[SESS] Destroying media session");

    /* Change state */
    change_state(sess, LWS_SESS_STATE_CLOSED);

    /* Destroy RTP payload encoder/decoder */
    if (sess->audio_encoder) {
        rtp_payload_encode_destroy(sess->audio_encoder);
    }

    if (sess->audio_decoder) {
        rtp_payload_decode_destroy(sess->audio_decoder);
    }

    /* Destroy RTP session */
    if (sess->rtp) {
        rtp_destroy(sess->rtp);
    }

    /* Destroy ICE agent */
    if (sess->ice_agent) {
        ice_agent_destroy(sess->ice_agent);
    }

    /* Free session structure */
    lws_free(sess);

    lws_log_info("[SESS] Media session destroyed");
}

/**
 * @brief Start gathering ICE candidates
 */
int lws_sess_gather_candidates(lws_sess_t* sess)
{
    if (!sess) {
        return -1;
    }

    lws_log_info("[SESS] Starting ICE candidate gathering");

    change_state(sess, LWS_SESS_STATE_GATHERING);

    /* Add local host candidates */
    /* TODO: Get local network interfaces and add host candidates */

    /* Start STUN/TURN gathering if configured */
    if (sess->config.stun_server) {
        struct sockaddr_in stun_addr;
        memset(&stun_addr, 0, sizeof(stun_addr));
        stun_addr.sin_family = AF_INET;
        stun_addr.sin_port = htons(sess->config.stun_port ?
                                    sess->config.stun_port : LWS_DEFAULT_STUN_PORT);
        inet_pton(AF_INET, sess->config.stun_server, &stun_addr.sin_addr);

        int turn = (sess->config.turn_server != NULL) ? 1 : 0;

        ice_agent_gather(sess->ice_agent, (struct sockaddr*)&stun_addr,
                        turn, 5000, /* timeout */
                        turn, /* credential type */
                        sess->config.turn_username,
                        sess->config.turn_password);
    } else {
        /* No STUN server, gathering done immediately */
        sess->ice_gathering_done = 1;
        on_ice_gather(sess, 0);
    }

    return 0;
}

/**
 * @brief Set remote SDP
 */
int lws_sess_set_remote_sdp(lws_sess_t* sess, const char* sdp)
{
    if (!sess || !sdp) {
        return -1;
    }

    lws_log_info("[SESS] Setting remote SDP (%zu bytes)", strlen(sdp));

    /* TODO: Parse SDP and extract:
     * - Remote ICE credentials (ufrag, pwd)
     * - Remote ICE candidates
     * - Media info (codec, port, etc.)
     */

    /* For now, just log it */
    lws_log_debug("[SESS] Remote SDP:\n%s", sdp);

    return 0;
}

/**
 * @brief Add remote ICE candidate (trickle ICE)
 */
int lws_sess_add_remote_candidate(lws_sess_t* sess, const char* candidate)
{
    if (!sess || !candidate) {
        return -1;
    }

    lws_log_debug("[SESS] Adding remote candidate: %s", candidate);

    /* TODO: Parse candidate string and add to ICE agent */

    return 0;
}

/**
 * @brief Start ICE connectivity checks
 */
int lws_sess_start_ice(lws_sess_t* sess)
{
    if (!sess) {
        return -1;
    }

    lws_log_info("[SESS] Starting ICE connectivity checks");

    change_state(sess, LWS_SESS_STATE_CONNECTING);

    return ice_agent_start(sess->ice_agent);
}

/**
 * @brief Session event loop (drives media sending)
 */
int lws_sess_loop(lws_sess_t* sess, int timeout_ms)
{
    if (!sess) {
        return -1;
    }

    (void)timeout_ms;

    /* Only send media when connected */
    if (sess->state != LWS_SESS_STATE_CONNECTED) {
        return 0;
    }

    /* Send audio if enabled */
    if (sess->config.enable_audio && sess->config.audio_capture_dev &&
        sess->audio_encoder) {

        /* Read audio from capture device */
        int frame_samples = sess->config.audio_sample_rate * LWS_DEFAULT_FRAME_DURATION / 1000;
        int16_t audio_buf[LWS_MAX_AUDIO_SAMPLES_20MS];

        int samples = lws_dev_read_audio(sess->config.audio_capture_dev,
                                         audio_buf, frame_samples);

        if (samples > 0) {
            /* Encode and send RTP */
            rtp_payload_encode_input(sess->audio_encoder, audio_buf,
                                    samples * 2, /* bytes */
                                    sess->audio_timestamp);

            /* Get encoded RTP packets and send through ICE */
            uint8_t rtp_packet[LWS_SESS_RTP_MTU];
            /* TODO: Get packets from encoder queue */

            /* Update timestamp */
            sess->audio_timestamp += samples;

            /* Update statistics */
            sess->audio_stats.sent_packets++;
            sess->audio_stats.sent_bytes += samples * 2;
            sess->audio_stats.sent_timestamp = sess->audio_timestamp;
        }
    }

    /* Check RTCP interval */
    if (sess->rtp && sess->config.enable_rtcp) {
        uint64_t now = get_current_time_us();
        int rtcp_interval = rtp_rtcp_interval(sess->rtp);

        if (rtcp_interval >= 0 &&
            (now - sess->last_rtcp_time) >= (uint64_t)(rtcp_interval * 1000)) {

            /* Generate and send RTCP report */
            uint8_t rtcp_buf[512];
            int rtcp_len = rtp_rtcp_report(sess->rtp, rtcp_buf, sizeof(rtcp_buf));

            if (rtcp_len > 0) {
                ice_agent_send(sess->ice_agent, 0, 2, rtcp_buf, rtcp_len);
                sess->last_rtcp_time = now;
            }
        }
    }

    return 0;
}

/**
 * @brief Stop media session
 */
void lws_sess_stop(lws_sess_t* sess)
{
    if (!sess) {
        return;
    }

    lws_log_info("[SESS] Stopping media session");

    if (sess->state == LWS_SESS_STATE_CONNECTED) {
        change_state(sess, LWS_SESS_STATE_DISCONNECTED);

        /* Trigger disconnected callback */
        if (sess->handler.on_disconnected) {
            sess->handler.on_disconnected(sess, "User requested",
                                         sess->handler.userdata);
        }
    }

    /* Stop ICE */
    if (sess->ice_agent) {
        ice_agent_stop(sess->ice_agent);
    }
}

/* ========================================
 * State Query APIs
 * ======================================== */

lws_sess_state_t lws_sess_get_state(lws_sess_t* sess)
{
    return sess ? sess->state : LWS_SESS_STATE_IDLE;
}

const char* lws_sess_get_local_sdp(lws_sess_t* sess)
{
    if (!sess || sess->state < LWS_SESS_STATE_GATHERED) {
        return NULL;
    }

    return sess->local_sdp;
}

int lws_sess_get_stats(lws_sess_t* sess, lws_sess_stats_t* stats)
{
    if (!sess || !stats) {
        return -1;
    }

    memset(stats, 0, sizeof(*stats));
    stats->state = sess->state;
    stats->audio_stats = sess->audio_stats;
    stats->start_time = sess->session_start_time;
    stats->duration = get_current_time_us() - sess->session_start_time;

    return 0;
}

lws_rtp_t* lws_sess_get_audio_rtp(lws_sess_t* sess)
{
    /* lws_rtp_t is our wrapper, but we're using librtp directly */
    (void)sess;
    return NULL;
}

lws_rtp_t* lws_sess_get_video_rtp(lws_sess_t* sess)
{
    (void)sess;
    return NULL;
}

lws_ice_t* lws_sess_get_ice(lws_sess_t* sess)
{
    /* lws_ice_t is our wrapper, but we're using libice directly */
    (void)sess;
    return NULL;
}

/* ========================================
 * Helper APIs
 * ======================================== */

void lws_sess_init_audio_config(lws_sess_config_t* config,
                                 const char* stun_server,
                                 lws_rtp_payload_t codec)
{
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));

    config->stun_server = stun_server;
    config->stun_port = LWS_DEFAULT_STUN_PORT;
    config->enable_audio = 1;
    config->audio_codec = codec;
    config->audio_sample_rate = LWS_DEFAULT_SAMPLE_RATE;
    config->audio_channels = LWS_DEFAULT_CHANNELS;
    config->media_dir = LWS_MEDIA_DIR_SENDRECV;
    config->enable_rtcp = 1;
    config->jitter_buffer_ms = LWS_DEFAULT_JITTER_BUFFER_MS;
}

void lws_sess_init_video_config(lws_sess_config_t* config,
                                 const char* stun_server,
                                 lws_rtp_payload_t codec)
{
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));

    config->stun_server = stun_server;
    config->stun_port = LWS_DEFAULT_STUN_PORT;
    config->enable_video = 1;
    config->video_codec = codec;
    config->video_width = LWS_DEFAULT_VIDEO_WIDTH;
    config->video_height = LWS_DEFAULT_VIDEO_HEIGHT;
    config->video_fps = LWS_DEFAULT_VIDEO_FPS;
    config->media_dir = LWS_MEDIA_DIR_SENDRECV;
    config->enable_rtcp = 1;
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

    config->stun_server = stun_server;
    config->stun_port = LWS_DEFAULT_STUN_PORT;
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
    config->enable_rtcp = 1;
    config->jitter_buffer_ms = LWS_DEFAULT_JITTER_BUFFER_MS;
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
