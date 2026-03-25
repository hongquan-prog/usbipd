/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-9-8      hongquan.li   add license declaration
 */

#ifndef USBIP_SERVER_H
#define USBIP_SERVER_H

#include "hal/usbip_transport.h"
#include "usbip_devmgr.h"
#include "usbip_protocol.h"

/*****************************************************************************
 * 服务器配置
 *****************************************************************************/

#define USBIP_DEFAULT_PORT 3240
#define USBIP_MAX_CLIENTS 16
#define USBIP_MAX_DRIVERS 8
#define USBIP_URB_QUEUE_SIZE 8      /* URB 队列槽位数 */
#define USBIP_URB_DATA_MAX_SIZE 256 /* 每个 URB 最大数据大小 */

/*****************************************************************************
 * 服务器主接口
 *****************************************************************************/

/**
 * usbip_server_init - 初始化服务器
 * @port: 监听端口
 * Return: 0成功，负数失败
 */
int usbip_server_init(uint16_t port);

/**
 * usbip_server_run - 运行服务器主循环
 * Return: 0正常退出，负数失败
 */
int usbip_server_run(void);

/**
 * usbip_server_stop - 停止服务器
 */
void usbip_server_stop(void);

/**
 * usbip_server_cleanup - 清理服务器资源
 */
void usbip_server_cleanup(void);

/*****************************************************************************
 * 连接处理接口
 *****************************************************************************/

/**
 * handle_connection - 处理单个连接
 * @transport: 传输层实例
 * @conn_fd: 连接句柄
 *
 * 处理客户端请求直到连接关闭
 */
void handle_connection(struct usbip_transport* transport, int conn_fd);

/**
 * server_urb_loop - URB 处理循环
 * @ctx: 连接上下文
 * @driver: 设备驱动
 * @busid: 设备总线 ID
 * Return: 0 正常退出，负数失败
 */
int usbip_urb_loop(struct usbip_conn_ctx* ctx, struct usbip_device_driver* driver,
                   const char* busid);

#endif /* USBIP_SERVER_H */