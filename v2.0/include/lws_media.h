/**
 * @file lws_media.h
 * @brief LwSIP Media Source/Sink Interface
 *
 * This layer provides media input/output abstraction:
 * - File I/O (WAV, MP4, etc.)
 * - Microphone capture
 * - Speaker playback
 * - Memory buffer
 */

#ifndef __LWS_MEDIA_H__
#define __LWS_MEDIA_H__

#include "lws_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Opaque Media Handle
 * ============================================================ */

typedef struct lws_media lws_media_t;

/* ============================================================
 * Media Type
 * ============================================================ */

typedef enum {
    LWS_MEDIA_TYPE_FILE = 0,
    LWS_MEDIA_TYPE_MEMORY,
    LWS_MEDIA_TYPE_DEVICE,  // microphone/speaker
} lws_media_type_t;

/* ============================================================
 * Media Configuration
 * ============================================================ */

typedef struct {
    lws_media_type_t type;

    // For FILE type
    const char* file_path;
    int loop;  // loop playback for file

    // For MEMORY type
    void* buffer;
    size_t buffer_size;

    // For DEVICE type
    const char* device_name;

    // Audio parameters
    lws_audio_codec_t audio_codec;
    int sample_rate;
    int channels;

    // Video parameters
    lws_video_codec_t video_codec;
    int width;
    int height;
    int fps;
} lws_media_config_t;

/* ============================================================
 * Media API
 * ============================================================ */

/**
 * @brief Create media source/sink
 * @param config Media configuration
 * @return Media handle or NULL on error
 */
lws_media_t* lws_media_create(const lws_media_config_t* config);

/**
 * @brief Destroy media handle
 * @param media Media handle
 */
void lws_media_destroy(lws_media_t* media);

/**
 * @brief Read audio data
 * @param media Media handle
 * @param data Buffer to store audio data
 * @param len Buffer size
 * @return Number of bytes read, 0=EOF, <0=error
 */
int lws_media_read_audio(lws_media_t* media, void* data, int len);

/**
 * @brief Read video data
 * @param media Media handle
 * @param data Buffer to store video data
 * @param len Buffer size
 * @return Number of bytes read, 0=EOF, <0=error
 */
int lws_media_read_video(lws_media_t* media, void* data, int len);

/**
 * @brief Write audio data
 * @param media Media handle
 * @param data Audio data
 * @param len Data length
 * @return Number of bytes written, <0=error
 */
int lws_media_write_audio(lws_media_t* media, const void* data, int len);

/**
 * @brief Write video data
 * @param media Media handle
 * @param data Video data
 * @param len Data length
 * @return Number of bytes written, <0=error
 */
int lws_media_write_video(lws_media_t* media, const void* data, int len);

/**
 * @brief Start media (for device type)
 * @param media Media handle
 * @return 0 on success, <0 on error
 */
int lws_media_start(lws_media_t* media);

/**
 * @brief Stop media (for device type)
 * @param media Media handle
 */
void lws_media_stop(lws_media_t* media);

/**
 * @brief Get audio parameters
 * @param media Media handle
 * @param codec Audio codec [out]
 * @param sample_rate Sample rate [out]
 * @param channels Channels [out]
 * @return 0 on success, <0 on error
 */
int lws_media_get_audio_params(lws_media_t* media,
    lws_audio_codec_t* codec,
    int* sample_rate,
    int* channels);

/**
 * @brief Get video parameters
 * @param media Media handle
 * @param codec Video codec [out]
 * @param width Video width [out]
 * @param height Video height [out]
 * @param fps Video FPS [out]
 * @return 0 on success, <0 on error
 */
int lws_media_get_video_params(lws_media_t* media,
    lws_video_codec_t* codec,
    int* width,
    int* height,
    int* fps);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_MEDIA_H__ */
