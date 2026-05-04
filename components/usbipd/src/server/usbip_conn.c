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
#include "usbip_urb.h"

LOG_MODULE_REGISTER(conn, CONFIG_USBIP_LOG_LEVEL);

static void* usbip_conn_rx_thread(void* arg);
static void* usbip_conn_processor_thread(void* arg);
static void* usbip_conn_cleanup_thread(void* arg);
static void usbip_connection_request_stop(struct usbip_connection* conn);

/* Connection manager singleton */
struct conn_manager
{
    struct usbip_connection* head; /* Head of active connections list */
    struct usbip_connection* tail; /* Tail of active connections list */
    struct osal_mutex lock;        /* Protects list operations */
    int active_count;              /* Current active connection count */
    int max_connections;           /* Maximum allowed connections */
    struct osal_thread cleanup_thread; /* Background cleanup thread */
    atomic_int cleanup_thread_running; /* Cleanup thread control flag */
    struct osal_sem reap_sem;      /* Signal cleanup thread to reap */
};
/* clang-format off */
static struct conn_manager s_conn_manager = {
      .head = NULL,
      .tail = NULL,
      .active_count = 0,
      .max_connections = USBIP_MAX_CONNECTIONS
    };
/* clang-format on */

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

    atomic_init(&s_conn_manager.cleanup_thread_running, 1);

    ret = osal_sem_init(&s_conn_manager.reap_sem);
    if (ret != OSAL_OK)
    {
        LOG_ERR("Failed to initialize reap semaphore");
        osal_mutex_destroy(&s_conn_manager.lock);
        return -1;
    }

    ret = osal_thread_create(&s_conn_manager.cleanup_thread, "ConnCleanup",
                             usbip_conn_cleanup_thread, NULL,
                             CONFIG_URB_THREAD_STACK_SIZE, CONFIG_URB_THREAD_PRIORITY);
    if (ret != OSAL_OK)
    {
        LOG_ERR("Failed to create connection cleanup thread");
        osal_sem_destroy(&s_conn_manager.reap_sem);
        osal_mutex_destroy(&s_conn_manager.lock);
        return -1;
    }

    LOG_INF("Connection manager initialized (max=%d)", s_conn_manager.max_connections);

    return 0;
}

void usbip_conn_manager_cleanup(void)
{
    struct usbip_connection* conn;
    struct usbip_connection* conns[USBIP_MAX_CONNECTIONS];
    int count = 0;

    LOG_INF("Cleaning up connection manager (%d active)", s_conn_manager.active_count);

    /* Collect all connections under lock, then stop outside lock */
    osal_mutex_lock(&s_conn_manager.lock);
    for (conn = s_conn_manager.head; conn != NULL && count < USBIP_MAX_CONNECTIONS; conn = conn->next)
    {
        conns[count++] = conn;
    }

    osal_mutex_unlock(&s_conn_manager.lock);
    for (int i = 0; i < count; i++)
    {
        usbip_connection_request_stop(conns[i]);
    }
    /* Signal cleanup thread to process remaining stops and exit */
    atomic_store_explicit(&s_conn_manager.cleanup_thread_running, 0, memory_order_seq_cst);
    osal_sem_post(&s_conn_manager.reap_sem);
    osal_thread_join(&s_conn_manager.cleanup_thread);
    osal_sem_destroy(&s_conn_manager.reap_sem);
    /* ConnCleanup has destroyed all connections; just clear the list state */
    s_conn_manager.head = NULL;
    s_conn_manager.tail = NULL;
    s_conn_manager.active_count = 0;
    osal_mutex_destroy(&s_conn_manager.lock);

    LOG_INF("Connection manager cleanup complete");
}

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

void usbip_conn_manager_remove(struct usbip_connection* conn)
{
    if (conn == NULL)
    {
        return;
    }

    osal_mutex_lock(&s_conn_manager.lock);

    /* Check if conn is actually in the list */
    if (conn->prev == NULL && conn->next == NULL && s_conn_manager.head != conn)
    {
        osal_mutex_unlock(&s_conn_manager.lock);
        return;
    }

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

int usbip_conn_manager_get_count(void)
{
    int count;

    osal_mutex_lock(&s_conn_manager.lock);
    count = s_conn_manager.active_count;
    osal_mutex_unlock(&s_conn_manager.lock);

    return count;
}

static void usbip_conn_manager_reap(void)
{
    struct usbip_connection* conn;

    osal_mutex_lock(&s_conn_manager.lock);
    conn = s_conn_manager.head;
    while (conn != NULL)
    {
        struct usbip_connection* next = conn->next;
        if (conn->state == CONN_STATE_CLOSED)
        {
            osal_mutex_unlock(&s_conn_manager.lock);
            usbip_connection_destroy(conn);
            osal_mutex_lock(&s_conn_manager.lock);
        }
        conn = next;
    }
    osal_mutex_unlock(&s_conn_manager.lock);
}

static void usbip_conn_manager_process_stopping(void)
{
    struct usbip_connection* conn;

    osal_mutex_lock(&s_conn_manager.lock);
    conn = s_conn_manager.head;
    while (conn != NULL)
    {
        if (conn->state == CONN_STATE_CLOSING &&
            atomic_load_explicit(&conn->rx_thread_started, memory_order_acquire) == 0 &&
            atomic_load_explicit(&conn->processor_started, memory_order_acquire) == 0)
        {
            LOG_DBG("ConnCleanup: Finishing stop for %s", conn->busid);

            /* Destroy URB queue */
            usbip_urb_queue_destroy(&conn->urb_queue);
            /* Unexport device and unbind */
            if (conn->driver && conn->busid[0] != '\0')
            {
                usbip_driver_unexport_device(conn->driver, conn->busid);
                usbip_unbind_device(conn->busid);
            }

            /* Transition to CLOSED */
            osal_mutex_lock(&conn->state_lock);
            conn->state = CONN_STATE_CLOSED;
            osal_mutex_unlock(&conn->state_lock);
        }
        conn = conn->next;
    }
    osal_mutex_unlock(&s_conn_manager.lock);
}

static void* usbip_conn_cleanup_thread(void* arg)
{
    (void)arg;

    LOG_DBG("Connection cleanup thread started");
    while (atomic_load_explicit(&s_conn_manager.cleanup_thread_running, memory_order_seq_cst))
    {
        osal_sem_wait(&s_conn_manager.reap_sem);
        usbip_conn_manager_process_stopping();
        usbip_conn_manager_reap();
    }

    /* Final pass before exit: handle any remaining connections */
    usbip_conn_manager_process_stopping();
    usbip_conn_manager_reap();
    LOG_DBG("Connection cleanup thread exiting");

    return NULL;
}

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

    memset(conn, 0, sizeof(*conn));
    conn->next = NULL;
    conn->prev = NULL;
    atomic_init(&conn->transport_ctx, ctx);
    conn->driver = NULL;
    memset(conn->busid, 0, sizeof(conn->busid));
    conn->state = CONN_STATE_INIT;
    atomic_init(&conn->stop_in_progress, 0);
    atomic_init(&conn->running, 0);
    atomic_init(&conn->rx_thread_started, 0);
    atomic_init(&conn->processor_started, 0);
    conn->urb_queue.priv = NULL;

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

void usbip_connection_destroy(struct usbip_connection* conn)
{
    if (conn == NULL)
    {
        return;
    }

    LOG_DBG("Destroying connection: %p", (void*)conn);
    /* Remove from manager list if still present */
    usbip_conn_manager_remove(conn);
    osal_mutex_destroy(&conn->state_lock);
    atomic_store_explicit(&conn->transport_ctx, NULL, memory_order_relaxed);
    osal_free(conn);

    LOG_DBG("Connection destroyed");
}

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
    ret = osal_thread_create(&conn->processor_thread, "Processor", usbip_conn_processor_thread, conn,
                             CONFIG_URB_THREAD_STACK_SIZE, CONFIG_URB_THREAD_PRIORITY);
    if (ret != OSAL_OK)
    {
        LOG_ERR("Failed to create processor thread for %s", busid);
        goto fail_queue;
    }

    atomic_store_explicit(&conn->processor_started, 1, memory_order_release);
    /* Start RX thread */
    ret = osal_thread_create(&conn->rx_thread, "RX", usbip_conn_rx_thread, conn,
                             CONFIG_URB_THREAD_STACK_SIZE, CONFIG_URB_THREAD_PRIORITY);
    if (ret != OSAL_OK)
    {
        LOG_ERR("Failed to create RX thread for %s", busid);
        goto fail_processor;
    }

    atomic_store_explicit(&conn->rx_thread_started, 1, memory_order_release);
    /* Mark connection as active */
    osal_mutex_lock(&conn->state_lock);
    conn->state = CONN_STATE_ACTIVE;
    osal_mutex_unlock(&conn->state_lock);
    LOG_INF("Connection started for device %s (RX + Processor threads)", busid);

    return 0;

fail_processor:
    atomic_store_explicit(&conn->running, 0, memory_order_seq_cst);
    usbip_urb_queue_close(&conn->urb_queue);
    osal_thread_join(&conn->processor_thread);
    atomic_store_explicit(&conn->processor_started, 0, memory_order_relaxed);
fail_queue:
    usbip_urb_queue_destroy(&conn->urb_queue);
    return -1;
}

static void usbip_connection_request_stop(struct usbip_connection* conn)
{
    int was_active;
    struct usbip_conn_ctx* ctx;

    if (conn == NULL)
    {
        return;
    }

    osal_mutex_lock(&conn->state_lock);
    was_active = (conn->state == CONN_STATE_ACTIVE);
    if (was_active)
    {
        LOG_DBG("Requesting stop for connection %s", conn->busid);
        conn->state = CONN_STATE_CLOSING;
        atomic_store_explicit(&conn->running, 0, memory_order_seq_cst);
    }

    osal_mutex_unlock(&conn->state_lock);
    if (!was_active)
    {
        return;
    }

    /* Ensure signal sequence runs only once */
    if (atomic_exchange_explicit(&conn->stop_in_progress, 1, memory_order_seq_cst) != 0)
    {
        return;
    }

    /* Close URB queue to signal processor thread to exit */
    usbip_urb_queue_close(&conn->urb_queue);

    /* Close transport to force unblock RX thread from recv */
    ctx = atomic_load_explicit(&conn->transport_ctx, memory_order_acquire);
    if (ctx)
    {
        LOG_DBG("Closing transport for %s", conn->busid);
        transport_close(ctx);
        atomic_store_explicit(&conn->transport_ctx, NULL, memory_order_release);
    }

    /* Notify cleanup thread that this connection needs processing */
    osal_sem_post(&s_conn_manager.reap_sem);
}

static int usbip_connection_send_reply(struct usbip_connection* conn, struct usbip_header* urb_ret,
                                       const void* data, size_t data_len)
{
    ssize_t n;
    struct usbip_conn_ctx* ctx;

    if (conn == NULL || urb_ret == NULL)
    {
        return -1;
    }

    ctx = atomic_load_explicit(&conn->transport_ctx, memory_order_acquire);
    if (ctx == NULL)
    {
        return -1;
    }

    LOG_DBG("Sending URB reply: cmd=%u seq=%u devid=%u dir=%u ep=%u status=%d len=%d",
            urb_ret->base.command, urb_ret->base.seqnum, urb_ret->base.devid,
            urb_ret->base.direction, urb_ret->base.ep, urb_ret->u.ret_submit.status,
            urb_ret->u.ret_submit.actual_length);

    usbip_pack_header(urb_ret, 1);

    n = transport_send(ctx, urb_ret, sizeof(*urb_ret));
    if (n != sizeof(*urb_ret))
    {
        LOG_ERR("send header failed");
        return -1;
    }

    if (data && data_len > 0)
    {
        n = transport_send(ctx, data, data_len);
        if (n != (ssize_t)data_len)
        {
            LOG_ERR("send data failed");
            return -1;
        }
    }

    return (int)(sizeof(*urb_ret) + data_len);
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

static void* usbip_conn_rx_thread(void* arg)
{
    struct usbip_connection* conn = (struct usbip_connection*)arg;
    struct usbip_header urb_cmd;
    uint8_t urb_data[USBIP_URB_DATA_MAX_SIZE];
    size_t urb_data_len;
    int recv_ret;
    struct usbip_conn_ctx* ctx;

    LOG_DBG("RX thread started for %s", conn->busid);

    while (atomic_load_explicit(&conn->running, memory_order_seq_cst))
    {
        ctx = atomic_load_explicit(&conn->transport_ctx, memory_order_acquire);
        if (ctx == NULL)
        {
            LOG_DBG("RX: transport_ctx is NULL for %s", conn->busid);
            usbip_connection_request_stop(conn);
            break;
        }

        recv_ret = usbip_recv_header(ctx, &urb_cmd);
        if (recv_ret < 0)
        {
            LOG_DBG("RX: Connection closed or error for %s", conn->busid);
            usbip_connection_request_stop(conn);
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

            if (transport_recv(ctx, urb_data, urb_data_len) !=
                (ssize_t)urb_data_len)
            {
                LOG_ERR("RX: Failed to receive URB data for %s", conn->busid);
                usbip_connection_request_stop(conn);
                break;
            }
        }

        if (usbip_urb_queue_push(&conn->urb_queue, &urb_cmd, urb_data, urb_data_len) < 0)
        {
            LOG_DBG("RX: Queue push failed, connection closing");
            usbip_connection_request_stop(conn);
            break;
        }
    }

    LOG_DBG("RX thread exiting for %s", conn->busid);

    /* Mark RX thread as exited and notify cleanup thread */
    atomic_store_explicit(&conn->rx_thread_started, 0, memory_order_release);
    osal_sem_post(&s_conn_manager.reap_sem);

    return NULL;
}

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
        ret = usbip_driver_handle_urb(conn->driver, &urb_cmd, &urb_ret, &data_out, &data_len,
                                       urb_data, urb_data_len);

        if (ret < 0)
        {
            LOG_ERR("Processor: URB handling error for %s", conn->busid);
            osal_free(data_out);
            usbip_connection_request_stop(conn);
            break;
        }

        /* Send reply for IN transfers or when driver indicates response needed */
        if (ret > 0 || urb_cmd.base.direction == USBIP_DIR_OUT)
        {
            if (usbip_connection_send_reply(conn, &urb_ret, data_out, data_len) < 0)
            {
                LOG_ERR("Processor: Failed to send URB reply for %s", conn->busid);
                osal_free(data_out);
                usbip_connection_request_stop(conn);
                break;
            }
        }

        osal_free(data_out);
    }

    /* Signal RX thread to stop if not already stopping */
    atomic_store_explicit(&conn->running, 0, memory_order_seq_cst);
    atomic_store_explicit(&conn->processor_started, 0, memory_order_release);
    osal_sem_post(&s_conn_manager.reap_sem);
    LOG_INF("Processor thread exiting for %s", conn->busid);

    return NULL;
}
