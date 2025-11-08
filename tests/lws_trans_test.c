/**
 * @file lws_trans_test.c
 * @brief Transport layer test
 *
 * Tests:
 * - UDP client/server communication
 * - Non-blocking I/O
 * - Event loop
 */

#include "lwsip.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* ========================================
 * Test Configuration
 * ======================================== */

#define TEST_UDP_PORT       15000
#define TEST_TCP_PORT       15001
#define TEST_MESSAGE        "Hello from client!"
#define TEST_RESPONSE       "Hello from server!"

/* ========================================
 * Test Status
 * ======================================== */

typedef struct {
    int server_received;
    int client_received;
    int server_error;
    int client_error;
    int connection_established;
} test_status_t;

/* ========================================
 * UDP Test
 * ======================================== */

static test_status_t udp_status = {0};

/* UDP Server回调 */
static void udp_server_on_data(lws_trans_t* trans, const void* data, int len,
                                const lws_addr_t* from, void* userdata)
{
    LWS_UNUSED(userdata);

    printf("[UDP Server] Received %d bytes from %s:%d\n", len, from->ip, from->port);
    printf("[UDP Server] Data: %.*s\n", len, (char*)data);

    udp_status.server_received = 1;

    /* 回应客户端 */
    int ret = lws_trans_send(trans, TEST_RESPONSE, strlen(TEST_RESPONSE), from);
    if (ret > 0) {
        printf("[UDP Server] Sent response: %s\n", TEST_RESPONSE);
    }
}

static void udp_server_on_error(lws_trans_t* trans, int error_code,
                                 const char* error_msg, void* userdata)
{
    LWS_UNUSED(trans);
    LWS_UNUSED(userdata);

    printf("[UDP Server] Error: %s (%d)\n", error_msg, error_code);
    udp_status.server_error = 1;
}

/* UDP Client回调 */
static void udp_client_on_data(lws_trans_t* trans, const void* data, int len,
                                const lws_addr_t* from, void* userdata)
{
    LWS_UNUSED(trans);
    LWS_UNUSED(userdata);

    printf("[UDP Client] Received %d bytes from %s:%d\n", len, from->ip, from->port);
    printf("[UDP Client] Data: %.*s\n", len, (char*)data);

    udp_status.client_received = 1;
}

static void udp_client_on_error(lws_trans_t* trans, int error_code,
                                 const char* error_msg, void* userdata)
{
    LWS_UNUSED(trans);
    LWS_UNUSED(userdata);

    printf("[UDP Client] Error: %s (%d)\n", error_msg, error_code);
    udp_status.client_error = 1;
}

/* UDP Server线程 */
static void* udp_server_thread(void* arg)
{
    LWS_UNUSED(arg);

    printf("[UDP Server] Starting on port %d...\n", TEST_UDP_PORT);

    /* 创建UDP服务端 */
    lws_trans_config_t config = {0};
    config.type = LWS_TRANS_TYPE_UDP;
    strcpy(config.sock.bind_addr, "127.0.0.1");
    config.sock.bind_port = TEST_UDP_PORT;
    config.sock.reuse_addr = 1;

    lws_trans_handler_t handler = {0};
    handler.on_data = udp_server_on_data;
    handler.on_error = udp_server_on_error;

    lws_trans_t* server = lws_trans_create(&config, &handler);
    if (!server) {
        printf("[UDP Server] Failed to create\n");
        return NULL;
    }

    lws_addr_t local_addr;
    lws_trans_get_local_addr(server, &local_addr);
    printf("[UDP Server] Listening on %s:%d\n", local_addr.ip, local_addr.port);

    /* 事件循环 - 运行3秒 */
    for (int i = 0; i < 30; i++) {
        lws_trans_loop(server, 100);
    }

    lws_trans_destroy(server);
    printf("[UDP Server] Stopped\n");

    return NULL;
}

int test_udp()
{
    printf("\n========================================\n");
    printf("UDP Transport Test\n");
    printf("========================================\n");

    memset(&udp_status, 0, sizeof(udp_status));

    /* 启动UDP服务端线程 */
    pthread_t server_tid;
    pthread_create(&server_tid, NULL, udp_server_thread, NULL);

    /* 等待服务端启动 */
    sleep(1);

    /* 创建UDP客户端 */
    printf("[UDP Client] Creating...\n");

    lws_trans_config_t config = {0};
    config.type = LWS_TRANS_TYPE_UDP;
    strcpy(config.sock.bind_addr, "0.0.0.0");
    config.sock.bind_port = 0;  /* 自动分配端口 */

    lws_trans_handler_t handler = {0};
    handler.on_data = udp_client_on_data;
    handler.on_error = udp_client_on_error;

    lws_trans_t* client = lws_trans_create(&config, &handler);
    if (!client) {
        printf("[UDP Client] Failed to create\n");
        return -1;
    }

    lws_addr_t local_addr;
    lws_trans_get_local_addr(client, &local_addr);
    printf("[UDP Client] Bound to %s:%d\n", local_addr.ip, local_addr.port);

    /* 发送数据到服务端 */
    lws_addr_t server_addr;
    strcpy(server_addr.ip, "127.0.0.1");
    server_addr.port = TEST_UDP_PORT;

    printf("[UDP Client] Sending to %s:%d: %s\n",
           server_addr.ip, server_addr.port, TEST_MESSAGE);

    int ret = lws_trans_send(client, TEST_MESSAGE, strlen(TEST_MESSAGE), &server_addr);
    if (ret > 0) {
        printf("[UDP Client] Sent %d bytes\n", ret);
    } else {
        printf("[UDP Client] Send failed\n");
    }

    /* 事件循环 - 等待响应 */
    for (int i = 0; i < 20; i++) {
        lws_trans_loop(client, 100);
        if (udp_status.client_received) {
            break;
        }
    }

    lws_trans_destroy(client);

    /* 等待服务端线程结束 */
    pthread_join(server_tid, NULL);

    /* 检查测试结果 */
    printf("\n[UDP Test] Results:\n");
    printf("  Server received: %s\n", udp_status.server_received ? "YES" : "NO");
    printf("  Client received: %s\n", udp_status.client_received ? "YES" : "NO");
    printf("  Server error: %s\n", udp_status.server_error ? "YES" : "NO");
    printf("  Client error: %s\n", udp_status.client_error ? "YES" : "NO");

    int success = udp_status.server_received && udp_status.client_received &&
                  !udp_status.server_error && !udp_status.client_error;

    printf("\n[UDP Test] Result: %s\n", success ? "PASS" : "FAIL");

    return success ? 0 : -1;
}


/* ========================================
 * Main
 * ======================================== */

int main(int argc, char* argv[])
{
    LWS_UNUSED(argc);
    LWS_UNUSED(argv);

    printf("========================================\n");
    printf("lwsip Transport Layer Test\n");
    printf("========================================\n");

    /* 初始化lwsip */
    if (lwsip_init() != LWS_OK) {
        printf("Failed to initialize lwsip\n");
        return 1;
    }

    printf("lwsip version: %s\n", lwsip_version());

    int udp_result = test_udp();

    /* 清理 */
    lwsip_cleanup();

    printf("\n========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("UDP Test: %s\n", udp_result == 0 ? "PASS" : "FAIL");

    printf("\nOverall: %s\n", udp_result == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");

    return udp_result;
}
