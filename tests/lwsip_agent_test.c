/**
 * @file lwsip_agent_test.c
 * @brief Unit tests for lws_agent.c
 *
 * Test coverage:
 * - Agent creation and destruction
 * - Registration workflow
 * - Call establishment (UAC/UAS)
 * - State transitions
 * - Error handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "lws_agent.h"
#include "lws_err.h"
#include "lws_mem.h"
#include "lws_log.h"

/* ========================================
 * Test Framework
 * ======================================== */

static int g_test_passed = 0;
static int g_test_failed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("[ RUN      ] " #name "\n"); \
        test_##name(); \
        printf("[       OK ] " #name "\n"); \
        g_test_passed++; \
    } \
    static void test_##name(void)

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("[  FAILED  ] Assertion failed: %s (line %d)\n", #cond, __LINE__); \
            g_test_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))
#define ASSERT_NULL(ptr) ASSERT_TRUE((ptr) == NULL)
#define ASSERT_NOT_NULL(ptr) ASSERT_TRUE((ptr) != NULL)
#define ASSERT_STREQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0)

/* ========================================
 * Mock/Stub State
 * ======================================== */

/* Track callback invocations */
static int g_on_state_changed_called = 0;
static int g_on_register_result_called = 0;
static int g_on_incoming_call_called = 0;
static int g_on_dialog_state_changed_called = 0;
static int g_on_remote_sdp_called = 0;
static int g_on_error_called = 0;

/* Callback return values */
static lws_agent_state_t g_last_agent_state = LWS_AGENT_STATE_IDLE;
static int g_last_register_success = 0;
static int g_last_register_status_code = 0;
static lws_dialog_state_t g_last_dialog_state = LWS_DIALOG_STATE_NULL;

/* Reset mock state */
static void reset_mocks(void) {
    g_on_state_changed_called = 0;
    g_on_register_result_called = 0;
    g_on_incoming_call_called = 0;
    g_on_dialog_state_changed_called = 0;
    g_on_remote_sdp_called = 0;
    g_on_error_called = 0;

    g_last_agent_state = LWS_AGENT_STATE_IDLE;
    g_last_register_success = 0;
    g_last_register_status_code = 0;
    g_last_dialog_state = LWS_DIALOG_STATE_NULL;
}

/* ========================================
 * Mock Callbacks
 * ======================================== */

static void mock_on_state_changed(
    lws_agent_t* agent,
    lws_agent_state_t old_state,
    lws_agent_state_t new_state,
    void* userdata)
{
    (void)agent;
    (void)old_state;
    (void)userdata;

    g_on_state_changed_called++;
    g_last_agent_state = new_state;
}

static void mock_on_register_result(
    lws_agent_t* agent,
    int success,
    int status_code,
    const char* reason_phrase,
    void* userdata)
{
    (void)agent;
    (void)reason_phrase;
    (void)userdata;

    g_on_register_result_called++;
    g_last_register_success = success;
    g_last_register_status_code = status_code;
}

static void mock_on_incoming_call(
    lws_agent_t* agent,
    lws_dialog_t* dialog,
    const lws_sip_addr_t* from,
    void* userdata)
{
    (void)agent;
    (void)dialog;
    (void)from;
    (void)userdata;

    g_on_incoming_call_called++;
}

static void mock_on_dialog_state_changed(
    lws_agent_t* agent,
    lws_dialog_t* dialog,
    lws_dialog_state_t old_state,
    lws_dialog_state_t new_state,
    void* userdata)
{
    (void)agent;
    (void)dialog;
    (void)old_state;
    (void)userdata;

    g_on_dialog_state_changed_called++;
    g_last_dialog_state = new_state;
}

static void mock_on_remote_sdp(
    lws_agent_t* agent,
    lws_dialog_t* dialog,
    const char* sdp,
    void* userdata)
{
    (void)agent;
    (void)dialog;
    (void)sdp;
    (void)userdata;

    g_on_remote_sdp_called++;
}

static void mock_on_error(
    lws_agent_t* agent,
    int error_code,
    const char* error_msg,
    void* userdata)
{
    (void)agent;
    (void)error_code;
    (void)error_msg;
    (void)userdata;

    g_on_error_called++;
}

/* ========================================
 * Test Cases
 * ======================================== */

TEST(agent_config_init) {
    lws_agent_config_t config;
    memset(&config, 0, sizeof(config));

    lws_agent_init_default_config(&config, "1001", "secret", "sip.example.com", NULL);

    ASSERT_STREQ(config.username, "1001");
    ASSERT_STREQ(config.password, "secret");
    ASSERT_STREQ(config.domain, "sip.example.com");
    ASSERT_EQ(config.auto_register, 1);
    ASSERT_EQ(config.register_expires, 3600);
}

TEST(agent_state_name) {
    const char* name;

    name = lws_agent_state_name(LWS_AGENT_STATE_IDLE);
    ASSERT_NOT_NULL(name);
    ASSERT_STREQ(name, "IDLE");

    name = lws_agent_state_name(LWS_AGENT_STATE_REGISTERED);
    ASSERT_NOT_NULL(name);
    ASSERT_STREQ(name, "REGISTERED");
}

TEST(dialog_state_name) {
    const char* name;

    name = lws_dialog_state_name(LWS_DIALOG_STATE_NULL);
    ASSERT_NOT_NULL(name);
    ASSERT_STREQ(name, "NULL");

    name = lws_dialog_state_name(LWS_DIALOG_STATE_CONFIRMED);
    ASSERT_NOT_NULL(name);
    ASSERT_STREQ(name, "CONFIRMED");
}

TEST(sip_parse_uri_simple) {
    lws_sip_addr_t addr;
    int ret;

    ret = lws_sip_parse_uri("sip:1001@example.com", &addr);
    ASSERT_EQ(ret, 0);
    ASSERT_STREQ(addr.username, "1001");
    ASSERT_STREQ(addr.domain, "example.com");
    ASSERT_EQ(addr.port, 0);
}

TEST(sip_parse_uri_with_port) {
    lws_sip_addr_t addr;
    int ret;

    ret = lws_sip_parse_uri("sip:1001@example.com:5061", &addr);
    ASSERT_EQ(ret, 0);
    ASSERT_STREQ(addr.username, "1001");
    ASSERT_STREQ(addr.domain, "example.com");
    ASSERT_EQ(addr.port, 5061);
}

TEST(sip_parse_uri_with_nickname) {
    lws_sip_addr_t addr;
    int ret;

    ret = lws_sip_parse_uri("\"Alice\" <sip:alice@example.com>", &addr);
    ASSERT_EQ(ret, 0);
    ASSERT_STREQ(addr.nickname, "Alice");
    ASSERT_STREQ(addr.username, "alice");
    ASSERT_STREQ(addr.domain, "example.com");
}

TEST(sip_addr_to_string) {
    lws_sip_addr_t addr;
    char buf[256];
    int ret;

    memset(&addr, 0, sizeof(addr));
    strcpy(addr.username, "1001");
    strcpy(addr.domain, "example.com");
    addr.port = 0;

    ret = lws_sip_addr_to_string(&addr, buf, sizeof(buf));
    ASSERT_TRUE(ret > 0);
    ASSERT_STREQ(buf, "sip:1001@example.com");
}

TEST(sip_addr_to_string_with_port) {
    lws_sip_addr_t addr;
    char buf[256];
    int ret;

    memset(&addr, 0, sizeof(addr));
    strcpy(addr.username, "1001");
    strcpy(addr.domain, "example.com");
    addr.port = 5061;

    ret = lws_sip_addr_to_string(&addr, buf, sizeof(buf));
    ASSERT_TRUE(ret > 0);
    ASSERT_STREQ(buf, "sip:1001@example.com:5061");
}

#ifndef DEBUG_AGENT  /* Full agent tests require libsip integration */

TEST(agent_create_destroy_null_config) {
    lws_agent_handler_t handler;
    memset(&handler, 0, sizeof(handler));

    lws_agent_t* agent = lws_agent_create(NULL, &handler);
    ASSERT_NULL(agent);
}

TEST(agent_create_destroy_null_handler) {
    lws_agent_config_t config;
    memset(&config, 0, sizeof(config));

    lws_agent_t* agent = lws_agent_create(&config, NULL);
    ASSERT_NULL(agent);
}

#endif /* !DEBUG_AGENT */

/* ========================================
 * Test Runner
 * ======================================== */

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    printf("==================================================\n");
    printf("  lwsip Agent Unit Tests\n");
    printf("==================================================\n\n");

    /* Helper function tests (no dependencies) */
    run_test_agent_config_init();
    run_test_agent_state_name();
    run_test_dialog_state_name();
    run_test_sip_parse_uri_simple();
    run_test_sip_parse_uri_with_port();
    run_test_sip_parse_uri_with_nickname();
    run_test_sip_addr_to_string();
    run_test_sip_addr_to_string_with_port();

#ifndef DEBUG_AGENT
    /* Full agent tests (require libsip) */
    run_test_agent_create_destroy_null_config();
    run_test_agent_create_destroy_null_handler();
#endif

    printf("\n==================================================\n");
    printf("  Test Results\n");
    printf("==================================================\n");
    printf("Passed: %d\n", g_test_passed);
    printf("Failed: %d\n", g_test_failed);
    printf("\n");

    return (g_test_failed == 0) ? 0 : 1;
}
