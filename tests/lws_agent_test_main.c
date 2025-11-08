/**
 * @file lws_agent_test_main.c
 * @brief Test main for lws_agent with timer system
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "lws_timer.h"
#include "../osal/include/lws_log.h"

/* Simple test callback */
static void test_timer_callback(void* userdata)
{
    int* counter = (int*)userdata;
    (*counter)++;
    printf("Timer expired! Counter = %d\n", *counter);
}

int main(void)
{
    printf("========================================\n");
    printf("lws_agent Test Program\n");
    printf("========================================\n\n");

    /* Test 1: Timer system init */
    printf("[Test 1] Initializing timer system...\n");
    if (lws_timer_init() != 0) {
        printf("  FAILED: Timer init failed\n");
        return 1;
    }
    printf("  PASSED: Timer system initialized\n\n");

    /* Test 2: Start and stop timers */
    printf("[Test 2] Starting test timers...\n");

    int counter1 = 0;
    int counter2 = 0;

    void* timer_id1 = (void*)0x1001;
    void* timer_id2 = (void*)0x1002;

    /* Start timer 1: 100ms */
    if (sip_timer_start(timer_id1, 100, test_timer_callback, &counter1) != 0) {
        printf("  FAILED: Timer 1 start failed\n");
        lws_timer_cleanup();
        return 1;
    }
    printf("  Timer 1 started (100ms)\n");

    /* Start timer 2: 200ms */
    if (sip_timer_start(timer_id2, 200, test_timer_callback, &counter2) != 0) {
        printf("  FAILED: Timer 2 start failed\n");
        lws_timer_cleanup();
        return 1;
    }
    printf("  Timer 2 started (200ms)\n\n");

    /* Test 3: Wait for timer 1 to expire */
    printf("[Test 3] Waiting for timer 1 (100ms)...\n");
    usleep(150000);  /* 150ms */

    if (counter1 != 1) {
        printf("  FAILED: Timer 1 didn't expire (counter=%d, expected=1)\n", counter1);
        lws_timer_cleanup();
        return 1;
    }
    printf("  PASSED: Timer 1 expired correctly\n\n");

    /* Test 4: Stop timer 2 before it expires */
    printf("[Test 4] Stopping timer 2 before expiry...\n");
    if (sip_timer_stop(timer_id2) != 0) {
        printf("  FAILED: Timer 2 stop failed\n");
        lws_timer_cleanup();
        return 1;
    }
    printf("  Timer 2 stopped\n");

    /* Wait to confirm timer 2 doesn't fire */
    usleep(100000);  /* 100ms more (total 250ms > 200ms) */

    if (counter2 != 0) {
        printf("  FAILED: Timer 2 fired after being stopped (counter=%d)\n", counter2);
        lws_timer_cleanup();
        return 1;
    }
    printf("  PASSED: Timer 2 correctly stopped\n\n");

    /* Test 5: Multiple timers */
    printf("[Test 5] Testing multiple timers...\n");

    counter1 = 0;
    counter2 = 0;

    /* Start multiple timers with different timeouts */
    sip_timer_start((void*)0x2001, 50, test_timer_callback, &counter1);
    sip_timer_start((void*)0x2002, 100, test_timer_callback, &counter1);
    sip_timer_start((void*)0x2003, 150, test_timer_callback, &counter2);

    printf("  Started 3 timers (50ms, 100ms, 150ms)\n");

    /* Wait for all to expire */
    usleep(200000);  /* 200ms */

    if (counter1 != 2 || counter2 != 1) {
        printf("  FAILED: Timers didn't expire correctly (counter1=%d expected=2, counter2=%d expected=1)\n",
               counter1, counter2);
        lws_timer_cleanup();
        return 1;
    }
    printf("  PASSED: All timers expired correctly\n\n");

    /* Cleanup */
    printf("[Cleanup] Shutting down timer system...\n");
    lws_timer_cleanup();
    printf("  Timer system cleaned up\n\n");

    printf("========================================\n");
    printf("ALL TESTS PASSED!\n");
    printf("========================================\n");

    return 0;
}
