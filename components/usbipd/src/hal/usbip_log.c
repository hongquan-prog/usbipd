/*
 * USBIP Log System
 *
 * Reference Zephyr logging design:
 * - Unified logging macros: LOG_ERR, LOG_WRN, LOG_INF, LOG_DBG
 * - Define module TAG and log level via LOG_MODULE_REGISTER
 * - Support timestamp output
 */

/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-3-24      hongquan.li   add license declaration
 */

#include "hal/usbip_log.h"
#include "hal/usbip_osal.h"

#include <stdio.h>
#include <time.h>

static struct osal_mutex log_mutex;
static int log_initialized = 0;

int usbip_log_init(void)
{
    int ret;

    if (log_initialized)
    {
        return 0;
    }

    ret = osal_mutex_init(&log_mutex);
    if (ret != OSAL_OK)
    {
        return ret;
    }

    log_initialized = 1;
    return 0;
}

void usbip_log_printf(int level, const char* tag, const unsigned char* data, uint32_t len,
                      const char* fmt, ...)
{
    va_list args;
    time_t now;
    struct tm tm_info;
    struct timespec ts;
    char time_buf[16];
    const char* level_str;
    const char* level_color;
    FILE* fp;

    osal_mutex_lock(&log_mutex);
    /* Get time */
    clock_gettime(CLOCK_REALTIME, &ts);
    now = ts.tv_sec;
    localtime_r(&now, &tm_info);

    /* Format timestamp: HH:MM:SS.mmm */
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_info);

    /* Level string and color */
    switch (level)
    {
        case LOG_LEVEL_ERR:
            level_str = "ERR";
            level_color = LOG_COLOR_RED;
            fp = stderr;
            break;
        case LOG_LEVEL_WRN:
            level_str = "WRN";
            level_color = LOG_COLOR_YELLOW;
            fp = stderr;
            break;
        case LOG_LEVEL_INF:
            level_str = "INF";
            level_color = LOG_COLOR_GREEN;
            fp = stdout;
            break;
        case LOG_LEVEL_DBG:
            level_str = "DBG";
            level_color = LOG_COLOR_NONE;
            fp = stdout;
            break;
        default:
            level_str = "???";
            level_color = LOG_COLOR_NONE;
            fp = stdout;
            break;
    }

    /* Output format: [color]HH:MM:SS.mmm [L] [tag] message */
    if (LOG_USE_COLOR)
    {
        fprintf(fp, "%s%s.%03ld %s [%s] ", level_color, time_buf, ts.tv_nsec / 1000000, tag,
                level_str);
    }
    else
    {
        fprintf(fp, "%s.%03ld %s [%s] ", time_buf, ts.tv_nsec / 1000000, tag, level_str);
    }

    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    /* Output hex data (if any) */
    if (data && len > 0)
    {
        for (uint32_t i = 0; i < len; i++)
        {
            if ((i % 16) == 0)
            {
                fprintf(fp, "\n");
            }

            fprintf(fp, "%02X ", data[i]);
        }
    }

    fprintf(fp, "\n");
    if (LOG_USE_COLOR)
    {
        fprintf(fp, LOG_COLOR_NONE);
    }
    fflush(fp);

    osal_mutex_unlock(&log_mutex);
}