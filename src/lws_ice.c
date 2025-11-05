/**
 * @file lws_ice.c
 * @brief LwSIP ICE implementation using libice
 */

#include "lws_ice.h"
#include "lws_error.h"
#include "lws_log.h"
#include "lws_mem.h"

// libice headers
//#include "ice-candidate.h"

#include "sockutil.h"
#include "ice-agent.h"
#include "stun-agent.h"
#include "stun-proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ============================================================
 * Internal Structures
 * ============================================================ */

struct lws_ice_agent {
    lws_config_t config;
    lws_ice_handler_t handler;

    struct ice_agent_t* ice_agent;  // libice ice agent

    // ICE credentials
    char local_ufrag[64];
    char local_pwd[64];

    // State
    int gathering;      // Gathering in progress
    int connected;      // ICE connected

    // STUN/TURN server addresses
    struct sockaddr_storage stun_addr;
    struct sockaddr_storage turn_addr;
};

/* ============================================================
 * Helper Functions
 * ============================================================ */

/**
 * Generate random ICE username fragment and password
 */
static void generate_ice_credentials(char* ufrag, int ufrag_len,
                                       char* pwd, int pwd_len)
{
    const char* chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int chars_len = strlen(chars);

    // Generate ufrag (4-8 characters)
    int ufrag_size = 8;
    for (int i = 0; i < ufrag_size && i < ufrag_len - 1; i++) {
        ufrag[i] = chars[rand() % chars_len];
    }
    ufrag[ufrag_size] = '\0';

    // Generate pwd (22-24 characters)
    int pwd_size = 24;
    for (int i = 0; i < pwd_size && i < pwd_len - 1; i++) {
        pwd[i] = chars[rand() % chars_len];
    }
    pwd[pwd_size] = '\0';
}

/**
 * Convert string address to sockaddr
 */
static int string_to_sockaddr(const char* addr, uint16_t port,
                                struct sockaddr_storage* sa)
{
    memset(sa, 0, sizeof(*sa));

    // Try IPv4 first
    struct sockaddr_in* sin = (struct sockaddr_in*)sa;
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);

    if (inet_pton(AF_INET, addr, &sin->sin_addr) == 1) {
        return 0;
    }

    // Try IPv6
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)sa;
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(port);

    if (inet_pton(AF_INET6, addr, &sin6->sin6_addr) == 1) {
        return 0;
    }

    // TODO: Resolve hostname
    lws_log_error(LWS_ERR_INVALID_PARAM, "invalid address: %s\n", addr);
    return LWS_ERR_INVALID_PARAM;
}

/* ============================================================
 * libice Callbacks
 * ============================================================ */

/**
 * Send callback for ICE agent
 */
static int ice_send_callback(void* param, int protocol,
                               const struct sockaddr* local,
                               const struct sockaddr* remote,
                               const void* data, int bytes)
{
    lws_ice_agent_t* agent = (lws_ice_agent_t*)param;

    // TODO: Actually send via transport layer
    // For now, just log
    lws_log_debug("ICE send: %d bytes\n", bytes);

    return 0;
}

/**
 * Data received via TURN callback
 */
static void ice_ondata_callback(void* param, uint8_t stream, uint16_t component,
                                  const void* data, int bytes)
{
    lws_ice_agent_t* agent = (lws_ice_agent_t*)param;

    lws_log_debug("ICE data received: stream=%d, component=%d, bytes=%d\n",
                  stream, component, bytes);

    if (agent->handler.on_data) {
        agent->handler.on_data(agent->handler.param, stream, component,
                                data, bytes);
    }
}

/**
 * Candidate gathering complete callback
 */
static void ice_ongather_callback(void* param, int code)
{
    lws_ice_agent_t* agent = (lws_ice_agent_t*)param;

    lws_log_info("ICE gather done: code=%d\n", code);

    agent->gathering = 0;

    if (agent->handler.on_gather_done) {
        agent->handler.on_gather_done(agent->handler.param, code);
    }
}

/**
 * ICE connected callback
 */
static void ice_onconnected_callback(void* param, uint64_t flags, uint64_t mask)
{
    lws_ice_agent_t* agent = (lws_ice_agent_t*)param;

    lws_log_info("ICE connected: flags=0x%llx, mask=0x%llx\n",
                 (unsigned long long)flags, (unsigned long long)mask);

    agent->connected = 1;

    // Notify all connected streams/components
    for (uint8_t stream = 0; stream < 64; stream++) {
        if ((flags & (1ULL << stream)) && (mask & (1ULL << stream))) {
            if (agent->handler.on_ice_connected) {
                agent->handler.on_ice_connected(agent->handler.param, stream, 1);
            }
        }
    }
}

/* ============================================================
 * Public API Implementation
 * ============================================================ */

lws_ice_agent_t* lws_ice_agent_create(const lws_config_t* config,
                                        lws_ice_handler_t* handler)
{
    if (!config || !handler) {
        lws_log_error(LWS_ERR_INVALID_PARAM, "invalid parameters\n");
        return NULL;
    }

    lws_ice_agent_t* agent = (lws_ice_agent_t*)lws_calloc(1, sizeof(lws_ice_agent_t));
    if (!agent) {
        lws_log_error(LWS_ERR_NOMEM, "failed to allocate ice agent\n");
        return NULL;
    }

    // Copy config and handler
    memcpy(&agent->config, config, sizeof(lws_config_t));
    memcpy(&agent->handler, handler, sizeof(lws_ice_handler_t));

    // Generate ICE credentials
    generate_ice_credentials(agent->local_ufrag, sizeof(agent->local_ufrag),
                              agent->local_pwd, sizeof(agent->local_pwd));

    lws_log_info("Generated ICE credentials: ufrag=%s, pwd=%s\n",
                 agent->local_ufrag, agent->local_pwd);

    // Create libice agent
    struct ice_agent_handler_t ice_handler;
    memset(&ice_handler, 0, sizeof(ice_handler));
    ice_handler.send = ice_send_callback;
    ice_handler.ondata = ice_ondata_callback;
    ice_handler.ongather = ice_ongather_callback;
    ice_handler.onconnected = ice_onconnected_callback;

    agent->ice_agent = ice_agent_create(config->ice_controlling, &ice_handler, agent);
    if (!agent->ice_agent) {
        lws_log_error(LWS_ERR_INTERNAL, "failed to create libice agent\n");
        lws_free(agent);
        return NULL;
    }

    // Set local auth
    ice_agent_set_local_auth(agent->ice_agent, agent->local_ufrag, agent->local_pwd);

    // Set ICE-lite if configured
    if (config->ice_lite) {
        ice_agent_set_icelite(agent->ice_agent, 1);
    }

    // Parse STUN server address
    if (config->stun_server[0] != '\0') {
        if (string_to_sockaddr(config->stun_server, config->stun_port,
                                &agent->stun_addr) != 0) {
            lws_log_warn(LWS_ERR_INVALID_PARAM, "failed to parse STUN server address\n");
        }
    }

    // Parse TURN server address
    if (config->enable_turn && config->turn_server[0] != '\0') {
        if (string_to_sockaddr(config->turn_server, config->turn_port,
                                &agent->turn_addr) != 0) {
            lws_log_warn(LWS_ERR_INVALID_PARAM, "failed to parse TURN server address\n");
        }
    }

    lws_log_info("ICE agent created: controlling=%d, lite=%d\n",
                 config->ice_controlling, config->ice_lite);

    return agent;
}

void lws_ice_agent_destroy(lws_ice_agent_t* agent)
{
    if (!agent) {
        return;
    }

    if (agent->ice_agent) {
        ice_agent_destroy(agent->ice_agent);
        agent->ice_agent = NULL;
    }

    lws_free(agent);
}

int lws_ice_add_local_candidate(lws_ice_agent_t* agent, uint8_t stream,
                                  uint16_t component, const char* local_addr,
                                  uint16_t local_port)
{
    if (!agent || !local_addr) {
        return LWS_ERR_INVALID_PARAM;
    }

    struct ice_candidate_t cand;
    memset(&cand, 0, sizeof(cand));

    cand.type = ICE_CANDIDATE_HOST;
    cand.stream = stream;
    cand.component = component;
    cand.protocol = STUN_PROTOCOL_UDP;

    // Convert address
    if (string_to_sockaddr(local_addr, local_port,
                            (struct sockaddr_storage*)&cand.addr) != 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    // Copy to host address
    memcpy(&cand.host, &cand.addr, sizeof(cand.host));

    // Calculate priority
    ice_candidate_priority(&cand);

    // Add to ice agent
    int ret = ice_agent_add_local_candidate(agent->ice_agent, &cand);
    if (ret != 0) {
        lws_log_error(ret, "failed to add local candidate\n");
        return LWS_ERR_INTERNAL;
    }

    lws_log_info("Added local candidate: %s:%d (stream=%d, component=%d)\n",
                 local_addr, local_port, stream, component);

    return LWS_OK;
}

int lws_ice_gather_candidates(lws_ice_agent_t* agent)
{
    if (!agent) {
        return LWS_ERR_INVALID_PARAM;
    }

    agent->gathering = 1;

    // Gather from STUN server
    if (agent->config.stun_server[0] != '\0') {
        int timeout = agent->config.ice_gather_timeout > 0 ?
                      agent->config.ice_gather_timeout : 3000;

        int ret = ice_agent_gather(agent->ice_agent,
                                     (const struct sockaddr*)&agent->stun_addr,
                                     0,  // STUN (not TURN)
                                     timeout, 0, NULL, NULL);
        if (ret != 0) {
            lws_log_error(ret, "failed to gather from STUN\n");
            agent->gathering = 0;
            return LWS_ERR_INTERNAL;
        }

        lws_log_info("Started gathering from STUN: %s:%d\n",
                     agent->config.stun_server, agent->config.stun_port);
    }

    // Gather from TURN server
    if (agent->config.enable_turn && agent->config.turn_server[0] != '\0') {
        int timeout = agent->config.ice_gather_timeout > 0 ?
                      agent->config.ice_gather_timeout : 3000;

        int ret = ice_agent_gather(agent->ice_agent,
                                     (const struct sockaddr*)&agent->turn_addr,
                                     1,  // TURN
                                     timeout, 1,  // Long-term credential
                                     agent->config.turn_username,
                                     agent->config.turn_password);
        if (ret != 0) {
            lws_log_error(ret, "failed to gather from TURN\n");
        } else {
            lws_log_info("Started gathering from TURN: %s:%d\n",
                         agent->config.turn_server, agent->config.turn_port);
        }
    }

    return LWS_OK;
}

int lws_ice_add_remote_candidate(lws_ice_agent_t* agent, uint8_t stream,
                                   uint16_t component, const char* foundation,
                                   uint32_t priority, const char* addr,
                                   uint16_t port, const char* type)
{
    if (!agent || !addr || !type) {
        return LWS_ERR_INVALID_PARAM;
    }

    struct ice_candidate_t cand;
    memset(&cand, 0, sizeof(cand));

    // Parse type
    if (strcmp(type, "host") == 0) {
        cand.type = ICE_CANDIDATE_HOST;
    } else if (strcmp(type, "srflx") == 0) {
        cand.type = ICE_CANDIDATE_SERVER_REFLEXIVE;
    } else if (strcmp(type, "relay") == 0) {
        cand.type = ICE_CANDIDATE_RELAYED;
    } else {
        lws_log_warn(LWS_ERR_INVALID_PARAM, "unknown candidate type: %s\n", type);
        return LWS_ERR_INVALID_PARAM;
    }

    cand.stream = stream;
    cand.component = component;
    cand.priority = priority;
    cand.protocol = STUN_PROTOCOL_UDP;

    if (foundation) {
        strncpy(cand.foundation, foundation, sizeof(cand.foundation) - 1);
    }

    // Convert address
    if (string_to_sockaddr(addr, port,
                            (struct sockaddr_storage*)&cand.addr) != 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    // Add to ice agent
    int ret = ice_agent_add_remote_candidate(agent->ice_agent, &cand);
    if (ret != 0) {
        lws_log_error(ret, "failed to add remote candidate\n");
        return LWS_ERR_INTERNAL;
    }

    lws_log_info("Added remote candidate: %s:%d type=%s (stream=%d, component=%d)\n",
                 addr, port, type, stream, component);

    return LWS_OK;
}

int lws_ice_set_local_auth(lws_ice_agent_t* agent, const char* ufrag,
                             const char* pwd)
{
    if (!agent || !ufrag || !pwd) {
        return LWS_ERR_INVALID_PARAM;
    }

    strncpy(agent->local_ufrag, ufrag, sizeof(agent->local_ufrag) - 1);
    strncpy(agent->local_pwd, pwd, sizeof(agent->local_pwd) - 1);

    return ice_agent_set_local_auth(agent->ice_agent, ufrag, pwd);
}

int lws_ice_set_remote_auth(lws_ice_agent_t* agent, uint8_t stream,
                              const char* ufrag, const char* pwd)
{
    if (!agent || !ufrag || !pwd) {
        return LWS_ERR_INVALID_PARAM;
    }

    return ice_agent_set_remote_auth(agent->ice_agent, stream, ufrag, pwd);
}

int lws_ice_get_local_auth(lws_ice_agent_t* agent, char* ufrag, char* pwd)
{
    if (!agent || !ufrag || !pwd) {
        return LWS_ERR_INVALID_PARAM;
    }

    strcpy(ufrag, agent->local_ufrag);
    strcpy(pwd, agent->local_pwd);

    return LWS_OK;
}

int lws_ice_start(lws_ice_agent_t* agent)
{
    if (!agent) {
        return LWS_ERR_INVALID_PARAM;
    }

    int ret = ice_agent_start(agent->ice_agent);
    if (ret != 0) {
        lws_log_error(ret, "failed to start ICE\n");
        return LWS_ERR_INTERNAL;
    }

    lws_log_info("ICE connectivity checks started\n");

    return LWS_OK;
}

int lws_ice_stop(lws_ice_agent_t* agent)
{
    if (!agent) {
        return LWS_ERR_INVALID_PARAM;
    }

    return ice_agent_stop(agent->ice_agent);
}

int lws_ice_is_connected(lws_ice_agent_t* agent, uint8_t stream,
                          uint16_t component)
{
    if (!agent) {
        return 0;
    }

    return agent->connected;
}

int lws_ice_send(lws_ice_agent_t* agent, uint8_t stream, uint16_t component,
                  const void* data, int bytes)
{
    if (!agent || !data || bytes <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    return ice_agent_send(agent->ice_agent, stream, component, data, bytes);
}

int lws_ice_input(lws_ice_agent_t* agent, int protocol,
                   const char* local_addr, uint16_t local_port,
                   const char* remote_addr, uint16_t remote_port,
                   const void* data, int bytes)
{
    if (!agent || !data || bytes <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    struct sockaddr_storage local, remote;

    if (string_to_sockaddr(local_addr, local_port, &local) != 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    if (string_to_sockaddr(remote_addr, remote_port, &remote) != 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    return ice_agent_input(agent->ice_agent, protocol,
                            (const struct sockaddr*)&local,
                            (const struct sockaddr*)&remote,
                            data, bytes);
}

int lws_ice_generate_sdp_candidates(lws_ice_agent_t* agent, uint8_t stream,
                                      char* buffer, int size)
{
    if (!agent || !buffer || size <= 0) {
        return LWS_ERR_INVALID_PARAM;
    }

    // TODO: Implement candidate enumeration and SDP generation
    // For now, return empty
    buffer[0] = '\0';
    return 0;
}

int lws_ice_get_default_candidate(lws_ice_agent_t* agent, uint8_t stream,
                                    uint16_t component, char* addr,
                                    uint16_t* port)
{
    if (!agent || !addr || !port) {
        return LWS_ERR_INVALID_PARAM;
    }

    struct ice_candidate_t cand;

    int ret = ice_agent_get_local_candidate(agent->ice_agent, stream,
                                             component, &cand);
    if (ret != 0) {
        return LWS_ERR_INTERNAL;
    }

    // Convert sockaddr to string
    struct sockaddr* sa = (struct sockaddr*)&cand.addr;
    if (sa->sa_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)sa;
        inet_ntop(AF_INET, &sin->sin_addr, addr, 64);
        *port = ntohs(sin->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)sa;
        inet_ntop(AF_INET6, &sin6->sin6_addr, addr, 64);
        *port = ntohs(sin6->sin6_port);
    } else {
        return LWS_ERR_INTERNAL;
    }

    return LWS_OK;
}
