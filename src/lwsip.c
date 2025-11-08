/**
 * @file lwsip.c
 * @brief lwsip library implementation
 */

#include "lwsip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

/* ========================================
 * 全局变量
 * ======================================== */

static int g_lwsip_initialized = 0;
static lwsip_log_level_t g_log_level = LWSIP_LOG_INFO;
static lwsip_log_handler_f g_log_handler = NULL;
static lwsip_malloc_f g_malloc_func = NULL;
static lwsip_free_f g_free_func = NULL;
static lwsip_get_time_us_f g_get_time_func = NULL;

/* ========================================
 * 内部函数
 * ======================================== */

/**
 * @brief 默认日志处理函数
 */
static void default_log_handler(
    lwsip_log_level_t level,
    const char* file,
    int line,
    const char* func,
    const char* format,
    va_list args)
{
    const char* level_str[] = {
        "ERROR", "WARN", "INFO", "DEBUG", "TRACE"
    };

    fprintf(stderr, "[%s] %s:%d %s() - ",
            level_str[level], file, line, func);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
}

/**
 * @brief 默认获取时间函数（微秒）
 */
static uint64_t default_get_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* ========================================
 * 库初始化和清理
 * ======================================== */

int lwsip_init(void)
{
    if (g_lwsip_initialized) {
        return LWS_OK;
    }

    /* 设置默认日志处理器 */
    if (g_log_handler == NULL) {
        g_log_handler = default_log_handler;
    }

    /* 设置默认内存分配器 */
    if (g_malloc_func == NULL) {
        g_malloc_func = malloc;
    }
    if (g_free_func == NULL) {
        g_free_func = free;
    }

    /* 设置默认时间函数 */
    if (g_get_time_func == NULL) {
        g_get_time_func = default_get_time_us;
    }

    /* 初始化随机数种子 */
    srand((unsigned int)time(NULL));

    g_lwsip_initialized = 1;

    return LWS_OK;
}

void lwsip_cleanup(void)
{
    if (!g_lwsip_initialized) {
        return;
    }

    g_lwsip_initialized = 0;
    g_log_handler = NULL;
    g_malloc_func = NULL;
    g_free_func = NULL;
    g_get_time_func = NULL;
}

const char* lwsip_version(void)
{
    return LWSIP_VERSION_STRING;
}

void lwsip_version_number(int* major, int* minor, int* patch)
{
    if (major) *major = LWSIP_VERSION_MAJOR;
    if (minor) *minor = LWSIP_VERSION_MINOR;
    if (patch) *patch = LWSIP_VERSION_PATCH;
}

/* ========================================
 * 日志系统
 * ======================================== */

void lwsip_set_log_handler(lwsip_log_handler_f handler)
{
    g_log_handler = handler ? handler : default_log_handler;
}

void lwsip_set_log_level(lwsip_log_level_t level)
{
    g_log_level = level;
}

lwsip_log_level_t lwsip_get_log_level(void)
{
    return g_log_level;
}

void lwsip_log(
    lwsip_log_level_t level,
    const char* file,
    int line,
    const char* func,
    const char* format,
    ...)
{
    if (level > g_log_level) {
        return;
    }

    if (g_log_handler) {
        va_list args;
        va_start(args, format);
        g_log_handler(level, file, line, func, format, args);
        va_end(args);
    }
}

/* ========================================
 * 内存管理
 * ======================================== */

void lwsip_set_allocator(lwsip_malloc_f malloc_func, lwsip_free_f free_func)
{
    g_malloc_func = malloc_func ? malloc_func : malloc;
    g_free_func = free_func ? free_func : free;
}

void* lwsip_malloc(size_t size)
{
    return g_malloc_func ? g_malloc_func(size) : NULL;
}

void lwsip_free(void* ptr)
{
    if (g_free_func && ptr) {
        g_free_func(ptr);
    }
}

/* ========================================
 * 时间函数
 * ======================================== */

void lwsip_set_time_func(lwsip_get_time_us_f get_time_func)
{
    g_get_time_func = get_time_func ? get_time_func : default_get_time_us;
}

uint64_t lwsip_get_time_us(void)
{
    return g_get_time_func ? g_get_time_func() : 0;
}

uint64_t lwsip_get_time_ms(void)
{
    return lwsip_get_time_us() / 1000;
}

/* ========================================
 * 错误码
 * ======================================== */

const char* lwsip_strerror(int error_code)
{
    switch (error_code) {
        case LWS_OK:           return "Success";
        case LWS_ERROR:        return "General error";
        case LWS_EINVAL:       return "Invalid argument";
        case LWS_ENOMEM:       return "Out of memory";
        case LWS_ETIMEOUT:     return "Timeout";
        case LWS_ENOTCONN:     return "Not connected";
        case LWS_ECONNREFUSED: return "Connection refused";
        case LWS_ECONNRESET:   return "Connection reset";
        case LWS_EAGAIN:       return "Try again";
        case LWS_ENOTSUP:      return "Not supported";
        case LWS_EBUSY:        return "Device busy";
        case LWS_ENODEV:       return "No such device";
        default:               return "Unknown error";
    }
}

/* ========================================
 * 实用工具
 * ======================================== */

uint32_t lwsip_random(uint32_t min, uint32_t max)
{
    if (min >= max) {
        return min;
    }
    return min + (rand() % (max - min + 1));
}

int lwsip_generate_uuid(char* buf)
{
    if (!buf) {
        return LWS_EINVAL;
    }

    /* 简单的UUID生成 (格式: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx) */
    uint32_t d1 = lwsip_random(0, 0xFFFFFFFF);
    uint16_t d2 = (uint16_t)lwsip_random(0, 0xFFFF);
    uint16_t d3 = (uint16_t)lwsip_random(0, 0xFFFF);
    uint16_t d4 = (uint16_t)lwsip_random(0, 0xFFFF);
    uint64_t d5 = ((uint64_t)lwsip_random(0, 0xFFFF) << 32) |
                   lwsip_random(0, 0xFFFFFFFF);

    snprintf(buf, 37, "%08x-%04x-%04x-%04x-%012llx",
             d1, d2, d3, d4, (unsigned long long)d5);

    return LWS_OK;
}
