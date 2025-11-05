#ifndef __LWS_OSAL_H__
#define __LWS_OSAL_H__

/**
 * @file lws_osal.h
 * @brief Master header for lwsip OS Abstraction Layer
 *
 * Include this file to access all OSAL functionality.
 */

#include "lws_thread.h"
#include "lws_mutex.h"
#include "lws_mem.h"
#include "lws_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize OSAL subsystem
 * @return 0 on success, -1 on error
 */
int lws_osal_init(void);

/**
 * Cleanup OSAL subsystem
 */
void lws_osal_cleanup(void);

/**
 * Get OSAL version string
 * @return Version string
 */
const char* lws_osal_version(void);

/**
 * Get current platform name
 * @return Platform name ("linux", "macos", "freertos", etc.)
 */
const char* lws_osal_platform(void);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_OSAL_H__ */
