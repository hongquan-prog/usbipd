/*
 * USBIP Log System
 *
 * 参考 Zephyr 日志设计：
 * - 统一的日志宏：LOG_ERR, LOG_WRN, LOG_INF, LOG_DBG
 * - 通过 LOG_MODULE_REGISTER 定义模块 TAG 和日志等级
 * - 支持时间戳输出
 */

/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-9-8      hongquan.li   add license declaration
 */

#ifndef USBIP_LOG_H
#define USBIP_LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * 日志颜色定义 (ANSI Escape Codes)
 *****************************************************************************/

#define LOG_COLOR_NONE "\033[0m"    /* 默认/重置 */
#define LOG_COLOR_RED "\033[31m"    /* 错误 */
#define LOG_COLOR_YELLOW "\033[33m" /* 警告 */
#define LOG_COLOR_GREEN "\033[32m"  /* 通知 */
#define LOG_COLOR_WHITE "\033[37m"  /* DBG 默认 */

/* 颜色使能控制 */
#ifndef LOG_USE_COLOR
#    define LOG_USE_COLOR 1
#endif

/*****************************************************************************
 * 日志等级定义
 *****************************************************************************/

#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERR 1
#define LOG_LEVEL_WRN 2
#define LOG_LEVEL_INF 3
#define LOG_LEVEL_DBG 4

/* 默认全局日志等级 */
#ifndef LOG_GLOBAL_LEVEL
#    define LOG_GLOBAL_LEVEL LOG_LEVEL_INF
#endif

/*****************************************************************************
 * 模块注册宏
 *
 * 在源文件开头使用：
 *   LOG_MODULE_REGISTER(my_module, LOG_LEVEL_DBG);
 *
 * 这会定义当前模块的 TAG 和日志等级
 *****************************************************************************/

#define LOG_MODULE_REGISTER(name, level)                                                           \
    static const char* const _log_module_name = #name;                                             \
    static const int _log_module_level = level

/* 默认模块注册（如果未注册则使用此默认值） */
#define LOG_MODULE_DEFAULT()                                                                       \
    static const char* const _log_module_name = "default";                                         \
    static const int _log_module_level = LOG_GLOBAL_LEVEL

/* 当前模块的 TAG 和等级（用于日志宏） */
#define LOG_MODULE_NAME _log_module_name
#define LOG_MODULE_LEVEL _log_module_level

/*****************************************************************************
 * 日志输出函数（内联实现）
 *****************************************************************************/

static inline void usbip_log_printf(int level, const char* tag, const unsigned char* data,
                                    unsigned int len, const char* fmt, ...)
{
    va_list args;
    time_t now;
    struct tm* tm_info;
    struct timespec ts;
    char time_buf[16];
    const char* level_str;
    const char* level_color;
    FILE* fp;

    /* 获取时间 */
    clock_gettime(CLOCK_REALTIME, &ts);
    now = ts.tv_sec;
    tm_info = localtime(&now);

    /* 格式化时间戳: HH:MM:SS.mmm */
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

    /* 等级字符串和颜色 */
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

    /* 输出格式: [color]HH:MM:SS.mmm [L] [tag] message */
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

    /* 输出十六进制数据（如果有） */
    if (data && len > 0)
    {
        for (unsigned int i = 0; i < len; i++)
        {
            if ((i % 16) == 0)
            {
                fprintf(fp, "\n");
            }

            fprintf(fp, "%02X ", data[i]);
        }
    }

    fprintf(fp, "\n");
    if (LOG_USE_COLOR && level != LOG_LEVEL_DBG)
    {
        fprintf(fp, LOG_COLOR_NONE);
    }
    fflush(fp);
}

/*****************************************************************************
 * 日志宏
 *
 * 使用方式：
 *   LOG_MODULE_REGISTER(my_module, LOG_LEVEL_DBG);
 *   LOG_ERR("Error occurred: %d", err);
 *   LOG_DBG("Debug info: %s", str);
 *****************************************************************************/

#define LOG_ERR(fmt, ...)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (LOG_MODULE_LEVEL >= LOG_LEVEL_ERR)                                                     \
            usbip_log_printf(LOG_LEVEL_ERR, LOG_MODULE_NAME, NULL, 0, fmt, ##__VA_ARGS__);         \
    } while (0)

#define LOG_WRN(fmt, ...)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (LOG_MODULE_LEVEL >= LOG_LEVEL_WRN)                                                     \
            usbip_log_printf(LOG_LEVEL_WRN, LOG_MODULE_NAME, NULL, 0, fmt, ##__VA_ARGS__);         \
    } while (0)

#define LOG_INF(fmt, ...)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (LOG_MODULE_LEVEL >= LOG_LEVEL_INF)                                                     \
            usbip_log_printf(LOG_LEVEL_INF, LOG_MODULE_NAME, NULL, 0, fmt, ##__VA_ARGS__);         \
    } while (0)

#define LOG_DBG(fmt, ...)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (LOG_MODULE_LEVEL >= LOG_LEVEL_DBG)                                                     \
            usbip_log_printf(LOG_LEVEL_DBG, LOG_MODULE_NAME, NULL, 0, fmt, ##__VA_ARGS__);         \
    } while (0)

/*****************************************************************************
 * 原始日志宏（用于特殊情况，直接指定 TAG）
 *****************************************************************************/

#define LOG_RAW_ERR(tag, fmt, ...) usbip_log_printf(LOG_LEVEL_ERR, tag, NULL, 0, fmt, ##__VA_ARGS__)

#define LOG_RAW_WRN(tag, fmt, ...) usbip_log_printf(LOG_LEVEL_WRN, tag, NULL, 0, fmt, ##__VA_ARGS__)

#define LOG_RAW_INF(tag, fmt, ...) usbip_log_printf(LOG_LEVEL_INF, tag, NULL, 0, fmt, ##__VA_ARGS__)

#define LOG_RAW_DBG(tag, fmt, ...) usbip_log_printf(LOG_LEVEL_DBG, tag, NULL, 0, fmt, ##__VA_ARGS__)

/*****************************************************************************
 * 十六进制日志宏
 *
 * 使用方式：
 *   LOG_HEX_DBG("Data: ", data, len);
 *****************************************************************************/

#define LOG_HEX_ERR(fmt, data, len, ...)                                                           \
    do                                                                                             \
    {                                                                                              \
        if (LOG_MODULE_LEVEL >= LOG_LEVEL_ERR)                                                     \
            usbip_log_printf(LOG_LEVEL_ERR, LOG_MODULE_NAME, (const unsigned char*)data, len, fmt, \
                             ##__VA_ARGS__);                                                       \
    } while (0)

#define LOG_HEX_WRN(fmt, data, len, ...)                                                           \
    do                                                                                             \
    {                                                                                              \
        if (LOG_MODULE_LEVEL >= LOG_LEVEL_WRN)                                                     \
            usbip_log_printf(LOG_LEVEL_WRN, LOG_MODULE_NAME, (const unsigned char*)data, len, fmt, \
                             ##__VA_ARGS__);                                                       \
    } while (0)

#define LOG_HEX_INF(fmt, data, len, ...)                                                           \
    do                                                                                             \
    {                                                                                              \
        if (LOG_MODULE_LEVEL >= LOG_LEVEL_INF)                                                     \
            usbip_log_printf(LOG_LEVEL_INF, LOG_MODULE_NAME, (const unsigned char*)data, len, fmt, \
                             ##__VA_ARGS__);                                                       \
    } while (0)

#define LOG_HEX_DBG(fmt, data, len, ...)                                                           \
    do                                                                                             \
    {                                                                                              \
        if (LOG_MODULE_LEVEL >= LOG_LEVEL_DBG)                                                     \
            usbip_log_printf(LOG_LEVEL_DBG, LOG_MODULE_NAME, (const unsigned char*)data, len, fmt, \
                             ##__VA_ARGS__);                                                       \
    } while (0)

#define LOG_RAW_HEX_ERR(tag, fmt, data, len, ...)                                                  \
    usbip_log_printf(LOG_LEVEL_ERR, tag, (const unsigned char*)data, len, fmt, ##__VA_ARGS__)

#define LOG_RAW_HEX_WRN(tag, fmt, data, len, ...)                                                  \
    usbip_log_printf(LOG_LEVEL_WRN, tag, (const unsigned char*)data, len, fmt, ##__VA_ARGS__)

#define LOG_RAW_HEX_INF(tag, fmt, data, len, ...)                                                  \
    usbip_log_printf(LOG_LEVEL_INF, tag, (const unsigned char*)data, len, fmt, ##__VA_ARGS__)

#define LOG_RAW_HEX_DBG(tag, fmt, data, len, ...)                                                  \
    usbip_log_printf(LOG_LEVEL_DBG, tag, (const unsigned char*)data, len, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* USBIP_LOG_H */