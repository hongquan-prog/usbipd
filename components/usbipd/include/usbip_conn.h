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

#include "hal/usbip_transport.h"
#include "hal/usbip_osal.h"
#include "usbip_devmgr.h"
#include "usbip_protocol.h"

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
 * Forward Declarations
 *****************************************************************************/
struct usbip_connection;
struct usbip_device_driver;

/*****************************************************************************
 * Connection State Enumeration
 *****************************************************************************/

/**
 * enum usbip_conn_state - Connection state
 */
enum usbip_conn_state {
    CONN_STATE_INIT = 0,        /* Initializing */
    CONN_STATE_ACTIVE,          /* Active URB processing */
    CONN_STATE_CLOSING,         /* Graceful shutdown */
    CONN_STATE_CLOSED           /* Cleaned up */
};

/*****************************************************************************
 * URB Queue (Per-Connection)
 *****************************************************************************/

/* Forward declaration - defined in usbip_urb.c */
struct urb_slot;

/**
 * struct usbip_conn_urb_queue - Per-connection URB queue
 */
struct usbip_conn_urb_queue {
    void* priv;  /* Opaque pointer to implementation-specific data */
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
struct usbip_connection {
    /* List management - must be first for list macros */
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

    /* URB processing */
    struct usbip_conn_urb_queue urb_queue;
    struct osal_thread rx_thread;
    struct osal_thread processor_thread;
    volatile int running;

    /* Flags for thread synchronization */
    int rx_thread_started;
    int processor_started;
};

/*****************************************************************************
 * Per-Connection URB Queue Interface
 *****************************************************************************/

/**
 * usbip_urb_queue_init - Initialize URB queue
 * @q: Queue to initialize
 * Return: 0 on success, -1 on failure
 */
int usbip_urb_queue_init(struct usbip_conn_urb_queue* q);

/**
 * usbip_urb_queue_destroy - Destroy URB queue
 * @q: Queue to destroy
 */
void usbip_urb_queue_destroy(struct usbip_conn_urb_queue* q);

/**
 * usbip_urb_queue_push - Push URB to queue
 * @q: Queue
 * @header: URB header
 * @data: URB data (can be NULL for IN transfers)
 * @data_len: Data length
 * Return: 0 on success, -1 on failure or queue closed
 */
int usbip_urb_queue_push(struct usbip_conn_urb_queue* q,
                         const struct usbip_header* header,
                         const void* data, size_t data_len);

/**
 * usbip_urb_queue_pop - Pop URB from queue
 * @q: Queue
 * @header: Output URB header
 * @data: Output data buffer
 * @data_len: Input/output data buffer size/actual length
 * Return: 0 on success, -1 on failure or queue closed
 */
int usbip_urb_queue_pop(struct usbip_conn_urb_queue* q,
                        struct usbip_header* header,
                        void* data, size_t* data_len);

/**
 * usbip_urb_queue_close - Close queue to signal threads to exit
 * @q: Queue
 */
void usbip_urb_queue_close(struct usbip_conn_urb_queue* q);

/**
 * usbip_urb_send_reply - Send URB response to client
 * @ctx: Connection context
 * @urb_ret: URB return header
 * @data: Response data
 * @data_len: Data length
 * Return: 0 on success, -1 on failure
 */
int usbip_urb_send_reply(struct usbip_conn_ctx* ctx, struct usbip_header* urb_ret,
                         const void* data, size_t data_len);

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
int usbip_connection_start(struct usbip_connection* conn,
                           struct usbip_device_driver* driver,
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
