/**
 * @file lwsip-cli.c
 * @brief lwsip command-line SIP client
 *
 * A simple command-line tool demonstrating lwsip library usage:
 * - Register to SIP server
 * - Make outgoing calls or receive incoming calls
 * - Handle audio/video media sessions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>

#include "../include/lwsip.h"
#include "../include/lws_timer.h"

/* ========================================
 * Global State
 * ======================================== */

typedef struct {
    /* Configuration */
    char device_path[256];
    char server_addr[256];
    char username[64];
    char password[64];
    char call_target[64];
    int has_call_target;

    /* Runtime objects */
    lws_trans_t* trans;
    lws_dev_t* audio_capture;
    lws_dev_t* audio_playback;
    lws_sess_t* sess;
    lws_agent_t* agent;

    /* State */
    int registered;
    int in_call;
    int running;
} app_context_t;

static app_context_t g_app = {0};

/* ========================================
 * Signal Handler
 * ======================================== */

static void signal_handler(int sig) {
    (void)sig;
    printf("\n[CLI] Received signal, shutting down...\n");
    g_app.running = 0;
}

/* ========================================
 * Callbacks
 * ======================================== */

/**
 * @brief Agent state change callback
 */
static void on_agent_state_changed(
    lws_agent_t* agent,
    lws_agent_state_t old_state,
    lws_agent_state_t new_state,
    void* userdata
) {
    (void)agent;
    (void)userdata;

    printf("[CLI] Agent state: %d -> %d\n", old_state, new_state);

    if (new_state == LWS_AGENT_STATE_REGISTERED) {
        g_app.registered = 1;
        printf("[CLI] ✓ Registered successfully\n");

        /* If we have a call target, make the call */
        if (g_app.has_call_target) {
            printf("[CLI] Making call to: %s\n", g_app.call_target);
            lws_dialog_t* dialog = lws_agent_make_call(g_app.agent, g_app.call_target);
            if (!dialog) {
                fprintf(stderr, "[CLI] Failed to make call\n");
            }
        } else {
            printf("[CLI] Waiting for incoming calls (press Ctrl+C to quit)...\n");
        }
    }
}

/**
 * @brief Incoming call callback
 */
static void on_incoming_call(
    lws_agent_t* agent,
    lws_dialog_t* dialog,
    const lws_sip_addr_t* from,
    void* userdata
) {
    (void)agent;
    (void)dialog;
    (void)userdata;

    printf("[CLI] Incoming call from: %s@%s\n", from->username, from->domain);

    /* Auto-answer incoming calls */
    printf("[CLI] Auto-answering call...\n");
    /* Note: Need to implement answer logic with actual API */
    g_app.in_call = 1;
}

/**
 * @brief Dialog state change callback
 */
static void on_dialog_state_changed(
    lws_agent_t* agent,
    lws_dialog_t* dialog,
    lws_dialog_state_t old_state,
    lws_dialog_state_t new_state,
    void* userdata
) {
    (void)agent;
    (void)dialog;
    (void)userdata;

    printf("[CLI] Dialog state: %d -> %d\n", old_state, new_state);

    if (new_state == LWS_DIALOG_STATE_CONFIRMED) {
        printf("[CLI] Call connected!\n");
        g_app.in_call = 1;
    } else if (new_state == LWS_DIALOG_STATE_TERMINATED) {
        printf("[CLI] Call terminated\n");
        g_app.in_call = 0;

        /* If we made the call, quit after it ends */
        if (g_app.has_call_target) {
            printf("[CLI] Call ended, exiting...\n");
            g_app.running = 0;
        } else {
            printf("[CLI] Ready for next call\n");
        }
    }
}

/**
 * @brief Remote SDP callback
 */
static void on_remote_sdp(
    lws_agent_t* agent,
    lws_dialog_t* dialog,
    const char* sdp,
    void* userdata
) {
    (void)agent;
    (void)dialog;
    (void)userdata;

    printf("[CLI] Received remote SDP (%zu bytes)\n", strlen(sdp));

    /* Start media session */
    if (g_app.sess) {
        printf("[CLI] Starting media session...\n");
        lws_sess_set_remote_sdp(g_app.sess, sdp);
        lws_sess_start_ice(g_app.sess);
    }
}

/**
 * @brief Agent error callback
 */
static void on_agent_error(
    lws_agent_t* agent,
    int error_code,
    const char* error_msg,
    void* userdata
) {
    (void)agent;
    (void)userdata;

    printf("[CLI] Agent error: %d - %s\n", error_code, error_msg);
}

/**
 * @brief Media session state change callback
 */
static void on_sess_state_changed(
    lws_sess_t* sess,
    lws_sess_state_t old_state,
    lws_sess_state_t new_state,
    void* userdata
) {
    (void)sess;
    (void)userdata;

    printf("[CLI] Media session state: %d -> %d\n", old_state, new_state);

    if (new_state == LWS_SESS_STATE_CONNECTED) {
        printf("[CLI] ✓ Media session connected!\n");
    }
}

/**
 * @brief SDP ready callback
 */
static void on_sdp_ready(
    lws_sess_t* sess,
    const char* sdp,
    void* userdata
) {
    (void)sess;
    (void)userdata;

    printf("[CLI] Local SDP ready (%zu bytes)\n", strlen(sdp));
    /* SDP will be used by agent when making/answering calls */
}

/**
 * @brief Media session error callback
 */
static void on_sess_error(
    lws_sess_t* sess,
    int error_code,
    const char* error_msg,
    void* userdata
) {
    (void)sess;
    (void)userdata;

    printf("[CLI] Media session error: %d - %s\n", error_code, error_msg);
}

/* ========================================
 * Initialization & Cleanup
 * ======================================== */

/**
 * @brief Detect audio file format from file extension
 * @param filepath File path
 * @param format Output audio format
 * @param sample_rate Output sample rate
 * @param channels Output channels
 * @return 0 on success, -1 on failure
 */
static int detect_audio_file_format(const char* filepath,
                                     lws_audio_format_t* format,
                                     int* sample_rate,
                                     int* channels) {
    const char* ext = strrchr(filepath, '.');
    if (!ext) {
        fprintf(stderr, "[CLI] No file extension found in: %s\n", filepath);
        return -1;
    }

    ext++; /* Skip the dot */

    /* Convert to lowercase for comparison */
    char ext_lower[16] = {0};
    for (int i = 0; ext[i] && i < 15; i++) {
        ext_lower[i] = tolower(ext[i]);
    }

    if (strcmp(ext_lower, "wav") == 0) {
        /* WAV files typically use PCM 16-bit */
        *format = LWS_AUDIO_FMT_PCM_S16LE;
        *sample_rate = 8000;  /* Default to 8kHz, will be overridden by WAV header */
        *channels = 1;
        printf("[CLI] Detected WAV file format (PCM 16-bit)\n");
        return 0;
    } else if (strcmp(ext_lower, "mp4") == 0 || strcmp(ext_lower, "m4a") == 0) {
        /* MP4 files with audio - assume AAC or other codec */
        /* For now, we'll use PCMA as a placeholder */
        /* TODO: Implement proper MP4 audio extraction */
        *format = LWS_AUDIO_FMT_PCMA;
        *sample_rate = 8000;
        *channels = 1;
        printf("[CLI] Detected MP4 file format\n");
        return 0;
    } else {
        fprintf(stderr, "[CLI] Unsupported file format: %s\n", ext);
        fprintf(stderr, "[CLI] Supported formats: .wav, .mp4\n");
        return -1;
    }
}

/**
 * @brief Initialize lwsip components
 */
static int init_lwsip(void) {
    /* Initialize timer system (required by SIP and ICE) */
    if (lws_timer_init() < 0) {
        fprintf(stderr, "[CLI] Failed to initialize timer system\n");
        return -1;
    }

    /* Create transport layer */
    lws_trans_config_t trans_config = {0};
    trans_config.type = LWS_TRANS_TYPE_UDP;
    trans_config.sock.bind_port = 0;  /* Random port */
    trans_config.nonblock = 1;

    /* Transport handler - NULL is acceptable as agent will set its own handler */
    g_app.trans = lws_trans_create(&trans_config, NULL);
    if (!g_app.trans) {
        fprintf(stderr, "[CLI] Failed to create transport\n");
        return -1;
    }

    /* Get and display the assigned port */
    lws_addr_t local_addr;
    if (lws_trans_get_local_addr(g_app.trans, &local_addr) == 0) {
        printf("[CLI] Transport created on port %d\n", local_addr.port);
    } else {
        printf("[CLI] Transport created\n");
    }

    /* Create audio devices */
    lws_dev_config_t dev_config;

    /* Audio capture device (file or real device) */
    if (strlen(g_app.device_path) > 0) {
        /* Use file device */
        lws_dev_init_file_reader_config(&dev_config, g_app.device_path);

        /* Auto-detect audio format from file extension */
        lws_audio_format_t format;
        int sample_rate, channels;
        if (detect_audio_file_format(g_app.device_path, &format, &sample_rate, &channels) < 0) {
            fprintf(stderr, "[CLI] Failed to detect audio file format\n");
            return -1;
        }

        dev_config.audio.format = format;
        dev_config.audio.sample_rate = sample_rate;
        dev_config.audio.channels = channels;
        dev_config.audio.frame_duration_ms = 20;
    } else {
        /* Use real audio device */
        lws_dev_init_audio_capture_config(&dev_config);
        dev_config.device_name = NULL;  /* Default device */
    }

    g_app.audio_capture = lws_dev_create(&dev_config, NULL);
    if (!g_app.audio_capture) {
        fprintf(stderr, "[CLI] Failed to create audio capture device\n");
        return -1;
    }

    if (lws_dev_open(g_app.audio_capture) < 0) {
        fprintf(stderr, "[CLI] Failed to open audio capture device\n");
        return -1;
    }

    if (lws_dev_start(g_app.audio_capture) < 0) {
        fprintf(stderr, "[CLI] Failed to start audio capture device\n");
        return -1;
    }

    printf("[CLI] Audio capture device started\n");

    /* Audio playback device */
    lws_dev_init_audio_playback_config(&dev_config);
    dev_config.device_name = NULL;  /* Default device */

    g_app.audio_playback = lws_dev_create(&dev_config, NULL);
    if (!g_app.audio_playback) {
        fprintf(stderr, "[CLI] Failed to create audio playback device\n");
        return -1;
    }

    if (lws_dev_open(g_app.audio_playback) < 0) {
        fprintf(stderr, "[CLI] Failed to open audio playback device\n");
        return -1;
    }

    if (lws_dev_start(g_app.audio_playback) < 0) {
        fprintf(stderr, "[CLI] Failed to start audio playback device\n");
        return -1;
    }

    printf("[CLI] Audio playback device started\n");

    /* Create media session */
    lws_sess_config_t sess_config;
    lws_sess_init_audio_config(&sess_config, "stun.l.google.com:19302", LWS_RTP_PAYLOAD_PCMA);
    sess_config.audio_capture_dev = g_app.audio_capture;
    sess_config.audio_playback_dev = g_app.audio_playback;

    lws_sess_handler_t sess_handler = {
        .on_state_changed = on_sess_state_changed,
        .on_sdp_ready = on_sdp_ready,
        .on_error = on_sess_error,
        .userdata = NULL
    };

    g_app.sess = lws_sess_create(&sess_config, &sess_handler);
    if (!g_app.sess) {
        fprintf(stderr, "[CLI] Failed to create media session\n");
        return -1;
    }

    printf("[CLI] Media session created\n");

    /* Start ICE candidate gathering */
    if (lws_sess_gather_candidates(g_app.sess) < 0) {
        fprintf(stderr, "[CLI] Failed to start candidate gathering\n");
        return -1;
    }

    printf("[CLI] ICE candidate gathering started...\n");

    /* Parse server address to extract domain and registrar */
    /* Expected format: sip:host:port or host:port */
    char registrar[LWS_MAX_HOSTNAME_LEN] = {0};
    char domain[LWS_MAX_DOMAIN_LEN] = {0};
    uint16_t port = 5060;

    /* Simple parsing: remove "sip:" prefix if present */
    const char* server_str = g_app.server_addr;
    if (strncmp(server_str, "sip:", 4) == 0) {
        server_str += 4;
    }

    /* Extract host and port */
    const char* colon = strchr(server_str, ':');
    if (colon) {
        size_t host_len = colon - server_str;
        if (host_len >= sizeof(registrar)) {
            host_len = sizeof(registrar) - 1;
        }
        strncpy(registrar, server_str, host_len);
        registrar[host_len] = '\0';
        port = (uint16_t)atoi(colon + 1);
    } else {
        strncpy(registrar, server_str, sizeof(registrar) - 1);
    }

    /* Use registrar as domain for now */
    strncpy(domain, registrar, sizeof(domain) - 1);

    /* Create SIP agent */
    lws_agent_config_t agent_config = {0};
    strncpy(agent_config.username, g_app.username, sizeof(agent_config.username) - 1);
    strncpy(agent_config.password, g_app.password, sizeof(agent_config.password) - 1);
    strncpy(agent_config.nickname, g_app.username, sizeof(agent_config.nickname) - 1);
    strncpy(agent_config.domain, domain, sizeof(agent_config.domain) - 1);
    strncpy(agent_config.registrar, registrar, sizeof(agent_config.registrar) - 1);
    agent_config.registrar_port = port;
    agent_config.auto_register = 1;
    agent_config.register_expires = 3600;
    snprintf(agent_config.user_agent, sizeof(agent_config.user_agent), "lwsip-cli/1.0");

    lws_agent_handler_t agent_handler = {
        .on_state_changed = on_agent_state_changed,
        .on_incoming_call = on_incoming_call,
        .on_dialog_state_changed = on_dialog_state_changed,
        .on_remote_sdp = on_remote_sdp,
        .on_error = on_agent_error,
        .userdata = NULL
    };

    g_app.agent = lws_agent_create(&agent_config, &agent_handler);
    if (!g_app.agent) {
        fprintf(stderr, "[CLI] Failed to create SIP agent\n");
        return -1;
    }

    printf("[CLI] SIP agent created\n");

    /* Start agent (will auto-register if configured) */
    printf("[CLI] Starting agent (registering to %s:%d as %s)...\n",
           registrar, port, g_app.username);
    if (lws_agent_start(g_app.agent) < 0) {
        fprintf(stderr, "[CLI] Failed to start agent\n");
        return -1;
    }

    return 0;
}

/**
 * @brief Cleanup lwsip components
 */
static void cleanup_lwsip(void) {
    printf("[CLI] Cleaning up...\n");

    if (g_app.agent) {
        lws_agent_destroy(g_app.agent);
        g_app.agent = NULL;
    }

    if (g_app.sess) {
        lws_sess_destroy(g_app.sess);
        g_app.sess = NULL;
    }

    if (g_app.audio_capture) {
        lws_dev_stop(g_app.audio_capture);
        lws_dev_close(g_app.audio_capture);
        lws_dev_destroy(g_app.audio_capture);
        g_app.audio_capture = NULL;
    }

    if (g_app.audio_playback) {
        lws_dev_stop(g_app.audio_playback);
        lws_dev_close(g_app.audio_playback);
        lws_dev_destroy(g_app.audio_playback);
        g_app.audio_playback = NULL;
    }

    if (g_app.trans) {
        lws_trans_destroy(g_app.trans);
        g_app.trans = NULL;
    }

    /* Cleanup timer system */
    lws_timer_cleanup();

    printf("[CLI] Cleanup complete\n");
}

/* ========================================
 * Command-Line Parsing
 * ======================================== */

static void print_usage(const char* progname) {
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("\nRequired options:\n");
    printf("  -s, --server <addr>    SIP server address (e.g., sip:192.168.1.100:5060)\n");
    printf("  -u, --username <name>  SIP username\n");
    printf("  -p, --password <pwd>   SIP password\n");
    printf("\nOptional options:\n");
    printf("  -d, --device <path>    Audio file for playback (.wav or .mp4)\n");
    printf("                         If not specified, use real microphone\n");
    printf("  -c, --call <target>    Make call to target user\n");
    printf("                         If not specified, wait for incoming calls\n");
    printf("  -h, --help             Show this help message\n");
    printf("\nExamples:\n");
    printf("  # Wait for incoming calls:\n");
    printf("  %s -s sip:192.168.1.100:5060 -u 1001 -p secret\n\n", progname);
    printf("  # Make outgoing call:\n");
    printf("  %s -s sip:192.168.1.100:5060 -u 1001 -p secret -c 1002\n\n", progname);
    printf("  # Use audio file:\n");
    printf("  %s -s sip:192.168.1.100:5060 -u 1001 -p secret -d audio.mp4 -c 1002\n\n", progname);
}

static int parse_args(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"device",   required_argument, 0, 'd'},
        {"server",   required_argument, 0, 's'},
        {"username", required_argument, 0, 'u'},
        {"password", required_argument, 0, 'p'},
        {"call",     required_argument, 0, 'c'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    int option_index = 0;

    while ((c = getopt_long(argc, argv, "d:s:u:p:c:h", long_options, &option_index)) != -1) {
        switch (c) {
            case 'd':
                strncpy(g_app.device_path, optarg, sizeof(g_app.device_path) - 1);
                break;
            case 's':
                strncpy(g_app.server_addr, optarg, sizeof(g_app.server_addr) - 1);
                break;
            case 'u':
                strncpy(g_app.username, optarg, sizeof(g_app.username) - 1);
                break;
            case 'p':
                strncpy(g_app.password, optarg, sizeof(g_app.password) - 1);
                break;
            case 'c':
                strncpy(g_app.call_target, optarg, sizeof(g_app.call_target) - 1);
                g_app.has_call_target = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    /* Validate required arguments */
    if (strlen(g_app.server_addr) == 0) {
        fprintf(stderr, "Error: Server address is required\n\n");
        print_usage(argv[0]);
        return -1;
    }

    if (strlen(g_app.username) == 0) {
        fprintf(stderr, "Error: Username is required\n\n");
        print_usage(argv[0]);
        return -1;
    }

    if (strlen(g_app.password) == 0) {
        fprintf(stderr, "Error: Password is required\n\n");
        print_usage(argv[0]);
        return -1;
    }

    return 0;
}

/* ========================================
 * Main Function
 * ======================================== */

int main(int argc, char* argv[]) {
    printf("===========================================\n");
    printf("  lwsip CLI - SIP Client v1.0\n");
    printf("===========================================\n\n");

    /* Parse command-line arguments */
    if (parse_args(argc, argv) < 0) {
        return 1;
    }

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize lwsip */
    if (init_lwsip() < 0) {
        fprintf(stderr, "[CLI] Initialization failed\n");
        cleanup_lwsip();
        return 1;
    }

    printf("[CLI] Initialization complete\n\n");

    /* Main event loop */
    g_app.running = 1;

    while (g_app.running) {
        /* Run agent event loop */
        if (g_app.agent) {
            lws_agent_loop(g_app.agent, 10);  /* 10ms timeout */
        }

        /* Run session event loop */
        if (g_app.sess && g_app.in_call) {
            lws_sess_loop(g_app.sess, 10);  /* 10ms timeout */
        }

        /* Run transport event loop */
        if (g_app.trans) {
            lws_trans_loop(g_app.trans, 10);  /* 10ms timeout */
        }

        /* Small sleep to avoid busy waiting */
        usleep(1000);  /* 1ms */
    }

    /* Cleanup */
    cleanup_lwsip();

    printf("\n[CLI] Goodbye!\n");

    return 0;
}
