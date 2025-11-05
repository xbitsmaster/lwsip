/**
 * @file lws_types.h
 * @brief LwSIP Common Type Definitions
 */

#ifndef __LWS_TYPES_H__
#define __LWS_TYPES_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Configuration Constants
 * ============================================================ */

#define LWS_MAX_HOST_LEN        256
#define LWS_MAX_USERNAME_LEN    64
#define LWS_MAX_PASSWORD_LEN    64
#define LWS_MAX_DISPLAY_NAME    128
#define LWS_MAX_URI_LEN         512
#define LWS_MAX_SDP_LEN         4096

/* ============================================================
 * Audio/Video Codec Enums
 * ============================================================ */

typedef enum {
    LWS_AUDIO_CODEC_NONE = 0,
    LWS_AUDIO_CODEC_PCMU = 0,      // G.711 μ-law, payload type 0
    LWS_AUDIO_CODEC_PCMA = 8,      // G.711 A-law, payload type 8
    LWS_AUDIO_CODEC_G722 = 9,      // G.722, payload type 9
    LWS_AUDIO_CODEC_OPUS = 111,    // Opus, dynamic payload
    LWS_AUDIO_CODEC_AAC = 97,      // AAC, dynamic payload
} lws_audio_codec_t;

typedef enum {
    LWS_VIDEO_CODEC_NONE = 0,
    LWS_VIDEO_CODEC_H264 = 96,     // H.264, dynamic payload
    LWS_VIDEO_CODEC_H265 = 98,     // H.265, dynamic payload
    LWS_VIDEO_CODEC_VP8 = 100,     // VP8, dynamic payload
    LWS_VIDEO_CODEC_VP9 = 101,     // VP9, dynamic payload
} lws_video_codec_t;

/* ============================================================
 * SIP State Enums
 * ============================================================ */

typedef enum {
    LWS_REG_NONE = 0,
    LWS_REG_REGISTERING,
    LWS_REG_REGISTERED,
    LWS_REG_UNREGISTERING,
    LWS_REG_UNREGISTERED,
    LWS_REG_FAILED,
} lws_reg_state_t;

typedef enum {
    LWS_CALL_IDLE = 0,
    LWS_CALL_CALLING,
    LWS_CALL_RINGING,
    LWS_CALL_ANSWERED,
    LWS_CALL_ESTABLISHED,
    LWS_CALL_HANGUP,
    LWS_CALL_FAILED,
    LWS_CALL_TERMINATED,
} lws_call_state_t;

typedef enum {
    LWS_TRANSPORT_STATE_DISCONNECTED = 0,
    LWS_TRANSPORT_STATE_CONNECTING,
    LWS_TRANSPORT_STATE_CONNECTED,
    LWS_TRANSPORT_STATE_ERROR,
} lws_transport_state_t;

/* ============================================================
 * Configuration Structure
 * ============================================================ */

/* ============================================================
 * Media Backend Type
 * ============================================================ */

typedef enum {
    LWS_MEDIA_BACKEND_NONE = 0,
    LWS_MEDIA_BACKEND_FILE,     // Read/write to file (WAV, MP4, etc.)
    LWS_MEDIA_BACKEND_MEMORY,   // Use memory buffer
    LWS_MEDIA_BACKEND_DEVICE,   // Use audio/video device (mic/speaker/camera)
} lws_media_backend_t;

/* ============================================================
 * Transport Type
 * ============================================================ */

typedef enum {
    LWS_TRANSPORT_UDP = 0,      // Standard UDP (default)
    LWS_TRANSPORT_TCP,          // Standard TCP
    LWS_TRANSPORT_TLS,          // TLS over TCP
    LWS_TRANSPORT_MQTT,         // MQTT pub/sub (for IoT)
    LWS_TRANSPORT_CUSTOM,       // Custom transport
} lws_transport_type_t;

typedef struct {
    // SIP server settings
    char server_host[LWS_MAX_HOST_LEN];
    uint16_t server_port;

    // User credentials
    char username[LWS_MAX_USERNAME_LEN];
    char password[LWS_MAX_PASSWORD_LEN];
    char display_name[LWS_MAX_DISPLAY_NAME];

    // Local settings
    uint16_t local_port;
    int expires;  // Registration expires (seconds), renamed from register_expires
    int use_tcp;  // Deprecated: use transport_type instead

    // Transport settings (NEW)
    lws_transport_type_t transport_type;  // Transport type
    int enable_tls;                       // Enable TLS encryption
    char tls_ca_file[256];                // CA certificate file for TLS
    char tls_cert_file[256];              // Client certificate file
    char tls_key_file[256];               // Client private key file

    // MQTT transport settings (when transport_type == LWS_TRANSPORT_MQTT)
    char mqtt_broker_host[LWS_MAX_HOST_LEN];
    uint16_t mqtt_broker_port;
    char mqtt_client_id[128];
    char mqtt_pub_topic[256];  // Topic to publish SIP messages
    char mqtt_sub_topic[256];  // Topic to subscribe for receiving

    // Media settings
    int enable_audio;
    int enable_video;
    lws_audio_codec_t audio_codec;
    lws_video_codec_t video_codec;

    // Media backend settings (NEW)
    lws_media_backend_t media_backend_type;  // Media backend type

    // File backend settings
    char audio_input_file[256];   // Input audio file (for playback)
    char audio_output_file[256];  // Output audio file (for recording)
    char video_input_file[256];   // Input video file
    char video_output_file[256];  // Output video file

    // Memory backend settings
    void* audio_input_buffer;     // Audio input buffer
    int audio_input_buffer_size;
    void* audio_output_buffer;    // Audio output buffer
    int audio_output_buffer_size;

    // Device backend settings
    char audio_device_name[128];  // Audio device name (e.g., "default", "hw:0,0")
    char video_device_name[128];  // Video device name (e.g., "/dev/video0")

    // RTP settings
    uint16_t audio_rtp_port;      // Local RTP port for audio (0=auto)
    uint16_t video_rtp_port;      // Local RTP port for video (0=auto)
    int audio_sample_rate;        // 8000, 16000, 48000
    int audio_channels;           // 1 or 2
    int video_width;
    int video_height;
    int video_fps;

    // ICE (Interactive Connectivity Establishment) settings
    int enable_ice;               // Enable ICE for NAT traversal (0=disabled, 1=enabled)
    int ice_controlling;          // ICE role: 1=controlling (caller), 0=controlled (callee)
    int ice_lite;                 // Enable ICE-lite mode (0=full ICE, 1=lite)

    // STUN server settings (for gathering server reflexive candidates)
    char stun_server[LWS_MAX_HOST_LEN];  // STUN server address (e.g., "stun.stunprotocol.org")
    uint16_t stun_port;                   // STUN server port (default: 3478)

    // TURN server settings (for relayed candidates, optional)
    int enable_turn;                      // Enable TURN relay (0=disabled, 1=enabled)
    char turn_server[LWS_MAX_HOST_LEN];  // TURN server address
    uint16_t turn_port;                   // TURN server port (default: 3478)
    char turn_username[LWS_MAX_USERNAME_LEN];  // TURN username
    char turn_password[LWS_MAX_PASSWORD_LEN];  // TURN password

    // ICE timing settings
    int ice_gather_timeout;       // Candidate gathering timeout in ms (default: 3000)
    int ice_connect_timeout;      // ICE connectivity check timeout in ms (default: 5000)

    // Threading settings
    int use_worker_thread;        // Use background worker thread (0=manual polling, 1=auto)
} lws_config_t;

#ifdef __cplusplus
}
#endif

#endif /* __LWS_TYPES_H__ */
