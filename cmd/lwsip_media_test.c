/**
 * @file lwsip_media_test.c
 * @brief lwsip媒体传输测试程序
 *
 * 测试音频+视频文件的RTP传输
 * - 发送端: 读取媒体文件发送
 * - 接收端: 接收RTP包但不解码，只打印统计信息
 */

#include "lws_client.h"
#include "lws_session.h"
#include "lws_media.h"
#include "lws_log.h"
#include "lws_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

/* ============================================================
 * Test Configuration
 * ============================================================ */

#define LOCAL_IP "192.168.10.131"
#define SENDER_PORT 15000
#define RECEIVER_PORT 16000

#define AUDIO_FILE "media/test_audio_pcmu.wav"
#define VIDEO_FILE "media/test_video.mp4"

#define TEST_DURATION_SEC 30  // 测试持续时间

/* ============================================================
 * Statistics
 * ============================================================ */

typedef struct {
    uint32_t audio_packet_count;
    uint32_t audio_byte_count;
    uint32_t audio_first_ts;
    uint32_t audio_last_ts;

    uint32_t video_packet_count;
    uint32_t video_byte_count;
    uint32_t video_first_ts;
    uint32_t video_last_ts;

    time_t start_time;
    time_t last_print_time;
} test_stats_t;

/* ============================================================
 * Global Variables
 * ============================================================ */

static volatile int g_running = 1;
static test_stats_t g_stats;

/* ============================================================
 * Signal Handler
 * ============================================================ */

static void signal_handler(int sig)
{
    (void)sig;
    printf("\n[SIGNAL] Received interrupt signal, stopping...\n");
    g_running = 0;
}

/* ============================================================
 * Receiver Callbacks
 * ============================================================ */

static int on_media_ready_cb(void* param,
    lws_audio_codec_t audio_codec,
    int audio_rate,
    int audio_channels,
    lws_video_codec_t video_codec,
    int video_width,
    int video_height,
    int video_fps)
{
    (void)param;

    printf("\n========================================\n");
    printf("[RECEIVER] Media negotiation completed\n");
    printf("========================================\n");
    printf("Audio:\n");
    printf("  - Codec: %d\n", audio_codec);
    printf("  - Sample rate: %d Hz\n", audio_rate);
    printf("  - Channels: %d\n", audio_channels);
    printf("\n");
    printf("Video:\n");
    printf("  - Codec: %d\n", video_codec);
    printf("  - Resolution: %dx%d\n", video_width, video_height);
    printf("  - FPS: %d\n", video_fps);
    printf("========================================\n\n");

    // 初始化统计
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.start_time = time(NULL);
    g_stats.last_print_time = g_stats.start_time;

    return 0;
}

static int on_audio_frame_cb(void* param,
    const void* data,
    int bytes,
    uint32_t timestamp)
{
    (void)param;
    (void)data;  // 不解码，直接丢弃

    // 更新统计
    g_stats.audio_packet_count++;
    g_stats.audio_byte_count += bytes;

    if (g_stats.audio_packet_count == 1) {
        g_stats.audio_first_ts = timestamp;
    }
    g_stats.audio_last_ts = timestamp;

    // 每秒打印一次统计
    time_t now = time(NULL);
    if (now - g_stats.last_print_time >= 1) {
        time_t elapsed = now - g_stats.start_time;

        printf("[RTP-STATS] Time: %lds | ", elapsed);
        printf("Audio: %u pkts, %u bytes (%.2f KB/s) | ",
            g_stats.audio_packet_count,
            g_stats.audio_byte_count,
            g_stats.audio_byte_count / (float)elapsed / 1024.0f);
        printf("Video: %u pkts, %u bytes (%.2f KB/s)\n",
            g_stats.video_packet_count,
            g_stats.video_byte_count,
            g_stats.video_byte_count / (float)elapsed / 1024.0f);

        g_stats.last_print_time = now;
    }

    return 0;
}

static int on_video_frame_cb(void* param,
    const void* data,
    int bytes,
    uint32_t timestamp)
{
    (void)param;
    (void)data;  // 不解码，直接丢弃

    // 更新统计
    g_stats.video_packet_count++;
    g_stats.video_byte_count += bytes;

    if (g_stats.video_packet_count == 1) {
        g_stats.video_first_ts = timestamp;
    }
    g_stats.video_last_ts = timestamp;

    return 0;
}

static void on_bye_cb(void* param)
{
    (void)param;
    printf("\n[RECEIVER] RTCP BYE received\n");
}

static void on_error_cb(void* param, int errcode)
{
    (void)param;
    printf("\n[RECEIVER] Error occurred: %d\n", errcode);
}

/* ============================================================
 * Print Final Statistics
 * ============================================================ */

static void print_final_stats(void)
{
    time_t elapsed = time(NULL) - g_stats.start_time;

    printf("\n");
    printf("========================================\n");
    printf("测试完成 - 最终统计\n");
    printf("========================================\n");
    printf("测试时长: %ld 秒\n", elapsed);
    printf("\n");

    printf("音频统计:\n");
    printf("  - 接收包数: %u\n", g_stats.audio_packet_count);
    printf("  - 接收字节: %u (%.2f KB)\n",
        g_stats.audio_byte_count,
        g_stats.audio_byte_count / 1024.0f);
    if (elapsed > 0) {
        printf("  - 平均速率: %.2f KB/s\n",
            g_stats.audio_byte_count / (float)elapsed / 1024.0f);
        printf("  - 包速率: %.2f pps\n",
            g_stats.audio_packet_count / (float)elapsed);
    }
    if (g_stats.audio_packet_count > 0) {
        printf("  - 时间戳范围: %u - %u (Δ=%u)\n",
            g_stats.audio_first_ts,
            g_stats.audio_last_ts,
            g_stats.audio_last_ts - g_stats.audio_first_ts);
    }
    printf("\n");

    printf("视频统计:\n");
    printf("  - 接收包数: %u\n", g_stats.video_packet_count);
    printf("  - 接收字节: %u (%.2f KB)\n",
        g_stats.video_byte_count,
        g_stats.video_byte_count / 1024.0f);
    if (elapsed > 0) {
        printf("  - 平均速率: %.2f KB/s\n",
            g_stats.video_byte_count / (float)elapsed / 1024.0f);
        printf("  - 包速率: %.2f pps\n",
            g_stats.video_packet_count / (float)elapsed);
    }
    if (g_stats.video_packet_count > 0) {
        printf("  - 时间戳范围: %u - %u (Δ=%u)\n",
            g_stats.video_first_ts,
            g_stats.video_last_ts,
            g_stats.video_last_ts - g_stats.video_first_ts);
    }
    printf("\n");

    printf("总计:\n");
    printf("  - 总包数: %u\n",
        g_stats.audio_packet_count + g_stats.video_packet_count);
    printf("  - 总字节: %u (%.2f KB)\n",
        g_stats.audio_byte_count + g_stats.video_byte_count,
        (g_stats.audio_byte_count + g_stats.video_byte_count) / 1024.0f);
    if (elapsed > 0) {
        printf("  - 总速率: %.2f KB/s\n",
            (g_stats.audio_byte_count + g_stats.video_byte_count) / (float)elapsed / 1024.0f);
    }
    printf("========================================\n");
}

/* ============================================================
 * Sender Thread
 * ============================================================ */

static void* sender_thread(void* arg)
{
    (void)arg;
    lws_session_t* session = NULL;
    lws_media_t* audio_media = NULL;
    lws_media_t* video_media = NULL;
    int ret;

    printf("[SENDER] Starting sender thread...\n");

    // 创建音频媒体源
    lws_media_config_t audio_config = {0};
    audio_config.type = LWS_MEDIA_TYPE_FILE;
    audio_config.file_path = AUDIO_FILE;
    audio_config.loop = 1;  // 循环播放
    audio_config.audio_codec = LWS_AUDIO_CODEC_PCMU;
    audio_config.sample_rate = 8000;
    audio_config.channels = 1;

    audio_media = lws_media_create(&audio_config);
    if (!audio_media) {
        printf("[SENDER] Failed to create audio media source\n");
        return NULL;
    }
    printf("[SENDER] Created audio media source: %s\n", AUDIO_FILE);

    // 创建视频媒体源
    lws_media_config_t video_config = {0};
    video_config.type = LWS_MEDIA_TYPE_FILE;
    video_config.file_path = VIDEO_FILE;
    video_config.loop = 1;  // 循环播放
    video_config.video_codec = LWS_VIDEO_CODEC_H264;
    video_config.width = 640;
    video_config.height = 480;
    video_config.fps = 25;

    video_media = lws_media_create(&video_config);
    if (!video_media) {
        printf("[SENDER] Failed to create video media source\n");
        lws_media_destroy(audio_media);
        return NULL;
    }
    printf("[SENDER] Created video media source: %s\n", VIDEO_FILE);

    // 创建会话
    lws_config_t config = {0};
    config.local_port = SENDER_PORT;

    lws_session_handler_t handler = {0};
    // 发送端不需要接收回调

    session = lws_session_create(&config, &handler);
    if (!session) {
        printf("[SENDER] Failed to create session\n");
        lws_media_destroy(audio_media);
        lws_media_destroy(video_media);
        return NULL;
    }
    printf("[SENDER] Created RTP session\n");

    // 设置媒体源
    ret = lws_session_set_media_source(session, audio_media);
    if (ret < 0) {
        printf("[SENDER] Failed to set audio media source: %d\n", ret);
        goto cleanup;
    }

    ret = lws_session_set_media_source(session, video_media);
    if (ret < 0) {
        printf("[SENDER] Failed to set video media source: %d\n", ret);
        goto cleanup;
    }

    // TODO: 生成SDP并与接收端交换
    // 这里需要实现P2P的SDP交换机制
    // 暂时先打印提示
    printf("[SENDER] TODO: Implement SDP exchange with receiver\n");

    // 启动会话
    ret = lws_session_start(session);
    if (ret < 0) {
        printf("[SENDER] Failed to start session: %d\n", ret);
        goto cleanup;
    }
    printf("[SENDER] Session started\n");

    // 主循环：读取媒体并发送
    time_t start_time = time(NULL);
    uint32_t audio_ts = 0;
    uint32_t video_ts = 0;
    uint8_t audio_buf[1024];
    uint8_t video_buf[4096];

    while (g_running && (time(NULL) - start_time < TEST_DURATION_SEC)) {
        // 发送音频 (20ms per frame = 160 samples = 160 bytes for PCMU)
        int audio_bytes = lws_media_read_audio(audio_media, audio_buf, sizeof(audio_buf));
        if (audio_bytes > 0) {
            ret = lws_session_send_audio(session, audio_buf, audio_bytes, audio_ts);
            if (ret < 0) {
                printf("[SENDER] Failed to send audio: %d\n", ret);
            }
            audio_ts += 160;  // 20ms @ 8kHz = 160 samples
        }

        // 发送视频 (40ms per frame @ 25fps)
        int video_bytes = lws_media_read_video(video_media, video_buf, sizeof(video_buf));
        if (video_bytes > 0) {
            ret = lws_session_send_video(session, video_buf, video_bytes, video_ts);
            if (ret < 0) {
                printf("[SENDER] Failed to send video: %d\n", ret);
            }
            video_ts += 3600;  // 40ms @ 90kHz = 3600 ticks
        }

        // Poll会话事件
        lws_session_poll(session, 10);

        usleep(20000);  // 20ms
    }

    printf("[SENDER] Stopping sender thread...\n");

cleanup:
    if (session) {
        lws_session_stop(session);
        lws_session_destroy(session);
    }
    if (audio_media) {
        lws_media_destroy(audio_media);
    }
    if (video_media) {
        lws_media_destroy(video_media);
    }

    printf("[SENDER] Sender thread exited\n");
    return NULL;
}

/* ============================================================
 * Receiver Thread
 * ============================================================ */

static void* receiver_thread(void* arg)
{
    (void)arg;
    lws_session_t* session = NULL;
    int ret;

    printf("[RECEIVER] Starting receiver thread...\n");

    // 创建会话
    lws_config_t config = {0};
    config.local_port = RECEIVER_PORT;

    // 设置接收回调
    lws_session_handler_t handler = {0};
    handler.on_media_ready = on_media_ready_cb;
    handler.on_audio_frame = on_audio_frame_cb;
    handler.on_video_frame = on_video_frame_cb;
    handler.on_bye = on_bye_cb;
    handler.on_error = on_error_cb;
    handler.param = NULL;

    session = lws_session_create(&config, &handler);
    if (!session) {
        printf("[RECEIVER] Failed to create session\n");
        return NULL;
    }
    printf("[RECEIVER] Created RTP session\n");

    // TODO: 接收并处理来自发送端的SDP
    printf("[RECEIVER] TODO: Implement SDP exchange with sender\n");

    // 启动会话
    ret = lws_session_start(session);
    if (ret < 0) {
        printf("[RECEIVER] Failed to start session: %d\n", ret);
        goto cleanup;
    }
    printf("[RECEIVER] Session started, waiting for RTP packets...\n\n");

    // 主循环：接收RTP包
    time_t start_time = time(NULL);
    while (g_running && (time(NULL) - start_time < TEST_DURATION_SEC)) {
        ret = lws_session_poll(session, 100);
        if (ret < 0) {
            printf("[RECEIVER] Poll error: %d\n", ret);
            break;
        }
    }

    printf("[RECEIVER] Stopping receiver thread...\n");

cleanup:
    if (session) {
        lws_session_stop(session);
        lws_session_destroy(session);
    }

    printf("[RECEIVER] Receiver thread exited\n");
    return NULL;
}

/* ============================================================
 * Main
 * ============================================================ */

int main(int argc, char** argv)
{
    pthread_t sender_tid;
    pthread_t receiver_tid;

    (void)argc;
    (void)argv;

    printf("\n");
    printf("========================================\n");
    printf("lwsip 媒体传输测试\n");
    printf("========================================\n");
    printf("本地IP: %s\n", LOCAL_IP);
    printf("发送端端口: %d\n", SENDER_PORT);
    printf("接收端端口: %d\n", RECEIVER_PORT);
    printf("\n");
    printf("音频文件: %s\n", AUDIO_FILE);
    printf("视频文件: %s\n", VIDEO_FILE);
    printf("\n");
    printf("测试时长: %d 秒\n", TEST_DURATION_SEC);
    printf("========================================\n\n");

    // 检查媒体文件是否存在
    if (access(AUDIO_FILE, R_OK) != 0) {
        printf("[ERROR] Cannot access audio file: %s\n", AUDIO_FILE);
        return 1;
    }
    if (access(VIDEO_FILE, R_OK) != 0) {
        printf("[ERROR] Cannot access video file: %s\n", VIDEO_FILE);
        return 1;
    }

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 启动接收端线程
    if (pthread_create(&receiver_tid, NULL, receiver_thread, NULL) != 0) {
        printf("[ERROR] Failed to create receiver thread\n");
        return 1;
    }

    sleep(1);  // 等待接收端准备好

    // 启动发送端线程
    if (pthread_create(&sender_tid, NULL, sender_thread, NULL) != 0) {
        printf("[ERROR] Failed to create sender thread\n");
        g_running = 0;
        pthread_join(receiver_tid, NULL);
        return 1;
    }

    // 等待线程结束
    pthread_join(sender_tid, NULL);
    pthread_join(receiver_tid, NULL);

    // 打印最终统计
    print_final_stats();

    printf("\n[MAIN] Test completed\n\n");

    return 0;
}
