/**
 * @file trans_stub.h
 * @brief Intelligent transport stub for lws_agent unit tests
 *
 * Provides scenario-based SIP response generation for comprehensive testing.
 * This stub intercepts lws_trans_send() calls, parses SIP requests,
 * and generates appropriate SIP responses based on the configured scenario.
 */

#ifndef __TRANS_STUB_H__
#define __TRANS_STUB_H__

#include <stdint.h>

/**
 * @brief Test scenario types
 */
typedef enum {
    TRANS_STUB_SCENARIO_NONE = 0,           /**< No automatic responses */

    /* REGISTER scenarios */
    TRANS_STUB_SCENARIO_REGISTER_SUCCESS,   /**< REGISTER → 200 OK */
    TRANS_STUB_SCENARIO_REGISTER_AUTH,      /**< REGISTER → 401 → 200 OK (with digest auth) */
    TRANS_STUB_SCENARIO_REGISTER_FAILURE,   /**< REGISTER → 403 Forbidden */

    /* INVITE scenarios */
    TRANS_STUB_SCENARIO_INVITE_SUCCESS,     /**< INVITE → 180 Ringing → 200 OK */
    TRANS_STUB_SCENARIO_INVITE_BUSY,        /**< INVITE → 486 Busy Here */
    TRANS_STUB_SCENARIO_INVITE_DECLINED,    /**< INVITE → 603 Decline */

    /* BYE scenarios */
    TRANS_STUB_SCENARIO_BYE_SUCCESS,        /**< BYE → 200 OK */

    /* CANCEL scenarios */
    TRANS_STUB_SCENARIO_CANCEL_SUCCESS,     /**< CANCEL → 200 OK */
} trans_stub_scenario_t;

/**
 * @brief Initialize transport stub
 * @return 0 on success, -1 on error
 */
int trans_stub_init(void);

/**
 * @brief Cleanup transport stub
 */
void trans_stub_cleanup(void);

/**
 * @brief Set current test scenario
 * @param scenario Test scenario to use
 */
void trans_stub_set_scenario(trans_stub_scenario_t scenario);

/**
 * @brief Get current test scenario
 * @return Current scenario
 */
trans_stub_scenario_t trans_stub_get_scenario(void);

/**
 * @brief Process pending responses (call from main test loop)
 *
 * This function should be called periodically to trigger queued responses.
 * It simulates asynchronous response delivery by invoking the transport
 * on_data callback for queued responses.
 *
 * @return Number of responses processed
 */
int trans_stub_process_responses(void);

/**
 * @brief Get last sent SIP request (for verification in tests)
 * @return Pointer to last request buffer (read-only), NULL if none
 */
const char* trans_stub_get_last_request(void);

/**
 * @brief Get length of last sent request
 * @return Length in bytes, 0 if none
 */
int trans_stub_get_last_request_len(void);

/**
 * @brief Clear last sent request buffer
 */
void trans_stub_clear_last_request(void);

/**
 * @brief Set response delay in milliseconds
 * @param delay_ms Delay before responses are delivered (0 = immediate, default)
 */
void trans_stub_set_response_delay(int delay_ms);

/**
 * @brief Get response delay setting
 * @return Current delay in milliseconds
 */
int trans_stub_get_response_delay(void);

#endif /* __TRANS_STUB_H__ */
