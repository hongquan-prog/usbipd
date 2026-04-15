/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-3-24      hongquan.li   add license declaration
 */

#ifndef USBIP_SERVER_H
#define USBIP_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Server Main Interface
 *****************************************************************************/

/**
 * usbip_server_init - Initialize server
 * @port: Listen port
 * Return: 0 on success, negative on failure
 */
int usbip_server_init(uint16_t port);

/**
 * usbip_server_run - Run server main loop
 * Return: 0 normal exit, negative on failure
 */
int usbip_server_run(void);

/**
 * usbip_server_stop - Stop server
 */
void usbip_server_stop(void);

/**
 * usbip_server_cleanup - Cleanup server resources
 */
void usbip_server_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* USBIP_SERVER_H */