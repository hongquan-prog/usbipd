/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-12     hongquan.li  Initial implementation
 */

#ifndef USBIP_URB_H
#define USBIP_URB_H

#ifdef __cplusplus
extern "C" {
#endif

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
int usbip_urb_queue_push(struct usbip_conn_urb_queue* q, const struct usbip_header* header,
                         const void* data, size_t data_len);

/**
 * usbip_urb_queue_pop - Pop URB from queue
 * @q: Queue
 * @header: Output URB header
 * @data: Output data buffer
 * @data_len: Input/output data buffer size/actual length
 * Return: 0 on success, -1 on failure or queue closed
 */
int usbip_urb_queue_pop(struct usbip_conn_urb_queue* q, struct usbip_header* header, void* data,
                        size_t* data_len);

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
int usbip_urb_send_reply(struct usbip_conn_ctx* ctx, struct usbip_header* urb_ret, const void* data,
                         size_t data_len);

#ifdef __cplusplus
}
#endif

#endif