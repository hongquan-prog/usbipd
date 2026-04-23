/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-3-24      hongquan.li   add license declaration
 */

/*
 * OSAL POSIX Implementation
 *
 * OS Abstraction Layer implementation for Linux/Unix systems
 */

#ifndef __unix__
#error "POSIX OSAL implementation can only be compiled on Unix-like systems"
#endif

#define _GNU_SOURCE

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

static int posix_mutex_init(void** handle)
{
    pthread_mutex_t* pm = malloc(sizeof(pthread_mutex_t));
    if (!pm)
    {
        return OSAL_ERROR;
    }

    if (pthread_mutex_init(pm, NULL) != 0)
    {
        free(pm);
        return OSAL_ERROR;
    }

    *handle = pm;
    return OSAL_OK;
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
    free(handle);
}

/*****************************************************************************
 * POSIX Condition Variable Implementation
 *****************************************************************************/

static int posix_cond_init(void** handle)
{
    pthread_cond_t* pc = malloc(sizeof(pthread_cond_t));
    if (!pc)
    {
        return OSAL_ERROR;
    }

    if (pthread_cond_init(pc, NULL) != 0)
    {
        free(pc);
        return OSAL_ERROR;
    }

    *handle = pc;
    return OSAL_OK;
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
    free(handle);
}

/*****************************************************************************
 * POSIX Thread Implementation
 *****************************************************************************/

static int posix_thread_create(void** handle, const char *name, void* (*func)(void*), void* arg, size_t stack_size,
                               int priority)
{
    int ret;
    pthread_attr_t attr;
    pthread_t* pt = NULL;

    /* Priority is handled via scheduling policy in POSIX, ignore here */
    (void)priority;

    pthread_attr_init(&attr);
    if (stack_size > 0)
    {
        pthread_attr_setstacksize(&attr, stack_size);
    }

    pt = malloc(sizeof(pthread_t));
    if (!pt)
    {
        pthread_attr_destroy(&attr);
        return OSAL_ERROR;
    }

    ret = pthread_create(pt, &attr, func, arg);
    if (name)
    {
        pthread_setname_np(*pt, name);
    }

    pthread_attr_destroy(&attr);
    if (ret != 0)
    {
        free(pt);
        return OSAL_ERROR;
    }

    *handle = pt;
    return OSAL_OK;
}

static int posix_thread_join(void* handle)
{
    int ret = pthread_join(*(pthread_t*)handle, NULL);
    if (ret == 0)
    {
        free(handle);
    }
    return ret == 0 ? OSAL_OK : OSAL_ERROR;
}

static int posix_thread_is_self(void* handle)
{
    return (pthread_self() == *(pthread_t*)handle) ? 1 : 0;
}

static int posix_thread_detach(void* handle)
{
    int ret = pthread_detach(*(pthread_t*)handle);
    if (ret == 0)
    {
        free(handle);
    }
    return ret == 0 ? OSAL_OK : OSAL_ERROR;
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
    .thread_is_self = posix_thread_is_self,
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
__attribute__((section(".usbip.init"), used)) void default_os_register(void)
{
    osal_register("posix", &posix_ops);
}
