/**
 * @file lws_err.h
 * @brief lwsip hierarchical error code system
 *
 * Error code format: INT32 (0x8MMMEEEE)
 *   - Bit 31: Always 1 (negative number)
 *   - Bits 24-30: Reserved (all 0)
 *   - Bits 16-23 (MMM): Module ID
 *     - 000: Common errors
 *     - 001: Transport layer
 *     - 002: SIP layer
 *     - 003: RTP layer
 *     - 004: Codec layer
 *   - Bits 0-15 (EEEE): Error number within module
 *
 * Example:
 *   LWS_ERR_TRANS_CREATE = 0x80010001
 *     - Module 001 (Transport)
 *     - Error 0001 (Create failed)
 */

#ifndef __LWS_ERR_H__
#define __LWS_ERR_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================
 * Module ID definitions
 * ======================================== */
#define LWS_MODULE_COMMON       0x00    /**< Common errors */
#define LWS_MODULE_TRANSPORT    0x01    /**< Transport layer */
#define LWS_MODULE_SIP          0x02    /**< SIP layer */
#define LWS_MODULE_RTP          0x03    /**< RTP layer */
#define LWS_MODULE_CODEC        0x04    /**< Codec layer */
#define LWS_MODULE_MEDIA        0x05    /**< Media session layer */

/* ========================================
 * Error code helper macro
 * ======================================== */
#define LWS_MAKE_ERROR(module, error) \
    ((int32_t)(0x80000000 | ((module) << 16) | (error)))

/* ========================================
 * Common errors (Module 000: 0x8000EEEE)
 * Note: Basic errors are in lws_defs.h
 * ======================================== */
/* LWS_OK, LWS_ERROR, LWS_EINVAL, LWS_ENOMEM, etc. are in lws_defs.h */

/* ========================================
 * Transport layer errors (Module 001: 0x8001EEEE)
 * ======================================== */

/** Failed to create transport instance */
#define LWS_ERR_TRANS_CREATE    LWS_MAKE_ERROR(LWS_MODULE_TRANSPORT, 0x0001)

/** Failed to create socket */
#define LWS_ERR_SOCK_CREATE     LWS_MAKE_ERROR(LWS_MODULE_TRANSPORT, 0x0002)

/** Failed to set socket options */
#define LWS_ERR_SOCK_SETOPT     LWS_MAKE_ERROR(LWS_MODULE_TRANSPORT, 0x0003)

/** Failed to bind socket */
#define LWS_ERR_SOCK_BIND       LWS_MAKE_ERROR(LWS_MODULE_TRANSPORT, 0x0004)

/** Invalid address format */
#define LWS_ERR_INVALID_ADDR    LWS_MAKE_ERROR(LWS_MODULE_TRANSPORT, 0x0005)

/** Send operation failed */
#define LWS_ERR_TRANS_SEND      LWS_MAKE_ERROR(LWS_MODULE_TRANSPORT, 0x0006)

/** Receive operation failed */
#define LWS_ERR_TRANS_RECV      LWS_MAKE_ERROR(LWS_MODULE_TRANSPORT, 0x0007)

/** Connection timeout */
#define LWS_ERR_TRANS_TIMEOUT   LWS_MAKE_ERROR(LWS_MODULE_TRANSPORT, 0x0008)

/* ========================================
 * SIP layer errors (Module 002: 0x8002EEEE)
 * ======================================== */

/** Failed to create SIP agent */
#define LWS_ERR_SIP_CREATE      LWS_MAKE_ERROR(LWS_MODULE_SIP, 0x0001)

/** Failed to send SIP message */
#define LWS_ERR_SIP_SEND        LWS_MAKE_ERROR(LWS_MODULE_SIP, 0x0002)

/** Failed to parse SIP message */
#define LWS_ERR_SIP_PARSE       LWS_MAKE_ERROR(LWS_MODULE_SIP, 0x0003)

/** Invalid SIP URI format */
#define LWS_ERR_SIP_INVALID_URI LWS_MAKE_ERROR(LWS_MODULE_SIP, 0x0004)

/** SIP transaction timeout */
#define LWS_ERR_SIP_TIMEOUT     LWS_MAKE_ERROR(LWS_MODULE_SIP, 0x0005)

/** SIP authentication failed */
#define LWS_ERR_SIP_AUTH        LWS_MAKE_ERROR(LWS_MODULE_SIP, 0x0006)

/** SIP registration failed */
#define LWS_ERR_SIP_REGISTER    LWS_MAKE_ERROR(LWS_MODULE_SIP, 0x0007)

/** SIP call setup failed */
#define LWS_ERR_SIP_CALL        LWS_MAKE_ERROR(LWS_MODULE_SIP, 0x0008)

/* ========================================
 * RTP layer errors (Module 003: 0x8003EEEE)
 * ======================================== */

/** Failed to create RTP session */
#define LWS_ERR_RTP_CREATE      LWS_MAKE_ERROR(LWS_MODULE_RTP, 0x0001)

/** Failed to send RTP packet */
#define LWS_ERR_RTP_SEND        LWS_MAKE_ERROR(LWS_MODULE_RTP, 0x0002)

/** Failed to receive RTP packet */
#define LWS_ERR_RTP_RECV        LWS_MAKE_ERROR(LWS_MODULE_RTP, 0x0003)

/** Invalid RTP packet */
#define LWS_ERR_RTP_INVALID     LWS_MAKE_ERROR(LWS_MODULE_RTP, 0x0004)

/* ========================================
 * Codec layer errors (Module 004: 0x8004EEEE)
 * ======================================== */

/** Failed to create codec */
#define LWS_ERR_CODEC_CREATE    LWS_MAKE_ERROR(LWS_MODULE_CODEC, 0x0001)

/** Codec encode failed */
#define LWS_ERR_CODEC_ENCODE    LWS_MAKE_ERROR(LWS_MODULE_CODEC, 0x0002)

/** Codec decode failed */
#define LWS_ERR_CODEC_DECODE    LWS_MAKE_ERROR(LWS_MODULE_CODEC, 0x0003)

/** Unsupported codec */
#define LWS_ERR_CODEC_NOTSUP    LWS_MAKE_ERROR(LWS_MODULE_CODEC, 0x0004)

/* ========================================
 * Media session layer errors (Module 005: 0x8005EEEE)
 * ======================================== */

/** Failed to create media session */
#define LWS_ERR_MEDIA_SESSION   LWS_MAKE_ERROR(LWS_MODULE_MEDIA, 0x0001)

/** General media error */
#define LWS_ERR_MEDIA           LWS_MAKE_ERROR(LWS_MODULE_MEDIA, 0x0002)

/** Failed to gather ICE candidates */
#define LWS_ERR_MEDIA_ICE       LWS_MAKE_ERROR(LWS_MODULE_MEDIA, 0x0003)

/** Failed to set remote SDP */
#define LWS_ERR_MEDIA_SDP       LWS_MAKE_ERROR(LWS_MODULE_MEDIA, 0x0004)

/** Media session connection timeout */
#define LWS_ERR_MEDIA_TIMEOUT   LWS_MAKE_ERROR(LWS_MODULE_MEDIA, 0x0005)

/* ========================================
 * Error code string conversion
 * ======================================== */

/**
 * @brief Get error code module name
 * @param errcode Error code
 * @return Module name string
 */
static inline const char* lws_err_module_name(int32_t errcode)
{
    int module = (errcode >> 16) & 0xFF;
    switch (module) {
        case LWS_MODULE_COMMON:    return "COMMON";
        case LWS_MODULE_TRANSPORT: return "TRANSPORT";
        case LWS_MODULE_SIP:       return "SIP";
        case LWS_MODULE_RTP:       return "RTP";
        case LWS_MODULE_CODEC:     return "CODEC";
        case LWS_MODULE_MEDIA:     return "MEDIA";
        default:                   return "UNKNOWN";
    }
}

/**
 * @brief Get error code description
 * @param errcode Error code
 * @return Error description string
 */
static inline const char* lws_err_string(int32_t errcode)
{
    switch (errcode) {
        /* Transport errors */
        case LWS_ERR_TRANS_CREATE:  return "Failed to create transport";
        case LWS_ERR_SOCK_CREATE:   return "Failed to create socket";
        case LWS_ERR_SOCK_SETOPT:   return "Failed to set socket option";
        case LWS_ERR_SOCK_BIND:     return "Failed to bind socket";
        case LWS_ERR_INVALID_ADDR:  return "Invalid address format";
        case LWS_ERR_TRANS_SEND:    return "Transport send failed";
        case LWS_ERR_TRANS_RECV:    return "Transport receive failed";
        case LWS_ERR_TRANS_TIMEOUT: return "Transport timeout";

        /* SIP errors */
        case LWS_ERR_SIP_CREATE:    return "Failed to create SIP agent";
        case LWS_ERR_SIP_SEND:      return "Failed to send SIP message";
        case LWS_ERR_SIP_PARSE:     return "Failed to parse SIP message";
        case LWS_ERR_SIP_INVALID_URI: return "Invalid SIP URI";
        case LWS_ERR_SIP_TIMEOUT:   return "SIP transaction timeout";
        case LWS_ERR_SIP_AUTH:      return "SIP authentication failed";
        case LWS_ERR_SIP_REGISTER:  return "SIP registration failed";
        case LWS_ERR_SIP_CALL:      return "SIP call setup failed";

        /* RTP errors */
        case LWS_ERR_RTP_CREATE:    return "Failed to create RTP session";
        case LWS_ERR_RTP_SEND:      return "Failed to send RTP packet";
        case LWS_ERR_RTP_RECV:      return "Failed to receive RTP packet";
        case LWS_ERR_RTP_INVALID:   return "Invalid RTP packet";

        /* Codec errors */
        case LWS_ERR_CODEC_CREATE:  return "Failed to create codec";
        case LWS_ERR_CODEC_ENCODE:  return "Codec encode failed";
        case LWS_ERR_CODEC_DECODE:  return "Codec decode failed";
        case LWS_ERR_CODEC_NOTSUP:  return "Unsupported codec";

        /* Media session errors */
        case LWS_ERR_MEDIA_SESSION: return "Media session error";
        case LWS_ERR_MEDIA:         return "General media error";
        case LWS_ERR_MEDIA_ICE:     return "Failed to gather ICE candidates";
        case LWS_ERR_MEDIA_SDP:     return "Failed to set remote SDP";
        case LWS_ERR_MEDIA_TIMEOUT: return "Media session connection timeout";

        default:                    return "Unknown error";
    }
}

#ifdef __cplusplus
}
#endif

#endif // __LWS_ERR_H__
