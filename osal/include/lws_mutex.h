#ifndef __LWS_MUTEX_H__
#define __LWS_MUTEX_H__

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif


#ifdef __LWS_PTHREAD__
typedef pthread_mutex_t lws_mutex_t;
#else
#error "Unsupported platform"
#endif

/**
 * Create a new mutex
 * @return Mutex handle on success, NULL on error
 */
lws_mutex_t* lws_mutex_create(void);

/**
 * Initialize a mutex (for stack-allocated mutex)
 * @param mutex Mutex handle
 */
void lws_mutex_init(lws_mutex_t* mutex);

/**
 * Cleanup a mutex (for stack-allocated mutex)
 * Does NOT free the memory, only destroys the pthread mutex
 * @param mutex Mutex handle
 */
void lws_mutex_cleanup(lws_mutex_t* mutex);

/**
 * Destroy a mutex (for heap-allocated mutex)
 * Destroys the pthread mutex AND frees the memory
 * @param mutex Mutex handle
 */
void lws_mutex_destroy(lws_mutex_t* mutex);

/**
 * Lock a mutex (blocking)
 * @param mutex Mutex handle
 * @return 0 on success, -1 on error
 */
int lws_mutex_lock(lws_mutex_t* mutex);

/**
 * Try to lock a mutex (non-blocking)
 * @param mutex Mutex handle
 * @return 0 on success, -1 if mutex is already locked or error
 */
int lws_mutex_trylock(lws_mutex_t* mutex);

/**
 * Unlock a mutex
 * @param mutex Mutex handle
 * @return 0 on success, -1 on error
 */
int lws_mutex_unlock(lws_mutex_t* mutex);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_MUTEX_H__ */
