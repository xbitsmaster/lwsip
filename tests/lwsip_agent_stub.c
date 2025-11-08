/**
 * @file lwsip_agent_stub.c
 * @brief Stub implementations for lws_agent unit tests
 *
 * Provides minimal stub implementations of dependencies:
 * - libsip (sip_agent_*, sip_uac_*, sip_uas_*)
 * - lws_timer
 * - lws_trans
 * - lws_sess
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Stub for libsip sip_agent functions */
void* sip_agent_create(void* param) {
    (void)param;
    return (void*)0x1234; /* Return dummy pointer */
}

void sip_agent_destroy(void* agent) {
    (void)agent;
}

int sip_agent_input(void* agent, const void* data, int len) {
    (void)agent;
    (void)data;
    (void)len;
    return 0;
}

void sip_agent_set_rport(void* agent, int enable) {
    (void)agent;
    (void)enable;
}

/* Stub for libsip sip_uac functions */
void* sip_uac_register(void* agent, const char* from, const char* to, const char* contact, int expires) {
    (void)agent;
    (void)from;
    (void)to;
    (void)contact;
    (void)expires;
    return (void*)0x5678;
}

int sip_uac_send(void* uac, const void* msg, int len, void* cb, void* param) {
    (void)uac;
    (void)msg;
    (void)len;
    (void)cb;
    (void)param;
    return 0;
}

void sip_uac_transaction_release(void* uac) {
    (void)uac;
}

void* sip_uac_invite(void* agent, const char* from, const char* to, const char* subject) {
    (void)agent;
    (void)from;
    (void)to;
    (void)subject;
    return (void*)0x9ABC;
}

int sip_uac_add_header(void* uac, const char* name, const char* value) {
    (void)uac;
    (void)name;
    (void)value;
    return 0;
}

void* sip_uac_bye(void* agent, const void* dialog) {
    (void)agent;
    (void)dialog;
    return (void*)0xDEF0;
}

void* sip_uac_cancel(void* agent, const void* dialog) {
    (void)agent;
    (void)dialog;
    return (void*)0x1111;
}

/* Stub for libsip sip_uas functions */
int sip_uas_reply(void* uas, int code, const char* reason) {
    (void)uas;
    (void)code;
    (void)reason;
    return 0;
}

int sip_uas_add_header(void* uas, const char* name, const char* value) {
    (void)uas;
    (void)name;
    (void)value;
    return 0;
}

void sip_uas_transaction_addref(void* uas) {
    (void)uas;
}

void sip_uas_transaction_release(void* uas) {
    (void)uas;
}

/* Stub for libsip sip_message functions */
void* sip_message_create(int type) {
    (void)type;
    return (void*)0x2222;
}

void sip_message_destroy(void* msg) {
    (void)msg;
}

int sip_message_load(const void* data, int len, void* msg) {
    (void)data;
    (void)len;
    (void)msg;
    return 0;
}

/* Stub for lws_timer functions */
int lws_timer_init(void) {
    return 0;
}

void lws_timer_cleanup(void) {
}

/* Stub for lws_trans functions */
void* lws_trans_create(const void* config, const void* handler) {
    (void)config;
    (void)handler;
    return (void*)0x3333;
}

void lws_trans_destroy(void* trans) {
    (void)trans;
}

int lws_trans_send(void* trans, const void* data, int len, const char* dest) {
    (void)trans;
    (void)data;
    (void)len;
    (void)dest;
    return 0;
}

int lws_trans_loop(void* trans, int timeout_ms) {
    (void)trans;
    (void)timeout_ms;
    return 0;
}

int lws_trans_get_local_addr(void* trans, char* buf, int size) {
    (void)trans;
    if (buf && size > 0) {
        strncpy(buf, "127.0.0.1:5060", size - 1);
        buf[size - 1] = '\0';
    }
    return 0;
}

/* Stub for lws_sess functions */
void* lws_sess_create(const void* config, const void* handler) {
    (void)config;
    (void)handler;
    return (void*)0x4444;
}

void lws_sess_destroy(void* sess) {
    (void)sess;
}

int lws_sess_gather_candidates(void* sess) {
    (void)sess;
    return 0;
}

int lws_sess_set_remote_sdp(void* sess, const char* sdp) {
    (void)sess;
    (void)sdp;
    return 0;
}

int lws_sess_start_ice(void* sess) {
    (void)sess;
    return 0;
}

void lws_sess_stop(void* sess) {
    (void)sess;
}

const char* lws_sess_get_local_sdp(void* sess) {
    (void)sess;
    return "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=lwsip\r\n";
}

int lws_sess_get_state(void* sess) {
    (void)sess;
    return 0; /* LWS_SESS_STATE_IDLE */
}

/* Stub for libhttp http_parser functions */
void* http_parser_create(int type) {
    (void)type;
    return (void*)0x6666;
}

void http_parser_destroy(void* parser) {
    (void)parser;
}

int http_parser_input(void* parser, const void* data, int len) {
    (void)parser;
    (void)data;
    (void)len;
    return 0;
}
