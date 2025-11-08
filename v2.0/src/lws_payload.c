/**
 * @file lws_payload.c
 * @brief RTP payload encoder/decoder implementation
 *
 * This wraps media-server/librtp payload interface
 */

#include "lws_payload.h"
#include "lws_error.h"
#include "lws_mem.h"
#include "lws_log.h"
#include "rtp-payload.h"
#include <string.h>

/* ============================================================
 * Encoder Structure
 * ============================================================ */

struct lws_payload_encoder {
    void* rtp_encoder;  // rtp_payload_encode handle
    lws_payload_packet_cb packet_cb;
    void* param;

    int payload_type;
    char encoding[32];
    uint32_t ssrc;
};

/* ============================================================
 * Decoder Structure
 * ============================================================ */

struct lws_payload_decoder {
    void* rtp_decoder;  // rtp_payload_decode handle
    lws_payload_frame_cb frame_cb;
    void* param;

    int payload_type;
    char encoding[32];
};

/* ============================================================
 * Internal Callbacks (bridge to librtp)
 * ============================================================ */

static int encoder_packet_cb(void* param,
    const void *packet,
    int bytes,
    uint32_t timestamp,
    int flags)
{
    lws_payload_encoder_t* encoder = (lws_payload_encoder_t*)param;

    if (encoder->packet_cb) {
        return encoder->packet_cb(encoder->param, packet, bytes, timestamp, flags);
    }

    return 0;
}

static int decoder_frame_cb(void* param,
    const void *frame,
    int bytes,
    uint32_t timestamp,
    int flags)
{
    lws_payload_decoder_t* decoder = (lws_payload_decoder_t*)param;

    if (decoder->frame_cb) {
        return decoder->frame_cb(decoder->param, frame, bytes, timestamp, flags);
    }

    return 0;
}

/* ============================================================
 * Encoder API
 * ============================================================ */

lws_payload_encoder_t* lws_payload_encoder_create(
    int payload_type,
    const char* encoding,
    uint32_t ssrc,
    uint16_t seq,
    lws_payload_packet_cb packet_cb,
    void* param)
{
    lws_payload_encoder_t* encoder;
    struct rtp_payload_t handler;

    if (!encoding || !packet_cb) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    encoder = (lws_payload_encoder_t*)lws_malloc(sizeof(lws_payload_encoder_t));
    if (!encoder) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate encoder\n");
        return NULL;
    }

    memset(encoder, 0, sizeof(lws_payload_encoder_t));
    encoder->payload_type = payload_type;
    strncpy(encoder->encoding, encoding, sizeof(encoder->encoding) - 1);
    encoder->ssrc = ssrc;
    encoder->packet_cb = packet_cb;
    encoder->param = param;

    // Create librtp encoder
    memset(&handler, 0, sizeof(handler));
    handler.alloc = NULL;
    handler.free = NULL;
    handler.packet = encoder_packet_cb;

    encoder->rtp_encoder = rtp_payload_encode_create(
        payload_type, encoding, seq, ssrc, &handler, encoder);

    if (!encoder->rtp_encoder) {
        lws_log_error(LWS_ERR_RTP_CREATE, "failed to create rtp encoder\n");
        lws_free(encoder);
        return NULL;
    }

    lws_log_info("payload encoder created: type=%d, encoding=%s\n",
                 payload_type, encoding);
    return encoder;
}

void lws_payload_encoder_destroy(lws_payload_encoder_t* encoder)
{
    if (!encoder) {
        return;
    }

    if (encoder->rtp_encoder) {
        rtp_payload_encode_destroy(encoder->rtp_encoder);
        encoder->rtp_encoder = NULL;
    }

    lws_free(encoder);
    lws_log_info("payload encoder destroyed\n");
}

int lws_payload_encode(lws_payload_encoder_t* encoder,
    const void* frame,
    int bytes,
    uint32_t timestamp)
{
    if (!encoder || !frame || bytes <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    return rtp_payload_encode_input(encoder->rtp_encoder, frame, bytes, timestamp);
}

void lws_payload_encoder_get_info(lws_payload_encoder_t* encoder,
    uint16_t* seq,
    uint32_t* timestamp)
{
    if (!encoder) {
        return;
    }

    rtp_payload_encode_getinfo(encoder->rtp_encoder, seq, timestamp);
}

/* ============================================================
 * Decoder API
 * ============================================================ */

lws_payload_decoder_t* lws_payload_decoder_create(
    int payload_type,
    const char* encoding,
    lws_payload_frame_cb frame_cb,
    void* param)
{
    lws_payload_decoder_t* decoder;
    struct rtp_payload_t handler;

    if (!encoding || !frame_cb) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    decoder = (lws_payload_decoder_t*)lws_malloc(sizeof(lws_payload_decoder_t));
    if (!decoder) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate decoder\n");
        return NULL;
    }

    memset(decoder, 0, sizeof(lws_payload_decoder_t));
    decoder->payload_type = payload_type;
    strncpy(decoder->encoding, encoding, sizeof(decoder->encoding) - 1);
    decoder->frame_cb = frame_cb;
    decoder->param = param;

    // Create librtp decoder
    memset(&handler, 0, sizeof(handler));
    handler.alloc = NULL;
    handler.free = NULL;
    handler.packet = decoder_frame_cb;

    decoder->rtp_decoder = rtp_payload_decode_create(
        payload_type, encoding, &handler, decoder);

    if (!decoder->rtp_decoder) {
        lws_log_error(LWS_ERR_RTP_CREATE, "failed to create rtp decoder\n");
        lws_free(decoder);
        return NULL;
    }

    lws_log_info("payload decoder created: type=%d, encoding=%s\n",
                 payload_type, encoding);
    return decoder;
}

void lws_payload_decoder_destroy(lws_payload_decoder_t* decoder)
{
    if (!decoder) {
        return;
    }

    if (decoder->rtp_decoder) {
        rtp_payload_decode_destroy(decoder->rtp_decoder);
        decoder->rtp_decoder = NULL;
    }

    lws_free(decoder);
    lws_log_info("payload decoder destroyed\n");
}

int lws_payload_decode(lws_payload_decoder_t* decoder,
    const void* packet,
    int bytes)
{
    if (!decoder || !packet || bytes <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    return rtp_payload_decode_input(decoder->rtp_decoder, packet, bytes);
}
