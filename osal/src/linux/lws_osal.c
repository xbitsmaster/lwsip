#include "lws_osal.h"

#define LWS_OSAL_VERSION "1.0.0"
#define LWS_OSAL_PLATFORM "linux"

int lws_osal_init(void)
{
    /* No initialization needed for Linux */
    return 0;
}

void lws_osal_cleanup(void)
{
    /* No cleanup needed for Linux */
}

const char* lws_osal_version(void)
{
    return LWS_OSAL_VERSION;
}

const char* lws_osal_platform(void)
{
    return LWS_OSAL_PLATFORM;
}
