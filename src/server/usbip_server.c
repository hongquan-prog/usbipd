/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-9-8      hongquan.li   add license declaration
 */

/*
 * USBIP Server Core
 *
 * 服务器核心逻辑：连接管理、设备枚举
 */
#include <endian.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hal/usbip_log.h"
#include "hal/usbip_osal.h"
#include "hal/usbip_transport.h"
#include "usbip_protocol.h"
#include "usbip_server.h"

LOG_MODULE_REGISTER(server, CONFIG_SERVER_LOG_LEVEL);

/*****************************************************************************
 * 全局状态
 *****************************************************************************/

static volatile int s_running = 1;

/*****************************************************************************
 * 静态函数声明
 *****************************************************************************/

static int usbip_server_handle_devlist(struct usbip_conn_ctx* ctx);
static int usbip_server_handle_import_req(struct usbip_conn_ctx* ctx);

/*
 * OP_REQ_DEVLIST 处理
 */

static int usbip_server_handle_devlist(struct usbip_conn_ctx* ctx)
{
    struct usbip_usb_device* devices = NULL;
    struct usbip_usb_interface iface;
    int device_count = 0;
    uint32_t reply_count;
    struct usbip_device_driver* driver;
    int ret = -1;

    /* 发送操作头 */
    if (usbip_send_op_common(ctx, OP_REP_DEVLIST, ST_OK) < 0)
    {
        LOG_ERR("Failed to send OP_REP_DEVLIST header");
        return -1;
    }

    /* 收集所有驱动的设备 */
    for (driver = usbip_get_first_driver(); driver != NULL; driver = usbip_get_next_driver(driver))
    {

        struct usbip_usb_device* drv_devices = NULL;
        int drv_count = 0;

        if (driver->get_device_list(driver, &drv_devices, &drv_count) == 0 && drv_count > 0)
        {
            struct usbip_usb_device* new_devices =
                realloc(devices, (device_count + drv_count) * sizeof(*devices));
            if (!new_devices)
            {
                free(drv_devices);
                continue;
            }
            devices = new_devices;
            memcpy(&devices[device_count], drv_devices, drv_count * sizeof(*devices));
            device_count += drv_count;
            free(drv_devices);
        }
    }

    /* 发送设备数量 */
    reply_count = htobe32(device_count);
    if (transport_send(ctx, &reply_count, sizeof(reply_count)) != sizeof(reply_count))
    {
        LOG_ERR("Failed to send device count");
        goto out;
    }

    LOG_DBG("Sending %d device(s)", device_count);

    /* 发送每个设备 */
    for (int i = 0; i < device_count; i++)
    {
        struct usbip_usb_device udev;

        memcpy(&udev, &devices[i], sizeof(udev));
        usbip_pack_usb_device(&udev, 1);

        if (transport_send(ctx, &udev, sizeof(udev)) != sizeof(udev))
        {
            LOG_ERR("Failed to send device %d", i);
            goto out;
        }

        /* 发送接口信息 */
        memset(&iface, 0, sizeof(iface));
        iface.bInterfaceClass = 0x03; /* HID */
        iface.bInterfaceSubClass = 0x01;
        iface.bInterfaceProtocol = 0x01;
        usbip_pack_usb_interface(&iface, 1);

        if (transport_send(ctx, &iface, sizeof(iface)) != sizeof(iface))
        {
            LOG_ERR("Failed to send interface for device %d", i);
            goto out;
        }
    }

    ret = 0;

out:
    free(devices);
    return ret;
}

/*****************************************************************************
 * OP_REQ_IMPORT 处理
 *****************************************************************************/

static int usbip_server_handle_import_req(struct usbip_conn_ctx* ctx)
{
    char busid[SYSFS_BUS_ID_SIZE];
    struct usbip_device_driver* driver = NULL;
    const struct usbip_usb_device* found_dev = NULL;
    int found = 0;

    /* 接收 busid */
    memset(busid, 0, sizeof(busid));
    if (transport_recv(ctx, busid, sizeof(busid)) != sizeof(busid))
    {
        LOG_ERR("Failed to receive busid");
        usbip_send_op_common(ctx, OP_REP_IMPORT, ST_ERROR);
        return -1;
    }

    /* 查找设备 */
    for (driver = usbip_get_first_driver(); driver != NULL && !found;
         driver = usbip_get_next_driver(driver))
    {

        found_dev = driver->get_device(driver, busid);
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

    if (usbip_is_device_busy(busid))
    {
        LOG_WRN("Device busy: %s", busid);
        usbip_send_op_common(ctx, OP_REP_IMPORT, ST_DEV_BUSY);
        return -1;
    }

    /* 发送成功响应 */
    if (usbip_send_op_common(ctx, OP_REP_IMPORT, ST_OK) < 0)
    {
        LOG_ERR("Failed to send OP_REP_IMPORT header");
        return -1;
    }

    /* 发送设备描述 */
    struct usbip_usb_device reply_dev;
    memcpy(&reply_dev, found_dev, sizeof(reply_dev));
    usbip_pack_usb_device(&reply_dev, 1);

    if (transport_send(ctx, &reply_dev, sizeof(reply_dev)) != sizeof(reply_dev))
    {
        LOG_ERR("Failed to send device description");
        return -1;
    }

    /* 导出设备 */
    if (driver->export_device(driver, busid, ctx) < 0)
    {
        LOG_ERR("Failed to export device: %s", busid);
        return -1;
    }

    LOG_DBG("Device imported: %s", busid);

    /* 进入 URB 处理循环 */
    return usbip_urb_loop(ctx, driver, busid);
}

/*****************************************************************************
 * 连接处理
 *****************************************************************************/

void usbip_server_handle_connection(struct usbip_conn_ctx* ctx)
{
    struct op_common op;

    LOG_DBG("Handling new connection");

    /* 接收操作头 */
    if (usbip_recv_op_common(ctx, &op) < 0)
    {
        LOG_DBG("Failed to receive op_common");
        transport_close(ctx);
        return;
    }

    LOG_DBG("Received op: version=0x%04x code=0x%04x status=0x%08x", op.version, op.code,
            op.status);

    /* 检查版本 */
    if (op.version != USBIP_VERSION)
    {
        LOG_ERR("Unsupported version: 0x%04x", op.version);
        usbip_send_op_common(ctx, op.code | OP_REPLY, ST_ERROR);
        transport_close(ctx);
        return;
    }

    /* 处理操作 */
    switch (op.code)
    {
        case OP_REQ_DEVLIST:
            usbip_server_handle_devlist(ctx);
            break;

        case OP_REQ_IMPORT:
            usbip_server_handle_import_req(ctx);
            break;

        default:
            LOG_WRN("Unknown operation: 0x%04x", op.code);
            usbip_send_op_common(ctx, op.code | OP_REPLY, ST_NA);
            break;
    }

    transport_close(ctx);
}

/*****************************************************************************
 * 服务器接口
 *****************************************************************************/

int usbip_server_init(uint16_t port)
{
    if (transport_listen(port) < 0)
    {
        LOG_ERR("Failed to listen on port %d", port);
        return -1;
    }

    LOG_INF("Server listening on port %d", port);

    return 0;
}

int usbip_server_run(void)
{
    struct pollfd pfd;
    struct usbip_conn_ctx* ctx;
    int listen_fd;

    listen_fd = transport_get_poll_fd();

    while (s_running)
    {
        pfd.fd = listen_fd;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 1000);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            LOG_ERR("poll failed");
            break;
        }

        if (ret == 0)
            continue;

        if (pfd.revents & POLLIN)
        {
            ctx = transport_accept();
            if (ctx)
            {
                usbip_server_handle_connection(ctx);
            }
        }
    }

    return 0;
}

void usbip_server_stop(void)
{
    s_running = 0;
}

void usbip_server_cleanup(void)
{
    transport_destroy();
}