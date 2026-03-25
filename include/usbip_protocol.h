/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-9-8      hongquan.li   add license declaration
 */

#ifndef USBIP_PROTOCOL_H
#define USBIP_PROTOCOL_H

#include <stdint.h>
#include "hal/usbip_transport.h"

/*****************************************************************************
 * USBIP 协议常量
 *****************************************************************************/

#define USBIP_VERSION 0x0111

/* 操作码 */
#define OP_REQUEST (0x80 << 8)
#define OP_REPLY (0x00 << 8)

#define OP_REQ_DEVLIST (OP_REQUEST | 0x05)
#define OP_REP_DEVLIST (OP_REPLY | 0x05)
#define OP_REQ_IMPORT (OP_REQUEST | 0x03)
#define OP_REP_IMPORT (OP_REPLY | 0x03)

/* URB 命令 */
#define USBIP_CMD_SUBMIT 0x0001
#define USBIP_CMD_UNLINK 0x0002
#define USBIP_RET_SUBMIT 0x0003
#define USBIP_RET_UNLINK 0x0004

/* 方向 */
#define USBIP_DIR_OUT 0x00
#define USBIP_DIR_IN 0x01

/* 状态码 */
#define ST_OK 0x00
#define ST_NA 0x01
#define ST_DEV_BUSY 0x02
#define ST_DEV_ERR 0x03
#define ST_NODEV 0x04
#define ST_ERROR 0x05

/* USB 速度 */
#define USB_SPEED_UNKNOWN 0
#define USB_SPEED_LOW 1
#define USB_SPEED_FULL 2
#define USB_SPEED_HIGH 3
#define USB_SPEED_WIRELESS 4
#define USB_SPEED_SUPER 5
#define USB_SPEED_SUPER_PLUS 6

/*****************************************************************************
 * 协议数据结构（与官方实现兼容）
 *****************************************************************************/

#define SYSFS_PATH_MAX 256
#define SYSFS_BUS_ID_SIZE 32

/* 操作公共头 (8 bytes) */
struct op_common
{
    uint16_t version;
    uint16_t code;
    uint32_t status;
} __attribute__((packed));

/* USB设备描述 (312 bytes) */
struct usbip_usb_device
{
    char path[SYSFS_PATH_MAX];
    char busid[SYSFS_BUS_ID_SIZE];
    uint32_t busnum;
    uint32_t devnum;
    uint32_t speed;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bConfigurationValue;
    uint8_t bNumConfigurations;
    uint8_t bNumInterfaces;
} __attribute__((packed));

/* USB接口描述 (4 bytes) */
struct usbip_usb_interface
{
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t padding;
} __attribute__((packed));

/* URB基础头 (20 bytes) */
struct usbip_header_basic
{
    uint32_t command;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
} __attribute__((packed));

/* SUBMIT 命令 (28 bytes) */
struct usbip_header_cmd_submit
{
    uint32_t transfer_flags;
    int32_t transfer_buffer_length;
    int32_t start_frame;
    int32_t number_of_packets;
    int32_t interval;
    uint8_t setup[8];
} __attribute__((packed));

/* SUBMIT 返回 (20 bytes) */
struct usbip_header_ret_submit
{
    int32_t status;
    int32_t actual_length;
    int32_t start_frame;
    int32_t number_of_packets;
    int32_t error_count;
} __attribute__((packed));

/* UNLINK 命令 (4 bytes) */
struct usbip_header_cmd_unlink
{
    uint32_t seqnum;
} __attribute__((packed));

/* UNLINK 返回 (4 bytes) */
struct usbip_header_ret_unlink
{
    int32_t status;
} __attribute__((packed));

/* URB 完整头 (48 bytes) */
struct usbip_header
{
    struct usbip_header_basic base;
    union
    {
        struct usbip_header_cmd_submit cmd_submit;
        struct usbip_header_ret_submit ret_submit;
        struct usbip_header_cmd_unlink cmd_unlink;
        struct usbip_header_ret_unlink ret_unlink;
    } u;
} __attribute__((packed));

/*****************************************************************************
 * USB 描述符常量
 *****************************************************************************/

#define USB_DT_DEVICE 0x01
#define USB_DT_CONFIG 0x02
#define USB_DT_STRING 0x03
#define USB_DT_INTERFACE 0x04
#define USB_DT_ENDPOINT 0x05
#define USB_DT_DEVICE_QUALIFIER 0x06
#define USB_DT_OTHER_SPEED_CONFIG 0x07
#define USB_DT_HID 0x21
#define USB_DT_REPORT 0x22

/* USB 请求类型 */
#define USB_REQ_GET_STATUS 0x00
#define USB_REQ_CLEAR_FEATURE 0x01
#define USB_REQ_SET_FEATURE 0x03
#define USB_REQ_SET_ADDRESS 0x05
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_DESCRIPTOR 0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE 0x0A
#define USB_REQ_SET_INTERFACE 0x0B

/* 端点方向 */
#define USB_DIR_OUT 0x00
#define USB_DIR_IN 0x80

/* 端点类型 */
#define USB_ENDPOINT_XFER_CONTROL 0x00
#define USB_ENDPOINT_XFER_ISOC 0x01
#define USB_ENDPOINT_XFER_BULK 0x02
#define USB_ENDPOINT_XFER_INT 0x03

/* 标准请求类型 bmRequestType */
#define USB_TYPE_STANDARD (0x00 << 5)
#define USB_TYPE_CLASS (0x01 << 5)
#define USB_TYPE_VENDOR (0x02 << 5)

#define USB_RECIP_DEVICE 0x00
#define USB_RECIP_INTERFACE 0x01
#define USB_RECIP_ENDPOINT 0x02

/*****************************************************************************
 * 协议编解码函数
 *****************************************************************************/

/**
 * usbip_pack_op_common - 转换 op_common 字节序
 * @op: 操作头指针
 * @to_network: 1=主机序转网络序, 0=网络序转主机序
 */
void usbip_pack_op_common(struct op_common* op, int to_network);

/**
 * usbip_pack_usb_device - 转换 usbip_usb_device 字节序
 * @udev: 设备描述指针
 * @to_network: 1=主机序转网络序, 0=网络序转主机序
 */
void usbip_pack_usb_device(struct usbip_usb_device* udev, int to_network);

/**
 * usbip_pack_usb_interface - 转换 usbip_usb_interface 字节序
 * @uinf: 接口描述指针
 * @to_network: 1=主机序转网络序, 0=网络序转主机序
 */
void usbip_pack_usb_interface(struct usbip_usb_interface* uinf, int to_network);

/**
 * usbip_pack_header - 转换 usbip_header 字节序
 * @hdr: URB头指针
 * @to_network: 1=主机序转网络序, 0=网络序转主机序
 */
void usbip_pack_header(struct usbip_header* hdr, int to_network);

/*****************************************************************************
 * 协议收发函数
 *****************************************************************************/

/**
 * usbip_recv_op_common - 接收操作头
 * @ctx: 连接上下文
 * @op: 输出操作头
 * Return: 0成功，负数失败
 */
int usbip_recv_op_common(struct usbip_conn_ctx* ctx, struct op_common* op);

/**
 * usbip_send_op_common - 发送操作头
 * @ctx: 连接上下文
 * @code: 操作码
 * @status: 状态码
 * Return: 0成功，负数失败
 */
int usbip_send_op_common(struct usbip_conn_ctx* ctx, uint16_t code, uint32_t status);

/**
 * usbip_recv_header - 接收URB头
 * @ctx: 连接上下文
 * @hdr: 输出URB头
 * Return: 0成功，负数失败
 */
int usbip_recv_header(struct usbip_conn_ctx* ctx, struct usbip_header* hdr);

/**
 * usbip_send_header - 发送URB头
 * @ctx: 连接上下文
 * @hdr: URB头
 * Return: 0成功，负数失败
 */
int usbip_send_header(struct usbip_conn_ctx* ctx, struct usbip_header* hdr);

#endif /* USBIP_PROTOCOL_H */