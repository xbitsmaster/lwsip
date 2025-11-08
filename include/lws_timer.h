/**
 * @file lws_timer.h
 * @brief Timer system for libsip integration
 *
 * Provides timer initialization/cleanup and implements sip_timer_start/stop callbacks
 */

#ifndef __LWS_TIMER_H__
#define __LWS_TIMER_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize timer system
 * Must be called before using libsip
 *
 * @return 0 on success, -1 on error
 */
int lws_timer_init(void);

/**
 * Cleanup timer system
 * Should be called when shutting down
 */
void lws_timer_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_TIMER_H__ */
