/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-3-24      hongquan.li   add license declaration
 */

#ifndef USBIP_PACK_H
#define USBIP_PACK_H

#include <stdint.h>
#include "usbip_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Protocol Encode/Decode Functions
 *****************************************************************************/

/**
 * usbip_pack_op_common - Convert op_common byte order
 * @op: Operation header pointer
 * @to_network: 1=host to network, 0=network to host
 */
void usbip_pack_op_common(struct op_common* op, int to_network);

/**
 * usbip_pack_usb_device - Convert usbip_usb_device byte order
 * @udev: Device descriptor pointer
 * @to_network: 1=host to network, 0=network to host
 */
void usbip_pack_usb_device(struct usbip_usb_device* udev, int to_network);

/**
 * usbip_pack_usb_interface - Convert usbip_usb_interface byte order
 * @uinf: Interface descriptor pointer
 * @to_network: 1=host to network, 0=network to host
 */
void usbip_pack_usb_interface(struct usbip_usb_interface* uinf, int to_network);

/**
 * usbip_pack_header - Convert usbip_header byte order
 * @hdr: URB header pointer
 * @to_network: 1=host to network, 0=network to host
 */
void usbip_pack_header(struct usbip_header* hdr, int to_network);

#ifdef __cplusplus
}
#endif

#endif /* USBIP_PACK_H */