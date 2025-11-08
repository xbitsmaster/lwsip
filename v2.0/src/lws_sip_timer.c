/**
 * @file lws_sip_timer.c
 * @brief Timer implementation for libsip and libice (sip_timer and stun_timer)
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "lws_mem.h"
#include "lws_log.h"

/* ============================================================
 * Timer Structure
 * ============================================================ */

typedef struct lws_timer {
    int timeout_ms;                      // Timeout in milliseconds
    void (*callback)(void* param);       // Callback function
    void* param;                         // Callback parameter
    pthread_t thread;                    // Timer thread
    volatile int cancelled;              // Cancellation flag
} lws_timer_t;

/* ============================================================
 * Timer Thread Function
 * ============================================================ */

static void* timer_thread_func(void* arg)
{
    lws_timer_t* timer = (lws_timer_t*)arg;

    lws_log_info("timer_thread_func: %d ms\n", timer->timeout_ms);

    // Sleep for the specified timeout
    usleep(timer->timeout_ms * 1000);  // usleep takes microseconds

    // Check if cancelled
    if (!timer->cancelled && timer->callback) {
        timer->callback(timer->param);
    }

    // Free timer structure
    lws_free(timer);
    return NULL;
}

/* ============================================================
 * SIP Timer Interface (libsip)
 * ============================================================ */

void* sip_timer_start(int timeout, void (*ontimeout)(void* param), void* param)
{
    lws_log_info("sip_timer_start: %d ms\n", timeout);

    if (timeout <= 0 || !ontimeout) {
        return NULL;
    }

    lws_timer_t* timer = (lws_timer_t*)lws_calloc(1, sizeof(lws_timer_t));
    if (!timer) {
        return NULL;
    }

    timer->timeout_ms = timeout;
    timer->callback = ontimeout;
    timer->param = param;
    timer->cancelled = 0;

    // Create timer thread
    if (pthread_create(&timer->thread, NULL, timer_thread_func, timer) != 0) {
        lws_free(timer);
        return NULL;
    }

    // Detach thread so it can clean up automatically
    pthread_detach(timer->thread);

    return timer;
}

void sip_timer_stop(void* id)
{
    if (!id) {
        return;
    }

    lws_timer_t* timer = (lws_timer_t*)id;

    // Set cancellation flag
    timer->cancelled = 1;

    // Note: We can't free the timer here because the thread may still be running
    // The thread will free it when it completes
}

/* ============================================================
 * STUN Timer Interface (libice)
 * ============================================================ */

void* stun_timer_start(int ms, void (*ontimer)(void* param), void* param)
{
    lws_log_info("stun_timer_start: %d ms\n", ms);

    // Same implementation as sip_timer_start
    return sip_timer_start(ms, ontimer, param);
}

int stun_timer_stop(void* timer)
{
    lws_log_info("stun_timer_stop: %p\n", timer);

    // Same implementation as sip_timer_stop
    sip_timer_stop(timer);
    return 0;  // Always return success
}
