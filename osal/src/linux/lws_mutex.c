#include "lws_mutex.h"
#include "lws_mem.h"
#include <pthread.h>

lws_mutex_t* lws_mutex_create(void)
{
    lws_mutex_t* mutex = (lws_mutex_t*)lws_calloc(1, sizeof(lws_mutex_t));
    if (!mutex)
        return NULL;

    if (pthread_mutex_init(mutex, NULL) != 0) {
        lws_free(mutex);
        return NULL;
    }

    return mutex;
}

void lws_mutex_init(lws_mutex_t* mutex)
{
    if (mutex)
        pthread_mutex_init(mutex, NULL);
}

void lws_mutex_cleanup(lws_mutex_t* mutex)
{
    if (mutex)
        pthread_mutex_destroy(mutex);
}

void lws_mutex_destroy(lws_mutex_t* mutex)
{
    if (mutex) {
        pthread_mutex_destroy(mutex);
        lws_free(mutex);
    }
}

int lws_mutex_lock(lws_mutex_t* mutex)
{
    if (!mutex)
        return -1;

    return pthread_mutex_lock(mutex) == 0 ? 0 : -1;
}

int lws_mutex_trylock(lws_mutex_t* mutex)
{
    if (!mutex)
        return -1;

    return pthread_mutex_trylock(mutex) == 0 ? 0 : -1;
}

int lws_mutex_unlock(lws_mutex_t* mutex)
{
    if (!mutex)
        return -1;

    return pthread_mutex_unlock(mutex) == 0 ? 0 : -1;
}
