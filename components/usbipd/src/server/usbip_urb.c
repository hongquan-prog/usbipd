/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-3-24      hongquan.li   add license declaration
 * 2026-04-09     hongquan.li   Refactor to per-connection URB queues
 */

/*
 * URB Handler with Per-Connection Queues
 *
 * Supports multi-client by providing dedicated URB queue per connection.
 */
#include <endian.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hal/usbip_log.h"
#include "hal/usbip_osal.h"
#include "usbip_conn.h"

LOG_MODULE_REGISTER(urb, CONFIG_USBIP_LOG_LEVEL);

/*****************************************************************************
 * URB Queue Implementation (Per-Connection)
 *****************************************************************************/

/**
 * struct urb_slot - URB queue slot for storing URB data
 */
struct urb_slot
{
    struct usbip_header header;
    uint8_t data[USBIP_URB_DATA_MAX_SIZE];
    size_t data_len;
    int valid;
};

/**
 * struct urb_queue - Internal URB queue structure
 *
 * Implementation detail of struct usbip_conn_urb_queue
 */
struct urb_queue
{
    struct urb_slot slots[USBIP_URB_QUEUE_SIZE];
    int head;
    int tail;
    struct osal_mutex lock;
    struct osal_cond not_empty;
    struct osal_cond not_full;
    int closed;
};

/*****************************************************************************
 * Per-Connection URB Queue Operations
 *****************************************************************************/

/**
 * usbip_urb_queue_init - Initialize URB queue
 * @q: Queue to initialize (opaque pointer to implementation)
 *
 * Initializes mutex and condition variables for the queue.
 *
 * Return: 0 on success, -1 on failure
 */
int usbip_urb_queue_init(struct usbip_conn_urb_queue* q)
{
    struct urb_queue* queue;

    if (q == NULL)
    {
        return -1;
    }

    queue = osal_malloc(sizeof(struct urb_queue));
    if (queue == NULL)
    {
        LOG_ERR("Failed to allocate URB queue");
        return -1;
    }

    memset(queue, 0, sizeof(*queue));

    if (osal_mutex_init(&queue->lock) != OSAL_OK)
    {
        osal_free(queue);
        return -1;
    }

    if (osal_cond_init(&queue->not_empty) != OSAL_OK)
    {
        osal_mutex_destroy(&queue->lock);
        osal_free(queue);
        return -1;
    }

    if (osal_cond_init(&queue->not_full) != OSAL_OK)
    {
        osal_cond_destroy(&queue->not_empty);
        osal_mutex_destroy(&queue->lock);
        osal_free(queue);
        return -1;
    }

    q->priv = queue;
    return 0;
}

/**
 * usbip_urb_queue_destroy - Destroy URB queue
 * @q: Queue to destroy
 *
 * Releases all resources associated with the queue.
 */
void usbip_urb_queue_destroy(struct usbip_conn_urb_queue* q)
{
    struct urb_queue* queue;

    if (q == NULL || q->priv == NULL)
    {
        return;
    }

    queue = (struct urb_queue*)q->priv;

    osal_mutex_destroy(&queue->lock);
    osal_cond_destroy(&queue->not_empty);
    osal_cond_destroy(&queue->not_full);

    osal_free(queue);
    q->priv = NULL;
}

/**
 * usbip_urb_queue_push - Push URB to queue
 * @q: Queue
 * @header: URB header
 * @data: URB data (can be NULL for IN transfers)
 * @data_len: Data length
 *
 * Blocks if queue is full until space is available.
 *
 * Return: 0 on success, -1 on failure or queue closed
 */
int usbip_urb_queue_push(struct usbip_conn_urb_queue* q, const struct usbip_header* header,
                         const void* data, size_t data_len)
{
    struct urb_queue* queue;
    struct urb_slot* slot;

    if (q == NULL || q->priv == NULL || header == NULL)
    {
        return -1;
    }

    if (data_len > USBIP_URB_DATA_MAX_SIZE)
    {
        LOG_ERR("URB data size %zu exceeds max %d", data_len, USBIP_URB_DATA_MAX_SIZE);
        return -1;
    }

    queue = (struct urb_queue*)q->priv;

    osal_mutex_lock(&queue->lock);

    while (((queue->tail + 1) % USBIP_URB_QUEUE_SIZE) == queue->head && !queue->closed)
    {
        osal_cond_wait(&queue->not_full, &queue->lock);
    }

    if (queue->closed)
    {
        osal_mutex_unlock(&queue->lock);
        return -1;
    }

    slot = &queue->slots[queue->tail];
    memcpy(&slot->header, header, sizeof(*header));

    if (data && data_len > 0)
    {
        memcpy(slot->data, data, data_len);
    }

    slot->data_len = data_len;
    slot->valid = 1;
    queue->tail = (queue->tail + 1) % USBIP_URB_QUEUE_SIZE;

    osal_cond_signal(&queue->not_empty);
    osal_mutex_unlock(&queue->lock);

    return 0;
}

/**
 * usbip_urb_queue_pop - Pop URB from queue
 * @q: Queue
 * @header: Output URB header
 * @data: Output data buffer
 * @data_len: Input/output data buffer size/actual length
 *
 * Blocks if queue is empty until data is available.
 *
 * Return: 0 on success, -1 on failure or queue closed
 */
int usbip_urb_queue_pop(struct usbip_conn_urb_queue* q, struct usbip_header* header, void* data,
                        size_t* data_len)
{
    struct urb_queue* queue;
    struct urb_slot* slot;

    if (q == NULL || q->priv == NULL || header == NULL || data_len == NULL)
    {
        return -1;
    }

    queue = (struct urb_queue*)q->priv;

    osal_mutex_lock(&queue->lock);

    while (queue->head == queue->tail && !queue->closed)
    {
        osal_cond_wait(&queue->not_empty, &queue->lock);
    }

    if (queue->head == queue->tail)
    {
        osal_mutex_unlock(&queue->lock);
        return -1;
    }

    slot = &queue->slots[queue->head];
    memcpy(header, &slot->header, sizeof(*header));
    *data_len = slot->data_len;

    if (slot->data_len > 0 && data)
    {
        if (*data_len < slot->data_len)
        {
            osal_mutex_unlock(&queue->lock);
            return -1;
        }
        memcpy(data, slot->data, slot->data_len);
    }

    slot->valid = 0;
    queue->head = (queue->head + 1) % USBIP_URB_QUEUE_SIZE;

    osal_cond_signal(&queue->not_full);
    osal_mutex_unlock(&queue->lock);

    return 0;
}

/**
 * usbip_urb_queue_close - Close queue to signal threads to exit
 * @q: Queue
 *
 * Sets closed flag and wakes all waiting threads.
 */
void usbip_urb_queue_close(struct usbip_conn_urb_queue* q)
{
    struct urb_queue* queue;

    if (q == NULL || q->priv == NULL)
    {
        return;
    }

    queue = (struct urb_queue*)q->priv;

    osal_mutex_lock(&queue->lock);
    queue->closed = 1;
    osal_cond_broadcast(&queue->not_empty);
    osal_cond_broadcast(&queue->not_full);
    osal_mutex_unlock(&queue->lock);
}
