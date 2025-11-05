#include "lws_thread.h"
#include "lws_mem.h"
#include <pthread.h>
#include <unistd.h>
#include <time.h>

lws_thread_t* lws_thread_create(lws_thread_func_t func, void* arg)
{
    if (!func)
        return NULL;

    lws_thread_t* thread = (lws_thread_t*)lws_malloc(sizeof(lws_thread_t));
    if (!thread)
        return NULL;

    /* Create thread directly - lws_thread_t is pthread_t */
    if (pthread_create(thread, NULL, func, arg) != 0) {
        lws_free(thread);
        return NULL;
    }

    return thread;
}

int lws_thread_join(lws_thread_t* thread, void** retval)
{
    if (!thread)
        return -1;

    return pthread_join(*thread, retval) == 0 ? 0 : -1;
}

void lws_thread_destroy(lws_thread_t* thread)
{
    lws_free(thread);
}

int lws_thread_detach(lws_thread_t* thread)
{
    if (!thread)
        return -1;

    return pthread_detach(*thread) == 0 ? 0 : -1;
}

unsigned long lws_thread_self(void)
{
    return (unsigned long)pthread_self();
}

void lws_thread_sleep(unsigned int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
