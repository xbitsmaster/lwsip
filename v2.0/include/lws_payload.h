/**
 * @file lws_payload.h
 * @brief LwSIP RTP Payload Encoder/Decoder
 *
 * This layer wraps media-server/librtp payload interface,
 * providing automatic RTP packetization and depacketization
 * for various codecs (H.264, H.265, PCMU, PCMA, etc.)
 */

#ifndef __LWS_PAYLOAD_H__
#define __LWS_PAYLOAD_H__

#include "lws_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Opaque Payload Handle
 * ============================================================ */

typedef struct lws_payload_encoder lws_payload_encoder_t;
typedef struct lws_payload_decoder lws_payload_decoder_t;

/* ============================================================
 * Payload Callbacks
 * ============================================================ */

/**
 * @brief RTP packet callback (for encoder)
 * Called when encoder outputs an RTP packet
 * @param param User parameter
 * @param packet RTP packet (including 12-byte header)
 * @param bytes Packet size
 * @param timestamp RTP timestamp
 * @param flags Packet flags
 * @return 0 on success, <0 on error
 */
typedef int (*lws_payload_packet_cb)(void* param,
    const void* packet,
    int bytes,
    uint32_t timestamp,
    int flags);

/**
 * @brief Frame callback (for decoder)
 * Called when decoder outputs a complete frame
 * @param param User parameter
 * @param frame Frame data
 * @param bytes Frame size
 * @param timestamp RTP timestamp
 * @param flags Frame flags (RTP_PAYLOAD_FLAG_PACKET_LOST, etc.)
 * @return 0 on success, <0 on error
 */
typedef int (*lws_payload_frame_cb)(void* param,
    const void* frame,
    int bytes,
    uint32_t timestamp,
    int flags);

/* ============================================================
 * Encoder API
 * ============================================================ */

/**
 * @brief Create RTP payload encoder
 * @param payload_type RTP payload type (0-127)
 * @param encoding Encoding name ("H264", "PCMU", etc.)
 * @param ssrc RTP SSRC
 * @param seq Initial sequence number
 * @param packet_cb RTP packet callback
 * @param param User parameter
 * @return Encoder handle or NULL on error
 */
lws_payload_encoder_t* lws_payload_encoder_create(
    int payload_type,
    const char* encoding,
    uint32_t ssrc,
    uint16_t seq,
    lws_payload_packet_cb packet_cb,
    void* param);

/**
 * @brief Destroy encoder
 * @param encoder Encoder handle
 */
void lws_payload_encoder_destroy(lws_payload_encoder_t* encoder);

/**
 * @brief Encode frame into RTP packets
 * @param encoder Encoder handle
 * @param frame Frame data
 * @param bytes Frame size
 * @param timestamp RTP timestamp
 * @return 0 on success, <0 on error
 */
int lws_payload_encode(lws_payload_encoder_t* encoder,
    const void* frame,
    int bytes,
    uint32_t timestamp);

/**
 * @brief Get current sequence number and timestamp
 * @param encoder Encoder handle
 * @param seq Current sequence number [out]
 * @param timestamp Current timestamp [out]
 */
void lws_payload_encoder_get_info(lws_payload_encoder_t* encoder,
    uint16_t* seq,
    uint32_t* timestamp);

/* ============================================================
 * Decoder API
 * ============================================================ */

/**
 * @brief Create RTP payload decoder
 * @param payload_type RTP payload type (0-127)
 * @param encoding Encoding name ("H264", "PCMU", etc.)
 * @param frame_cb Frame callback
 * @param param User parameter
 * @return Decoder handle or NULL on error
 */
lws_payload_decoder_t* lws_payload_decoder_create(
    int payload_type,
    const char* encoding,
    lws_payload_frame_cb frame_cb,
    void* param);

/**
 * @brief Destroy decoder
 * @param decoder Decoder handle
 */
void lws_payload_decoder_destroy(lws_payload_decoder_t* decoder);

/**
 * @brief Decode RTP packet
 * @param decoder Decoder handle
 * @param packet RTP packet (including 12-byte header)
 * @param bytes Packet size
 * @return 1=packet handled, 0=packet discarded, <0=error
 */
int lws_payload_decode(lws_payload_decoder_t* decoder,
    const void* packet,
    int bytes);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_PAYLOAD_H__ */
