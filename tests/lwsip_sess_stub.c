/**
 * @file lwsip_sess_stub.c
 * @brief Stub implementations for lws_sess unit tests
 *
 * Provides minimal stub implementations of dependencies:
 * - libice (ice_agent_*)
 * - librtp (rtp_*)
 * - lws_dev
 * - lws_trans
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Stub for libice functions */
void* ice_agent_create(const void* config) {
    (void)config;
    return (void*)0x1234;
}

void ice_agent_destroy(void* agent) {
    (void)agent;
}

int ice_agent_gather_candidates(void* agent) {
    (void)agent;
    return 0;
}

int ice_agent_set_remote_sdp(void* agent, const char* sdp) {
    (void)agent;
    (void)sdp;
    return 0;
}

int ice_agent_start(void* agent) {
    (void)agent;
    return 0;
}

void ice_agent_stop(void* agent) {
    (void)agent;
}

const char* ice_agent_get_local_sdp(void* agent) {
    (void)agent;
    return "a=candidate:1 1 UDP 2130706431 127.0.0.1 10000 typ host\r\n";
}

int ice_agent_get_state(void* agent) {
    (void)agent;
    return 0;
}

/* Stub for librtp functions */
void* rtp_create(const void* config) {
    (void)config;
    return (void*)0x5678;
}

void rtp_destroy(void* rtp) {
    (void)rtp;
}

int rtp_send(void* rtp, const void* data, int len, uint32_t timestamp) {
    (void)rtp;
    (void)data;
    (void)len;
    (void)timestamp;
    return 0;
}

int rtp_recv(void* rtp, void* buf, int size, uint32_t* timestamp) {
    (void)rtp;
    (void)buf;
    (void)size;
    (void)timestamp;
    return 0;
}

int rtp_get_stats(void* rtp, void* stats) {
    (void)rtp;
    (void)stats;
    return 0;
}

/* Stub for lws_dev functions */
void* lws_dev_create(const void* config, const void* ops) {
    (void)config;
    (void)ops;
    return (void*)0x9ABC;
}

void lws_dev_destroy(void* dev) {
    (void)dev;
}

int lws_dev_open(void* dev) {
    (void)dev;
    return 0;
}

void lws_dev_close(void* dev) {
    (void)dev;
}

int lws_dev_start(void* dev) {
    (void)dev;
    return 0;
}

void lws_dev_stop(void* dev) {
    (void)dev;
}

int lws_dev_read_audio(void* dev, void* buf, int size) {
    (void)dev;
    (void)buf;
    (void)size;
    return 0;
}

int lws_dev_write_audio(void* dev, const void* data, int len) {
    (void)dev;
    (void)data;
    (void)len;
    return 0;
}

/* Stub for lws_trans functions */
void* lws_trans_create(const void* config, const void* handler) {
    (void)config;
    (void)handler;
    return (void*)0xDEF0;
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

int lws_trans_recv(void* trans, void* buf, int size, char* from, int from_size) {
    (void)trans;
    (void)buf;
    (void)size;
    (void)from;
    (void)from_size;
    return 0;
}

int lws_trans_loop(void* trans, int timeout_ms) {
    (void)trans;
    (void)timeout_ms;
    return 0;
}

/* Stub for lws_timer functions */
int lws_timer_init(void) {
    return 0;
}

void lws_timer_cleanup(void) {
}

void* lws_timer_add(uint64_t timeout_ms, void* callback, void* userdata) {
    (void)timeout_ms;
    (void)callback;
    (void)userdata;
    return (void*)0x1111;
}

void lws_timer_remove(void* timer) {
    (void)timer;
}

/* Additional ice_agent stubs */
int ice_agent_gather(void* agent) {
    (void)agent;
    return 0;
}

int ice_agent_send(void* agent, const void* data, int len, const char* dest) {
    (void)agent;
    (void)data;
    (void)len;
    (void)dest;
    return 0;
}

int ice_agent_set_local_auth(void* agent, const char* username, const char* password) {
    (void)agent;
    (void)username;
    (void)password;
    return 0;
}

/* Additional rtp stubs */
void* rtp_payload_encode_create(int payload_type, const void* config) {
    (void)payload_type;
    (void)config;
    return (void*)0x7777;
}

void rtp_payload_encode_destroy(void* encoder) {
    (void)encoder;
}

int rtp_payload_encode_input(void* encoder, const void* data, int len, void* out, int out_size) {
    (void)encoder;
    (void)data;
    (void)len;
    (void)out;
    (void)out_size;
    return 0;
}

void* rtp_payload_decode_create(int payload_type, const void* config) {
    (void)payload_type;
    (void)config;
    return (void*)0x8888;
}

void rtp_payload_decode_destroy(void* decoder) {
    (void)decoder;
}

int rtp_payload_decode_input(void* decoder, const void* data, int len, void* out, int out_size) {
    (void)decoder;
    (void)data;
    (void)len;
    (void)out;
    (void)out_size;
    return 0;
}

int rtp_rtcp_interval(void* rtp) {
    (void)rtp;
    return 5000; /* 5 seconds */
}

int rtp_rtcp_report(void* rtp, void* buf, int size) {
    (void)rtp;
    (void)buf;
    (void)size;
    return 0;
}

int rtp_onreceived_rtcp(void* rtp, const void* data, int len) {
    (void)rtp;
    (void)data;
    (void)len;
    return 0;
}

/* Stub for STUN timer functions */
int stun_timer_start(void* stun, void* req, int timeout_ms, void* callback) {
    (void)stun;
    (void)req;
    (void)timeout_ms;
    (void)callback;
    return 0;
}

void stun_timer_stop(void* stun, void* req) {
    (void)stun;
    (void)req;
}

/* Stub for ice_candidate_priority function */
uint32_t ice_candidate_priority(void* candidate) {
    (void)candidate;
    return 2130706431; /* host candidate priority */
}
