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
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>

/* lwsip headers */
#include "lws_sess.h"
#include "lws_err.h"
#include "lws_mem.h"
#include "lws_log.h"

/* librtp headers */
#include "rtp.h"
#include "rtp-payload.h"
#include "rtp-profile.h"

/* libice headers */
#include "ice-agent.h"
#include "ice-candidate.h"
#include "stun-agent.h"
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

    /* Runtime transport mode (determined from remote SDP) */
    lws_transport_mode_t active_transport_mode;  /* 当前使用的传输模式 */
    int transport_mode_determined;                /* 传输模式是否已确定 */

    /* Network */
    int media_socket;               /* UDP socket for RTP/STUN */
    uint16_t local_port;            /* Local port for media */

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
static int get_local_ipv4(struct sockaddr_in* addr);

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
 * @brief 检测 SDP 中是否包含 ICE 属性
 * @param sdp SDP 字符串
 * @return 1 表示包含 ICE 属性，0 表示不包含
 */
static int sdp_has_ice_attributes(const char* sdp)
{
    if (!sdp) {
        return 0;
    }

    /* 检查是否有 ice-ufrag 属性 */
    if (strstr(sdp, "a=ice-ufrag:") != NULL) {
        return 1;
    }

    /* 检查是否有 ice-pwd 属性 */
    if (strstr(sdp, "a=ice-pwd:") != NULL) {
        return 1;
    }

    /* 检查是否有 candidate 属性 */
    if (strstr(sdp, "a=candidate:") != NULL) {
        return 1;
    }

    return 0;
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
    LWS_UNUSED(protocol);
    LWS_UNUSED(local);

    if (!sess || sess->media_socket < 0 || !remote || !data || bytes <= 0) {
        return -1;
    }

    /* Send via UDP socket */
    ssize_t sent = sendto(sess->media_socket, data, bytes, 0, remote,
                          remote->sa_family == AF_INET ? sizeof(struct sockaddr_in) :
                                                          sizeof(struct sockaddr_in6));

    if (sent < 0) {
        lws_log_error(0, "[SESS] ICE send failed: %s\n", strerror(errno));
        return -1;
    }

    if (sent != bytes) {
        lws_log_warn(0, "[SESS] ICE partial send: %zd/%d bytes\n", sent, bytes);
    }

    lws_log_debug("[SESS] ICE sent %zd bytes to %s:%d", sent,
                  inet_ntoa(((struct sockaddr_in*)remote)->sin_addr),
                  ntohs(((struct sockaddr_in*)remote)->sin_port));

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
    LWS_UNUSED(code);

    lws_log_info("[SESS] ICE gathering done");

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
    LWS_UNUSED(param);
    LWS_UNUSED(msg);

    lws_log_debug("[SESS] RTCP message received");
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
    LWS_UNUSED(flags);

    lws_log_debug("[SESS] RTP packet decoded: %d bytes, ts=%u",
                  bytes, timestamp);

    int samples = bytes / 2; /* Assuming 16-bit PCM */

    /* Write to playback device */
    if (sess->config.audio_playback_dev) {
        lws_dev_write_audio(sess->config.audio_playback_dev, packet, samples);
    }

    /* Write to recording device (if configured) */
    if (sess->config.audio_record_dev) {
        lws_dev_write_audio(sess->config.audio_record_dev, packet, samples);
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

    /* Get local IP address for SDP */
    struct sockaddr_in local_addr;
    char local_ip[INET_ADDRSTRLEN];
    if (get_local_ipv4(&local_addr) == 0) {
        inet_ntop(AF_INET, &local_addr.sin_addr, local_ip, sizeof(local_ip));
    } else {
        /* Fallback to 0.0.0.0 if we can't get local IP */
        strcpy(local_ip, "0.0.0.0");
        lws_log_warn(0, "[SESS] Failed to get local IP for SDP, using 0.0.0.0\n");
    }

    /* SDP header with real local IP */
    n = snprintf(p, remain,
        "v=0\r\n"
        "o=lwsip %llu %llu IN IP4 %s\r\n"
        "s=lwsip media session\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n",
        (unsigned long long)session_id,
        (unsigned long long)session_id,
        local_ip,
        local_ip);

    if (n < 0 || n >= remain) return -1;
    p += n; remain -= n;

    /* Audio media line - use real socket port instead of dummy port 9 */
    if (sess->config.enable_audio) {
        /* Basic media line and RTP attributes */
        n = snprintf(p, remain,
            "m=audio %d RTP/AVP %d\r\n"
            "a=rtpmap:%d %s/%d\r\n"
            "a=%s\r\n",
            sess->local_port,  /* Use real socket port */
            (int)sess->config.audio_codec,
            (int)sess->config.audio_codec,
            payload_to_name(sess->config.audio_codec),
            payload_clock_rate(sess->config.audio_codec),
            sess->config.media_dir == LWS_MEDIA_DIR_SENDRECV ? "sendrecv" :
            sess->config.media_dir == LWS_MEDIA_DIR_SENDONLY ? "sendonly" :
            sess->config.media_dir == LWS_MEDIA_DIR_RECVONLY ? "recvonly" : "inactive");

        if (n < 0 || n >= remain) return -1;
        p += n; remain -= n;

        /*
         * 默认不添加 ICE 属性，适用于服务器中转模式
         * 如果需要 ICE 模式，会在检测到远程 SDP 包含 ICE 后动态启用
         */
    }

    lws_log_info("[SESS] Generated local SDP (%d bytes)",
                 (int)(p - sess->local_sdp));
    lws_log_info("[SESS] Local SDP:\n%s", sess->local_sdp);

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

    /* Create UDP socket for media (RTP/STUN) */
    sess->media_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (sess->media_socket < 0) {
        lws_log_error(0, "[SESS] Failed to create media socket: %s\n", strerror(errno));
        lws_free(sess);
        return NULL;
    }

    /* Set non-blocking */
    int flags = fcntl(sess->media_socket, F_GETFL, 0);
    fcntl(sess->media_socket, F_SETFL, flags | O_NONBLOCK);

    /* Bind to any port (OS will assign) */
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0;  /* Let OS assign port */

    if (bind(sess->media_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        lws_log_error(0, "[SESS] Failed to bind media socket: %s\n", strerror(errno));
        close(sess->media_socket);
        lws_free(sess);
        return NULL;
    }

    /* Get the assigned port */
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sess->media_socket, (struct sockaddr*)&local_addr, &addr_len) == 0) {
        sess->local_port = ntohs(local_addr.sin_port);
        lws_log_info("[SESS] Media socket bound to port %d", sess->local_port);
    } else {
        lws_log_warn(0, "[SESS] Failed to get local port\n");
        sess->local_port = 0;
    }

    /* Initialize random seed for credentials */
    srand((unsigned int)time(NULL));

    /*
     * 默认不创建 ICE agent（服务器中转模式）
     * 如果后续检测到远程 SDP 包含 ICE 属性，可以动态创建 ICE agent
     * TODO: 实现动态ICE切换功能
     */
    sess->ice_agent = NULL;
    sess->transport_mode_determined = 0;
    sess->active_transport_mode = LWS_TRANSPORT_MODE_RTP_DIRECT;
    lws_log_info("[SESS] Media session created (default: RTP direct mode)");

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
    /* Decoder needed if we have playback device OR recording device */
    if (sess->config.enable_audio && (sess->config.audio_playback_dev || sess->config.audio_record_dev)) {
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
        lws_log_info("[SESS] Created RTP payload decoder\n");
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

    /* Close media socket */
    if (sess->media_socket >= 0) {
        close(sess->media_socket);
        sess->media_socket = -1;
    }

    /* Free session structure */
    lws_free(sess);

    lws_log_info("[SESS] Media session destroyed");
}

/**
 * @brief Start gathering ICE candidates
 */
/**
 * @brief Get local IP address from network interfaces
 * Returns the first non-loopback IPv4 address found
 */
static int get_local_ipv4(struct sockaddr_in* addr)
{
#ifdef __APPLE__
    /* For macOS, use getifaddrs */
    struct ifaddrs* ifaddr = NULL;
    struct ifaddrs* ifa = NULL;
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) {
        return -1;
    }

    /* Iterate through network interfaces */
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }

        /* Look for IPv4 non-loopback address */
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;

            /* Skip loopback (127.x.x.x) */
            if ((ntohl(sin->sin_addr.s_addr) & 0xFF000000) == 0x7F000000) {
                continue;
            }

            /* Found a valid address */
            memcpy(addr, sin, sizeof(*addr));
            found = 1;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return found ? 0 : -1;
#else
    /* For Linux/other platforms */
    /* Simplified version: just use any non-loopback interface */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    /* Connect to a remote address to determine local IP */
    struct sockaddr_in remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53); /* DNS port */
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

    if (connect(sock, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
        close(sock);
        return -1;
    }

    socklen_t len = sizeof(*addr);
    if (getsockname(sock, (struct sockaddr*)addr, &len) < 0) {
        close(sock);
        return -1;
    }

    close(sock);
    return 0;
#endif
}

/**
 * @brief Add a local host candidate to ICE agent
 */
static int add_local_host_candidate(lws_sess_t* sess, uint8_t stream,
                                    uint16_t component, const struct sockaddr* addr)
{
    struct ice_candidate_t cand;

    memset(&cand, 0, sizeof(cand));
    cand.stream = stream;
    cand.component = component;
    cand.type = ICE_CANDIDATE_HOST;
    cand.protocol = STUN_PROTOCOL_UDP;

    /* For host candidates, both addr and host are the same */
    memcpy(&cand.host, addr, sizeof(struct sockaddr_in));
    memcpy(&cand.addr, addr, sizeof(struct sockaddr_in));

    /* Calculate priority */
    ice_candidate_priority(&cand);

    /* Set foundation */
    snprintf(cand.foundation, sizeof(cand.foundation), "%d", component);

    int ret = ice_agent_add_local_candidate(sess->ice_agent, &cand);
    if (ret == 0) {
        char addr_str[INET_ADDRSTRLEN];
        struct sockaddr_in* sin = (struct sockaddr_in*)addr;
        inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
        lws_log_info("[SESS] Added local host candidate: %s:%d (stream=%d, component=%d, priority=%u)\n",
                    addr_str, ntohs(sin->sin_port), stream, component, cand.priority);
    }

    return ret;
}

int lws_sess_gather_candidates(lws_sess_t* sess)
{
    if (!sess) {
        return -1;
    }

    /*
     * 默认直接生成 SDP，不进行 ICE 候选收集
     * 这适用于服务器中转模式（最常见的场景）
     * 如果后续检测到远程 SDP 包含 ICE，会动态切换到 ICE 模式
     */
    lws_log_info("[SESS] Generating local SDP (without ICE gathering)");

    change_state(sess, LWS_SESS_STATE_GATHERING);

    /* Generate SDP immediately */
    if (generate_local_sdp(sess) == 0) {
        change_state(sess, LWS_SESS_STATE_GATHERED);

        /* Notify SDP ready */
        if (sess->handler.on_sdp_ready) {
            sess->handler.on_sdp_ready(sess, sess->local_sdp, sess->handler.userdata);
        }
    }

    return 0;
}

/**
 * @brief Parse SDP attribute value
 * @param sdp SDP string
 * @param attr Attribute name (e.g., "ice-ufrag")
 * @param value Output buffer for value
 * @param value_size Size of output buffer
 * @return 0 on success, -1 on failure
 */
static int parse_sdp_attribute(const char* sdp, const char* attr, char* value, size_t value_size)
{
    char search_str[128];
    const char* line;
    const char* end;
    size_t attr_len;

    snprintf(search_str, sizeof(search_str), "a=%s:", attr);
    line = strstr(sdp, search_str);

    if (!line) {
        return -1;
    }

    /* Move past "a=attr:" */
    line += strlen(search_str);

    /* Find end of line */
    end = strchr(line, '\r');
    if (!end) {
        end = strchr(line, '\n');
    }
    if (!end) {
        end = line + strlen(line);
    }

    /* Copy value */
    attr_len = end - line;
    if (attr_len >= value_size) {
        attr_len = value_size - 1;
    }

    memcpy(value, line, attr_len);
    value[attr_len] = '\0';

    return 0;
}

/**
 * @brief Set remote SDP
 */
int lws_sess_set_remote_sdp(lws_sess_t* sess, const char* sdp)
{
    char ufrag[LWS_MAX_UFRAG_LEN];
    char pwd[LWS_MAX_PWD_LEN];
    int ret;

    if (!sess || !sdp) {
        return -1;
    }

    lws_log_info("[SESS] Setting remote SDP (%zu bytes)", strlen(sdp));
    lws_log_debug("[SESS] Remote SDP:\n%s", sdp);

    /* 根据远程 SDP 内容确定传输模式 */
    if (!sess->transport_mode_determined) {
        if (sdp_has_ice_attributes(sdp)) {
            sess->active_transport_mode = LWS_TRANSPORT_MODE_ICE;
            sess->transport_mode_determined = 1;
            lws_log_info("[SESS] Detected ICE attributes in remote SDP - using ICE mode");
        } else {
            sess->active_transport_mode = LWS_TRANSPORT_MODE_RTP_DIRECT;
            sess->transport_mode_determined = 1;
            lws_log_info("[SESS] No ICE attributes in remote SDP - using RTP direct mode");
        }
    }

    /* 如果是 RTP 直连模式，跳过 ICE 处理 */
    if (sess->active_transport_mode == LWS_TRANSPORT_MODE_RTP_DIRECT) {
        lws_log_info("[SESS] RTP direct mode - skipping ICE processing");
        /* TODO: 解析 c= 行获取远程 IP 和 m= 行获取远程端口 */
        return 0;
    }

    /* ICE 模式：解析 ICE 凭证和候选者 */
    /* Parse ICE credentials */
    if (parse_sdp_attribute(sdp, "ice-ufrag", ufrag, sizeof(ufrag)) == 0 &&
        parse_sdp_attribute(sdp, "ice-pwd", pwd, sizeof(pwd)) == 0) {

        lws_log_info("[SESS] Extracted remote ICE credentials: ufrag=%s, pwd=%s", ufrag, pwd);

        /* Set remote auth for stream 0 */
        ret = ice_agent_set_remote_auth(sess->ice_agent, 0, ufrag, pwd);
        if (ret != 0) {
            lws_log_error(0, "[SESS] Failed to set remote ICE auth (ret=%d)\n", ret);
            return -1;
        }

        lws_log_info("[SESS] Remote ICE auth set successfully");
    } else {
        lws_log_warn(0, "[SESS] Failed to parse ICE credentials from SDP\n");
    }

    /* Parse remote ICE candidates */
    const char* line = sdp;
    const char* cand_prefix = "a=candidate:";
    int cand_count = 0;

    while ((line = strstr(line, cand_prefix)) != NULL) {
        const char* cand_line = line;
        const char* end;

        /* Find end of candidate line */
        end = strchr(cand_line, '\r');
        if (!end) {
            end = strchr(cand_line, '\n');
        }
        if (!end) {
            end = cand_line + strlen(cand_line);
        }

        /* Parse candidate fields */
        struct ice_candidate_t cand;
        char foundation[33];
        char transport[10];
        char ip[64];
        char cand_type[16];
        uint16_t component;
        uint32_t priority;
        uint16_t port;

        memset(&cand, 0, sizeof(cand));

        /* Parse: a=candidate:foundation component transport priority ip port typ type */
        int fields = sscanf(cand_line, "a=candidate:%32s %hu %9s %u %63s %hu typ %15s",
                           foundation, &component, transport, &priority, ip, &port, cand_type);

        if (fields == 7) {
            /* Set candidate fields */
            snprintf(cand.foundation, sizeof(cand.foundation), "%s", foundation);
            cand.stream = 0;
            cand.component = component;
            cand.protocol = STUN_PROTOCOL_UDP;  /* Assume UDP for now */
            cand.priority = priority;

            /* Parse candidate type */
            if (strcmp(cand_type, "host") == 0) {
                cand.type = ICE_CANDIDATE_HOST;
            } else if (strcmp(cand_type, "srflx") == 0) {
                cand.type = ICE_CANDIDATE_SERVER_REFLEXIVE;
            } else if (strcmp(cand_type, "relay") == 0) {
                cand.type = ICE_CANDIDATE_RELAYED;
            } else {
                lws_log_warn(0, "[SESS] Unknown candidate type: %s\n", cand_type);
                line = end;
                continue;
            }

            /* Parse IP address and port */
            struct sockaddr_in* sin = (struct sockaddr_in*)&cand.addr;
            sin->sin_family = AF_INET;
            sin->sin_port = htons(port);
            if (inet_pton(AF_INET, ip, &sin->sin_addr) != 1) {
                lws_log_warn(0, "[SESS] Failed to parse candidate IP: %s\n", ip);
                line = end;
                continue;
            }

            /* For host candidates, addr and host are the same */
            memcpy(&cand.host, &cand.addr, sizeof(cand.host));

            /* Add candidate to ICE agent */
            ret = ice_agent_add_remote_candidate(sess->ice_agent, &cand);
            if (ret == 0) {
                cand_count++;
                lws_log_info("[SESS] Added remote %s candidate: %s:%d (component=%d, priority=%u)",
                            cand_type, ip, port, component, priority);
            } else {
                lws_log_warn(0, "[SESS] Failed to add remote candidate (ret=%d)\n", ret);
            }
        }

        line = end;
    }

    if (cand_count > 0) {
        lws_log_info("[SESS] Parsed %d remote ICE candidates", cand_count);
    } else {
        lws_log_warn(0, "[SESS] No remote ICE candidates found in SDP\n");
    }

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

    /* In RTP direct mode, skip ICE and go directly to connected state */
    if (!sess->transport_mode_determined ||
        sess->active_transport_mode == LWS_TRANSPORT_MODE_RTP_DIRECT) {
        lws_log_info("[SESS] RTP direct mode - skipping ICE, going to CONNECTED");
        change_state(sess, LWS_SESS_STATE_CONNECTED);

        /* Notify connected callback */
        if (sess->handler.on_connected) {
            sess->handler.on_connected(sess, sess->handler.userdata);
        }

        return 0;
    }

    /* ICE mode - start ICE connectivity checks */
    lws_log_info("[SESS] Starting ICE connectivity checks");

    change_state(sess, LWS_SESS_STATE_CONNECTING);

    return ice_agent_start(sess->ice_agent);
}

/**
 * @brief Session event loop (drives media sending and receiving)
 */
int lws_sess_loop(lws_sess_t* sess, int timeout_ms)
{
    if (!sess) {
        return -1;
    }

    (void)timeout_ms;

    /* Process incoming packets (STUN/RTP) from socket */
    if (sess->media_socket >= 0) {
        uint8_t buffer[2048];
        struct sockaddr_in local_addr;
        struct sockaddr_in remote_addr;
        socklen_t addr_len;

        /* Get local address for this socket */
        addr_len = sizeof(local_addr);
        getsockname(sess->media_socket, (struct sockaddr*)&local_addr, &addr_len);

        /* Read all available packets (non-blocking) */
        while (1) {
            addr_len = sizeof(remote_addr);
            ssize_t bytes = recvfrom(sess->media_socket, buffer, sizeof(buffer), 0,
                                    (struct sockaddr*)&remote_addr, &addr_len);

            if (bytes < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    /* No more data available */
                    break;
                } else {
                    lws_log_error(0, "[SESS] recvfrom error: %s\n", strerror(errno));
                    break;
                }
            } else if (bytes == 0) {
                break;
            }

            /* Process received data */
            if (sess->ice_agent) {
                /* ICE mode: Feed data to ICE agent for processing */
                ice_agent_input(sess->ice_agent, 0,
                               (struct sockaddr*)&local_addr,
                               (struct sockaddr*)&remote_addr,
                               buffer, (int)bytes);
            } else if (sess->active_transport_mode == LWS_TRANSPORT_MODE_RTP_DIRECT) {
                /* RTP direct mode: Process RTP data directly */
                if (sess->audio_decoder && bytes > 12) {
                    /* Simple RTP packet validation (minimum RTP header is 12 bytes) */
                    static int rtp_count = 0;
                    if ((rtp_count++ % 50) == 0) {
                        lws_log_info("[SESS] RTP direct mode: received packet %d bytes (log every 50)\n", (int)bytes);
                    }
                    rtp_payload_decode_input(sess->audio_decoder, buffer, (int)bytes);
                }
            }
        }
    }

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
            LWS_UNUSED(rtp_packet);
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
