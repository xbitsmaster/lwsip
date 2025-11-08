/**
 * @file lws_timer.h
 * @brief Timer system for lwsip (libsip timer backend)
 *
 * Provides timer management with sorted linked list and background thread checking.
 * Time slice: 10ms (sufficient precision for SIP timers)
 */

#ifndef __LWS_TIMER_H__
#define __LWS_TIMER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Timer callback function type (matches libsip requirement)
 */
typedef void (*sip_timer_handle)(void* usrptr);

/**
 * @brief Timer ID type (matches libsip requirement)
 */
typedef void* sip_timer_t;

/**
 * @brief Initialize timer system
 *
 * Creates background thread that checks for expired timers every 10ms.
 *
 * @return 0 on success, negative error code on failure
 */
int lws_timer_init(void);

/**
 * @brief Cleanup timer system
 *
 * Stops background thread and frees all resources.
 */
void lws_timer_cleanup(void);

/**
 * @brief Start a timer (called by libsip as sip_timer_start)
 *
 * Creates a timer and adds it to sorted linked list (sorted by expire time).
 *
 * @param timeout Timeout in milliseconds
 * @param handler Callback function to call when timer expires
 * @param usrptr User data passed to callback
 * @return Timer ID (pointer to timer node), or NULL on failure
 */
sip_timer_t sip_timer_start(int timeout, sip_timer_handle handler, void* usrptr);

/**
 * @brief Stop a timer (called by libsip as sip_timer_stop)
 *
 * Removes timer from linked list and sets ID to NULL.
 *
 * @param id Pointer to timer identifier (will be set to NULL)
 * @return 0 on success, negative error code on failure
 */
int sip_timer_stop(sip_timer_t* id);

#ifdef __cplusplus
}
#endif

#endif // __LWS_TIMER_H__
