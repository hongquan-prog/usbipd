/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-09     hongquan.li  Initial implementation
 */

#ifndef USBIP_CONN_H
#define USBIP_CONN_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include "usbip_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Configuration
 *****************************************************************************/

/* URB queue slots - from Kconfig USBIP_URB_QUEUE_SIZE, default 8 */
#ifndef CONFIG_USBIP_URB_QUEUE_SIZE
#define CONFIG_USBIP_URB_QUEUE_SIZE 8
#endif
#define USBIP_URB_QUEUE_SIZE CONFIG_USBIP_URB_QUEUE_SIZE

/* Max URB data size - from Kconfig USBIP_URB_DATA_MAX_SIZE, default 512 */
#ifndef CONFIG_USBIP_URB_DATA_MAX_SIZE
#define CONFIG_USBIP_URB_DATA_MAX_SIZE 512
#endif
#define USBIP_URB_DATA_MAX_SIZE CONFIG_USBIP_URB_DATA_MAX_SIZE

/* Max connections - from Kconfig USBIP_MAX_CONNECTIONS, default 4 */
#ifndef CONFIG_USBIP_MAX_CONNECTIONS
#define CONFIG_USBIP_MAX_CONNECTIONS 4
#endif
#define USBIP_MAX_CONNECTIONS CONFIG_USBIP_MAX_CONNECTIONS

/*****************************************************************************
 * Connection State Enumeration
 *****************************************************************************/

/**
 * enum usbip_conn_state - Connection state
 */
enum usbip_conn_state {
    CONN_STATE_INIT = 0, /* Initializing */
    CONN_STATE_ACTIVE,   /* Active URB processing */
    CONN_STATE_CLOSING,  /* Graceful shutdown */
    CONN_STATE_CLOSED    /* Cleaned up */
};

/*****************************************************************************
 * URB Queue (Per-Connection)
 *****************************************************************************/

/* Forward declaration - defined in usbip_urb.c */
struct urb_slot;

/**
 * struct usbip_conn_urb_queue - Per-connection URB queue
 */
struct usbip_conn_urb_queue
{
    void* priv; /* Opaque pointer to implementation-specific data */
};

/*****************************************************************************
 * Connection Context
 *****************************************************************************/

/**
 * struct usbip_connection - Connection context for multi-client support
 *
 * Each exported device gets its own connection instance with dedicated
 * URB queue and processing threads.
 */
struct usbip_connection
{
    /* Doubly-linked list pointers for connection manager */
    struct usbip_connection* next;
    struct usbip_connection* prev;

    /* Transport context */
    struct usbip_conn_ctx* transport_ctx;

    /* Device binding */
    struct usbip_device_driver* driver;
    char busid[SYSFS_BUS_ID_SIZE];

    /* Connection state */
    enum usbip_conn_state state;
    struct osal_mutex state_lock;
    atomic_int stop_in_progress;

    /* URB processing */
    struct usbip_conn_urb_queue urb_queue;
    struct osal_thread rx_thread;
    struct osal_thread processor_thread;
    atomic_int running;

    /* Flags for thread synchronization */
    int rx_thread_started;
    int processor_started;
};

/*****************************************************************************
 * Connection Manager Interface
 *****************************************************************************/

/**
 * usbip_conn_manager_init - Initialize connection manager
 * Return: 0 on success, -1 on failure
 */
int usbip_conn_manager_init(void);

/**
 * usbip_conn_manager_cleanup - Cleanup all connections and manager resources
 */
void usbip_conn_manager_cleanup(void);

/**
 * usbip_conn_manager_add - Add connection to active list
 * @conn: Connection to add (must be fully initialized)
 * Return: 0 on success, -1 if max connections reached or error
 */
int usbip_conn_manager_add(struct usbip_connection* conn);

/**
 * usbip_conn_manager_remove - Remove connection from active list
 * @conn: Connection to remove
 */
void usbip_conn_manager_remove(struct usbip_connection* conn);

/**
 * usbip_conn_manager_get_count - Get current active connection count
 * Return: Number of active connections
 */
int usbip_conn_manager_get_count(void);

/*****************************************************************************
 * Connection Lifecycle Interface
 *****************************************************************************/

/**
 * usbip_connection_create - Create new connection
 * @ctx: Transport context (ownership transferred to connection)
 * Return: Connection pointer on success, NULL on failure
 */
struct usbip_connection* usbip_connection_create(struct usbip_conn_ctx* ctx);

/**
 * usbip_connection_destroy - Destroy connection and free resources
 * @conn: Connection to destroy
 */
void usbip_connection_destroy(struct usbip_connection* conn);

/**
 * usbip_connection_start - Start connection URB processing threads
 * @conn: Connection to start
 * @driver: Device driver for this connection
 * @busid: Device bus ID
 * Return: 0 on success, -1 on failure
 */
int usbip_connection_start(struct usbip_connection* conn, struct usbip_device_driver* driver,
                           const char* busid);

/**
 * usbip_connection_stop - Stop connection URB processing threads
 * @conn: Connection to stop
 */
void usbip_connection_stop(struct usbip_connection* conn);

#ifdef __cplusplus
}
#endif

#endif /* USBIP_CONN_H */
