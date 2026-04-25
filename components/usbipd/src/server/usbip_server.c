/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-3-24      hongquan.li   add license declaration
 * 2026-04-09     hongquan.li   Refactor for multi-client support
 */

/*
 * USBIP Server Core - Multi-Client Support
 *
 * Server core logic: connection management, device enumeration,
 * multi-client support with per-connection URB processing
 */
#include <endian.h>
#include <stdio.h>
#include <string.h>

#include "hal/usbip_log.h"
#include "hal/usbip_osal.h"
#include "hal/usbip_transport.h"
#include "usbip_conn.h"
#include "usbip_devmgr.h"
#include "usbip_pack.h"
#include "usbip_server.h"

LOG_MODULE_REGISTER(server, CONFIG_USBIP_LOG_LEVEL);

/*****************************************************************************
 * Global State
 *****************************************************************************/

static volatile int s_running = 1;

/*****************************************************************************
 * Static Function Declarations
 *****************************************************************************/

static int usbip_server_handle_devlist(struct usbip_conn_ctx* ctx);
static int usbip_server_handle_import_req(struct usbip_conn_ctx* ctx);

/*****************************************************************************
 * Protocol Send/Receive Functions
 *****************************************************************************/

static int usbip_recv_op_common(struct usbip_conn_ctx* ctx, struct op_common* op)
{
    ssize_t n;

    n = transport_recv(ctx, op, sizeof(*op));
    if (n != sizeof(*op))
    {
        return -1;
    }

    usbip_pack_op_common(op, 0); /* Network order to host order */
    return 0;
}

static int usbip_send_op_common(struct usbip_conn_ctx* ctx, uint16_t code, uint32_t status)
{
    struct op_common op = {.version = USBIP_VERSION, .code = code, .status = status};
    ssize_t n;

    usbip_pack_op_common(&op, 1); /* Host order to network order */

    n = transport_send(ctx, &op, sizeof(op));
    if (n != sizeof(op))
    {
        return -1;
    }
    return 0;
}

/*****************************************************************************
 * OP_REQ_DEVLIST Processing
 *****************************************************************************/

/**
 * usbip_server_handle_devlist - Handle device list request
 * @ctx: Connection context
 *
 * Sends list of all available devices to client.
 *
 * Return: 0 on success, -1 on failure
 */
static int usbip_server_handle_devlist(struct usbip_conn_ctx* ctx)
{
    struct usbip_usb_device udev;
    struct usbip_usb_interface iface;
    struct usbip_device_driver* driver;
    uint32_t reply_count;
    int device_count = 0;
    int drv_idx;
    int drv_count = 0;

    /* Send operation header */
    if (usbip_send_op_common(ctx, OP_REP_DEVLIST, ST_OK) < 0)
    {
        LOG_ERR("Failed to send OP_REP_DEVLIST header");
        return -1;
    }

    /* Count available (not busy) devices from all drivers */
    for (driver = usbip_devmgr_begin(); driver != NULL; driver = usbip_devmgr_next(driver))
    {
        drv_count = usbip_driver_get_device_count(driver);
        for (drv_idx = 0; drv_idx < drv_count; drv_idx++)
        {
            if (usbip_driver_get_device_by_index(driver, drv_idx, &udev) < 0)
            {
                continue;
            }

            if (!usbip_is_device_available(udev.busid))
            {
                continue;
            }
            device_count++;
        }
    }

    /* Send device count */
    reply_count = htobe32(device_count);

    if (transport_send(ctx, &reply_count, sizeof(reply_count)) != sizeof(reply_count))
    {
        LOG_ERR("Failed to send device count");
        return -1;
    }

    LOG_DBG("Sending %d device(s)", device_count);

    /* Send each available device from all drivers */
    for (driver = usbip_devmgr_begin(); driver != NULL; driver = usbip_devmgr_next(driver))
    {
        drv_count = usbip_driver_get_device_count(driver);
        for (drv_idx = 0; drv_idx < drv_count; drv_idx++)
        {
            if (usbip_driver_get_device_by_index(driver, drv_idx, &udev) < 0)
            {
                continue;
            }

            /* Filter out already-exported devices */
            if (!usbip_is_device_available(udev.busid))
            {
                continue;
            }

            usbip_pack_usb_device(&udev, 1);

            if (transport_send(ctx, &udev, sizeof(udev)) != sizeof(udev))
            {
                LOG_ERR("Failed to send device");
                return -1;
            }

            /* Send interface information */
            memset(&iface, 0, sizeof(iface));
            if (usbip_driver_get_interface(driver, drv_idx, &iface) < 0)
            {
                LOG_WRN("Driver %s does not implement get_interface, using HID fallback",
                        driver->name);
                iface.bInterfaceClass = 0x03;
                iface.bInterfaceSubClass = 0x01;
                iface.bInterfaceProtocol = 0x01;
            }
            usbip_pack_usb_interface(&iface, 1);

            if (transport_send(ctx, &iface, sizeof(iface)) != sizeof(iface))
            {
                LOG_ERR("Failed to send interface");
                return -1;
            }
        }
    }

    return 0;
}

/*****************************************************************************
 * OP_REQ_IMPORT Processing (Multi-Client)
 *****************************************************************************/

/**
 * usbip_server_handle_import_req - Handle device import request
 * @ctx: Connection context
 *
 * Finds device, checks availability, creates connection and starts URB processing.
 * For multi-client: non-blocking, returns immediately after starting connection.
 *
 * Return: 0 on success (connection started), -1 on failure
 */
static int usbip_server_handle_import_req(struct usbip_conn_ctx* ctx)
{
    char busid[SYSFS_BUS_ID_SIZE];
    struct usbip_device_driver* driver = NULL;
    const struct usbip_usb_device* found_dev = NULL;
    struct usbip_connection* conn = NULL;
    struct usbip_usb_device reply_dev;
    int found = 0;

    /* Receive busid */
    memset(busid, 0, sizeof(busid));
    if (transport_recv(ctx, busid, sizeof(busid)) != sizeof(busid))
    {
        LOG_ERR("Failed to receive busid");
        usbip_send_op_common(ctx, OP_REP_IMPORT, ST_ERROR);
        return -1;
    }

    /* Check connection limit before processing import */
    if (usbip_conn_manager_get_count() >= USBIP_MAX_CONNECTIONS)
    {
        LOG_WRN("Maximum connections (%d) reached, rejecting import request for %s",
                USBIP_MAX_CONNECTIONS, busid);
        usbip_send_op_common(ctx, OP_REP_IMPORT, ST_DEV_BUSY);
        return -1;
    }

    /* Find device */
    for (driver = usbip_devmgr_begin(); driver != NULL && !found;
         driver = usbip_devmgr_next(driver))
    {
        found_dev = usbip_driver_get_device(driver, busid);
        if (found_dev)
        {
            found = 1;
            break;
        }
    }

    if (!found)
    {
        LOG_WRN("Device not found: %s", busid);
        usbip_send_op_common(ctx, OP_REP_IMPORT, ST_NODEV);
        return -1;
    }

    if (!usbip_is_device_available(busid))
    {
        LOG_WRN("Device busy: %s", busid);
        usbip_send_op_common(ctx, OP_REP_IMPORT, ST_DEV_BUSY);
        return -1;
    }

    /* Send success response */
    if (usbip_send_op_common(ctx, OP_REP_IMPORT, ST_OK) < 0)
    {
        LOG_ERR("Failed to send OP_REP_IMPORT header");
        return -1;
    }

    /* Send device descriptor */
    memcpy(&reply_dev, found_dev, sizeof(reply_dev));
    usbip_pack_usb_device(&reply_dev, 1);

    if (transport_send(ctx, &reply_dev, sizeof(reply_dev)) != sizeof(reply_dev))
    {
        LOG_ERR("Failed to send device description");
        return -1;
    }

    /* Create connection for this device */
    conn = usbip_connection_create(ctx);
    if (conn == NULL)
    {
        LOG_ERR("Failed to create connection for device: %s", busid);
        return -1;
    }

    /* Add to connection manager */
    if (usbip_conn_manager_add(conn) < 0)
    {
        LOG_ERR("Failed to add connection to manager");
        usbip_connection_destroy(conn);
        return -1;
    }

    /* Bind device to connection */
    if (usbip_bind_device(busid, conn) < 0)
    {
        LOG_ERR("Failed to bind device: %s", busid);
        usbip_conn_manager_remove(conn);
        usbip_connection_destroy(conn);
        return -1;
    }

    /* Export device via driver */
    if (usbip_driver_export_device(driver, busid, conn) < 0)
    {
        LOG_ERR("Failed to export device: %s", busid);
        usbip_unbind_device(busid);
        usbip_conn_manager_remove(conn);
        usbip_connection_destroy(conn);
        return -1;
    }

    /* Start connection URB processing (non-blocking) */
    if (usbip_connection_start(conn, driver, busid) < 0)
    {
        LOG_ERR("Failed to start connection for device: %s", busid);
        usbip_driver_unexport_device(driver, busid);
        usbip_unbind_device(busid);
        usbip_conn_manager_remove(conn);
        usbip_connection_destroy(conn);
        return -1;
    }

    LOG_INF("Device imported and connection started: %s", busid);

    return 0;
}

/*****************************************************************************
 * Single Operation Handling (Non-Import)
 *****************************************************************************/

/**
 * usbip_server_handle_single_op - Handle single operation and close connection
 * @ctx: Connection context
 *
 * Handles DEVLIST or failed IMPORT operations, then closes connection.
 * For successful IMPORT, connection ownership is transferred and not closed here.
 */
static void usbip_server_handle_single_op(struct usbip_conn_ctx* ctx)
{
    struct op_common op;

    LOG_DBG("Handling new connection");

    /* Receive operation header */
    if (usbip_recv_op_common(ctx, &op) < 0)
    {
        LOG_DBG("Failed to receive op_common");
        transport_close(ctx);
        return;
    }

    LOG_DBG("Received op: version=0x%04x code=0x%04x status=0x%08x", op.version, op.code,
            op.status);

    /* Check version */
    if (op.version != USBIP_VERSION)
    {
        LOG_ERR("Unsupported version: 0x%04x", op.version);
        usbip_send_op_common(ctx, op.code | OP_REPLY, ST_ERROR);
        transport_close(ctx);
        return;
    }

    /* Handle operation */
    switch (op.code)
    {
        case OP_REQ_DEVLIST:
            usbip_server_handle_devlist(ctx);
            transport_close(ctx);
            break;

        case OP_REQ_IMPORT:
            /* Import transfers connection ownership on success */
            if (usbip_server_handle_import_req(ctx) < 0)
            {
                transport_close(ctx);
            }
            /* On success, connection is owned by connection manager */
            break;

        default:
            LOG_WRN("Unknown operation: 0x%04x", op.code);
            usbip_send_op_common(ctx, op.code | OP_REPLY, ST_NA);
            transport_close(ctx);
            break;
    }
}

/*****************************************************************************
 * Server Interface
 *****************************************************************************/

/**
 * usbip_server_init - Initialize server
 * @port: Listen port
 *
 * Initialize transport layer and connection manager.
 *
 * Return: 0 on success, -1 on failure
 */
int usbip_server_init(uint16_t port)
{
    /* Initialize device manager (hash table) */
    if (usbip_devmgr_init() < 0)
    {
        LOG_ERR("Failed to initialize device manager");
        return -1;
    }

    /* Initialize connection manager */
    if (usbip_conn_manager_init() < 0)
    {
        LOG_ERR("Failed to initialize connection manager");
        return -1;
    }

    /* Start listening */
    if (transport_listen(port) < 0)
    {
        LOG_ERR("Failed to listen on port %d", port);
        usbip_conn_manager_cleanup();
        return -1;
    }

    LOG_INF("Server initialized, listening on port %d", port);

    return 0;
}

/**
 * usbip_server_run - Run server main loop (Multi-Client)
 *
 * Accepts connections and handles them. Each successful IMPORT creates
 * a persistent connection handled by its own threads.
 *
 * Return: 0 on normal exit, -1 on failure
 */
int usbip_server_run(void)
{
    struct usbip_conn_ctx* ctx;

    LOG_INF("Server running (multi-client mode)");

    while (s_running)
    {
        ctx = transport_accept();
        if (ctx == NULL)
        {
            continue;
        }

        /* Handle single operation (DEVLIST or IMPORT) */
        usbip_server_handle_single_op(ctx);
    }

    return 0;
}

/**
 * usbip_server_stop - Stop server
 *
 * Signal server to stop accepting new connections.
 */
void usbip_server_stop(void)
{
    s_running = 0;

    /* Stop transport to interrupt blocking accept() */
    transport_stop();
}

/**
 * usbip_server_cleanup - Cleanup server resources
 *
 * Cleanup all connections and transport resources.
 */
void usbip_server_cleanup(void)
{
    /* Cleanup all active connections */
    usbip_conn_manager_cleanup();

    /* Destroy transport */
    transport_destroy();

    LOG_INF("Server cleanup complete");
}
