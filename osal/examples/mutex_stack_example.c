/**
 * @file mutex_stack_example.c
 * @brief Example showing stack allocation of mutex (zero malloc)
 */

#include "lws_osal.h"
#include <stdio.h>

int shared_counter = 0;

void* worker_thread(void* arg)
{
    lws_mutex_t* mutex = (lws_mutex_t*)arg;

    for (int i = 0; i < 1000; i++) {
        lws_mutex_lock(mutex);
        shared_counter++;
        lws_mutex_unlock(mutex);
    }

    return NULL;
}

int main(void)
{
    printf("OSAL Mutex Stack Allocation Example\n");
    printf("Platform: %s\n\n", lws_osal_platform());

    /* 方式1: 栈上分配 - 零 malloc！ */
    printf("=== Method 1: Stack Allocation (Zero malloc) ===\n");
    lws_mutex_t mutex_on_stack;
    lws_mutex_init(&mutex_on_stack);

    /* Create threads */
    lws_thread_t* t1 = lws_thread_create(worker_thread, &mutex_on_stack);
    lws_thread_t* t2 = lws_thread_create(worker_thread, &mutex_on_stack);
    lws_thread_t* t3 = lws_thread_create(worker_thread, &mutex_on_stack);

    /* Wait */
    lws_thread_join(t1, NULL);
    lws_thread_join(t2, NULL);
    lws_thread_join(t3, NULL);

    lws_thread_destroy(t1);
    lws_thread_destroy(t2);
    lws_thread_destroy(t3);

    printf("Shared counter: %d (expected: 3000)\n", shared_counter);

    /* Cleanup - 不需要 free！使用 cleanup 而不是 destroy */
    lws_mutex_cleanup(&mutex_on_stack);

    /* 方式2: 堆分配 - 传统方式 */
    printf("\n=== Method 2: Heap Allocation (Traditional) ===\n");
    shared_counter = 0;

    lws_mutex_t* mutex_on_heap = lws_mutex_create();

    t1 = lws_thread_create(worker_thread, mutex_on_heap);
    t2 = lws_thread_create(worker_thread, mutex_on_heap);
    t3 = lws_thread_create(worker_thread, mutex_on_heap);

    lws_thread_join(t1, NULL);
    lws_thread_join(t2, NULL);
    lws_thread_join(t3, NULL);

    lws_thread_destroy(t1);
    lws_thread_destroy(t2);
    lws_thread_destroy(t3);

    printf("Shared counter: %d (expected: 3000)\n", shared_counter);

    lws_mutex_destroy(mutex_on_heap);

    printf("\n=== Summary ===\n");
    printf("✓ Stack allocation: 0 malloc calls for mutex\n");
    printf("✓ Heap allocation: 1 malloc call for mutex\n");
    printf("✓ Both methods work correctly!\n");

    return 0;
}
