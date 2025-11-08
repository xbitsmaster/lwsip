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
#define LWS_MAX_NICKNAME_LEN    128
#define LWS_MAX_URI_LEN         512
#define LWS_MAX_SDP_LEN         4096

// Path and name length constants
#define LWS_MAX_PATH_LEN        256
#define LWS_MAX_DEVICE_NAME_LEN 128
#define LWS_MAX_CLIENT_ID_LEN   128
#define LWS_MAX_TOPIC_LEN       256

// TLS certificate/key buffer size limits (for memory mode)
#define LWS_MAX_TLS_CERT_SIZE   (8 * 1024)   // 8KB for certificates
#define LWS_MAX_TLS_KEY_SIZE    (4 * 1024)   // 4KB for private keys

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
    char nickname[LWS_MAX_NICKNAME_LEN];

    // Local settings
    uint16_t local_port;
    int expires;  // Registration expires (seconds), renamed from register_expires

    // Transport settings (NEW)
    lws_transport_type_t transport_type;  // Transport type
    int enable_tls;                       // Enable TLS encryption

    // TLS configuration: memory-based (for embedded systems without filesystem)
    const void* tls_ca;               // CA certificate in memory
    int tls_ca_size;                  // CA certificate size
    const void* tls_cert;             // Client certificate in memory
    int tls_cert_size;                // Client certificate size
    const void* tls_key;              // Client private key in memory
    int tls_key_size;                 // Client private key size

#if defined(LWS_ENABLE_TRANSPORT_MQTT)
    // MQTT transport settings (when transport_type == LWS_TRANSPORT_MQTT)
    char mqtt_broker_host[LWS_MAX_HOST_LEN];
    uint16_t mqtt_broker_port;
    char mqtt_client_id[LWS_MAX_CLIENT_ID_LEN];
    char mqtt_pub_topic[LWS_MAX_TOPIC_LEN];  // Topic to publish SIP messages
    char mqtt_sub_topic[LWS_MAX_TOPIC_LEN];  // Topic to subscribe for receiving
#endif

    // Media settings
    int enable_audio;
    int enable_video;
    lws_audio_codec_t audio_codec;
    lws_video_codec_t video_codec;

    // Media backend settings (NEW)
    lws_media_backend_t media_backend_type;  // Media backend type

#if defined(LWS_ENABLE_MEDIA_FILE)
    // File backend settings
    char audio_input_file[LWS_MAX_PATH_LEN];   // Input audio file (for playback)
    char audio_output_file[LWS_MAX_PATH_LEN];  // Output audio file (for recording)
    char video_input_file[LWS_MAX_PATH_LEN];   // Input video file
    char video_output_file[LWS_MAX_PATH_LEN];  // Output video file
#endif

#if defined(LWS_ENABLE_MEDIA_MEMORY)
    // Memory backend settings
    void* audio_input_buffer;     // Audio input buffer
    int audio_input_buffer_size;
    void* audio_output_buffer;    // Audio output buffer
    int audio_output_buffer_size;
#endif

#if defined(LWS_ENABLE_MEDIA_DEVICE)
    // Device backend settings
    char audio_device_name[LWS_MAX_DEVICE_NAME_LEN];  // Audio device name (e.g., "default", "hw:0,0")
    char video_device_name[LWS_MAX_DEVICE_NAME_LEN];  // Video device name (e.g., "/dev/video0")
#endif

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
} lws_config_t;

/* ============================================================
 * Forward Declarations for Opaque Types
 * ============================================================ */

/**
 * Forward declarations for opaque structures
 * The actual structure definitions are in internal headers or source files
 */
struct lws_agent;
typedef struct lws_agent lws_agent_t;

struct lws_session;
typedef struct lws_session lws_session_t;

#ifdef __cplusplus
}
#endif

#endif /* __LWS_TYPES_H__ */
