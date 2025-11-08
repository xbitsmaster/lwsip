/**
 * @file lws_session.c
 * @brief RTP session management implementation
 */

#include "lws_agent.h"
#include "lws_session.h"
#include "lws_payload.h"
#include "lws_ice.h"
#include "lws_error.h"
#include "lws_mem.h"
#include "lws_log.h"
#include "lws_mutex.h"
#include "lws_thread.h"
#include "rtp.h"
#include "sdp.h"
#include "sip-dialog.h"
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

/* ============================================================
 * Session Structure
 * ============================================================ */

#define MAX_SDP_SIZE 4096

struct lws_session {
    lws_config_t config;
    lws_session_handler_t handler;

    // Media source/sink
    lws_media_t* media_source;
    lws_media_t* media_sink;

    // RTP payload encoders/decoders
    lws_payload_encoder_t* audio_encoder;
    lws_payload_decoder_t* audio_decoder;
    lws_payload_encoder_t* video_encoder;
    lws_payload_decoder_t* video_decoder;

    // RTP session handles
    void* audio_rtp;
    void* video_rtp;

    // SDP
    char local_sdp[MAX_SDP_SIZE];
    int local_sdp_len;
    char remote_sdp[MAX_SDP_SIZE];
    int remote_sdp_len;

    // Socket handles
    int audio_rtp_sock;
    int audio_rtcp_sock;
    int video_rtp_sock;
    int video_rtcp_sock;

    // Port numbers
    uint16_t audio_local_port;
    uint16_t video_local_port;
    uint16_t audio_remote_port;
    uint16_t video_remote_port;

    // ICE support
    lws_ice_agent_t* ice_agent;       // ICE agent for NAT traversal
    int ice_enabled;                  // ICE enabled flag
    int ice_gathering;                // ICE gathering in progress
    int ice_connected;                // ICE connected flag
    char ice_ufrag[64];               // Local ICE username fragment
    char ice_pwd[64];                 // Local ICE password
    char remote_ice_ufrag[64];        // Remote ICE username fragment
    char remote_ice_pwd[64];          // Remote ICE password

    // SIP dialog (for sending BYE)
    void* dialog;                     // struct sip_dialog_t* from libsip

    // SIP transaction (for CANCEL)
    void* invite_transaction;         // struct sip_uac_transaction_t* from libsip

    // State
    int is_started;
    void* mutex;          // lws_mutex_t* from osal
    void* worker_thread;  // lws_thread_t* from osal
};

/* ============================================================
 * Forward Declarations
 * ============================================================ */

static int audio_packet_cb(void* param, const void* packet, int bytes,
                           uint32_t timestamp, int flags);
static int video_packet_cb(void* param, const void* packet, int bytes,
                           uint32_t timestamp, int flags);
static int audio_frame_cb(void* param, const void* frame, int bytes,
                         uint32_t timestamp, int flags);
static int video_frame_cb(void* param, const void* frame, int bytes,
                         uint32_t timestamp, int flags);

/* ============================================================
 * Internal Functions
 * ============================================================ */

static int audio_packet_cb(void* param,
    const void* packet,
    int bytes,
    uint32_t timestamp,
    int flags)
{
    lws_session_t* session = (lws_session_t*)param;
    int ret;
    struct sockaddr_in remote_addr;

    (void)timestamp;
    (void)flags;

    // If ICE is enabled and connected, send via ICE channel
    if (session->ice_enabled && session->ice_agent) {
        if (lws_ice_is_connected(session->ice_agent, 0, 1)) {
            // Stream 0 (audio), Component 1 (RTP)
            ret = lws_ice_send(session->ice_agent, 0, 1, packet, bytes);
            if (ret < 0) {
                lws_log_warn(LWS_ERR_RTP_PAYLOAD, "failed to send audio RTP via ICE: %d\n", ret);
                return -1;
            }
            return 0;
        }
    }

    // Fallback to direct socket sending when ICE is not connected
    if (session->audio_rtp_sock > 0 && session->audio_remote_port > 0) {
        memset(&remote_addr, 0, sizeof(remote_addr));
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons(session->audio_remote_port);
        // TODO: Parse remote IP from SDP instead of using localhost
        remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        ret = sendto(session->audio_rtp_sock, packet, bytes, 0,
                     (struct sockaddr*)&remote_addr, sizeof(remote_addr));
        if (ret < 0) {
            lws_log_warn(LWS_ERR_SOCKET_SEND, "failed to send audio RTP via socket: %d\n", ret);
            return -1;
        }
        return 0;
    }

    lws_log_debug("audio RTP packet ready (%d bytes), but no transport available\n", bytes);
    return 0;
}

static int video_packet_cb(void* param,
    const void* packet,
    int bytes,
    uint32_t timestamp,
    int flags)
{
    lws_session_t* session = (lws_session_t*)param;
    int ret;
    struct sockaddr_in remote_addr;

    (void)timestamp;
    (void)flags;

    // If ICE is enabled and connected, send via ICE channel
    if (session->ice_enabled && session->ice_agent) {
        if (lws_ice_is_connected(session->ice_agent, 1, 1)) {
            // Stream 1 (video), Component 1 (RTP)
            ret = lws_ice_send(session->ice_agent, 1, 1, packet, bytes);
            if (ret < 0) {
                lws_log_warn(LWS_ERR_RTP_PAYLOAD, "failed to send video RTP via ICE: %d\n", ret);
                return -1;
            }
            return 0;
        }
    }

    // Fallback to direct socket sending when ICE is not connected
    if (session->video_rtp_sock > 0 && session->video_remote_port > 0) {
        memset(&remote_addr, 0, sizeof(remote_addr));
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons(session->video_remote_port);
        // TODO: Parse remote IP from SDP instead of using localhost
        remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        ret = sendto(session->video_rtp_sock, packet, bytes, 0,
                     (struct sockaddr*)&remote_addr, sizeof(remote_addr));
        if (ret < 0) {
            lws_log_warn(LWS_ERR_SOCKET_SEND, "failed to send video RTP via socket: %d\n", ret);
            return -1;
        }
        return 0;
    }

    lws_log_debug("video RTP packet ready (%d bytes), but no transport available\n", bytes);
    return 0;
}

static int audio_frame_cb(void* param,
    const void* frame,
    int bytes,
    uint32_t timestamp,
    int flags)
{
    lws_session_t* session = (lws_session_t*)param;

    if (session->handler.on_audio_frame) {
        return session->handler.on_audio_frame(session->handler.param,
                                               frame, bytes, timestamp);
    }

    return 0;
}

static int video_frame_cb(void* param,
    const void* frame,
    int bytes,
    uint32_t timestamp,
    int flags)
{
    lws_session_t* session = (lws_session_t*)param;

    if (session->handler.on_video_frame) {
        return session->handler.on_video_frame(session->handler.param,
                                               frame, bytes, timestamp);
    }

    return 0;
}

/* ============================================================
 * Public API
 * ============================================================ */

lws_session_t* lws_session_create(lws_config_t* config, lws_session_handler_t* handler, int enable_video)
{
    lws_session_t* session;

    if (!config || !handler) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    session = (lws_session_t*)lws_malloc(sizeof(lws_session_t));
    if (!session) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate session\n");
        return NULL;
    }

    memset(session, 0, sizeof(lws_session_t));
    memcpy(&session->config, config, sizeof(lws_config_t));
    memcpy(&session->handler, handler, sizeof(lws_session_handler_t));

    session->mutex = lws_mutex_create();
    if (!session->mutex) {
        lws_log_error(LWS_ERR_NOMEM, "failed to create mutex\n");
        lws_free(session);
        return NULL;
    }

    // Create ICE agent if enabled
    if (config->enable_ice) {
        lws_ice_handler_t ice_handler;
        memset(&ice_handler, 0, sizeof(ice_handler));
        ice_handler.on_gather_done = NULL;  // TODO: implement callbacks
        ice_handler.on_ice_connected = NULL;
        ice_handler.on_ice_failed = NULL;
        ice_handler.on_data = NULL;
        ice_handler.param = session;

        session->ice_agent = lws_ice_agent_create(config, &ice_handler);
        if (session->ice_agent) {
            session->ice_enabled = 1;

            // Get local ICE credentials for SDP
            lws_ice_get_local_auth(session->ice_agent,
                                   session->ice_ufrag,
                                   session->ice_pwd);

            lws_log_info("ICE agent created: ufrag=%s\n", session->ice_ufrag);
        } else {
            lws_log_warn(LWS_ERR_INTERNAL, "failed to create ICE agent, ICE disabled\n");
            session->ice_enabled = 0;
        }
    }

    lws_log_info("session created\n");
    return session;
}

void lws_session_destroy(lws_session_t* session)
{
    if (!session) {
        return;
    }

    if (session->is_started) {
        lws_session_stop(session);
    }

    if (session->audio_encoder) {
        lws_payload_encoder_destroy(session->audio_encoder);
    }
    if (session->audio_decoder) {
        lws_payload_decoder_destroy(session->audio_decoder);
    }
    if (session->video_encoder) {
        lws_payload_encoder_destroy(session->video_encoder);
    }
    if (session->video_decoder) {
        lws_payload_decoder_destroy(session->video_decoder);
    }

    // Destroy ICE agent if enabled
    if (session->ice_agent) {
        lws_ice_agent_destroy(session->ice_agent);
        session->ice_agent = NULL;
    }

    // Release SIP dialog if exists
    if (session->dialog) {
        sip_dialog_release((struct sip_dialog_t*)session->dialog);
        session->dialog = NULL;
    }

    if (session->mutex) {
        lws_mutex_destroy(session->mutex);
    }

    lws_free(session);
    lws_log_info("session destroyed\n");
}

int lws_session_set_media_source(lws_session_t* session, lws_media_t* media)
{
    if (!session) {
        return LWS_ERR_INVALID_PARAM;
    }

    session->media_source = media;
    return LWS_OK;
}

int lws_session_set_media_sink(lws_session_t* session, lws_media_t* media)
{
    if (!session) {
        return LWS_ERR_INVALID_PARAM;
    }

    session->media_sink = media;
    return LWS_OK;
}

int lws_session_generate_sdp_offer(lws_session_t* session,
    const char* local_ip,
    char* sdp,
    int len)
{
    int n = 0;
    int offset = 0;
    uint32_t sess_id;
    uint32_t sess_version;

    if (!session || !local_ip || !sdp || len <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    // Generate session ID and version (use current timestamp)
    sess_id = (uint32_t)time(NULL);
    sess_version = sess_id;

    // Allocate RTP ports if not done yet
    if (session->audio_local_port == 0 && session->config.enable_audio) {
        session->audio_local_port = session->config.audio_rtp_port;
        if (session->audio_local_port == 0) {
            session->audio_local_port = 10000;  // Default port
        }
    }

    if (session->video_local_port == 0 && session->config.enable_video) {
        session->video_local_port = session->config.video_rtp_port;
        if (session->video_local_port == 0) {
            session->video_local_port = 10002;  // Default port
        }
    }

    // v=0
    n = snprintf(sdp + offset, len - offset, "v=0\r\n");
    if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
    offset += n;

    // o=<username> <sess-id> <sess-version> IN IP4 <address>
    n = snprintf(sdp + offset, len - offset,
                 "o=%s %u %u IN IP4 %s\r\n",
                 session->config.username, sess_id, sess_version, local_ip);
    if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
    offset += n;

    // s=<session name>
    n = snprintf(sdp + offset, len - offset, "s=lwsip call\r\n");
    if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
    offset += n;

    // c=IN IP4 <address>
    n = snprintf(sdp + offset, len - offset, "c=IN IP4 %s\r\n", local_ip);
    if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
    offset += n;

    // t=0 0 (session is not bounded)
    n = snprintf(sdp + offset, len - offset, "t=0 0\r\n");
    if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
    offset += n;

    // Audio media
    if (session->config.enable_audio) {
        // m=audio <port> RTP/AVP <format>
        n = snprintf(sdp + offset, len - offset,
                     "m=audio %u RTP/AVP %d",
                     session->audio_local_port,
                     session->config.audio_codec);
        if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
        offset += n;

        // Add alternative codecs
        if (session->config.audio_codec == LWS_AUDIO_CODEC_PCMU) {
            n = snprintf(sdp + offset, len - offset, " 8");  // PCMA
        } else if (session->config.audio_codec == LWS_AUDIO_CODEC_PCMA) {
            n = snprintf(sdp + offset, len - offset, " 0");  // PCMU
        }
        if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
        offset += n;

        n = snprintf(sdp + offset, len - offset, "\r\n");
        if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
        offset += n;

        // a=rtpmap:<payload> <encoding>/<clock rate>
        const char* encoding = NULL;
        int clock_rate = 8000;

        switch (session->config.audio_codec) {
            case LWS_AUDIO_CODEC_PCMU:
                encoding = "PCMU";
                break;
            case LWS_AUDIO_CODEC_PCMA:
                encoding = "PCMA";
                break;
            case LWS_AUDIO_CODEC_G722:
                encoding = "G722";
                break;
            case LWS_AUDIO_CODEC_OPUS:
                encoding = "opus";
                clock_rate = 48000;
                break;
            default:
                encoding = "PCMU";
                break;
        }

        n = snprintf(sdp + offset, len - offset,
                     "a=rtpmap:%d %s/%d\r\n",
                     session->config.audio_codec, encoding, clock_rate);
        if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
        offset += n;

        // a=sendrecv
        n = snprintf(sdp + offset, len - offset, "a=sendrecv\r\n");
        if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
        offset += n;

        // ICE attributes for audio
        if (session->ice_enabled && session->ice_agent) {
            // a=ice-ufrag
            n = snprintf(sdp + offset, len - offset,
                         "a=ice-ufrag:%s\r\n", session->ice_ufrag);
            if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
            offset += n;

            // a=ice-pwd
            n = snprintf(sdp + offset, len - offset,
                         "a=ice-pwd:%s\r\n", session->ice_pwd);
            if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
            offset += n;

            // a=candidate lines
            int cand_len = lws_ice_generate_sdp_candidates(session->ice_agent, 0,
                                                            sdp + offset, len - offset);
            if (cand_len > 0) {
                offset += cand_len;
            }
        }
    }

    // Video media
    if (session->config.enable_video) {
        // m=video <port> RTP/AVP <format>
        n = snprintf(sdp + offset, len - offset,
                     "m=video %u RTP/AVP %d\r\n",
                     session->video_local_port,
                     session->config.video_codec);
        if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
        offset += n;

        // a=rtpmap:<payload> <encoding>/<clock rate>
        const char* encoding_v = NULL;

        switch (session->config.video_codec) {
            case LWS_VIDEO_CODEC_H264:
                encoding_v = "H264";
                break;
            case LWS_VIDEO_CODEC_H265:
                encoding_v = "H265";
                break;
            case LWS_VIDEO_CODEC_VP8:
                encoding_v = "VP8";
                break;
            case LWS_VIDEO_CODEC_VP9:
                encoding_v = "VP9";
                break;
            default:
                encoding_v = "H264";
                break;
        }

        n = snprintf(sdp + offset, len - offset,
                     "a=rtpmap:%d %s/90000\r\n",
                     session->config.video_codec, encoding_v);
        if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
        offset += n;

        // a=sendrecv
        n = snprintf(sdp + offset, len - offset, "a=sendrecv\r\n");
        if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
        offset += n;

        // ICE attributes for video
        if (session->ice_enabled && session->ice_agent) {
            // a=ice-ufrag
            n = snprintf(sdp + offset, len - offset,
                         "a=ice-ufrag:%s\r\n", session->ice_ufrag);
            if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
            offset += n;

            // a=ice-pwd
            n = snprintf(sdp + offset, len - offset,
                         "a=ice-pwd:%s\r\n", session->ice_pwd);
            if (n < 0 || n >= len - offset) return LWS_ERR_NOMEM;
            offset += n;

            // a=candidate lines
            int cand_len = lws_ice_generate_sdp_candidates(session->ice_agent, 1,
                                                            sdp + offset, len - offset);
            if (cand_len > 0) {
                offset += cand_len;
            }
        }
    }

    // Save local SDP
    memcpy(session->local_sdp, sdp, offset);
    session->local_sdp_len = offset;

    lws_log_info("generated SDP offer (%d bytes):\n%.*s\n", offset, offset, sdp);

    return offset;
}

int lws_session_generate_sdp_answer(lws_session_t* session,
    const char* remote_sdp,
    int remote_len,
    const char* local_ip,
    char* sdp,
    int len)
{
    if (!session || !remote_sdp || !local_ip || !sdp) {
        return LWS_ERR_INVALID_PARAM;
    }

    // Store remote SDP
    if (remote_len > (int)sizeof(session->remote_sdp) - 1) {
        return LWS_ERR_SESSION_SDP;
    }
    memcpy(session->remote_sdp, remote_sdp, remote_len);
    session->remote_sdp[remote_len] = '\0';
    session->remote_sdp_len = remote_len;

    // Generate answer (similar to offer)
    return lws_session_generate_sdp_offer(session, local_ip, sdp, len);
}

int lws_session_process_sdp(lws_session_t* session,
    const char* sdp_str,
    int len)
{
    sdp_t* sdp;
    int media_count;
    int i;
    char remote_ip[64] = {0};

    if (!session || !sdp_str || len <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    // Store remote SDP
    if (len > (int)sizeof(session->remote_sdp) - 1) {
        return LWS_ERR_SESSION_SDP;
    }

    memcpy(session->remote_sdp, sdp_str, len);
    session->remote_sdp[len] = '\0';
    session->remote_sdp_len = len;

    lws_log_info("processing remote SDP (%d bytes):\n%.*s\n", len, len, sdp_str);

    // Parse SDP using librtsp
    sdp = sdp_parse(sdp_str, len);
    if (!sdp) {
        lws_log_error(LWS_ERR_SDP_PARSE, "failed to parse SDP\n");
        return LWS_ERR_SDP_PARSE;
    }

    // Get connection address
    if (sdp_connection_get_address(sdp, remote_ip, sizeof(remote_ip)) == 0) {
        lws_log_info("remote IP: %s\n", remote_ip);
    }

    // Parse media descriptions
    media_count = sdp_media_count(sdp);
    lws_log_info("media count: %d\n", media_count);

    for (i = 0; i < media_count; i++) {
        const char* media_type = sdp_media_type(sdp, i);
        int ports[2];
        int port_count;
        int formats[16];
        int format_count;

        port_count = sdp_media_port(sdp, i, ports, 2);
        format_count = sdp_media_formats(sdp, i, formats, 16);

        lws_log_info("media[%d]: type=%s, port=%d, formats=%d\n",
                     i, media_type, ports[0], format_count);

        // Process audio media
        if (strcmp(media_type, "audio") == 0 && port_count > 0) {
            session->audio_remote_port = ports[0];

            // Extract audio codec
            if (format_count > 0) {
                // Use first format
                lws_log_info("audio codec: %d\n", formats[0]);

                // Store codec info
                if (formats[0] == 0) {
                    session->config.audio_codec = LWS_AUDIO_CODEC_PCMU;
                } else if (formats[0] == 8) {
                    session->config.audio_codec = LWS_AUDIO_CODEC_PCMA;
                }
            }
        }
        // Process video media
        else if (strcmp(media_type, "video") == 0 && port_count > 0) {
            session->video_remote_port = ports[0];

            // Extract video codec
            if (format_count > 0) {
                lws_log_info("video codec: %d\n", formats[0]);

                // Store codec info
                if (formats[0] == 96) {
                    session->config.video_codec = LWS_VIDEO_CODEC_H264;
                }
            }
        }

        // Parse ICE attributes if ICE is enabled
        if (session->ice_enabled && session->ice_agent) {
            // Simple string-based parsing for ICE attributes
            const char* line_start = sdp_str;
            const char* line_end;

            while ((line_end = strstr(line_start, "\r\n")) != NULL ||
                   (line_end = strstr(line_start, "\n")) != NULL) {
                int line_len = line_end - line_start;

                // Parse a=ice-ufrag:
                if (strncmp(line_start, "a=ice-ufrag:", 12) == 0) {
                    int ufrag_len = line_len - 12;
                    if (ufrag_len > 0 && ufrag_len < 64) {
                        strncpy(session->remote_ice_ufrag, line_start + 12, ufrag_len);
                        session->remote_ice_ufrag[ufrag_len] = '\0';
                        lws_log_info("parsed ICE ufrag: %s\n", session->remote_ice_ufrag);
                    }
                }
                // Parse a=ice-pwd:
                else if (strncmp(line_start, "a=ice-pwd:", 10) == 0) {
                    int pwd_len = line_len - 10;
                    if (pwd_len > 0 && pwd_len < 64) {
                        strncpy(session->remote_ice_pwd, line_start + 10, pwd_len);
                        session->remote_ice_pwd[pwd_len] = '\0';
                        lws_log_info("parsed ICE pwd: %s\n", session->remote_ice_pwd);
                    }
                }
                // Parse a=candidate:
                else if (strncmp(line_start, "a=candidate:", 12) == 0) {
                    // Parse candidate line: a=candidate:<foundation> <component> <protocol> <priority> <ip> <port> typ <type>
                    char foundation[33];
                    int component;
                    char protocol[10];
                    uint32_t priority;
                    char ip[64];
                    int port;
                    char typ_str[10];
                    char type[10];

                    int parsed = sscanf(line_start + 12, "%32s %d %9s %u %63s %d %9s %9s",
                                        foundation, &component, protocol, &priority,
                                        ip, &port, typ_str, type);

                    if (parsed >= 8 && strcmp(typ_str, "typ") == 0) {
                        lws_log_info("parsed candidate: foundation=%s, component=%d, protocol=%s, priority=%u, ip=%s, port=%d, type=%s\n",
                                     foundation, component, protocol, priority, ip, port, type);

                        // Add to ICE agent
                        lws_ice_add_remote_candidate(session->ice_agent, i, component,
                                                      foundation, priority, ip, port, type);
                    }
                }

                // Move to next line
                if (*line_end == '\r' && *(line_end + 1) == '\n') {
                    line_start = line_end + 2;
                } else {
                    line_start = line_end + 1;
                }

                if (line_start >= sdp_str + len) {
                    break;
                }
            }

            // Set remote ICE authentication
            if (session->remote_ice_ufrag[0] != '\0' && session->remote_ice_pwd[0] != '\0') {
                lws_ice_set_remote_auth(session->ice_agent, 0,
                                        session->remote_ice_ufrag,
                                        session->remote_ice_pwd);

                // Start ICE connectivity checks
                lws_log_info("starting ICE connectivity checks\n");
                lws_ice_start(session->ice_agent);
            }
        }
    }

    sdp_destroy(sdp);

    lws_log_info("SDP processed: audio_port=%u, video_port=%u\n",
                 session->audio_remote_port, session->video_remote_port);

    // Trigger on_media_ready callback
    if (session->handler.on_media_ready) {
        session->handler.on_media_ready(session->handler.param,
            session->config.audio_codec,
            session->config.audio_sample_rate,
            session->config.audio_channels,
            session->config.video_codec,
            session->config.video_width,
            session->config.video_height,
            session->config.video_fps);
    }

    return LWS_OK;
}

int lws_session_start(lws_session_t* session)
{
    if (!session) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (session->is_started) {
        return LWS_OK;
    }

    // Create payload encoders/decoders
    if (session->config.enable_audio) {
        session->audio_encoder = lws_payload_encoder_create(
            session->config.audio_codec,
            "PCMU",
            12345,  // SSRC
            0,      // seq
            audio_packet_cb,
            session);

        session->audio_decoder = lws_payload_decoder_create(
            session->config.audio_codec,
            "PCMU",
            audio_frame_cb,
            session);
    }

    if (session->config.enable_video) {
        session->video_encoder = lws_payload_encoder_create(
            session->config.video_codec,
            "H264",
            12346,  // SSRC
            0,      // seq
            video_packet_cb,
            session);

        session->video_decoder = lws_payload_decoder_create(
            session->config.video_codec,
            "H264",
            video_frame_cb,
            session);
    }

    session->is_started = 1;
    lws_log_info("session started\n");
    return LWS_OK;
}

void lws_session_stop(lws_session_t* session)
{
    if (!session || !session->is_started) {
        return;
    }

    session->is_started = 0;
    lws_log_info("session stopped\n");
}

int lws_session_poll(lws_session_t* session, int timeout_ms)
{
    fd_set readfds;
    struct timeval tv, *tvp;
    int max_fd = -1;
    int ret;
    int packet_count = 0;
    uint8_t buffer[2048];  // Max RTP packet size
    struct sockaddr_in from_addr;
    socklen_t from_len;

    if (!session) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (!session->is_started) {
        return 0;  // Not started, nothing to poll
    }

    FD_ZERO(&readfds);

    // Add audio RTP socket
    if (session->audio_rtp_sock > 0) {
        FD_SET(session->audio_rtp_sock, &readfds);
        if (session->audio_rtp_sock > max_fd) {
            max_fd = session->audio_rtp_sock;
        }
    }

    // Add audio RTCP socket
    if (session->audio_rtcp_sock > 0) {
        FD_SET(session->audio_rtcp_sock, &readfds);
        if (session->audio_rtcp_sock > max_fd) {
            max_fd = session->audio_rtcp_sock;
        }
    }

    // Add video RTP socket
    if (session->video_rtp_sock > 0) {
        FD_SET(session->video_rtp_sock, &readfds);
        if (session->video_rtp_sock > max_fd) {
            max_fd = session->video_rtp_sock;
        }
    }

    // Add video RTCP socket
    if (session->video_rtcp_sock > 0) {
        FD_SET(session->video_rtcp_sock, &readfds);
        if (session->video_rtcp_sock > max_fd) {
            max_fd = session->video_rtcp_sock;
        }
    }

    if (max_fd < 0) {
        // No sockets to poll
        return 0;
    }

    // Setup timeout
    if (timeout_ms < 0) {
        tvp = NULL;  // Infinite timeout
    } else {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }

    // Select on all RTP/RTCP sockets
    ret = select(max_fd + 1, &readfds, NULL, NULL, tvp);
    if (ret < 0) {
        lws_log_error(LWS_ERR_SOCKET_RECV, "select failed\n");
        return LWS_ERR_SOCKET_RECV;
    }

    if (ret == 0) {
        // Timeout
        return 0;
    }

    // Process audio RTP packets
    if (session->audio_rtp_sock > 0 && FD_ISSET(session->audio_rtp_sock, &readfds)) {
        from_len = sizeof(from_addr);
        ret = recvfrom(session->audio_rtp_sock, buffer, sizeof(buffer), 0,
                      (struct sockaddr*)&from_addr, &from_len);
        if (ret > 0) {
            lws_log_debug("received audio RTP packet: %d bytes\n", ret);

            // Pass to decoder
            if (session->audio_decoder) {
                lws_payload_decode(session->audio_decoder, buffer, ret);
            }
            packet_count++;
        }
    }

    // Process audio RTCP packets
    if (session->audio_rtcp_sock > 0 && FD_ISSET(session->audio_rtcp_sock, &readfds)) {
        from_len = sizeof(from_addr);
        ret = recvfrom(session->audio_rtcp_sock, buffer, sizeof(buffer), 0,
                      (struct sockaddr*)&from_addr, &from_len);
        if (ret > 0) {
            lws_log_debug("received audio RTCP packet: %d bytes\n", ret);
            // TODO: Process RTCP statistics
            packet_count++;
        }
    }

    // Process video RTP packets
    if (session->video_rtp_sock > 0 && FD_ISSET(session->video_rtp_sock, &readfds)) {
        from_len = sizeof(from_addr);
        ret = recvfrom(session->video_rtp_sock, buffer, sizeof(buffer), 0,
                      (struct sockaddr*)&from_addr, &from_len);
        if (ret > 0) {
            lws_log_debug("received video RTP packet: %d bytes\n", ret);

            // Pass to decoder
            if (session->video_decoder) {
                lws_payload_decode(session->video_decoder, buffer, ret);
            }
            packet_count++;
        }
    }

    // Process video RTCP packets
    if (session->video_rtcp_sock > 0 && FD_ISSET(session->video_rtcp_sock, &readfds)) {
        from_len = sizeof(from_addr);
        ret = recvfrom(session->video_rtcp_sock, buffer, sizeof(buffer), 0,
                      (struct sockaddr*)&from_addr, &from_len);
        if (ret > 0) {
            lws_log_debug("received video RTCP packet: %d bytes\n", ret);
            // TODO: Process RTCP statistics
            packet_count++;
        }
    }

    return packet_count;
}

int lws_session_send_audio(lws_session_t* session,
    const void* data,
    int bytes,
    uint32_t timestamp)
{
    if (!session || !data || bytes <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (!session->audio_encoder) {
        return LWS_ERR_NOT_INITIALIZED;
    }

    return lws_payload_encode(session->audio_encoder, data, bytes, timestamp);
}

int lws_session_send_video(lws_session_t* session,
    const void* data,
    int bytes,
    uint32_t timestamp)
{
    if (!session || !data || bytes <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (!session->video_encoder) {
        return LWS_ERR_NOT_INITIALIZED;
    }

    return lws_payload_encode(session->video_encoder, data, bytes, timestamp);
}

int lws_session_get_local_port(lws_session_t* session,
    uint16_t* audio_port,
    uint16_t* video_port)
{
    if (!session) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (audio_port) {
        *audio_port = session->audio_local_port;
    }
    if (video_port) {
        *video_port = session->video_local_port;
    }

    return LWS_OK;
}

int lws_session_get_media_count(lws_session_t* session)
{
    int count = 0;

    if (!session) {
        return 0;
    }

    if (session->config.enable_audio) {
        count++;
    }
    if (session->config.enable_video) {
        count++;
    }

    return count;
}

const char* lws_session_get_local_sdp(lws_session_t* session, int* len)
{
    if (!session) {
        return NULL;
    }

    if (len) {
        *len = session->local_sdp_len;
    }

    return session->local_sdp;
}

void lws_session_set_dialog(lws_session_t* session, void* dialog)
{
    if (!session || !dialog) {
        return;
    }

    // Release old dialog if exists
    if (session->dialog) {
        sip_dialog_release((struct sip_dialog_t*)session->dialog);
    }

    // Add reference and save
    session->dialog = dialog;
    sip_dialog_addref((struct sip_dialog_t*)dialog);

    lws_log_info("dialog saved to session\n");
}

void lws_session_set_invite_transaction(lws_session_t* session, void* transaction)
{
    if (!session || !transaction) {
        return;
    }

    // Note: libsip transaction lifetime is managed by libsip itself
    // We just store the pointer without taking ownership
    session->invite_transaction = transaction;
    lws_log_info("INVITE transaction saved to session\n");
}

void* lws_session_get_invite_transaction(lws_session_t* session)
{
    if (!session) {
        return NULL;
    }
    return session->invite_transaction;
}

void* lws_session_get_dialog(lws_session_t* session)
{
    if (!session) {
        return NULL;
    }

    return session->dialog;
}
