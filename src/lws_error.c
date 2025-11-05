/**
 * @file lws_error.c
 * @brief Error code to string mapping
 */

#include "lws_error.h"

static const struct {
    int code;
    const char* desc;
} error_map[] = {
    { LWS_OK, "Success" },

    // General errors
    { LWS_ERR_NOMEM, "Out of memory" },
    { LWS_ERR_INVALID_PARAM, "Invalid parameter" },
    { LWS_ERR_NOT_INITIALIZED, "Not initialized" },
    { LWS_ERR_ALREADY_INITIALIZED, "Already initialized" },
    { LWS_ERR_TIMEOUT, "Operation timeout" },
    { LWS_ERR_NOT_FOUND, "Not found" },

    // Network errors
    { LWS_ERR_SOCKET_CREATE, "Failed to create socket" },
    { LWS_ERR_SOCKET_BIND, "Failed to bind socket" },
    { LWS_ERR_SOCKET_CONNECT, "Failed to connect socket" },
    { LWS_ERR_SOCKET_SEND, "Failed to send data" },
    { LWS_ERR_SOCKET_RECV, "Failed to receive data" },

    // SIP errors
    { LWS_ERR_SIP_PARSE, "SIP message parse error" },
    { LWS_ERR_SIP_REGISTER, "SIP registration failed" },
    { LWS_ERR_SIP_INVITE, "SIP invite failed" },
    { LWS_ERR_SIP_AUTH, "SIP authentication failed" },
    { LWS_ERR_SIP_TRANSPORT, "SIP transport error" },

    // RTP errors
    { LWS_ERR_RTP_CREATE, "Failed to create RTP session" },
    { LWS_ERR_RTP_ENCODE, "RTP encode error" },
    { LWS_ERR_RTP_DECODE, "RTP decode error" },
    { LWS_ERR_RTP_PAYLOAD, "RTP payload error" },

    // Media errors
    { LWS_ERR_MEDIA_OPEN, "Failed to open media" },
    { LWS_ERR_MEDIA_READ, "Media read error" },
    { LWS_ERR_MEDIA_WRITE, "Media write error" },
    { LWS_ERR_MEDIA_FORMAT, "Unsupported media format" },
    { LWS_ERR_MEDIA_CODEC, "Unsupported codec" },

    // Session errors
    { LWS_ERR_SESSION_CREATE, "Failed to create session" },
    { LWS_ERR_SESSION_START, "Failed to start session" },
    { LWS_ERR_SESSION_STOP, "Failed to stop session" },
    { LWS_ERR_SESSION_SDP, "SDP processing error" },
};

const char* lws_error_string(int errcode)
{
    int i;
    int count = sizeof(error_map) / sizeof(error_map[0]);

    for (i = 0; i < count; i++) {
        if (error_map[i].code == errcode) {
            return error_map[i].desc;
        }
    }

    return "Unknown error";
}
