/**
 * @file test_transport_tcp.c
 * @brief Unit tests for TCP transport layer
 */

#include "../../unity_framework/src/unity.h"
#include "lws_transport.h"
#include "../mocks/mock_sip_agent.h"
#include <string.h>

/* ============================================================
 * Test Setup and Teardown
 * ============================================================ */

static lws_transport_config_t test_config;
static lws_transport_handler_t test_handler;

void setUp(void)
{
    // Reset mocks before each test
    mock_sip_agent_reset();

    // Setup test config
    memset(&test_config, 0, sizeof(test_config));
    test_config.remote_host = "127.0.0.1";
    test_config.remote_port = 5060;
    test_config.local_port = 0;
    test_config.use_tcp = 1;

    // Setup test handler
    memset(&test_handler, 0, sizeof(test_handler));
}

void tearDown(void)
{
    // Cleanup after each test
}

/* ============================================================
 * Transport TCP Basic Tests
 * ============================================================ */

void test_tcp_transport_create_should_return_null_with_null_config(void)
{
    lws_transport_t* transport = lws_transport_socket_create(NULL, &test_handler);
    TEST_ASSERT_NULL(transport);
}

void test_tcp_transport_create_should_return_null_with_null_handler(void)
{
    lws_transport_t* transport = lws_transport_socket_create(&test_config, NULL);
    TEST_ASSERT_NULL(transport);
}

void test_tcp_transport_create_should_allocate_memory(void)
{
    lws_transport_t* transport = lws_transport_socket_create(&test_config, &test_handler);

    TEST_ASSERT_NOT_NULL(transport);

    // Clean up
    if (transport) {
        lws_transport_destroy(transport);
    }
}

void test_tcp_transport_should_have_valid_ops(void)
{
    lws_transport_t* transport = lws_transport_socket_create(&test_config, &test_handler);

    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_NOT_NULL(transport->ops);
    TEST_ASSERT_NOT_NULL(transport->ops->connect);
    TEST_ASSERT_NOT_NULL(transport->ops->disconnect);
    TEST_ASSERT_NOT_NULL(transport->ops->send);
    TEST_ASSERT_NOT_NULL(transport->ops->poll);
    TEST_ASSERT_NOT_NULL(transport->ops->destroy);

    lws_transport_destroy(transport);
}

void test_tcp_transport_initial_state_should_be_disconnected(void)
{
    lws_transport_t* transport = lws_transport_socket_create(&test_config, &test_handler);

    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_EQUAL(LWS_TRANSPORT_STATE_DISCONNECTED, transport->state);

    lws_transport_destroy(transport);
}

void test_tcp_transport_should_copy_config(void)
{
    lws_transport_t* transport = lws_transport_socket_create(&test_config, &test_handler);

    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_EQUAL_STRING("127.0.0.1", transport->config.remote_host);
    TEST_ASSERT_EQUAL(5060, transport->config.remote_port);
    TEST_ASSERT_EQUAL(0, transport->config.local_port);
    TEST_ASSERT_EQUAL(1, transport->config.use_tcp);

    lws_transport_destroy(transport);
}

void test_tcp_transport_destroy_should_handle_null(void)
{
    // Should not crash
    lws_transport_destroy(NULL);
    TEST_PASS();
}

/* ============================================================
 * Main Test Runner
 * ============================================================ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_tcp_transport_create_should_return_null_with_null_config);
    RUN_TEST(test_tcp_transport_create_should_return_null_with_null_handler);
    RUN_TEST(test_tcp_transport_create_should_allocate_memory);
    RUN_TEST(test_tcp_transport_should_have_valid_ops);
    RUN_TEST(test_tcp_transport_initial_state_should_be_disconnected);
    RUN_TEST(test_tcp_transport_should_copy_config);
    RUN_TEST(test_tcp_transport_destroy_should_handle_null);

    return UNITY_END();
}
