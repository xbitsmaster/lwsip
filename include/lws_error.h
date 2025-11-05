/**
 * @file lws_error.h
 * @brief LwSIP Error Code Definitions
 */

#ifndef __LWS_ERROR_H__
#define __LWS_ERROR_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Error Code Categories (0x8xxxxxxx format)
 * ============================================================ */

// Success
#define LWS_OK                      0

// General errors (0x8000xxxx)
#define LWS_ERR_NOMEM               0x80000001
#define LWS_ERR_INVALID_PARAM       0x80000002
#define LWS_ERR_NOT_INITIALIZED     0x80000003
#define LWS_ERR_ALREADY_INITIALIZED 0x80000004
#define LWS_ERR_TIMEOUT             0x80000005
#define LWS_ERR_NOT_FOUND           0x80000006
#define LWS_ERR_INTERNAL            0x80000007
#define LWS_ERR_NOT_IMPLEMENTED     0x80000008

// Network errors (0x8001xxxx)
#define LWS_ERR_SOCKET_CREATE       0x80010001
#define LWS_ERR_SOCKET_BIND         0x80010002
#define LWS_ERR_SOCKET_CONNECT      0x80010003
#define LWS_ERR_SOCKET_SEND         0x80010004
#define LWS_ERR_SOCKET_RECV         0x80010005

// SIP errors (0x8002xxxx)
#define LWS_ERR_SIP_PARSE           0x80020001
#define LWS_ERR_SIP_REGISTER        0x80020002
#define LWS_ERR_SIP_INVITE          0x80020003
#define LWS_ERR_SIP_AUTH            0x80020004
#define LWS_ERR_SIP_TRANSPORT       0x80020005
#define LWS_ERR_SIP_CREATE          0x80020006
#define LWS_ERR_SIP_INPUT           0x80020007
#define LWS_ERR_SIP_SEND            0x80020008
#define LWS_ERR_SIP_BYE             0x80020009
#define LWS_ERR_SIP_NO_DIALOG       0x8002000A

// RTP errors (0x8003xxxx)
#define LWS_ERR_RTP_CREATE          0x80030001
#define LWS_ERR_RTP_ENCODE          0x80030002
#define LWS_ERR_RTP_DECODE          0x80030003
#define LWS_ERR_RTP_PAYLOAD         0x80030004

// Media errors (0x8004xxxx)
#define LWS_ERR_MEDIA_OPEN          0x80040001
#define LWS_ERR_MEDIA_READ          0x80040002
#define LWS_ERR_MEDIA_WRITE         0x80040003
#define LWS_ERR_MEDIA_FORMAT        0x80040004
#define LWS_ERR_MEDIA_CODEC         0x80040005

// Session errors (0x8005xxxx)
#define LWS_ERR_SESSION_CREATE      0x80050001
#define LWS_ERR_SESSION_START       0x80050002
#define LWS_ERR_SESSION_STOP        0x80050003
#define LWS_ERR_SESSION_SDP         0x80050004
#define LWS_ERR_SDP_GENERATE        0x80050005
#define LWS_ERR_SDP_PARSE           0x80050006

/**
 * @brief Get error string description
 * @param errcode Error code
 * @return Error description string
 */
const char* lws_error_string(int errcode);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_ERROR_H__ */
