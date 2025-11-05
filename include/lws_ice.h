/**
 * @file lws_ice.h
 * @brief LwSIP ICE (Interactive Connectivity Establishment) Support
 *
 * This module integrates libice to provide NAT traversal capabilities
 * for SIP media sessions using ICE, STUN, and TURN protocols.
 */

#ifndef __LWS_ICE_H__
#define __LWS_ICE_H__

#include "lws_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct lws_ice_agent lws_ice_agent_t;

/* ============================================================
 * ICE Agent Handler Callbacks
 * ============================================================ */

typedef struct {
    /**
     * Called when candidate gathering is complete
     * @param param User parameter
     * @param code 0=success, other=error
     */
    void (*on_gather_done)(void* param, int code);

    /**
     * Called when ICE connection is established
     * @param param User parameter
     * @param stream Stream ID
     * @param component Component ID (1=RTP, 2=RTCP)
     */
    void (*on_ice_connected)(void* param, uint8_t stream, uint16_t component);

    /**
     * Called when ICE connection fails
     * @param param User parameter
     * @param stream Stream ID
     * @param component Component ID
     */
    void (*on_ice_failed)(void* param, uint8_t stream, uint16_t component);

    /**
     * Called when data is received via ICE/TURN
     * @param param User parameter
     * @param stream Stream ID
     * @param component Component ID
     * @param data Received data
     * @param bytes Data length
     */
    void (*on_data)(void* param, uint8_t stream, uint16_t component,
                    const void* data, int bytes);

    /* User parameter passed to callbacks */
    void* param;
} lws_ice_handler_t;

/* ============================================================
 * ICE Agent Lifecycle
 * ============================================================ */

/**
 * Create an ICE agent
 * @param config LwSIP configuration
 * @param handler ICE event handler
 * @return ICE agent handle, NULL on failure
 */
lws_ice_agent_t* lws_ice_agent_create(const lws_config_t* config,
                                       lws_ice_handler_t* handler);

/**
 * Destroy an ICE agent
 * @param agent ICE agent handle
 */
void lws_ice_agent_destroy(lws_ice_agent_t* agent);

/* ============================================================
 * ICE Candidate Management
 * ============================================================ */

/**
 * Add a local host candidate (local IP address)
 * @param agent ICE agent handle
 * @param stream Stream ID (0 for audio, 1 for video, etc.)
 * @param component Component ID (1=RTP, 2=RTCP)
 * @param local_addr Local IP address and port
 * @return 0 on success, error code otherwise
 */
int lws_ice_add_local_candidate(lws_ice_agent_t* agent, uint8_t stream,
                                  uint16_t component, const char* local_addr,
                                  uint16_t local_port);

/**
 * Gather server reflexive and relayed candidates
 * Initiates STUN/TURN requests to discover public IP addresses
 * Calls on_gather_done when complete
 * @param agent ICE agent handle
 * @return 0 on success, error code otherwise
 */
int lws_ice_gather_candidates(lws_ice_agent_t* agent);

/**
 * Add a remote candidate from SDP
 * @param agent ICE agent handle
 * @param stream Stream ID
 * @param component Component ID
 * @param foundation Candidate foundation
 * @param priority Candidate priority
 * @param addr Remote IP address
 * @param port Remote port
 * @param type Candidate type ("host", "srflx", "relay")
 * @return 0 on success, error code otherwise
 */
int lws_ice_add_remote_candidate(lws_ice_agent_t* agent, uint8_t stream,
                                   uint16_t component, const char* foundation,
                                   uint32_t priority, const char* addr,
                                   uint16_t port, const char* type);

/* ============================================================
 * ICE Authentication
 * ============================================================ */

/**
 * Set local ICE credentials (username fragment and password)
 * @param agent ICE agent handle
 * @param ufrag ICE username fragment
 * @param pwd ICE password
 * @return 0 on success, error code otherwise
 */
int lws_ice_set_local_auth(lws_ice_agent_t* agent, const char* ufrag,
                             const char* pwd);

/**
 * Set remote ICE credentials from SDP
 * @param agent ICE agent handle
 * @param stream Stream ID
 * @param ufrag Remote ICE username fragment
 * @param pwd Remote ICE password
 * @return 0 on success, error code otherwise
 */
int lws_ice_set_remote_auth(lws_ice_agent_t* agent, uint8_t stream,
                              const char* ufrag, const char* pwd);

/**
 * Get local ICE credentials for SDP generation
 * @param agent ICE agent handle
 * @param ufrag Buffer for username fragment (min 64 bytes)
 * @param pwd Buffer for password (min 64 bytes)
 * @return 0 on success, error code otherwise
 */
int lws_ice_get_local_auth(lws_ice_agent_t* agent, char* ufrag, char* pwd);

/* ============================================================
 * ICE Connectivity
 * ============================================================ */

/**
 * Start ICE connectivity checks
 * Should be called after both local and remote candidates are added
 * @param agent ICE agent handle
 * @return 0 on success, error code otherwise
 */
int lws_ice_start(lws_ice_agent_t* agent);

/**
 * Stop ICE connectivity checks
 * @param agent ICE agent handle
 * @return 0 on success, error code otherwise
 */
int lws_ice_stop(lws_ice_agent_t* agent);

/**
 * Check if ICE is connected for a given stream/component
 * @param agent ICE agent handle
 * @param stream Stream ID
 * @param component Component ID
 * @return 1 if connected, 0 otherwise
 */
int lws_ice_is_connected(lws_ice_agent_t* agent, uint8_t stream,
                          uint16_t component);

/* ============================================================
 * Data Transmission
 * ============================================================ */

/**
 * Send data via ICE connection
 * @param agent ICE agent handle
 * @param stream Stream ID
 * @param component Component ID
 * @param data Data to send
 * @param bytes Data length
 * @return Number of bytes sent, or error code
 */
int lws_ice_send(lws_ice_agent_t* agent, uint8_t stream, uint16_t component,
                  const void* data, int bytes);

/**
 * Process incoming network data (STUN/ICE messages)
 * @param agent ICE agent handle
 * @param protocol Protocol type (UDP/TCP)
 * @param local_addr Local address
 * @param remote_addr Remote address
 * @param data Received data
 * @param bytes Data length
 * @return 0 on success, error code otherwise
 */
int lws_ice_input(lws_ice_agent_t* agent, int protocol,
                   const char* local_addr, uint16_t local_port,
                   const char* remote_addr, uint16_t remote_port,
                   const void* data, int bytes);

/* ============================================================
 * SDP Generation Helpers
 * ============================================================ */

/**
 * Generate ICE candidate lines for SDP
 * @param agent ICE agent handle
 * @param stream Stream ID
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written, or error code
 */
int lws_ice_generate_sdp_candidates(lws_ice_agent_t* agent, uint8_t stream,
                                      char* buffer, int size);

/**
 * Get the default candidate address for SDP c= line
 * @param agent ICE agent handle
 * @param stream Stream ID
 * @param component Component ID
 * @param addr Buffer for IP address (min 64 bytes)
 * @param port Pointer to store port
 * @return 0 on success, error code otherwise
 */
int lws_ice_get_default_candidate(lws_ice_agent_t* agent, uint8_t stream,
                                    uint16_t component, char* addr,
                                    uint16_t* port);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_ICE_H__ */
