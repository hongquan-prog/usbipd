/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-09     hongquan.li  Initial implementation
 */

/*
 * Connection Management
 *
 * Manages lifecycle of client connections for multi-client support.
 * Each exported USB device gets its own connection with dedicated
 * URB processing threads.
 */

#include <stdatomic.h>
#include <string.h>

#include "hal/usbip_log.h"
#include "hal/usbip_osal.h"
#include "hal/usbip_transport.h"
#include "usbip_conn.h"
#include "usbip_devmgr.h"
#include "usbip_pack.h"
#include "usbip_server.h"
#include "usbip_urb.h"

LOG_MODULE_REGISTER(conn, CONFIG_USBIP_LOG_LEVEL);

/*****************************************************************************
 * Forward Declarations for Thread Functions
 *****************************************************************************/

static void* usbip_conn_rx_thread(void* arg);
static void* usbip_conn_processor_thread(void* arg);

/*****************************************************************************
 * Global Connection Manager Instance
 *****************************************************************************/

/**
 * struct conn_manager - Connection manager singleton
 *
 * Tracks all active connections and provides thread-safe access.
 */
struct conn_manager
{
    struct usbip_connection* head; /* Head of active connections list */
    struct usbip_connection* tail; /* Tail of active connections list */
    struct osal_mutex lock;        /* Protects list operations */
    int active_count;              /* Current active connection count */
    int max_connections;           /* Maximum allowed connections */
};
/* clang-format off */
static struct conn_manager s_conn_manager = {                                                                                                                                  
      .head = NULL,                                                                                                                                                              
      .tail = NULL,                                                                                                                                                              
      .active_count = 0,                                                                                                                                                         
      .max_connections = USBIP_MAX_CONNECTIONS
    };
/* clang-format on */

/*****************************************************************************
 * Connection Manager Operations
 *****************************************************************************/

/**
 * usbip_conn_manager_init - Initialize connection manager
 *
 * Initializes the global connection manager singleton. Must be called
 * before any other connection operations.
 *
 * Return: 0 on success, -1 on failure
 */
int usbip_conn_manager_init(void)
{
    int ret;

    ret = osal_mutex_init(&s_conn_manager.lock);
    if (ret != OSAL_OK)
    {
        LOG_ERR("Failed to initialize connection manager mutex");
        return -1;
    }

    s_conn_manager.head = NULL;
    s_conn_manager.tail = NULL;
    s_conn_manager.active_count = 0;

    LOG_INF("Connection manager initialized (max=%d)", s_conn_manager.max_connections);

    return 0;
}

/**
 * usbip_conn_manager_cleanup - Cleanup all connections and manager resources
 *
 * Stops and destroys all active connections, then releases manager resources.
 * Should be called during server shutdown.
 */
void usbip_conn_manager_cleanup(void)
{
    struct usbip_connection* conn;
    struct usbip_connection* next;

    LOG_INF("Cleaning up connection manager (%d active)", s_conn_manager.active_count);

    osal_mutex_lock(&s_conn_manager.lock);

    conn = s_conn_manager.head;
    while (conn != NULL)
    {
        next = conn->next;
        osal_mutex_unlock(&s_conn_manager.lock);

        /* Stop and destroy connection (this will remove from list) */
        usbip_connection_stop(conn);
        usbip_connection_destroy(conn);

        osal_mutex_lock(&s_conn_manager.lock);
        conn = next;
    }

    s_conn_manager.head = NULL;
    s_conn_manager.tail = NULL;
    s_conn_manager.active_count = 0;

    osal_mutex_unlock(&s_conn_manager.lock);
    osal_mutex_destroy(&s_conn_manager.lock);

    LOG_INF("Connection manager cleanup complete");
}

/**
 * usbip_conn_manager_add - Add connection to active list
 * @conn: Connection to add (must be fully initialized)
 *
 * Adds a connection to the active connections list. The connection
 * must be properly initialized before calling this function.
 *
 * Return: 0 on success, -1 if max connections reached or error
 */
int usbip_conn_manager_add(struct usbip_connection* conn)
{
    int ret = 0;

    if (conn == NULL)
    {
        LOG_ERR("Cannot add NULL connection");
        return -1;
    }

    osal_mutex_lock(&s_conn_manager.lock);

    if (s_conn_manager.active_count >= s_conn_manager.max_connections)
    {
        LOG_WRN("Maximum connections reached (%d)", s_conn_manager.max_connections);
        ret = -1;
        goto unlock;
    }

    /* Add to tail of list */
    conn->prev = s_conn_manager.tail;
    conn->next = NULL;

    if (s_conn_manager.tail != NULL)
    {
        s_conn_manager.tail->next = conn;
    }
    else
    {
        s_conn_manager.head = conn;
    }

    s_conn_manager.tail = conn;
    s_conn_manager.active_count++;

    LOG_DBG("Connection added (active=%d)", s_conn_manager.active_count);

unlock:
    osal_mutex_unlock(&s_conn_manager.lock);
    return ret;
}

/**
 * usbip_conn_manager_remove - Remove connection from active list
 * @conn: Connection to remove
 *
 * Removes a connection from the active connections list.
 * The connection is not destroyed by this function.
 */
void usbip_conn_manager_remove(struct usbip_connection* conn)
{
    if (conn == NULL)
    {
        return;
    }

    osal_mutex_lock(&s_conn_manager.lock);

    /* Unlink from list */
    if (conn->prev != NULL)
    {
        conn->prev->next = conn->next;
    }
    else
    {
        s_conn_manager.head = conn->next;
    }

    if (conn->next != NULL)
    {
        conn->next->prev = conn->prev;
    }
    else
    {
        s_conn_manager.tail = conn->prev;
    }

    conn->prev = NULL;
    conn->next = NULL;

    if (s_conn_manager.active_count > 0)
    {
        s_conn_manager.active_count--;
    }

    LOG_DBG("Connection removed (active=%d)", s_conn_manager.active_count);

    osal_mutex_unlock(&s_conn_manager.lock);
}

/**
 * usbip_conn_manager_get_count - Get current active connection count
 *
 * Returns the number of active connections. This is useful for checking
 * connection limits before accepting new import requests.
 *
 * Return: Number of active connections
 */
int usbip_conn_manager_get_count(void)
{
    int count;

    osal_mutex_lock(&s_conn_manager.lock);
    count = s_conn_manager.active_count;
    osal_mutex_unlock(&s_conn_manager.lock);

    return count;
}

/*****************************************************************************
 * Connection Lifecycle Operations
 *****************************************************************************/

/**
 * usbip_connection_create - Create new connection
 * @ctx: Transport context (ownership transferred to connection)
 *
 * Allocates and initializes a new connection structure. The transport
 * context is stored in the connection and will be closed when the
 * connection is destroyed.
 *
 * Return: Connection pointer on success, NULL on failure
 */
struct usbip_connection* usbip_connection_create(struct usbip_conn_ctx* ctx)
{
    struct usbip_connection* conn;
    int ret;

    if (ctx == NULL)
    {
        LOG_ERR("Cannot create connection with NULL context");
        return NULL;
    }

    conn = osal_malloc(sizeof(struct usbip_connection));
    if (conn == NULL)
    {
        LOG_ERR("Failed to allocate connection structure");
        return NULL;
    }

    memset(conn, 0, sizeof(struct usbip_connection));
    conn->transport_ctx = ctx;
    conn->state = CONN_STATE_INIT;
    atomic_init(&conn->stop_in_progress, 0);

    /* Initialize state lock */
    ret = osal_mutex_init(&conn->state_lock);
    if (ret != OSAL_OK)
    {
        LOG_ERR("Failed to initialize connection state lock");
        osal_free(conn);
        return NULL;
    }

    LOG_DBG("Connection created: %p", (void*)conn);

    return conn;
}

/**
 * usbip_connection_destroy - Destroy connection and free resources
 * @conn: Connection to destroy
 *
 * Destroys a connection and releases all associated resources.
 * If the connection is still in the active list, it is removed first.
 * Transport context should be closed by usbip_connection_stop() before calling this.
 */
void usbip_connection_destroy(struct usbip_connection* conn)
{
    if (conn == NULL)
    {
        return;
    }

    LOG_DBG("Destroying connection: %p", (void*)conn);

    /* Remove from manager list if still present */
    if (conn->next != NULL || conn->prev != NULL || s_conn_manager.head == conn)
    {
        usbip_conn_manager_remove(conn);
    }

    /* Ensure stopped - this will wait for threads to complete */
    if (conn->state == CONN_STATE_ACTIVE || conn->state == CONN_STATE_CLOSING)
    {
        usbip_connection_stop(conn);
    }

    /* Destroy state lock */
    osal_mutex_destroy(&conn->state_lock);

    /* Transport context already closed in usbip_connection_stop() */
    conn->transport_ctx = NULL;

    osal_free(conn);

    LOG_DBG("Connection destroyed");
}

/**
 * usbip_connection_start - Start connection URB processing threads
 * @conn: Connection to start
 * @driver: Device driver for this connection
 * @busid: Device bus ID
 *
 * Starts the RX and Processor threads for URB processing. The connection
 * transitions from CONN_STATE_INIT to CONN_STATE_ACTIVE.
 *
 * Return: 0 on success, -1 on failure
 */
int usbip_connection_start(struct usbip_connection* conn, struct usbip_device_driver* driver,
                           const char* busid)
{
    int ret;

    if (conn == NULL || driver == NULL || busid == NULL)
    {
        LOG_ERR("Invalid parameters for connection start");
        return -1;
    }

    if (conn->state != CONN_STATE_INIT)
    {
        LOG_ERR("Connection not in INIT state");
        return -1;
    }

    /* Store device binding */
    conn->driver = driver;
    strncpy(conn->busid, busid, SYSFS_BUS_ID_SIZE - 1);
    conn->busid[SYSFS_BUS_ID_SIZE - 1] = '\0';

    /* Initialize URB queue */
    if (usbip_urb_queue_init(&conn->urb_queue) < 0)
    {
        LOG_ERR("Failed to init URB queue for %s", busid);
        return -1;
    }

    /* Mark as running before starting threads */
    atomic_store_explicit(&conn->running, 1, memory_order_seq_cst);

    /* Start processor thread first (waits on queue) */
    ret = osal_thread_create(&conn->processor_thread, usbip_conn_processor_thread, conn,
                             CONFIG_URB_THREAD_STACK_SIZE, CONFIG_URB_THREAD_PRIORITY);
    if (ret != OSAL_OK)
    {
        LOG_ERR("Failed to create processor thread for %s", busid);
        usbip_urb_queue_destroy(&conn->urb_queue);
        atomic_store_explicit(&conn->running, 0, memory_order_seq_cst);
        return -1;
    }
    conn->processor_started = 1;

    /* Start RX thread */
    ret = osal_thread_create(&conn->rx_thread, usbip_conn_rx_thread, conn,
                             CONFIG_URB_THREAD_STACK_SIZE, CONFIG_URB_THREAD_PRIORITY);
    if (ret != OSAL_OK)
    {
        LOG_ERR("Failed to create RX thread for %s", busid);
        atomic_store_explicit(&conn->running, 0, memory_order_seq_cst);
        usbip_urb_queue_close(&conn->urb_queue);
        osal_thread_join(&conn->processor_thread);
        conn->processor_started = 0;
        usbip_urb_queue_destroy(&conn->urb_queue);
        return -1;
    }
    conn->rx_thread_started = 1;

    /* Mark connection as active */
    osal_mutex_lock(&conn->state_lock);
    conn->state = CONN_STATE_ACTIVE;
    osal_mutex_unlock(&conn->state_lock);

    LOG_INF("Connection started for device %s (RX + Processor threads)", busid);

    return 0;
}

/**
 * usbip_connection_stop - Stop connection URB processing threads
 * @conn: Connection to stop
 *
 * Signals the URB processing threads to stop and waits for them
 * to complete. The connection transitions to CONN_STATE_CLOSED.
 */
void usbip_connection_stop(struct usbip_connection* conn)
{
    int was_active;

    if (conn == NULL)
    {
        return;
    }

    osal_mutex_lock(&conn->state_lock);
    was_active = (conn->state == CONN_STATE_ACTIVE);

    if (was_active)
    {
        LOG_DBG("Stopping connection for device %s", conn->busid);
        conn->state = CONN_STATE_CLOSING;
        atomic_store_explicit(&conn->running, 0, memory_order_seq_cst);
    }
    osal_mutex_unlock(&conn->state_lock);

    if (!was_active)
    {
        return;
    }

    /* Ensure stop sequence runs only once */
    if (atomic_exchange_explicit(&conn->stop_in_progress, 1, memory_order_seq_cst) != 0)
    {
        /* Another thread is already tearing down. If we are the RX thread,
         * return immediately so the other thread can join us. */
        if (conn->rx_thread_started && osal_thread_is_self(&conn->rx_thread))
        {
            return;
        }
        /* Otherwise wait for stop to complete */
        LOG_WRN("Connection stop already in progress, waiting for completion. busid=%s state=%d",
                conn->busid, conn->state);
        while (conn->state != CONN_STATE_CLOSED)
        {
            osal_sleep_ms(10);
        }

        LOG_INF("Connection stop completed. busid=%s", conn->busid);
        return;
    }

    /* Close URB queue first to signal threads to exit gracefully */
    usbip_urb_queue_close(&conn->urb_queue);

    /* Give threads time to respond to queue close */
    osal_sleep_ms(10);

    /* Close transport to force unblock RX thread from recv */
    if (conn->transport_ctx)
    {
        LOG_DBG("Closing transport for %s", conn->busid);
        transport_close(conn->transport_ctx);
    }

    /* Wait for processor thread to complete */
    if (conn->processor_started)
    {
        LOG_DBG("Waiting for processor thread to complete for %s", conn->busid);
        osal_thread_join(&conn->processor_thread);
        conn->processor_started = 0;
    }

    /* Wait for RX thread to complete */
    if (conn->rx_thread_started)
    {
        LOG_DBG("Waiting for RX thread to complete for %s", conn->busid);
        /* Fix: FreeRTOS eTaskGetState never returns eDeleted for the calling task.
         * If we are the RX thread ourselves, skip join to avoid self-join deadlock. */
        if (!osal_thread_is_self(&conn->rx_thread))
        {
            osal_thread_join(&conn->rx_thread);
        }
        conn->rx_thread_started = 0;
    }

    /* Cleanup URB queue */
    usbip_urb_queue_destroy(&conn->urb_queue);

    /* Unexport device and unbind from connection */
    if (conn->driver && conn->busid[0] != '\0')
    {
        LOG_DBG("Unexporting device %s", conn->busid);
        conn->driver->unexport_device(conn->driver, conn->busid);
        usbip_unbind_device(conn->busid);
    }

    osal_mutex_lock(&conn->state_lock);
    conn->state = CONN_STATE_CLOSED;
    osal_mutex_unlock(&conn->state_lock);

    LOG_INF("Connection stopped for device %s", conn->busid);
}

static int usbip_recv_header(struct usbip_conn_ctx* ctx, struct usbip_header* hdr)
{
    ssize_t n;

    n = transport_recv(ctx, hdr, sizeof(*hdr));
    if (n != sizeof(*hdr))
    {
        return -1;
    }

    usbip_pack_header(hdr, 0);
    return 0;
}

/*****************************************************************************
 * Per-Connection URB Processing Threads
 *****************************************************************************/

/**
 * usbip_conn_rx_thread - RX thread for receiving URB requests
 * @arg: Connection pointer
 *
 * Receives URB headers and data from the client and pushes them to the
 * connection's URB queue for processing by the processor thread.
 */
static void* usbip_conn_rx_thread(void* arg)
{
    struct usbip_connection* conn = (struct usbip_connection*)arg;
    struct usbip_header urb_cmd;
    uint8_t urb_data[USBIP_URB_DATA_MAX_SIZE];
    size_t urb_data_len;

    LOG_DBG("RX thread started for %s", conn->busid);

    while (atomic_load_explicit(&conn->running, memory_order_seq_cst))
    {
        int recv_ret = usbip_recv_header(conn->transport_ctx, &urb_cmd);

        if (recv_ret < 0)
        {
            LOG_DBG("RX: Connection closed or error for %s", conn->busid);
            break;
        }

        LOG_DBG("RX: Received URB cmd=%u seq=%u dir=%u ep=%u", urb_cmd.base.command,
                urb_cmd.base.seqnum, urb_cmd.base.direction, urb_cmd.base.ep);

        urb_data_len = 0;
        if (urb_cmd.base.direction == USBIP_DIR_OUT &&
            urb_cmd.u.cmd_submit.transfer_buffer_length > 0)
        {
            urb_data_len = urb_cmd.u.cmd_submit.transfer_buffer_length;
            if (urb_data_len > USBIP_URB_DATA_MAX_SIZE)
            {
                urb_data_len = USBIP_URB_DATA_MAX_SIZE;
            }

            if (transport_recv(conn->transport_ctx, urb_data, urb_data_len) !=
                (ssize_t)urb_data_len)
            {
                LOG_ERR("RX: Failed to receive URB data for %s", conn->busid);
                break;
            }
        }

        if (usbip_urb_queue_push(&conn->urb_queue, &urb_cmd, urb_data, urb_data_len) < 0)
        {
            LOG_DBG("RX: Queue push failed, connection closing");
            break;
        }
    }

    /* Signal connection to stop */
    atomic_store_explicit(&conn->running, 0, memory_order_seq_cst);
    usbip_urb_queue_close(&conn->urb_queue);

    LOG_DBG("RX thread exiting for %s", conn->busid);

    /* Auto-cleanup: stop and destroy connection when RX thread exits */
    usbip_connection_stop(conn);
    usbip_connection_destroy(conn);
    /* FreeRTOS threads cannot return directly, or the system will crash */
    osal_thread_delete(&conn->rx_thread);

    return NULL;
}

/**
 * usbip_conn_processor_thread - Processor thread for handling URBs
 * @arg: Connection pointer
 *
 * Pops URBs from the queue, calls the device driver to handle them,
 * and sends responses back to the client.
 */
static void* usbip_conn_processor_thread(void* arg)
{
    struct usbip_connection* conn = (struct usbip_connection*)arg;
    struct usbip_header urb_cmd, urb_ret;
    uint8_t urb_data[USBIP_URB_DATA_MAX_SIZE];
    size_t urb_data_len;
    void* data_out = NULL;
    size_t data_len = 0;
    int ret;

    LOG_DBG("Processor thread started for %s", conn->busid);

    while (atomic_load_explicit(&conn->running, memory_order_seq_cst))
    {
        if (usbip_urb_queue_pop(&conn->urb_queue, &urb_cmd, urb_data, &urb_data_len) < 0)
        {
            LOG_DBG("Processor: Queue pop failed or closing for %s", conn->busid);
            break;
        }

        LOG_DBG("Processor: Handling URB cmd=%u seq=%u dir=%u ep=%u", urb_cmd.base.command,
                urb_cmd.base.seqnum, urb_cmd.base.direction, urb_cmd.base.ep);

        data_out = NULL;
        data_len = 0;
        ret = conn->driver->handle_urb(conn->driver, &urb_cmd, &urb_ret, &data_out, &data_len,
                                       urb_data, urb_data_len);

        if (ret < 0)
        {
            LOG_ERR("Processor: URB handling error for %s", conn->busid);
            osal_free(data_out);
            atomic_store_explicit(&conn->running, 0, memory_order_seq_cst);
            usbip_urb_queue_close(&conn->urb_queue);
            if (conn->transport_ctx)
            {
                transport_close(conn->transport_ctx);
                conn->transport_ctx = NULL;
            }
            break;
        }

        /* Send reply for IN transfers or when driver indicates response needed */
        if (ret > 0 || urb_cmd.base.direction == USBIP_DIR_OUT)
        {
            if (usbip_urb_send_reply(conn->transport_ctx, &urb_ret, data_out, data_len) < 0)
            {
                LOG_ERR("Processor: Failed to send URB reply for %s", conn->busid);
                osal_free(data_out);
                atomic_store_explicit(&conn->running, 0, memory_order_seq_cst);
                usbip_urb_queue_close(&conn->urb_queue);
                if (conn->transport_ctx)
                {
                    transport_close(conn->transport_ctx);
                    conn->transport_ctx = NULL;
                }
                break;
            }
        }

        osal_free(data_out);
    }

    /* Signal RX thread to stop if not already stopping */
    atomic_store_explicit(&conn->running, 0, memory_order_seq_cst);

    LOG_INF("Processor thread exiting for %s", conn->busid);
    /* FreeRTOS threads cannot return directly, or the system will crash */
    osal_thread_delete(&conn->processor_thread);

    return NULL;
}
