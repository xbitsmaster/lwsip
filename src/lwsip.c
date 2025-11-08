/**
 * @file lwsip.c
 * @brief lwsip library implementation
 *
 * Note: Memory management and logging are delegated to OSAL layer.
 * This file only contains library initialization, version info, and utilities.
 */

#include "../include/lwsip.h"
#include "../osal/include/lws_mem.h"
#include "../osal/include/lws_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ========================================
 * 全局变量
 * ======================================== */

static int g_lwsip_initialized = 0;

/* ========================================
 * 库初始化和清理
 * ======================================== */

int lwsip_init(void)
{
    if (g_lwsip_initialized) {
        return LWS_OK;
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
}

const char* lwsip_version(void)
{
    return LWS_VERSION_STRING;
}

void lwsip_version_number(int* major, int* minor, int* patch)
{
    if (major) *major = LWS_VERSION_MAJOR;
    if (minor) *minor = LWS_VERSION_MINOR;
    if (patch) *patch = LWS_VERSION_PATCH;
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

int lwsip_generate_uuid(char* buf, size_t size)
{
    if (!buf || size < 37) {
        return LWS_EINVAL;
    }

    /* 简单的UUID生成 (格式: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx) */
    uint32_t d1 = lwsip_random(0, 0xFFFFFFFF);
    uint16_t d2 = (uint16_t)lwsip_random(0, 0xFFFF);
    uint16_t d3 = (uint16_t)lwsip_random(0, 0xFFFF);
    uint16_t d4 = (uint16_t)lwsip_random(0, 0xFFFF);
    uint64_t d5 = ((uint64_t)lwsip_random(0, 0xFFFF) << 32) |
                   lwsip_random(0, 0xFFFFFFFF);

    snprintf(buf, size, "%08x-%04x-%04x-%04x-%012llx",
             d1, d2, d3, d4, (unsigned long long)d5);

    return LWS_OK;
}
