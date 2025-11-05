#ifndef __LWS_LOG_H__
#define __LWS_LOG_H__

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file lws_log.h
 * @brief Logging system for lwsip
 *
 * Usage:
 *   Info/Debug/Trace (only in DEBUG mode):
 *     lws_log_info("something wrong: %d, %s\n", param_int, param_string);
 *
 *   Error/Warning (always enabled):
 *     lws_log_error(LWS_ERR_NOMEM, "alloc memory fail in: %s\n", some_param);
 */

/**
 * Info/Debug/Trace logging (only enabled in DEBUG mode)
 * These macros are compiled out completely in release builds
 *
 * Usage:
 *   lws_log_info("message: %d\n", value);
 *   lws_log_debug("debug info: %s\n", str);
 *   lws_log_trace("trace: %p\n", ptr);
 */
#ifdef DEBUG
    #define lws_log_info(fmt, ...)  fprintf(stderr, "[INFO] " fmt, ##__VA_ARGS__)
    #define lws_log_debug(fmt, ...) fprintf(stderr, "[DEBUG] " fmt, ##__VA_ARGS__)
    #define lws_log_trace(fmt, ...) fprintf(stderr, "[TRACE] " fmt, ##__VA_ARGS__)
#else
    #define lws_log_info(fmt, ...)  ((void)0)
    #define lws_log_debug(fmt, ...) ((void)0)
    #define lws_log_trace(fmt, ...) ((void)0)
#endif

/**
 * Error/Warning logging (always enabled)
 * These macros output error code in 8-digit hexadecimal format along with custom message
 *
 * Output format (error codes are negative, displayed as 0x8xxxxxxx):
 *   [ERR:0x80000202] fail to bind socket
 *   [WARN:0x80000003] waiting for response
 *
 * Usage:
 *   lws_log_error(LWS_ERR_SOCKET_BIND, "allocation failed for: %s\n", name);
 *   lws_log_warn(LWS_ERR_TIMEOUT, "operation timeout: %d ms\n", timeout);
 */
#define lws_log_error(errcode, fmt, ...) \
    fprintf(stderr, "[ERR:0x%08x] " fmt, (unsigned int)(errcode), ##__VA_ARGS__)

#define lws_log_warn(errcode, fmt, ...) \
    fprintf(stderr, "[WARN:0x%08x] " fmt, (unsigned int)(errcode), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* __LWS_LOG_H__ */
