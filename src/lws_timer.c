/**
 * @file lws_timer.c
 * @brief Timer system implementation (libsip timer backend)
 *
 * Uses sorted doubly-linked list (list.h) and background thread with 10ms time slice.
 */

#include "lws_timer.h"
#include "lws_mem.h"
#include "lws_log.h"
#include "lws_thread.h"
#include "lws_mutex.h"
#include "list.h"
#include <string.h>
#include <sys/time.h>

/* ========================================
 * Timer Node Structure
 * ======================================== */

typedef struct timer_node_t {
    uint64_t expire_time_ms;           /**< Absolute expire time (milliseconds) */
    sip_timer_handle handler;          /**< Callback function */
    void* usrptr;                      /**< User data for callback */
    struct list_head list;             /**< list.h doubly-linked list node */
} timer_node_t;

/* ========================================
 * Timer Manager Structure
 * ======================================== */

typedef struct {
    struct list_head timers;           /**< list.h doubly-linked list head */
    lws_mutex_t mutex;                 /**< Mutex for thread safety */
    lws_thread_t* thread;              /**< Background timer thread */
    int running;                       /**< Thread running flag */
} timer_manager_t;

static timer_manager_t g_timer_mgr = {0};

/* ========================================
 * Helper Functions
 * ======================================== */

/**
 * @brief Get current time in milliseconds
 */
static uint64_t get_current_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/**
 * @brief Insert timer into sorted list (sorted by expire_time_ms)
 *
 * Uses list.h doubly-linked list infrastructure for sorted insertion.
 *
 * @param node Timer node to insert
 */
static void timer_list_insert_sorted(timer_node_t* node)
{
    struct list_head* __pos;
    timer_node_t* pos;

    /* Find insertion point (keep list sorted by expire time) */
    list_for_each(__pos, &g_timer_mgr.timers) {
        pos = list_entry(__pos, timer_node_t, list);
        if (node->expire_time_ms < pos->expire_time_ms) {
            /* Insert before this position */
            list_insert_before(&node->list, &pos->list);
            return;
        }
    }

    /* If we reach here, insert at tail (largest expire time) */
    list_insert_before(&node->list, &g_timer_mgr.timers);
}

/**
 * @brief Remove timer from list by node pointer
 *
 * Uses list.h doubly-linked list infrastructure.
 *
 * @param node Timer node pointer to remove
 * @return Removed timer node, or NULL if not found
 */
static timer_node_t* timer_list_remove(timer_node_t* node)
{
    struct list_head* __pos;
    timer_node_t* pos;

    /* Find timer with matching pointer address */
    list_for_each(__pos, &g_timer_mgr.timers) {
        pos = list_entry(__pos, timer_node_t, list);
        if (pos == node) {
            /* Remove from list */
            list_remove(&pos->list);
            return pos;
        }
    }

    return NULL;
}

/* ========================================
 * Timer Thread
 * ======================================== */

/**
 * @brief Timer loop thread function
 *
 * Checks for expired timers every 10ms and calls their callbacks.
 * Uses list.h for safe iteration.
 */
static void* lws_timer_loop(void* arg)
{
    (void)arg;

    lws_log_info("Timer thread started (10ms time slice)\n");

    while (g_timer_mgr.running) {
        uint64_t now = get_current_time_ms();
        struct list_head *__pos, *__n;
        timer_node_t* pos;

        lws_mutex_lock(&g_timer_mgr.mutex);

        /* Check for expired timers (list is sorted, so stop at first non-expired) */
        list_for_each_safe(__pos, __n, &g_timer_mgr.timers) {
            pos = list_entry(__pos, timer_node_t, list);
            if (pos->expire_time_ms > now) {
                /* List is sorted - no more expired timers */
                break;
            }

            /* Timer expired - remove from list */
            list_remove(&pos->list);

            /* Call callback without holding lock (avoid deadlock) */
            sip_timer_handle handler = pos->handler;
            void* usrptr = pos->usrptr;

            lws_mutex_unlock(&g_timer_mgr.mutex);

            if (handler) {
                handler(usrptr);
            }

            /* Free timer node */
            lws_free(pos);

            /* Re-acquire lock for next iteration */
            lws_mutex_lock(&g_timer_mgr.mutex);

            /* Refresh current time */
            now = get_current_time_ms();
        }

        lws_mutex_unlock(&g_timer_mgr.mutex);

        /* Sleep 10ms (time slice) */
        lws_thread_sleep(10);
    }

    lws_log_info("Timer thread stopped\n");
    return NULL;
}

/* ========================================
 * Public API
 * ======================================== */

int lws_timer_init(void)
{
    if (g_timer_mgr.running) {
        lws_log_warn(0, "Timer system already initialized\n");
        return 0;
    }

    memset(&g_timer_mgr, 0, sizeof(g_timer_mgr));

    /* Initialize list head */
    LIST_INIT_HEAD(&g_timer_mgr.timers);

    /* Create mutex */
    lws_mutex_init(&g_timer_mgr.mutex);

    /* Start timer thread */
    g_timer_mgr.running = 1;

    g_timer_mgr.thread = lws_thread_create(lws_timer_loop, NULL);
    if (g_timer_mgr.thread == NULL) {
        lws_log_error(0, "Failed to create timer thread\n");
        lws_mutex_destroy(&g_timer_mgr.mutex);
        g_timer_mgr.running = 0;
        return -1;
    }

    lws_log_info("Timer system initialized\n");
    return 0;
}

void lws_timer_cleanup(void)
{
    if (!g_timer_mgr.running) {
        return;
    }

    /* Stop timer thread */
    g_timer_mgr.running = 0;

    /* Wait for thread to finish */
    if (g_timer_mgr.thread) {
        lws_thread_join(g_timer_mgr.thread, NULL);
    }

    /* Free all remaining timers */
    lws_mutex_lock(&g_timer_mgr.mutex);

    struct list_head *__pos, *__n;
    timer_node_t* pos;
    list_for_each_safe(__pos, __n, &g_timer_mgr.timers) {
        pos = list_entry(__pos, timer_node_t, list);
        list_remove(&pos->list);
        lws_free(pos);
    }

    lws_mutex_unlock(&g_timer_mgr.mutex);

    /* Destroy mutex */
    lws_mutex_cleanup(&g_timer_mgr.mutex);

    lws_log_info("Timer system cleaned up\n");
}

sip_timer_t sip_timer_start(int timeout, sip_timer_handle handler, void* usrptr)
{
    if (!handler || timeout < 0) {
        lws_log_error(0, "Invalid timer parameters (handler=%p, timeout=%d)\n", handler, timeout);
        return NULL;
    }

    if (!g_timer_mgr.running) {
        lws_log_error(0, "Timer system not initialized\n");
        return NULL;
    }

    /* Allocate timer node */
    timer_node_t* node = (timer_node_t*)lws_calloc(1, sizeof(timer_node_t));
    if (!node) {
        lws_log_error(0, "Failed to allocate timer node\n");
        return NULL;
    }

    /* Initialize timer node */
    node->expire_time_ms = get_current_time_ms() + (uint64_t)timeout;
    node->handler = handler;
    node->usrptr = usrptr;
    LIST_INIT_HEAD(&node->list);

    /* Insert into sorted list */
    lws_mutex_lock(&g_timer_mgr.mutex);
    timer_list_insert_sorted(node);
    lws_mutex_unlock(&g_timer_mgr.mutex);

    lws_log_debug("Timer started: id=%p, timeout=%dms, expire=%llu\n",
                  (void*)node, timeout, (unsigned long long)node->expire_time_ms);

    /* Return the node pointer as the timer ID */
    return (sip_timer_t)node;
}

int sip_timer_stop(sip_timer_t* id)
{
    if (!id) {
        /* NULL pointer is an error */
        lws_log_error(0, "Invalid timer ID pointer (NULL)\n");
        return -1;
    }

    if (!(*id)) {
        /* Timer already stopped or never started - return error to match libsip behavior */
        lws_log_debug("Timer ID is NULL (already stopped or never started)\n");
        return -1;
    }

    timer_node_t* node = (timer_node_t*)(*id);

    if (!g_timer_mgr.running) {
        /* Timer system not running, treat as success (timer already gone) */
        *id = NULL;  /* Clear the ID */
        return 0;
    }

    /* Remove timer from list */
    lws_mutex_lock(&g_timer_mgr.mutex);
    timer_node_t* removed = timer_list_remove(node);
    lws_mutex_unlock(&g_timer_mgr.mutex);

    if (removed) {
        lws_log_debug("Timer stopped: id=%p\n", (void*)node);
        lws_free(removed);
        *id = NULL;  /* Clear the ID */
        return 0;
    } else {
        /* Timer not found (already expired/firing) - return error so caller
         * doesn't release transaction reference (callback will do it) */
        lws_log_debug("Timer not found: id=%p (already expired/firing)\n", (void*)node);
        *id = NULL;  /* Clear the ID anyway */
        return -1;  /* Return error - timer already gone */
    }
}

/* ========================================
 * STUN Timer API (aliases for libice)
 * ======================================== */

/**
 * @brief STUN timer start (alias for sip_timer_start)
 *
 * libice uses stun_timer_* functions which are the same as sip_timer_* functions.
 */
void* stun_timer_start(int timeout, void (*handler)(void*), void* usrptr)
{
    return sip_timer_start(timeout, handler, usrptr);
}

/**
 * @brief STUN timer stop (alias for sip_timer_stop)
 *
 * libice uses stun_timer_* functions which are the same as sip_timer_* functions.
 */
void stun_timer_stop(void** id)
{
    if (id) {
        sip_timer_stop((sip_timer_t*)id);
    }
}
