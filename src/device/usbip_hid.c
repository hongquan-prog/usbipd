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
 * Virtual HID Base - HID 通用框架实现
 *
 * 提供 HID 设备通用的请求处理
 */

#include "usbip_hid.h"
#include <stdlib.h>
#include <string.h>
#include "hal/usbip_log.h"
#include "usbip_common.h"
#include "usbip_hid.h"

LOG_MODULE_REGISTER(hid, CONFIG_HID_LOG_LEVEL);

/**************************************************************************
 * HID 上下文初始化
 **************************************************************************/

void hid_init_ctx(struct hid_device_ctx* ctx, const struct hid_device_ops* ops, uint8_t report_size,
                  void* user_data)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->ops = ops;
    ctx->report_size = report_size;
    ctx->user_data = user_data;
    ctx->report_id_mode = HID_REPORT_ID_AUTO;
    ctx->protocol = HID_PROTOCOL_REPORT;
}

/**************************************************************************
 * Report ID 规范化处理
 *
 * HID 规范关于 Report ID 的说明：
 *
 * 1. Report ID 是可选的
 *    - 如果报告描述符中没有 Report ID 项，则不使用 Report ID
 *    - 如果使用 Report ID，取值范围必须是 1-255 (0x01-0xFF)
 *    - 0x00 是保留值，表示"没有 Report ID"
 *
 * 2. 数据包格式：
 *    - 无 Report ID: [数据 0][数据 1]...[数据 n-1] (n = 报告大小)
 *    - 有 Report ID: [Report ID][数据 0]...[数据 n-2] (n = 报告大小)
 *
 * 3. Linux hidraw 驱动的行为：
 *    - 只有当报告描述符中确实定义了 Report ID 时才会处理 Report ID
 *    - 对于无 Report ID 的设备，hidraw 原样传输数据，不添加/删除任何字节
 *    - hidraw 不会自动添加或删除 0x00
 *
 * 本项目中的设备情况：
 *    - CMSIS-DAP: 报告描述符无 Report ID，报告大小 64 字节
 *    - 处理方式：原样传递，不添加/剥离任何字节
 **************************************************************************/
int hid_normalize_report_id(struct hid_device_ctx* ctx, const void* input, size_t input_len,
                            void* output, size_t* output_len, uint8_t* report_id)
{
    const uint8_t* src;
    uint8_t* dst;
    size_t report_size;

    if (!input || !output || !output_len)
    {
        return -1;
    }

    src = (const uint8_t*)input;
    dst = (uint8_t*)output;
    report_size = ctx->report_size ? ctx->report_size : HID_DEFAULT_REPORT_SIZE;

    /*
     * report_id = 0 表示没有 Report ID
     * 根据 HID 规范，0x00 是保留值，不应该用作实际的 Report ID
     */
    *report_id = 0;

    switch (ctx->report_id_mode)
    {
        case HID_REPORT_ID_NONE:
            /*
             * 不处理，原样复制
             * 适用于：确定不需要 Report ID 处理的情况
             */
            memcpy(dst, src, input_len);
            *output_len = input_len;
            break;

        case HID_REPORT_ID_STRIP:
            /*
             * 总是去掉第一个字节作为 Report ID
             * 适用于：报告描述符确实定义了 Report ID 的设备
             *
             * 注意：
             * - Report ID 应该是 1-255
             * - 0x00 在此模式下也被当作 Report ID 处理（虽然不符合规范）
             */
            if (input_len > 1)
            {
                *report_id = src[0];
                memcpy(dst, src + 1, input_len - 1);
                *output_len = input_len - 1;
            }
            else if (input_len == 1)
            {
                *report_id = src[0];
                *output_len = 0;
            }
            else
            {
                *output_len = 0;
            }
            break;

        case HID_REPORT_ID_PREPEND:
            /*
             * 总是添加 Report ID
             *
             * 注意：
             * - 此模式保留用于向后兼容
             * - 根据 HID 规范，不应该使用 0x00 作为 Report ID
             * - 如果需要 Report ID，应该使用 1-255 范围内的值
             */
            if (report_size < 2)
            {
                /* 报告大小太小，无法添加 Report ID */
                memcpy(dst, src, input_len < report_size ? input_len : report_size);
                *output_len = input_len < report_size ? input_len : report_size;
            }
            else
            {
                /*
                 * 使用 Report ID 0x00（虽然不符合规范，但保留用于兼容性）
                 * 新代码应该考虑使用 1-255 范围内的 Report ID
                 */
                dst[0] = 0x00;
                size_t copy_len = (input_len < report_size - 1) ? input_len : (report_size - 1);
                memcpy(dst + 1, src, copy_len);
                *output_len = copy_len + 1;
                *report_id = 0x00;
            }
            break;

        case HID_REPORT_ID_AUTO:
        default:
            /*
             * 自动检测模式 - 针对本项目的 CMSIS-DAP 设备优化
             *
             * CMSIS-DAP 设备特点：
             * - 报告描述符中没有 Report ID 定义
             * - 报告大小 = 64 字节
             * - 数据格式：[CMD][DATA...]，CMD 范围 0x00-0x7F
             *
             * 处理策略：
             * 1. 如果 input_len == report_size (64)：
             *    - 这是正常情况，原样传递
             *    - 第一个字节是 CMSIS-DAP 命令，不是 Report ID
             *
             * 2. 如果 input_len == report_size - 1 (63)：
             *    - 可能是某些驱动剥离了数据
             *    - 原样传递，不补 0x00（因为 0x00 不是有效的 Report ID）
             *
             * 3. 其他情况：原样传递
             *
             * 注意：不再使用启发式判断（src[0] > 0x1F），因为：
             * - 这不符合 HID 规范
             * - CMSIS-DAP 命令可以是 0x00-0x7F 范围内的任意值
             */
            if (input_len == report_size)
            {
                /*
                 * 正常情况：完整的 64 字节报告
                 * CMSIS-DAP 无 Report ID，原样传递
                 */
                memcpy(dst, src, input_len);
                *output_len = input_len;
                *report_id = 0;
            }
            else if (input_len == report_size - 1)
            {
                /* 
                 * 某些 Windows HID 驱动可能发送 63 字节
                 * 原样传递，末尾补 0 确保 DAP 不读到脏数据
                 */
                memcpy(dst, src, input_len);
                dst[input_len] = 0x00;
                *output_len = report_size;
                *report_id = 0;
                LOG_DBG("IN: Padded %zu bytes to %zu", input_len, report_size);
            }
            else
            {
                /* 其他情况，原样复制 */
                memcpy(dst, src, input_len);
                *output_len = input_len;
                *report_id = 0;
            }
            break;
    }

    return 0;
}

/**************************************************************************
 * HID OUT 报告处理
 **************************************************************************/

int hid_handle_out_report(struct hid_device_ctx* ctx, const void* data, size_t len)
{
    uint8_t buf[HID_DEFAULT_REPORT_SIZE];
    size_t out_len;
    uint8_t report_id;
    int ret;

    if (!ctx->ops || !ctx->ops->handle_data)
    {
        return -1;
    }

    /* 规范化 Report ID */
    ret = hid_normalize_report_id(ctx, data, len, buf, &out_len, &report_id);
    if (ret < 0)
    {
        return ret;
    }

    /* 调用设备特定的处理函数 */
    return ctx->ops->handle_data(report_id, buf, out_len, ctx->user_data);
}

/**************************************************************************
 * HID 类请求处理
 **************************************************************************/

int hid_class_request_handler(struct hid_device_ctx* ctx, const void* setup, void** data_out,
                              size_t* data_len)
{
    const struct usb_setup_packet* req;
    const struct hid_device_ops* ops;
    uint8_t report_type;
    uint8_t report_id;
    uint8_t duration;
    uint8_t protocol;
    void* buf;
    size_t report_size;
    int ret;

    req = (const struct usb_setup_packet*)setup;
    ops = ctx->ops;
    report_size = ctx->report_size ? ctx->report_size : HID_DEFAULT_REPORT_SIZE;

    switch (req->bRequest)
    {
        case HID_REQUEST_GET_REPORT:
            report_type = (req->wValue >> 8) & 0xFF;
            report_id = req->wValue & 0xFF;

            buf = malloc(report_size);
            if (!buf)
            {
                return -1;
            }

            *data_len = report_size;
            if (ops && ops->get_report)
            {
                ret = ops->get_report(report_type, report_id, buf, data_len, ctx->user_data);
                if (ret < 0)
                {
                    free(buf);
                    return -1;
                }
            }
            else
            {
                memset(buf, 0, report_size);
            }

            *data_out = buf;
            return 0;

        case HID_REQUEST_GET_IDLE:
            report_id = req->wValue & 0xFF;

            buf = malloc(1);
            if (!buf)
            {
                return -1;
            }

            if (ops && ops->get_idle)
            {
                ret = ops->get_idle(report_id, &duration, ctx->user_data);
                *(uint8_t*)buf = (ret == 0) ? duration : ctx->idle_duration;
            }
            else
            {
                *(uint8_t*)buf = ctx->idle_duration;
            }

            *data_out = buf;
            *data_len = 1;
            return 0;

        case HID_REQUEST_GET_PROTOCOL:
            buf = malloc(1);
            if (!buf)
            {
                return -1;
            }

            if (ops && ops->get_protocol)
            {
                ret = ops->get_protocol(&protocol, ctx->user_data);
                *(uint8_t*)buf = (ret == 0) ? protocol : ctx->protocol;
            }
            else
            {
                *(uint8_t*)buf = ctx->protocol;
            }

            *data_out = buf;
            *data_len = 1;
            return 0;

        case HID_REQUEST_SET_REPORT:
            /* 数据在 OUT 阶段传输 */
            return 0;

        case HID_REQUEST_SET_IDLE:
            duration = (req->wValue >> 8) & 0xFF;
            report_id = req->wValue & 0xFF;

            ctx->idle_duration = duration;

            if (ops && ops->set_idle)
            {
                ops->set_idle(report_id, duration, ctx->user_data);
            }

            *data_len = 0;
            return 0;

        case HID_REQUEST_SET_PROTOCOL:
            protocol = req->wValue & 0xFF;

            if (protocol <= HID_PROTOCOL_REPORT)
            {
                ctx->protocol = protocol;
                if (ops && ops->set_protocol)
                {
                    ops->set_protocol(protocol, ctx->user_data);
                }
            }

            *data_len = 0;
            return 0;
    }

    return -1;
}
