/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-9-8      hongquan.li   add license declaration
 */

#ifndef USB_CONTROL_H
#define USB_CONTROL_H

#include <stddef.h>
#include <stdint.h>
#include "usbip_common.h"

/*****************************************************************************
 * USB 标准控制传输框架
 *
 * 提供可复用的控制传输处理逻辑，减少各设备驱动的重复代码
 *****************************************************************************/

/*****************************************************************************
 * 返回值
 *****************************************************************************/

#define USB_CONTROL_OK 0         /* 成功，有数据返回 */
#define USB_CONTROL_OK_NO_DATA 1 /* 成功，无数据 */
#define USB_CONTROL_STALL 2      /* 不支持，返回 STALL */
#define USB_CONTROL_ERROR -1     /* 内部错误 */

/*****************************************************************************
 * 配置
 *****************************************************************************/

#define USB_MAX_INTERFACES 4

/*****************************************************************************
 * 回调类型
 *****************************************************************************/

/**
 * 描述符回调 - 处理非标准描述符请求
 */
typedef int (*usb_descriptor_handler)(uint8_t type, uint8_t index, void* user_data, void** data_out,
                                      size_t* data_len);

/**
 * 字符串描述符回调
 */
typedef int (*usb_string_handler)(uint8_t index, void* user_data, void** data_out,
                                  size_t* data_len);

/**
 * 配置变更回调
 */
typedef void (*usb_config_handler)(uint8_t config, void* user_data);

/**
 * 类请求回调
 */
typedef int (*usb_class_handler)(const struct usb_setup_packet* setup, void* user_data,
                                 void** data_out, size_t* data_len);

/**
 * 厂商请求回调
 */
typedef int (*usb_vendor_handler)(const struct usb_setup_packet* setup, void* user_data,
                                  void** data_out, size_t* data_len);

/*****************************************************************************
 * 控制传输上下文
 *****************************************************************************/

struct usb_control_context
{
    /*--- 设备描述符 ---*/
    const struct usb_device_descriptor* device_desc;
    const void* config_desc; /* 配置描述符（含接口和端点） */
    size_t config_desc_len;
    uint8_t num_configs;

    /*--- HID 描述符（可选） ---*/
    const struct usb_hid_descriptor* hid_desc;
    const uint8_t* report_desc;
    size_t report_desc_len;

    /*--- BOS 描述符（可选，USB 2.01+ 需要） ---*/
    const void* bos_desc;
    size_t bos_desc_len;

    /*--- 字符串描述符 ---*/
    const uint8_t* lang_id_desc;  /* 语言 ID 描述符 */
    const uint8_t** string_descs; /* 字符串描述符数组 */
    uint8_t num_strings;

    /*--- 设备状态 ---*/
    uint8_t address;
    uint8_t config_value;
    uint8_t alt_setting[USB_MAX_INTERFACES];

    /*--- 回调 ---*/
    usb_descriptor_handler descriptor_handler;
    usb_string_handler string_handler;
    usb_config_handler config_handler;
    usb_class_handler class_handler;
    usb_vendor_handler vendor_handler;

    /*--- 用户数据 ---*/
    void* user_data;
};

/**
 * usb_control_handle_setup - 处理 USB Setup 请求
 * @setup: Setup 数据包
 * @ctx: 控制传输上下文
 * @data_out: 输出数据缓冲区指针（需调用者 free）
 * @data_len: 输出数据长度
 * Return: USB_CONTROL_* 值
 *
 * 调用者负责 free(*data_out)
 */
int usb_control_handle_setup(const struct usb_setup_packet* setup, struct usb_control_context* ctx,
                             void** data_out, size_t* data_len);

/**
 * usb_control_get_descriptor - 处理 GET_DESCRIPTOR 请求
 * @setup: Setup 数据包
 * @ctx: 控制传输上下文
 * @data_out: 输出数据缓冲区指针
 * @data_len: 输出数据长度
 * Return: USB_CONTROL_* 值
 */
int usb_control_get_descriptor(const struct usb_setup_packet* setup,
                               struct usb_control_context* ctx, void** data_out, size_t* data_len);

/**
 * usb_control_get_string - 处理字符串描述符请求
 * @index: 字符串索引
 * @ctx: 控制传输上下文
 * @data_out: 输出数据缓冲区指针
 * @data_len: 输出数据长度
 * Return: USB_CONTROL_* 值
 */
int usb_control_get_string(uint8_t index, struct usb_control_context* ctx, void** data_out,
                           size_t* data_len);

/**
 * usb_control_set_config - 处理 SET_CONFIGURATION 请求
 * @setup: Setup 数据包
 * @ctx: 控制传输上下文
 * @data_out: 输出数据缓冲区指针
 * @data_len: 输出数据长度
 * Return: USB_CONTROL_* 值
 */
int usb_control_set_config(const struct usb_setup_packet* setup, struct usb_control_context* ctx,
                           void** data_out, size_t* data_len);

/**
 * usb_control_get_config - 处理 GET_CONFIGURATION 请求
 * @setup: Setup 数据包
 * @ctx: 控制传输上下文
 * @data_out: 输出数据缓冲区指针
 * @data_len: 输出数据长度
 * Return: USB_CONTROL_* 值
 */
int usb_control_get_config(const struct usb_setup_packet* setup, struct usb_control_context* ctx,
                           void** data_out, size_t* data_len);

/**
 * usb_control_get_status - 处理 GET_STATUS 请求
 * @setup: Setup 数据包
 * @ctx: 控制传输上下文
 * @data_out: 输出数据缓冲区指针
 * @data_len: 输出数据长度
 * Return: USB_CONTROL_* 值
 */
int usb_control_get_status(const struct usb_setup_packet* setup, struct usb_control_context* ctx,
                           void** data_out, size_t* data_len);

/**
 * usb_control_class_request - 处理类特定请求
 * @setup: Setup 数据包
 * @ctx: 控制传输上下文
 * @data_out: 输出数据缓冲区指针
 * @data_len: 输出数据长度
 * Return: USB_CONTROL_* 值
 */
int usb_control_class_request(const struct usb_setup_packet* setup, struct usb_control_context* ctx,
                              void** data_out, size_t* data_len);

/**
 * 初始化控制传输上下文（静态初始化）
 */
#define USB_CONTROL_CONTEXT_INIT(dev_desc, cfg_desc, cfg_len)                                      \
    {                                                                                              \
        .device_desc = (dev_desc),                                                                 \
        .config_desc = (cfg_desc),                                                                 \
        .config_desc_len = (cfg_len),                                                              \
        .num_configs = 1,                                                                          \
        .address = 0,                                                                              \
        .config_value = 0,                                                                         \
    }

#endif /* USB_CONTROL_H */