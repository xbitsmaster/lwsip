/**
 * @file thread_example.c
 * @brief Example of using lws_thread API
 */

#include "lws_osal.h"
#include <stdio.h>

/* Simple thread function - matches pthread signature */
void* worker_thread(void* arg)
{
    int id = (int)(long)arg;
    printf("Worker thread %d started\n", id);

    lws_thread_sleep(1000);  /* Sleep 1 second */

    printf("Worker thread %d finished\n", id);
    return (void*)(long)(id * 100);  /* Return exit code */
}

int main(void)
{
    printf("OSAL Thread Example\n");
    printf("Platform: %s\n", lws_osal_platform());
    printf("Version: %s\n\n", lws_osal_version());

    /* Create threads */
    lws_thread_t* t1 = lws_thread_create(worker_thread, (void*)1L);
    lws_thread_t* t2 = lws_thread_create(worker_thread, (void*)2L);
    lws_thread_t* t3 = lws_thread_create(worker_thread, (void*)3L);

    if (!t1 || !t2 || !t3) {
        fprintf(stderr, "Failed to create threads\n");
        return 1;
    }

    printf("All threads created, waiting...\n\n");

    /* Join threads and get return values */
    void* ret1;
    void* ret2;
    void* ret3;

    lws_thread_join(t1, &ret1);
    lws_thread_join(t2, &ret2);
    lws_thread_join(t3, &ret3);

    printf("\nThread 1 returned: %ld\n", (long)ret1);
    printf("Thread 2 returned: %ld\n", (long)ret2);
    printf("Thread 3 returned: %ld\n", (long)ret3);

    /* Cleanup */
    lws_thread_destroy(t1);
    lws_thread_destroy(t2);
    lws_thread_destroy(t3);

    printf("\nExample completed successfully\n");
    return 0;
}
