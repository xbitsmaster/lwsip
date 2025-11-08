/**
 * @file lwsip_cli.c
 * @brief lwsip command line interface
 */

#include "../include/lws_client.h"
#include "../include/lws_uac.h"
#include "../include/lws_uas.h"
#include "../include/lws_media.h"
#include "../include/lws_session.h"
#include "../include/lws_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

/* ============================================================
 * Global Variables
 * ============================================================ */

static lws_client_t* g_client = NULL;
static lws_session_t* g_session = NULL;
static int g_running = 1;

/* ============================================================
 * Client Callbacks
 * ============================================================ */

static void on_reg_state(void* param, lws_reg_state_t state, int code)
{
    const char* state_str[] = {
        "NONE", "REGISTERING", "REGISTERED", "UNREGISTERED", "FAILED"
    };
    printf("[REG] %s (code: %d)\n", state_str[state], code);
}

static void on_call_state(void* param, const char* peer, lws_call_state_t state)
{
    const char* state_str[] = {
        "IDLE", "CALLING", "RINGING", "ANSWERED", "ESTABLISHED",
        "HANGUP", "FAILED", "TERMINATED"
    };
    printf("[CALL] %s - %s\n", peer ? peer : "unknown", state_str[state]);
}

static void on_incoming_call(void* param, const char* from, const char* to,
                             const char* sdp, int sdp_len)
{
    printf("[INCOMING] %s -> %s (SDP: %d bytes)\n", from, to, sdp_len);
    printf("Type 'answer' to accept or 'reject' to decline\n");
}

static void on_error(void* param, int errcode, const char* description)
{
    printf("[ERROR] 0x%08x: %s\n", errcode, description);
}

/* ============================================================
 * Signal Handler
 * ============================================================ */

static void signal_handler(int sig)
{
    printf("\nReceived signal %d, shutting down...\n", sig);
    g_running = 0;
}

/* ============================================================
 * Command Processing
 * ============================================================ */

static void process_command(const char* cmd)
{
    char command[128] = {0};
    char arg[256] = {0};

    sscanf(cmd, "%127s %255s", command, arg);

    if (strcmp(command, "call") == 0) {
        if (strlen(arg) > 0) {
            printf("Calling %s...\n", arg);
            g_session = lws_call(g_client, arg);
        } else {
            printf("Usage: call <uri>\n");
        }
    }
    else if (strcmp(command, "answer") == 0) {
        if (g_client && g_session == NULL) {
            g_session = lws_answer(g_client, arg[0] ? arg : NULL);
            printf("Answered call\n");
        } else {
            printf("No incoming call\n");
        }
    }
    else if (strcmp(command, "reject") == 0) {
        printf("Rejected call\n");
        lws_uas_reject(g_client, arg[0] ? arg : NULL, 486);
    }
    else if (strcmp(command, "hangup") == 0) {
        if (g_session) {
            lws_hangup(g_client, g_session);
            g_session = NULL;
            printf("Hung up\n");
        } else {
            printf("No active call\n");
        }
    }
    else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
        g_running = 0;
    }
    else if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
        printf("Commands:\n");
        printf("  call <uri>     - Make a call\n");
        printf("  answer         - Answer incoming call\n");
        printf("  reject         - Reject incoming call\n");
        printf("  hangup         - Hang up current call\n");
        printf("  quit/exit      - Exit program\n");
        printf("  help/?         - Show this help\n");
    }
    else {
        printf("Unknown command: %s (type 'help' for commands)\n", command);
    }
}

/* ============================================================
 * Main
 * ============================================================ */

int main(int argc, char* argv[])
{
    lws_config_t config;
    lws_client_handler_t handler;
    int ret;

    // Parse command line arguments
    if (argc < 3) {
        printf("Usage: %s <server> <username> [password]\n", argv[0]);
        printf("Example: %s 192.168.1.100 1002 1234\n", argv[0]);
        return 1;
    }

    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Configure client
    memset(&config, 0, sizeof(config));

    // Parse server address (support both "host" and "host:port" formats)
    const char* colon = strchr(argv[1], ':');
    if (colon) {
        // Format: host:port
        size_t host_len = colon - argv[1];
        if (host_len >= sizeof(config.server_host)) {
            host_len = sizeof(config.server_host) - 1;
        }
        strncpy(config.server_host, argv[1], host_len);
        config.server_host[host_len] = '\0';
        config.server_port = atoi(colon + 1);
    } else {
        // Format: host (use default port)
        strncpy(config.server_host, argv[1], sizeof(config.server_host) - 1);
        config.server_port = 5060;
    }

    strncpy(config.username, argv[2], sizeof(config.username) - 1);
    if (argc >= 4) {
        strncpy(config.password, argv[3], sizeof(config.password) - 1);
    } else {
        strcpy(config.password, config.username);  // Use username as password
    }
    config.local_port = 0;  // Auto
    config.expires = 300;
    config.use_tcp = 0;  // UDP
    config.enable_audio = 1;
    config.enable_video = 0;
    config.audio_codec = LWS_AUDIO_CODEC_PCMU;

    // Setup callbacks
    memset(&handler, 0, sizeof(handler));
    handler.on_reg_state = on_reg_state;
    handler.on_call_state = on_call_state;
    handler.on_incoming_call = on_incoming_call;
    handler.on_error = on_error;

    // Create client
    printf("Creating SIP client...\n");
    g_client = lws_client_create(&config, &handler);
    if (!g_client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    // Start client
    printf("Starting client...\n");
    ret = lws_client_start(g_client);
    if (ret != LWS_OK) {
        fprintf(stderr, "Failed to start client: 0x%08x\n", ret);
        lws_client_destroy(g_client);
        return 1;
    }

    // Register
    printf("Registering as %s@%s...\n", config.username, config.server_host);
    lws_uac_register(g_client);

    printf("\nlwsip ready. Type 'help' for commands.\n");
    printf("> ");
    fflush(stdout);

    // Main loop
    while (g_running) {
        // Process SIP/RTP events (non-blocking)
        ret = lws_client_loop(g_client, 100);  // 100ms timeout
        if (ret < 0) {
            fprintf(stderr, "Client loop error: 0x%08x\n", ret);
            break;
        }

        // Check for user input (non-blocking)
        fd_set rfds;
        struct timeval tv = {0, 0};  // No wait
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);

        if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0) {
            char input[512];
            if (fgets(input, sizeof(input), stdin)) {
                // Remove newline
                input[strcspn(input, "\n")] = 0;

                if (strlen(input) > 0) {
                    process_command(input);
                }

                printf("> ");
                fflush(stdout);
            }
        }
    }

    // Cleanup
    printf("\nCleaning up...\n");
    if (g_session) {
        lws_hangup(g_client, g_session);
    }
    lws_client_stop(g_client);
    lws_client_destroy(g_client);

    printf("Goodbye!\n");
    return 0;
}
