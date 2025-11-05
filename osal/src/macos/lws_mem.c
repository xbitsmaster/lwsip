#include "lws_mem.h"
#include <stdlib.h>
#include <string.h>

void* lws_malloc(size_t size)
{
    return malloc(size);
}

void* lws_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void* lws_realloc(void* ptr, size_t size)
{
    return realloc(ptr, size);
}

void lws_free(void* ptr)
{
    free(ptr);
}

char* lws_strdup(const char* s)
{
    if (!s)
        return NULL;

    size_t len = strlen(s) + 1;
    char* dup = (char*)malloc(len);
    if (dup)
        memcpy(dup, s, len);
    return dup;
}

char* lws_strndup(const char* s, size_t n)
{
    if (!s)
        return NULL;

    size_t len = strlen(s);
    if (len > n)
        len = n;

    char* dup = (char*)malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}
