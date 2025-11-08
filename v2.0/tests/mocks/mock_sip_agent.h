/**
 * @file mock_sip_agent.h
 * @brief Mock implementation of SIP agent for unit testing
 */

#ifndef __MOCK_SIP_AGENT_H__
#define __MOCK_SIP_AGENT_H__

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * Mock Types (Simplified from real SIP agent)
 * ============================================================ */

typedef struct sip_agent_t sip_agent_t;
typedef struct sip_uac_transaction_t sip_uac_transaction_t;
typedef struct sip_message_t sip_message_t;
typedef struct sip_transport_t sip_transport_t;

/* ============================================================
 * Mock Function Pointers for Tracking Calls
 * ============================================================ */

typedef struct {
    int (*create_called)(const char* name);
    int (*destroy_called)(sip_agent_t* agent);
    int (*input_called)(sip_agent_t* agent, sip_message_t* msg);
    int (*poll_called)(sip_agent_t* agent, int timeout);
} mock_sip_agent_stats_t;

/* ============================================================
 * Mock Control Functions
 * ============================================================ */

/**
 * Reset mock statistics
 */
void mock_sip_agent_reset(void);

/**
 * Get mock statistics
 */
mock_sip_agent_stats_t* mock_sip_agent_get_stats(void);

/**
 * Set next return value for sip_agent_create
 */
void mock_sip_agent_set_create_result(sip_agent_t* result);

/**
 * Set next return value for sip_agent_input
 */
void mock_sip_agent_set_input_result(int result);

/**
 * Set next return value for sip_agent_poll
 */
void mock_sip_agent_set_poll_result(int result);

/* ============================================================
 * Mock SIP Agent Functions (matching real API)
 * ============================================================ */

sip_agent_t* sip_agent_create(const char* name);
int sip_agent_destroy(sip_agent_t* agent);
int sip_agent_input(sip_agent_t* agent, sip_message_t* msg);
int sip_agent_poll(sip_agent_t* agent, int timeout);

#endif // __MOCK_SIP_AGENT_H__
