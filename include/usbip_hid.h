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
 * Virtual HID Base - HID 通用框架
 *
 * 提供 HID 设备通用的请求处理和数据管理
 * 可被不同的 HID 设备实现复用
 */

#ifndef VIRTUAL_HID_H
#define VIRTUAL_HID_H

#include <stddef.h>
#include <stdint.h>

/*****************************************************************************
 * HID 常量定义
 *****************************************************************************/

/* HID 类请求码 */
#define HID_REQUEST_GET_REPORT 0x01
#define HID_REQUEST_GET_IDLE 0x02
#define HID_REQUEST_GET_PROTOCOL 0x03
#define HID_REQUEST_SET_REPORT 0x09
#define HID_REQUEST_SET_IDLE 0x0A
#define HID_REQUEST_SET_PROTOCOL 0x0B

/* HID 协议模式 */
#define HID_PROTOCOL_BOOT 0x00
#define HID_PROTOCOL_REPORT 0x01

/* HID 报告类型 */
#define HID_REPORT_TYPE_INPUT 0x01
#define HID_REPORT_TYPE_OUTPUT 0x02
#define HID_REPORT_TYPE_FEATURE 0x03

/* 默认报告大小 */
#define HID_DEFAULT_REPORT_SIZE 64

/*****************************************************************************
 * Report ID 处理选项
 *
 * HID 规范关于 Report ID 的说明：
 *
 * 1. Report ID 是可选的
 *    - 如果报告描述符中没有 Report ID 项，则不使用 Report ID
 *    - 如果使用 Report ID，取值范围必须是 1-255 (0x01-0xFF)
 *    - 0x00 是保留值，表示"没有 Report ID"
 *
 * 2. Linux hidraw 驱动的行为：
 *    - 只有当报告描述符中确实定义了 Report ID 时才会处理 Report ID
 *    - 对于无 Report ID 的设备，hidraw 原样传输数据，不添加/删除任何字节
 *    - hidraw 不会自动添加或删除 0x00
 *
 * 本项目中的设备情况：
 *    - CMSIS-DAP: 报告描述符无 Report ID，报告大小 64 字节
 *    - 处理方式：原样传递，不添加/剥离任何字节
 *****************************************************************************/

/* Report ID 处理模式 */
#define HID_REPORT_ID_NONE 0x00    /* 不处理，原样传递 */
#define HID_REPORT_ID_AUTO 0x01    /* 自动检测（针对 CMSIS-DAP 优化） */
#define HID_REPORT_ID_STRIP 0x02   /* 总是去掉第一个字节作为 Report ID */
#define HID_REPORT_ID_PREPEND 0x03 /* 总是添加 Report ID（兼容性模式） */

/*****************************************************************************
 * HID 设备回调接口
 *****************************************************************************/

struct hid_device_ops
{
    /**
     * handle_data - 处理 HID OUT 数据
     * @report_id: 报告 ID（已处理）
     * @data: 数据（已去掉 Report ID）
     * @len: 数据长度
     * @user_data: 用户数据指针
     * Return: 0 成功，负数失败
     */
    int (*handle_data)(uint8_t report_id, const void* data, size_t len, void* user_data);

    /**
     * get_report - 获取 HID 报告
     * @report_type: 报告类型 (INPUT/OUTPUT/FEATURE)
     * @report_id: 报告 ID
     * @data: 输出数据缓冲区
     * @len: 输出数据长度
     * @user_data: 用户数据指针
     * Return: 0 成功，负数失败
     */
    int (*get_report)(uint8_t report_type, uint8_t report_id, void* data, size_t* len,
                      void* user_data);

    /**
     * get_idle - 获取空闲速率
     */
    int (*get_idle)(uint8_t report_id, uint8_t* duration, void* user_data);

    /**
     * set_idle - 设置空闲速率
     */
    int (*set_idle)(uint8_t report_id, uint8_t duration, void* user_data);

    /**
     * get_protocol - 获取当前协议
     */
    int (*get_protocol)(uint8_t* protocol, void* user_data);

    /**
     * set_protocol - 设置协议
     */
    int (*set_protocol)(uint8_t protocol, void* user_data);
};

/*****************************************************************************
 * HID 设备上下文
 *****************************************************************************/

struct hid_device_ctx
{
    const struct hid_device_ops* ops;
    void* user_data;
    uint8_t report_size;
    uint8_t report_id_mode;
    uint8_t protocol;
    uint8_t idle_duration;
};

/*****************************************************************************
 * HID 通用函数接口
 *****************************************************************************/

/**
 * hid_init_ctx - 初始化 HID 设备上下文
 * @ctx: HID 设备上下文
 * @ops: 设备操作回调
 * @report_size: 报告大小
 * @user_data: 用户数据指针
 */
void hid_init_ctx(struct hid_device_ctx* ctx, const struct hid_device_ops* ops, uint8_t report_size,
                  void* user_data);

/**
 * hid_class_request_handler - HID 类请求处理器
 */
int hid_class_request_handler(struct hid_device_ctx* ctx, const void* setup, void** data_out,
                              size_t* data_len);

/**
 * hid_handle_out_report - 处理 HID OUT 报告
 * @ctx: HID 设备上下文
 * @data: 原始数据（可能包含 Report ID）
 * @len: 数据长度
 *
 * 自动处理 Report ID，然后调用 handle_data 回调
 */
int hid_handle_out_report(struct hid_device_ctx* ctx, const void* data, size_t len);

/**
 * hid_normalize_report_id - 规范化 Report ID
 * @ctx: HID 设备上下文
 * @input: 输入数据
 * @input_len: 输入数据长度
 * @output: 输出数据缓冲区
 * @output_len: 输出数据长度指针
 * @report_id: 输出 Report ID (0 = 无 Report ID)
 * Return: 0 成功，负数失败
 *
 * 根据 HID 规范处理 Report ID：
 * - Report ID 取值范围：1-255 (0x01-0xFF)
 * - 0x00 是保留值，表示"没有 Report ID"
 * - CMSIS-DAP 设备无 Report ID，原样传递 64 字节数据
 *
 * 各模式说明：
 * - HID_REPORT_ID_NONE: 原样传递，不处理
 * - HID_REPORT_ID_AUTO: 针对 CMSIS-DAP 优化，原样传递 64 字节数据
 * - HID_REPORT_ID_STRIP: 去掉第一个字节作为 Report ID
 * - HID_REPORT_ID_PREPEND: 添加 Report ID 0x00（向后兼容）
 */
int hid_normalize_report_id(struct hid_device_ctx* ctx, const void* input, size_t input_len,
                            void* output, size_t* output_len, uint8_t* report_id);

#endif /* VIRTUAL_HID_H */
