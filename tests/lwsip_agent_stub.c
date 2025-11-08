/**
 * @file lwsip_agent_stub.c
 * @brief Stub implementations for lws_agent unit tests
 *
 * Provides minimal stub implementations of dependencies:
 * - lws_timer (simple stub)
 * - lws_trans (intelligent stub - integrated with trans_stub.c)
 * - lws_sess (simple stub)
 *
 * NOTE: libsip is now linked as a real library, not stubbed!
 * NOTE: http_parser is provided by libhttp.a, not stubbed!
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "trans_stub.h"
#include "lws_trans.h"
#include "lws_sess.h"

/* ========================================
 * Forward declarations (internal trans_stub API)
 * ======================================== */

void trans_stub_set_handler(const lws_trans_handler_t* handler, void* trans);
int trans_stub_handle_send(const void* data, int len, const lws_addr_t* dest);

/* ========================================
 * Timer stub (simple)
 * ======================================== */

int lws_timer_init(void) {
    trans_stub_init();  /* Initialize trans stub */
    return 0;
}

void lws_timer_cleanup(void) {
    trans_stub_cleanup();  /* Cleanup trans stub */
}

/* libsip timer API (required by libsip) */
void* sip_timer_start(int timeout, void (*handler)(void*), void* usrptr) {
    (void)timeout;
    (void)handler;
    (void)usrptr;
    return (void*)0x7777; /* Return dummy timer ID */
}

int sip_timer_stop(void** id) {
    if (id) {
        *id = NULL;
    }
    return 0;
}

/* ========================================
 * Transport stub (intelligent - integrated with trans_stub.c)
 * ======================================== */

lws_trans_t* lws_trans_create(const lws_trans_config_t* config, const lws_trans_handler_t* handler) {
    (void)config;

    /* Store handler for response delivery */
    if (handler) {
        lws_trans_t* trans = (lws_trans_t*)0x3333;  /* Dummy transport pointer */
        trans_stub_set_handler(handler, trans);
        return trans;
    }

    return (lws_trans_t*)0x3333;
}

void lws_trans_destroy(lws_trans_t* trans) {
    (void)trans;
    /* Nothing to do - trans_stub is cleaned up in lws_timer_cleanup */
}

int lws_trans_send(lws_trans_t* trans, const void* data, int len, const lws_addr_t* dest) {
    (void)trans;

    /* Intelligent stub handles the request and generates responses */
    return trans_stub_handle_send(data, len, dest);
}

int lws_trans_loop(lws_trans_t* trans, int timeout_ms) {
    (void)trans;
    (void)timeout_ms;

    /* Process any pending responses */
    trans_stub_process_responses();

    return 0;
}

int lws_trans_get_local_addr(lws_trans_t* trans, lws_addr_t* addr) {
    (void)trans;

    /* Return dummy local address */
    if (addr) {
        strcpy(addr->ip, "127.0.0.1");
        addr->port = 5060;
        addr->family = 2;  /* AF_INET */
    }

    return 0;
}

int lws_trans_get_fd(lws_trans_t* trans) {
    (void)trans;
    return -1; /* Stub doesn't have real socket */
}

/* ========================================
 * Session stub (simple + SDP ready trigger)
 * ======================================== */

/* Store handler for triggering callbacks */
static struct {
    void* handler;
    void* userdata;
} g_sess_handler = {0};

lws_sess_t* lws_sess_create(const lws_sess_config_t* config, const lws_sess_handler_t* handler) {
    (void)config;

    /* Store handler for later callback */
    if (handler) {
        g_sess_handler.handler = (void*)handler->on_sdp_ready;
        g_sess_handler.userdata = handler->userdata;
    }

    return (lws_sess_t*)0x4444;
}

void lws_sess_destroy(lws_sess_t* sess) {
    (void)sess;
    g_sess_handler.handler = NULL;
    g_sess_handler.userdata = NULL;
}

int lws_sess_gather_candidates(lws_sess_t* sess) {
    /* Immediately trigger SDP ready callback (simulating instant ICE gathering) */
    if (g_sess_handler.handler) {
        typedef void (*on_sdp_ready_f)(lws_sess_t* sess, const char* sdp, void* userdata);
        on_sdp_ready_f callback = (on_sdp_ready_f)g_sess_handler.handler;

        const char* sdp = lws_sess_get_local_sdp(sess);
        callback(sess, sdp, g_sess_handler.userdata);
    }

    return 0;
}

int lws_sess_set_remote_sdp(lws_sess_t* sess, const char* sdp) {
    (void)sess;
    (void)sdp;
    return 0;
}

int lws_sess_start_ice(lws_sess_t* sess) {
    (void)sess;
    return 0;
}

void lws_sess_stop(lws_sess_t* sess) {
    (void)sess;
}

const char* lws_sess_get_local_sdp(lws_sess_t* sess) {
    (void)sess;
    return "v=0\r\n"
           "o=- 0 0 IN IP4 127.0.0.1\r\n"
           "s=lwsip stub\r\n"
           "c=IN IP4 127.0.0.1\r\n"
           "t=0 0\r\n"
           "m=audio 8000 RTP/AVP 0 8\r\n"
           "a=rtpmap:0 PCMU/8000\r\n"
           "a=rtpmap:8 PCMA/8000\r\n";
}

lws_sess_state_t lws_sess_get_state(lws_sess_t* sess) {
    (void)sess;
    return LWS_SESS_STATE_IDLE;
}

/* ========================================
 * HTTP parser - provided by libhttp.a (no stub needed)
 * ======================================== */
