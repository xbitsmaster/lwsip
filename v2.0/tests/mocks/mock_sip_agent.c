/**
 * @file mock_sip_agent.c
 * @brief Mock implementation of SIP agent for unit testing
 */

#include "mock_sip_agent.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Mock State
 * ============================================================ */

static struct {
    int create_count;
    int destroy_count;
    int input_count;
    int poll_count;

    sip_agent_t* create_result;
    int input_result;
    int poll_result;
} g_mock_state = {0};

/* ============================================================
 * Mock Control Functions
 * ============================================================ */

void mock_sip_agent_reset(void)
{
    memset(&g_mock_state, 0, sizeof(g_mock_state));
}

void mock_sip_agent_set_create_result(sip_agent_t* result)
{
    g_mock_state.create_result = result;
}

void mock_sip_agent_set_input_result(int result)
{
    g_mock_state.input_result = result;
}

void mock_sip_agent_set_poll_result(int result)
{
    g_mock_state.poll_result = result;
}

int mock_sip_agent_get_create_count(void)
{
    return g_mock_state.create_count;
}

int mock_sip_agent_get_destroy_count(void)
{
    return g_mock_state.destroy_count;
}

int mock_sip_agent_get_input_count(void)
{
    return g_mock_state.input_count;
}

int mock_sip_agent_get_poll_count(void)
{
    return g_mock_state.poll_count;
}

/* ============================================================
 * Mock SIP Agent Functions
 * ============================================================ */

sip_agent_t* sip_agent_create(const char* name)
{
    g_mock_state.create_count++;
    return g_mock_state.create_result;
}

int sip_agent_destroy(sip_agent_t* agent)
{
    g_mock_state.destroy_count++;
    return 0;
}

int sip_agent_input(sip_agent_t* agent, sip_message_t* msg)
{
    g_mock_state.input_count++;
    return g_mock_state.input_result;
}

int sip_agent_poll(sip_agent_t* agent, int timeout)
{
    g_mock_state.poll_count++;
    return g_mock_state.poll_result;
}
