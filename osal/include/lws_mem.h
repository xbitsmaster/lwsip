#ifndef __LWS_MEM_H__
#define __LWS_MEM_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file lws_memory.h
 * @brief Memory management abstraction layer for lwsip
 */

/**
 * Allocate memory block
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory, NULL on failure
 */
void* lws_malloc(size_t size);

/**
 * Allocate memory block and initialize to zero
 * @param nmemb Number of elements
 * @param size Size of each element in bytes
 * @return Pointer to allocated and zeroed memory, NULL on failure
 */
void* lws_calloc(size_t nmemb, size_t size);

/**
 * Reallocate memory block
 * @param ptr Pointer to previously allocated memory (can be NULL)
 * @param size New size in bytes
 * @return Pointer to reallocated memory, NULL on failure
 */
void* lws_realloc(void* ptr, size_t size);

/**
 * Free allocated memory
 * @param ptr Pointer to memory to free (can be NULL)
 */
void lws_free(void* ptr);

/**
 * Duplicate a string (allocates memory)
 * @param s String to duplicate
 * @return Pointer to duplicated string, NULL on failure
 */
char* lws_strdup(const char* s);

/**
 * Duplicate a string with maximum length (allocates memory)
 * @param s String to duplicate
 * @param n Maximum number of characters to copy
 * @return Pointer to duplicated string, NULL on failure
 */
char* lws_strndup(const char* s, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* __LWS_MEMORY_H__ */
