/**
 * @file caller.c
 * @brief Test UAC (caller) using only lws_agent (simplified version)
 *
 * This test program:
 * - Registers as user 1001
 * - Makes call to 1000
 * - Only tests SIP protocol layer (no transport or media session)
 * - Agent internally manages transport layer
 */

#include "../include/lws_agent.h"
#include "../osal/include/lws_mem.h"
#include "../osal/include/lws_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ========================================
 * Configuration
 * ======================================== */

#define SIP_SERVER "127.0.0.1:5060"
#define USERNAME "1001"
#define PASSWORD "1234"
#define CALLEE_URI "sip:1000@127.0.0.1"
#define CALL_DURATION_SEC 10

/* ========================================
 * Global State
 * ======================================== */

typedef struct {
    lws_agent_t* agent;
    lws_dialog_t* dialog;
    int registered;
    int call_initiated;
    int running;
    lws_thread_t* loop_thread;
} caller_ctx_t;

static caller_ctx_t g_ctx = {0};

/* ========================================
 * SIP Agent Callbacks
 * ======================================== */

static void agent_on_register_result(lws_agent_t* agent, int success,
                                      int status_code, const char* reason_phrase,
                                      void* userdata)
{
    printf("[CALLER_AGENT] Registration result: %s (code=%d, reason=%s)\n",
           success ? "SUCCESS" : "FAILED", status_code, reason_phrase);

    if (success) {
        g_ctx.registered = 1;

        // Make call after successful registration (with minimal SDP)
        printf("[CALLER] Making call to %s...\n", CALLEE_URI);

        const char* sdp =
            "v=0\r\n"
            "o=- 0 0 IN IP4 127.0.0.1\r\n"
            "s=-\r\n"
            "c=IN IP4 127.0.0.1\r\n"
            "t=0 0\r\n";

        g_ctx.dialog = lws_agent_make_call(agent, CALLEE_URI, sdp);
        if (g_ctx.dialog) {
            printf("[CALLER] Call initiated successfully\n");
            g_ctx.call_initiated = 1;
        } else {
            printf("[CALLER] ERROR: Failed to initiate call\n");
        }
    } else {
        printf("[CALLER] ERROR: Registration failed, cannot make call\n");
    }

    (void)agent;
    (void)userdata;
}

static void agent_on_incoming_call(lws_agent_t* agent, lws_dialog_t* dialog,
                                    const lws_sip_addr_t* from, void* userdata)
{
    printf("[CALLER_AGENT] Unexpected incoming call from %s@%s (rejecting)\n",
           from->username, from->domain);

    // Caller should not receive incoming calls in this test
    lws_agent_hangup(agent, dialog);

    (void)userdata;
}

static void agent_on_dialog_state_changed(lws_agent_t* agent, lws_dialog_t* dialog,
                                           lws_dialog_state_t old_state,
                                           lws_dialog_state_t new_state,
                                           void* userdata)
{
    printf("[CALLER_AGENT] Dialog state: %s -> %s\n",
           lws_dialog_state_name(old_state),
           lws_dialog_state_name(new_state));

    if (new_state == LWS_DIALOG_STATE_CONFIRMED) {
        printf("[CALLER] Call established!\n");
    } else if (new_state == LWS_DIALOG_STATE_TERMINATED) {
        printf("[CALLER] Call terminated\n");
        g_ctx.call_initiated = 0;
    }

    (void)agent;
    (void)dialog;
    (void)userdata;
}

static void agent_on_remote_sdp(lws_agent_t* agent, lws_dialog_t* dialog,
                                 const char* sdp, void* userdata)
{
    printf("[CALLER_AGENT] Received remote SDP (%zu bytes)\n", strlen(sdp));
    printf("[CALLER_AGENT] --- BEGIN REMOTE SDP ---\n%s\n[CALLER_AGENT] --- END REMOTE SDP ---\n", sdp);

    printf("[CALLER] Remote SDP received (media session not implemented in this test)\n");

    (void)agent;
    (void)dialog;
    (void)userdata;
}

/* ========================================
 * Main Event Loop Thread
 * ======================================== */

static void* event_loop_thread(void* arg)
{
    printf("[CALLER_THREAD] Event loop started\n");

    while (g_ctx.running) {
        // Process SIP agent (transport is handled internally by agent)
        if (g_ctx.agent) {
            lws_agent_loop(g_ctx.agent, 10);
        }

        // Sleep 10ms
        lws_thread_sleep(10);
    }

    printf("[CALLER_THREAD] Event loop stopped\n");
    return NULL;

    (void)arg;
}

/* ========================================
 * Main Function
 * ======================================== */

int main(void)
{
    printf("========================================\n");
    printf("CALLER (UAC) Test Program - Simplified\n");
    printf("========================================\n");
    printf("Username: %s\n", USERNAME);
    printf("SIP Server: %s\n", SIP_SERVER);
    printf("Callee: %s\n", CALLEE_URI);
    printf("Note: Only SIP protocol testing (no media)\n");
    printf("========================================\n\n");

    // Initialize SIP agent
    printf("[1/3] Initializing SIP agent...\n");
    lws_agent_config_t agent_cfg;
    memset(&agent_cfg, 0, sizeof(agent_cfg));
    snprintf(agent_cfg.username, sizeof(agent_cfg.username), "%s", USERNAME);
    snprintf(agent_cfg.password, sizeof(agent_cfg.password), "%s", PASSWORD);
    snprintf(agent_cfg.registrar, sizeof(agent_cfg.registrar), "%s", SIP_SERVER);
    agent_cfg.registrar_port = 5060;
    snprintf(agent_cfg.user_agent, sizeof(agent_cfg.user_agent), "lwsip-caller/2.0");

    lws_agent_handler_t agent_handler = {
        .on_register_result = agent_on_register_result,
        .on_incoming_call = agent_on_incoming_call,
        .on_dialog_state_changed = agent_on_dialog_state_changed,
        .on_remote_sdp = agent_on_remote_sdp,
        .userdata = NULL,
    };

    g_ctx.agent = lws_agent_create(&agent_cfg, &agent_handler);
    if (!g_ctx.agent) {
        printf("ERROR: Failed to create SIP agent\n");
        return 1;
    }
    printf("  SIP agent created successfully\n\n");

    // Start event loop thread
    printf("[2/3] Starting event loop thread...\n");
    g_ctx.running = 1;
    g_ctx.loop_thread = lws_thread_create(event_loop_thread, NULL);
    if (!g_ctx.loop_thread) {
        printf("ERROR: Failed to create event loop thread\n");
        lws_agent_destroy(g_ctx.agent);
        return 1;
    }
    printf("  Event loop thread started\n\n");

    // Start registration
    printf("[3/3] Starting SIP registration...\n");
    if (lws_agent_start(g_ctx.agent) != 0) {
        printf("ERROR: Failed to start registration\n");
        g_ctx.running = 0;
        lws_thread_join(g_ctx.loop_thread, NULL);
        lws_agent_destroy(g_ctx.agent);
        return 1;
    }
    printf("  Registration started\n\n");

    // Wait for call establishment
    printf("Waiting for call establishment...\n");
    int wait_count = 0;
    while (!g_ctx.call_initiated && wait_count < 100) {
        lws_thread_sleep(100);  // 100ms
        wait_count++;
    }

    if (g_ctx.call_initiated) {
        printf("\n========================================\n");
        printf("Call initiated! Maintaining call for %d seconds...\n", CALL_DURATION_SEC);
        printf("========================================\n\n");

        // Keep call for specified duration
        lws_thread_sleep(CALL_DURATION_SEC * 1000);

        // Hangup
        printf("\n========================================\n");
        printf("Hanging up...\n");
        printf("========================================\n\n");

        if (g_ctx.dialog) {
            lws_agent_hangup(g_ctx.agent, g_ctx.dialog);
        }

        // Wait for hangup to complete
        lws_thread_sleep(1000);
    } else {
        printf("\n========================================\n");
        printf("Call establishment timeout or failed\n");
        printf("========================================\n\n");
    }

    // Cleanup
    printf("Cleaning up...\n");

    // Stop event loop
    g_ctx.running = 0;
    if (g_ctx.loop_thread) {
        lws_thread_join(g_ctx.loop_thread, NULL);
    }

    // Stop SIP agent
    if (g_ctx.agent) {
        lws_agent_stop(g_ctx.agent);
        lws_agent_destroy(g_ctx.agent);
    }

    printf("\n========================================\n");
    printf("CALLER Test Completed\n");
    printf("========================================\n\n");

    return 0;
}
