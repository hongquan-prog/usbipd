/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-9-8      hongquan.li   add license declaration
 */

/*
 * OSAL POSIX Implementation
 *
 * OS Abstraction Layer implementation for Linux/Unix systems
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hal/usbip_osal.h"

/*****************************************************************************
 * POSIX Memory Operations (Low-level implementation, not using OSAL wrappers)
 *****************************************************************************/

static void* posix_malloc(size_t size)
{
    return malloc(size);
}

static void posix_free(void* ptr)
{
    free(ptr);
}

/*****************************************************************************
 * POSIX Mutex Implementation
 *****************************************************************************/

static int posix_mutex_init(void* handle)
{
    return pthread_mutex_init((pthread_mutex_t*)handle, NULL) == 0 ? OSAL_OK : OSAL_ERROR;
}

static int posix_mutex_lock(void* handle)
{
    return pthread_mutex_lock((pthread_mutex_t*)handle) == 0 ? OSAL_OK : OSAL_ERROR;
}

static int posix_mutex_unlock(void* handle)
{
    return pthread_mutex_unlock((pthread_mutex_t*)handle) == 0 ? OSAL_OK : OSAL_ERROR;
}

static void posix_mutex_destroy(void* handle)
{
    pthread_mutex_destroy((pthread_mutex_t*)handle);
}

/*****************************************************************************
 * POSIX Condition Variable Implementation
 *****************************************************************************/

static int posix_cond_init(void* handle)
{
    return pthread_cond_init((pthread_cond_t*)handle, NULL) == 0 ? OSAL_OK : OSAL_ERROR;
}

static int posix_cond_wait(void* cond, void* mutex)
{
    return pthread_cond_wait((pthread_cond_t*)cond, (pthread_mutex_t*)mutex) == 0 ? OSAL_OK
                                                                                  : OSAL_ERROR;
}

static int posix_cond_timedwait(void* cond, void* mutex, uint32_t timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;

    if (ts.tv_nsec >= 1000000000)
    {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    int ret = pthread_cond_timedwait((pthread_cond_t*)cond, (pthread_mutex_t*)mutex, &ts);

    if (ret == ETIMEDOUT)
    {
        return OSAL_TIMEOUT;
    }
    return ret == 0 ? OSAL_OK : OSAL_ERROR;
}

static int posix_cond_signal(void* cond)
{
    return pthread_cond_signal((pthread_cond_t*)cond) == 0 ? OSAL_OK : OSAL_ERROR;
}

static int posix_cond_broadcast(void* cond)
{
    return pthread_cond_broadcast((pthread_cond_t*)cond) == 0 ? OSAL_OK : OSAL_ERROR;
}

static void posix_cond_destroy(void* handle)
{
    pthread_cond_destroy((pthread_cond_t*)handle);
}

/*****************************************************************************
 * POSIX Thread Implementation
 *****************************************************************************/

static int posix_thread_create(void* handle, void* (*func)(void*), void* arg, size_t stack_size,
                               int priority)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if (stack_size > 0)
    {
        pthread_attr_setstacksize(&attr, stack_size);
    }

    /* Priority is handled via scheduling policy in POSIX, ignore here */
    (void)priority;

    int ret = pthread_create((pthread_t*)handle, &attr, func, arg);
    pthread_attr_destroy(&attr);

    return ret == 0 ? OSAL_OK : OSAL_ERROR;
}

static int posix_thread_join(void* handle)
{
    return pthread_join(*(pthread_t*)handle, NULL) == 0 ? OSAL_OK : OSAL_ERROR;
}

static int posix_thread_detach(void* handle)
{
    return pthread_detach(*(pthread_t*)handle) == 0 ? OSAL_OK : OSAL_ERROR;
}

/*****************************************************************************
 * POSIX Time Implementation
 *****************************************************************************/

static uint32_t posix_get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void posix_sleep_ms(uint32_t ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000};
    nanosleep(&ts, NULL);
}

/*****************************************************************************
 * POSIX Operations Interface
 *****************************************************************************/

static osal_ops_t posix_ops = {
    /* Mutex */
    .mutex_init = posix_mutex_init,
    .mutex_lock = posix_mutex_lock,
    .mutex_unlock = posix_mutex_unlock,
    .mutex_destroy = posix_mutex_destroy,

    /* Condition variable */
    .cond_init = posix_cond_init,
    .cond_wait = posix_cond_wait,
    .cond_timedwait = posix_cond_timedwait,
    .cond_signal = posix_cond_signal,
    .cond_broadcast = posix_cond_broadcast,
    .cond_destroy = posix_cond_destroy,

    /* Thread */
    .thread_create = posix_thread_create,
    .thread_join = posix_thread_join,
    .thread_detach = posix_thread_detach,

    /* Time */
    .get_time_ms = posix_get_time_ms,
    .sleep_ms = posix_sleep_ms,

    /* Memory */
    .malloc = posix_malloc,
    .free = posix_free,

    /* Platform name */
    .name = "posix",
};

/**
 * Auto-register POSIX implementation
 */
OSAL_REGISTER(posix, posix_ops);