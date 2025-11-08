/**
 * @file lws_media.c
 * @brief Media source/sink implementation
 */

#include "lws_media.h"
#include "lws_error.h"
#include "lws_mem.h"
#include "lws_log.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * Media Structure
 * ============================================================ */

struct lws_media {
    lws_media_type_t type;

    // File I/O
    FILE* fp;
    char file_path[256];
    int loop;

    // Memory buffer
    void* buffer;
    size_t buffer_size;
    size_t buffer_pos;

    // Parameters
    lws_audio_codec_t audio_codec;
    int sample_rate;
    int channels;

    lws_video_codec_t video_codec;
    int width;
    int height;
    int fps;

    int is_running;
};

/* ============================================================
 * Internal Functions
 * ============================================================ */

static int media_open_file(lws_media_t* media, const char* path)
{
    media->fp = fopen(path, "rb");
    if (!media->fp) {
        lws_log_error(LWS_ERR_MEDIA_OPEN, "failed to open file: %s\n", path);
        return LWS_ERR_MEDIA_OPEN;
    }

    strncpy(media->file_path, path, sizeof(media->file_path) - 1);
    lws_log_info("opened media file: %s\n", path);
    return LWS_OK;
}

/* ============================================================
 * Public API
 * ============================================================ */

lws_media_t* lws_media_create(const lws_media_config_t* config)
{
    lws_media_t* media;

    if (!config) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "config is NULL\n");
        return NULL;
    }

    media = (lws_media_t*)lws_malloc(sizeof(lws_media_t));
    if (!media) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate media\n");
        return NULL;
    }

    memset(media, 0, sizeof(lws_media_t));
    media->type = config->type;
    media->audio_codec = config->audio_codec;
    media->sample_rate = config->sample_rate;
    media->channels = config->channels;
    media->video_codec = config->video_codec;
    media->width = config->width;
    media->height = config->height;
    media->fps = config->fps;

    switch (config->type) {
    case LWS_MEDIA_TYPE_FILE:
        if (config->file_path) {
            media->loop = config->loop;
            if (media_open_file(media, config->file_path) != LWS_OK) {
                lws_free(media);
                return NULL;
            }
        }
        break;

    case LWS_MEDIA_TYPE_MEMORY:
        media->buffer = config->buffer;
        media->buffer_size = config->buffer_size;
        media->buffer_pos = 0;
        break;

    case LWS_MEDIA_TYPE_DEVICE:
        lws_log_info("device type not implemented yet\n");
        break;
    }

    lws_log_info("media created: type=%d\n", config->type);
    return media;
}

void lws_media_destroy(lws_media_t* media)
{
    if (!media) {
        return;
    }

    if (media->fp) {
        fclose(media->fp);
        media->fp = NULL;
    }

    lws_free(media);
    lws_log_info("media destroyed\n");
}

int lws_media_read_audio(lws_media_t* media, void* data, int len)
{
    int ret;

    if (!media || !data || len <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    switch (media->type) {
    case LWS_MEDIA_TYPE_FILE:
        if (!media->fp) {
            return LWS_ERR_MEDIA_OPEN;
        }

        ret = fread(data, 1, len, media->fp);
        if (ret == 0) {
            if (media->loop && !feof(media->fp)) {
                // Error occurred
                return LWS_ERR_MEDIA_READ;
            }

            if (media->loop) {
                // EOF, loop back
                fseek(media->fp, 0, SEEK_SET);
                ret = fread(data, 1, len, media->fp);
            }
        }
        return ret;

    case LWS_MEDIA_TYPE_MEMORY:
        if (media->buffer_pos >= media->buffer_size) {
            return 0;  // EOF
        }

        ret = (media->buffer_size - media->buffer_pos) < (size_t)len ?
              (media->buffer_size - media->buffer_pos) : len;
        memcpy(data, (char*)media->buffer + media->buffer_pos, ret);
        media->buffer_pos += ret;
        return ret;

    default:
        return LWS_ERR_MEDIA_FORMAT;
    }
}

int lws_media_read_video(lws_media_t* media, void* data, int len)
{
    // Video reading similar to audio
    return lws_media_read_audio(media, data, len);
}

int lws_media_write_audio(lws_media_t* media, const void* data, int len)
{
    int ret;

    if (!media || !data || len <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    switch (media->type) {
    case LWS_MEDIA_TYPE_FILE:
        if (!media->fp) {
            return LWS_ERR_MEDIA_OPEN;
        }

        ret = fwrite(data, 1, len, media->fp);
        if (ret != len) {
            return LWS_ERR_MEDIA_WRITE;
        }
        return ret;

    case LWS_MEDIA_TYPE_MEMORY:
        if (media->buffer_pos + len > media->buffer_size) {
            return LWS_ERR_NOMEM;
        }

        memcpy((char*)media->buffer + media->buffer_pos, data, len);
        media->buffer_pos += len;
        return len;

    default:
        return LWS_ERR_MEDIA_FORMAT;
    }
}

int lws_media_write_video(lws_media_t* media, const void* data, int len)
{
    return lws_media_write_audio(media, data, len);
}

int lws_media_start(lws_media_t* media)
{
    if (!media) {
        return LWS_ERR_INVALID_PARAM;
    }

    media->is_running = 1;
    lws_log_info("media started\n");
    return LWS_OK;
}

void lws_media_stop(lws_media_t* media)
{
    if (!media) {
        return;
    }

    media->is_running = 0;
    lws_log_info("media stopped\n");
}

int lws_media_get_audio_params(lws_media_t* media,
    lws_audio_codec_t* codec,
    int* sample_rate,
    int* channels)
{
    if (!media) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (codec) {
        *codec = media->audio_codec;
    }
    if (sample_rate) {
        *sample_rate = media->sample_rate;
    }
    if (channels) {
        *channels = media->channels;
    }

    return LWS_OK;
}

int lws_media_get_video_params(lws_media_t* media,
    lws_video_codec_t* codec,
    int* width,
    int* height,
    int* fps)
{
    if (!media) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (codec) {
        *codec = media->video_codec;
    }
    if (width) {
        *width = media->width;
    }
    if (height) {
        *height = media->height;
    }
    if (fps) {
        *fps = media->fps;
    }

    return LWS_OK;
}
