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
    struct usbip_device_driver* drivers[CONFIG_USBIP_MAX_DRIVERS];
    uint32_t reply_count;
    int device_count = 0;
    int dev_idx;
    int dev_count = 0;
    int num_drivers = 0;
    int i;

    /* Send operation header */
    if (usbip_send_op_common(ctx, OP_REP_DEVLIST, ST_OK) < 0)
    {
        LOG_ERR("Failed to send OP_REP_DEVLIST header");
        return -1;
    }

    /* Count available (not busy) devices from all drivers */
    num_drivers = usbip_devmgr_get_driver_snapshot(drivers, CONFIG_USBIP_MAX_DRIVERS);
    for (i = 0; i < num_drivers; i++)
    {
        driver = drivers[i];
        dev_count = usbip_driver_get_device_count(driver);
        for (dev_idx = 0; dev_idx < dev_count; dev_idx++)
        {
            if (usbip_driver_get_device_by_index(driver, dev_idx, &udev) < 0)
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
    num_drivers = usbip_devmgr_get_driver_snapshot(drivers, CONFIG_USBIP_MAX_DRIVERS);
    for (i = 0; i < num_drivers; i++)
    {
        driver = drivers[i];
        dev_count = usbip_driver_get_device_count(driver);
        for (dev_idx = 0; dev_idx < dev_count; dev_idx++)
        {
            if (usbip_driver_get_device_by_index(driver, dev_idx, &udev) < 0)
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
            if (usbip_driver_get_interface(driver, dev_idx, &iface) < 0)
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
    char busid[SYSFS_BUS_ID_SIZE + 1];
    struct usbip_usb_device reply_dev;
    struct usbip_device_driver* driver = NULL;
    struct usbip_device_driver* drivers[CONFIG_USBIP_MAX_DRIVERS];
    struct usbip_connection* conn = NULL;
    const struct usbip_usb_device* found_dev = NULL;
    int found = 0;
    int num_drivers = 0;
    int i, j, dev_count;

    memset(busid, 0, sizeof(busid));
    if (transport_recv(ctx, busid, SYSFS_BUS_ID_SIZE) != SYSFS_BUS_ID_SIZE)
    {
        LOG_ERR("Failed to receive busid");
        return -1;
    }

    busid[SYSFS_BUS_ID_SIZE] = '\0';
    LOG_DBG("Received OP_REQ_IMPORT for busid: %s", busid);

    /* Check device availability */
    if (!usbip_is_device_available(busid))
    {
        LOG_WRN("Device %s is not available", busid);
        usbip_send_op_common(ctx, OP_REP_IMPORT, ST_NODEV);
        return -1;
    }

    /* Find the device */
    num_drivers = usbip_devmgr_get_driver_snapshot(drivers, CONFIG_USBIP_MAX_DRIVERS);
    for (i = 0; i < num_drivers && !found; i++)
    {
        driver = drivers[i];
        dev_count = usbip_driver_get_device_count(driver);
        for (j = 0; j < dev_count; j++)
        {
            if (usbip_driver_get_device_by_index(driver, j, &reply_dev) < 0)
            {
                continue;
            }
            if (strcmp(reply_dev.busid, busid) == 0)
            {
                found = 1;
                found_dev = &reply_dev;
                break;
            }
        }
    }

    if (!found)
    {
        LOG_ERR("Device %s not found in driver", busid);
        usbip_send_op_common(ctx, OP_REP_IMPORT, ST_NODEV);
        return -1;
    }

    /* Check connection limit */
    if (usbip_conn_manager_get_count() >= USBIP_MAX_CONNECTIONS)
    {
        LOG_WRN("Connection limit reached");
        usbip_send_op_common(ctx, OP_REP_IMPORT, ST_ERROR);
        return -1;
    }

    /* Create connection */
    conn = usbip_connection_create(ctx);
    if (conn == NULL)
    {
        LOG_ERR("Failed to create connection for %s", busid);
        usbip_send_op_common(ctx, OP_REP_IMPORT, ST_ERROR);
        return -1;
    }

    if (usbip_conn_manager_add(conn) < 0)
    {
        LOG_ERR("Failed to add connection to manager");
        goto err_destroy_conn;
    }

    if (usbip_bind_device(busid, conn) < 0)
    {
        LOG_ERR("Failed to bind device %s", busid);
        goto err_remove_conn;
    }

    if (usbip_driver_export_device(driver, busid, conn) < 0)
    {
        LOG_ERR("Failed to export device %s", busid);
        goto err_unbind;
    }

    /* Start connection processing threads */
    if (usbip_connection_start(conn, driver, busid) < 0)
    {
        LOG_ERR("Failed to start connection for %s", busid);
        goto err_unexport;
    }

    /* All setup done, now send success response */
    if (usbip_send_op_common(ctx, OP_REP_IMPORT, ST_OK) < 0)
    {
        LOG_ERR("Failed to send OP_REP_IMPORT header");
        goto err_unexport;
    }

    /* Send device descriptor */
    memcpy(&reply_dev, found_dev, sizeof(reply_dev));
    usbip_pack_usb_device(&reply_dev, 1);

    if (transport_send(ctx, &reply_dev, sizeof(reply_dev)) != sizeof(reply_dev))
    {
        LOG_ERR("Failed to send device description");
        goto err_unexport;
    }

    return 0;

err_unexport:
    usbip_driver_unexport_device(driver, busid);
err_unbind:
    usbip_unbind_device(busid);
err_remove_conn:
    usbip_conn_manager_remove(conn);
err_destroy_conn:
    usbip_connection_destroy(conn);

    return -1;
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
        usbip_send_op_common(ctx, op.code & ~OP_REQUEST, ST_ERROR);
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
            usbip_send_op_common(ctx, op.code & ~OP_REQUEST, ST_NA);
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
