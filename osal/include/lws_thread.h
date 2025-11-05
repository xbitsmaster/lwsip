#ifndef __LWS_THREAD_H__
#define __LWS_THREAD_H__

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __LWS_PTHREAD__
typedef pthread_t lws_thread_t;
#else
#error "Unsupported platform"
#endif

/* Thread function prototype - compatible with pthread */
typedef void* (*lws_thread_func_t)(void* arg);

/**
 * Create and start a new thread
 * @param func Thread function to execute
 * @param arg Argument passed to thread function
 * @return Thread handle on success, NULL on error
 */
lws_thread_t* lws_thread_create(lws_thread_func_t func, void* arg);

/**
 * Wait for thread to complete
 * @param thread Thread handle
 * @param retval Pointer to store thread return value (can be NULL)
 * @return 0 on success, -1 on error
 */
int lws_thread_join(lws_thread_t* thread, void** retval);

/**
 * Destroy thread handle (must be called after join or detach)
 * @param thread Thread handle
 */
void lws_thread_destroy(lws_thread_t* thread);

/**
 * Detach thread (thread will clean up automatically on exit)
 * @param thread Thread handle
 * @return 0 on success, -1 on error
 */
int lws_thread_detach(lws_thread_t* thread);

/**
 * Get current thread ID
 * @return Current thread ID
 */
unsigned long lws_thread_self(void);

/**
 * Sleep for specified milliseconds
 * @param ms Milliseconds to sleep
 */
void lws_thread_sleep(unsigned int ms);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_THREAD_H__ */
