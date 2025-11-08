/**
 * @file callee.c
 * @brief Test UAS (callee) using only lws_agent (simplified version)
 *
 * This test program:
 * - Registers as user 1000
 * - Waits for incoming call from 1001
 * - Auto-answers incoming calls (without media)
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
#include <signal.h>
#include <execinfo.h>

/* ========================================
 * Configuration
 * ======================================== */

#define SIP_SERVER "127.0.0.1:5060"
#define USERNAME "1000"
#define PASSWORD "1234"
#define WAIT_TIME_SEC 30

/* ========================================
 * Global State
 * ======================================== */

typedef struct {
    lws_agent_t* agent;
    lws_dialog_t* dialog;
    int registered;
    int call_received;
    int running;
    lws_thread_t* loop_thread;
} callee_ctx_t;

static callee_ctx_t g_ctx = {0};

/* ========================================
 * Signal Handler for Backtrace
 * ======================================== */

static void signal_handler(int sig)
{
    void* callstack[128];
    int frames;
    char** strs;
    int i;
    int skip_frames = 0;

    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "SIGNAL CAUGHT: %d (%s)\n", sig,
            sig == SIGABRT ? "SIGABRT" :
            sig == SIGSEGV ? "SIGSEGV" : "UNKNOWN");
    fprintf(stderr, "========================================\n\n");

    /* Get the backtrace */
    frames = backtrace(callstack, 128);
    strs = backtrace_symbols(callstack, frames);

    if (strs == NULL) {
        fprintf(stderr, "ERROR: Failed to get backtrace symbols\n");
        exit(1);
    }

    /* Print the full backtrace with annotations */
    fprintf(stderr, "Full Backtrace (newest to oldest):\n");
    fprintf(stderr, "----------------------------------------\n");
    for (i = 0; i < frames; i++) {
        /* Mark signal handling frames */
        if (strstr(strs[i], "signal_handler") ||
            strstr(strs[i], "_sigtramp") ||
            strstr(strs[i], "pthread_kill") ||
            strstr(strs[i], "abort") ||
            strstr(strs[i], "err")) {
            fprintf(stderr, "#%-2d [SIGNAL] %s\n", i, strs[i]);
            if (i > skip_frames) skip_frames = i + 1;
        } else {
            fprintf(stderr, "#%-2d [APP   ] %s\n", i, strs[i]);
        }
    }

    /* Print application-only backtrace */
    fprintf(stderr, "\n");
    fprintf(stderr, "Application Call Stack (without signal handling):\n");
    fprintf(stderr, "----------------------------------------\n");
    for (i = skip_frames; i < frames; i++) {
        fprintf(stderr, "#%-2d %s\n", i - skip_frames, strs[i]);
    }
    fprintf(stderr, "----------------------------------------\n\n");

    /* Print assertion location hint */
    fprintf(stderr, "NOTE: Look for 'sip_uas_input' or similar in the stack above\n");
    fprintf(stderr, "      This indicates where the assertion failed.\n\n");

    free(strs);

    /* Re-raise the signal with default handler */
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ========================================
 * SIP Agent Callbacks
 * ======================================== */

static void agent_on_register_result(lws_agent_t* agent, int success,
                                      int status_code, const char* reason_phrase,
                                      void* userdata)
{
    printf("[CALLEE_AGENT] Registration result: %s (code=%d, reason=%s)\n",
           success ? "SUCCESS" : "FAILED", status_code, reason_phrase);

    if (success) {
        g_ctx.registered = 1;
        printf("[CALLEE] Waiting for incoming calls...\n");
    } else {
        printf("[CALLEE] ERROR: Registration failed, cannot receive calls\n");
    }

    (void)agent;
    (void)userdata;
}

static void agent_on_incoming_call(lws_agent_t* agent, lws_dialog_t* dialog,
                                    const lws_sip_addr_t* from, void* userdata)
{
    printf("[CALLEE_AGENT] Incoming call from %s@%s\n",
           from->username, from->domain);

    g_ctx.call_received = 1;
    g_ctx.dialog = dialog;

    printf("[CALLEE] Auto-answering call (without media)...\n");

    // Answer call with minimal SDP (no media)
    const char* sdp =
        "v=0\r\n"
        "o=- 0 0 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "c=IN IP4 127.0.0.1\r\n"
        "t=0 0\r\n";

    if (lws_agent_answer_call(agent, dialog, sdp) != 0) {
        printf("[CALLEE] ERROR: Failed to answer call\n");
    } else {
        printf("[CALLEE] Call answered successfully\n");
    }

    (void)userdata;
}

static void agent_on_dialog_state_changed(lws_agent_t* agent, lws_dialog_t* dialog,
                                           lws_dialog_state_t old_state,
                                           lws_dialog_state_t new_state,
                                           void* userdata)
{
    printf("[CALLEE_AGENT] Dialog state: %s -> %s\n",
           lws_dialog_state_name(old_state),
           lws_dialog_state_name(new_state));

    if (new_state == LWS_DIALOG_STATE_TERMINATED) {
        printf("[CALLEE] Call terminated\n");
        g_ctx.call_received = 0;
    }

    (void)agent;
    (void)dialog;
    (void)userdata;
}

static void agent_on_remote_sdp(lws_agent_t* agent, lws_dialog_t* dialog,
                                 const char* sdp, void* userdata)
{
    printf("[CALLEE_AGENT] Received remote SDP (%zu bytes)\n", strlen(sdp));
    printf("[CALLEE_AGENT] --- BEGIN REMOTE SDP ---\n%s\n[CALLEE_AGENT] --- END REMOTE SDP ---\n", sdp);

    printf("[CALLEE] Remote SDP received (media session not implemented in this test)\n");

    (void)agent;
    (void)dialog;
    (void)userdata;
}

/* ========================================
 * Main Event Loop Thread
 * ======================================== */

static void* event_loop_thread(void* arg)
{
    printf("[CALLEE_THREAD] Event loop started\n");

    while (g_ctx.running) {
        // Process SIP agent (transport is handled internally by agent)
        if (g_ctx.agent) {
            lws_agent_loop(g_ctx.agent, 10);
        }

        // Sleep 10ms
        lws_thread_sleep(10);
    }

    printf("[CALLEE_THREAD] Event loop stopped\n");
    return NULL;

    (void)arg;
}

/* ========================================
 * Main Function
 * ======================================== */

int main(void)
{
    /* Install signal handler for backtrace */
    signal(SIGABRT, signal_handler);
    signal(SIGSEGV, signal_handler);

    printf("========================================\n");
    printf("CALLEE (UAS) Test Program - Simplified\n");
    printf("========================================\n");
    printf("Username: %s\n", USERNAME);
    printf("SIP Server: %s\n", SIP_SERVER);
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
    snprintf(agent_cfg.user_agent, sizeof(agent_cfg.user_agent), "lwsip-callee/2.0");

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

    // Wait for registration
    printf("Waiting for registration...\n");
    int wait_count = 0;
    while (!g_ctx.registered && wait_count < 50) {
        lws_thread_sleep(100);  // 100ms
        wait_count++;
    }

    if (!g_ctx.registered) {
        printf("\n========================================\n");
        printf("Registration timeout or failed\n");
        printf("========================================\n\n");

        // Cleanup
        g_ctx.running = 0;
        if (g_ctx.loop_thread) {
            lws_thread_join(g_ctx.loop_thread, NULL);
        }
        if (g_ctx.agent) {
            lws_agent_stop(g_ctx.agent);
            lws_agent_destroy(g_ctx.agent);
        }
        return 1;
    }

    printf("\n========================================\n");
    printf("Registered successfully!\n");
    printf("Waiting for incoming call (max %d seconds)...\n", WAIT_TIME_SEC);
    printf("========================================\n\n");

    // Wait for incoming call
    wait_count = 0;
    while (!g_ctx.call_received && wait_count < WAIT_TIME_SEC * 10) {
        lws_thread_sleep(100);  // 100ms
        wait_count++;
    }

    if (g_ctx.call_received) {
        printf("\n========================================\n");
        printf("Call received and answered!\n");
        printf("Maintaining call for 15 seconds...\n");
        printf("========================================\n\n");

        // Keep call for a while
        lws_thread_sleep(15000);

        printf("\n========================================\n");
        printf("Call duration completed\n");
        printf("========================================\n\n");
    } else {
        printf("\n========================================\n");
        printf("No incoming call received within timeout\n");
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
    printf("CALLEE Test Completed\n");
    printf("========================================\n\n");

    return 0;
}
