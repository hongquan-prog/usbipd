/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-9-8      hongquan.li   add license declaration
 */

/*****************************************************************************
 * USB Standard Control Transfer Framework
 *
 * 提供标准 USB 控制传输的通用处理框架
 * 可被各设备驱动复用，减少代码重复
 *****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/usbip_log.h"
#include "usbip_common.h"
#include "usbip_control.h"

LOG_MODULE_REGISTER(control, CONFIG_CONTROL_LOG_LEVEL);

/*****************************************************************************
 * 默认字符串描述符
 *****************************************************************************/

static const uint8_t default_string0[] = {
    0x04, USB_DT_STRING, 0x09, 0x04 /* English (US) */
};

/*****************************************************************************
 * 控制传输处理
 *****************************************************************************/

int usb_control_handle_setup(const struct usb_setup_packet* setup, struct usb_control_context* ctx,
                             void** data_out, size_t* data_len)
{
    int ret = USB_CONTROL_OK;

    if (!setup || !ctx || !data_out || !data_len)
    {
        return USB_CONTROL_ERROR;
    }

    *data_out = NULL;
    *data_len = 0;

    uint8_t type = USB_SETUP_TYPE(setup);

    /* 标准设备请求 */
    if (type == USB_TYPE_STANDARD)
    {
        switch (setup->bRequest)
        {
            case USB_REQ_GET_DESCRIPTOR:
                ret = usb_control_get_descriptor(setup, ctx, data_out, data_len);
                break;

            case USB_REQ_SET_CONFIGURATION:
                ret = usb_control_set_config(setup, ctx, data_out, data_len);
                break;

            case USB_REQ_GET_CONFIGURATION:
                ret = usb_control_get_config(setup, ctx, data_out, data_len);
                break;

            case USB_REQ_SET_ADDRESS:
                /* 虚拟设备通常忽略地址设置 */
                ctx->address = setup->wValue & 0xFF;
                ret = USB_CONTROL_OK_NO_DATA;
                break;

            case USB_REQ_GET_STATUS:
                ret = usb_control_get_status(setup, ctx, data_out, data_len);
                break;

            case USB_REQ_SET_FEATURE:
            case USB_REQ_CLEAR_FEATURE:
                /* 虚拟设备通常不支持远程唤醒或端点暂停 */
                ret = USB_CONTROL_OK_NO_DATA;
                break;

            case USB_REQ_SET_INTERFACE:
                /* 默认支持，无实际操作 */
                ctx->alt_setting[setup->wIndex & 0xFF] = setup->wValue & 0xFF;
                ret = USB_CONTROL_OK_NO_DATA;
                break;

            case USB_REQ_GET_INTERFACE: {
                uint8_t intf = setup->wIndex & 0xFF;
                uint8_t* p = malloc(1);
                if (p)
                {
                    *p = (intf < USB_MAX_INTERFACES) ? ctx->alt_setting[intf] : 0;
                    *data_out = p;
                    *data_len = 1;
                    ret = USB_CONTROL_OK;
                }
                else
                {
                    ret = USB_CONTROL_ERROR;
                }
            }
            break;

            default:
                ret = USB_CONTROL_STALL;
                break;
        }
    }
    /* 类特定请求 */
    else if (type == USB_TYPE_CLASS)
    {
        ret = usb_control_class_request(setup, ctx, data_out, data_len);
    }
    /* 厂商请求 */
    else if (type == USB_TYPE_VENDOR)
    {
        if (ctx->vendor_handler)
        {
            ret = ctx->vendor_handler(setup, ctx->user_data, data_out, data_len);
        }
        else
        {
            ret = USB_CONTROL_STALL;
        }
    }
    else
    {
        ret = USB_CONTROL_STALL;
    }

    /* 限制返回数据长度 */
    if (*data_len > setup->wLength && *data_out)
    {
        *data_len = setup->wLength;
    }

    return ret;
}

/*****************************************************************************
 * GET_DESCRIPTOR 处理
 *****************************************************************************/

int usb_control_get_descriptor(const struct usb_setup_packet* setup,
                               struct usb_control_context* ctx, void** data_out, size_t* data_len)
{
    uint8_t desc_type = (setup->wValue >> 8) & 0xFF;
    uint8_t desc_index = setup->wValue & 0xFF;

    LOG_INF("GET_DESCRIPTOR: type=0x%02x index=%d wLength=%d", desc_type, desc_index,
            setup->wLength);

    switch (desc_type)
    {
        case USB_DT_DEVICE:
            if (ctx->device_desc)
            {
                *data_out = malloc(USB_DT_DEVICE_SIZE);
                if (*data_out)
                {
                    memcpy(*data_out, ctx->device_desc, USB_DT_DEVICE_SIZE);
                    *data_len = USB_DT_DEVICE_SIZE;
                    return USB_CONTROL_OK;
                }
            }
            break;

        case USB_DT_CONFIG:
            if (ctx->config_desc && ctx->config_desc_len > 0)
            {
                if (desc_index < ctx->num_configs)
                {
                    *data_out = malloc(ctx->config_desc_len);
                    if (*data_out)
                    {
                        memcpy(*data_out, ctx->config_desc, ctx->config_desc_len);
                        *data_len = ctx->config_desc_len;
                        return USB_CONTROL_OK;
                    }
                }
            }
            break;

        case USB_DT_STRING:
            return usb_control_get_string(desc_index, ctx, data_out, data_len);

        case USB_DT_DEVICE_QUALIFIER:
            /* USB 2.0 全速设备返回 STALL */
            return USB_CONTROL_STALL;

        case USB_DT_OTHER_SPEED_CONFIG:
            return USB_CONTROL_STALL;

        case USB_DT_HID:
            if (ctx->hid_desc)
            {
                *data_out = malloc(USB_DT_HID_SIZE);
                if (*data_out)
                {
                    memcpy(*data_out, ctx->hid_desc, USB_DT_HID_SIZE);
                    *data_len = USB_DT_HID_SIZE;
                    return USB_CONTROL_OK;
                }
            }
            break;

        case USB_DT_REPORT:
            if (ctx->report_desc && ctx->report_desc_len > 0)
            {
                *data_out = malloc(ctx->report_desc_len);
                if (*data_out)
                {
                    memcpy(*data_out, ctx->report_desc, ctx->report_desc_len);
                    *data_len = ctx->report_desc_len;
                    return USB_CONTROL_OK;
                }
            }
            break;

        case USB_DT_BOS:
            if (ctx->bos_desc && ctx->bos_desc_len > 0)
            {
                *data_out = malloc(ctx->bos_desc_len);
                if (*data_out)
                {
                    memcpy(*data_out, ctx->bos_desc, ctx->bos_desc_len);
                    *data_len = ctx->bos_desc_len;
                    return USB_CONTROL_OK;
                }
            }
            break;

        default:
            /* 尝试回调 */
            if (ctx->descriptor_handler)
            {
                return ctx->descriptor_handler(desc_type, desc_index, ctx->user_data, data_out,
                                               data_len);
            }
            break;
    }

    return USB_CONTROL_STALL;
}

/*****************************************************************************
 * GET_STRING_DESCRIPTOR 处理
 *****************************************************************************/

int usb_control_get_string(uint8_t index, struct usb_control_context* ctx, void** data_out,
                           size_t* data_len)
{
    const uint8_t* str_desc = NULL;
    size_t str_len = 0;

    if (index == 0)
    {
        /* 语言 ID 描述符 */
        if (ctx->lang_id_desc)
        {
            str_desc = ctx->lang_id_desc;
            str_len = ctx->lang_id_desc ? ctx->lang_id_desc[0] : 0;
        }
        else
        {
            str_desc = default_string0;
            str_len = sizeof(default_string0);
        }
    }
    else if (ctx->string_descs && index <= ctx->num_strings)
    {
        str_desc = ctx->string_descs[index - 1];
        str_len = str_desc[0]; /* 首字节是长度 */
    }
    else if (ctx->string_handler)
    {
        return ctx->string_handler(index, ctx->user_data, data_out, data_len);
    }

    if (str_desc && str_len > 0)
    {
        *data_out = malloc(str_len);
        if (*data_out)
        {
            memcpy(*data_out, str_desc, str_len);
            *data_len = str_len;
            return USB_CONTROL_OK;
        }
    }

    return USB_CONTROL_STALL;
}

/*****************************************************************************
 * SET/GET_CONFIGURATION 处理
 *****************************************************************************/

int usb_control_set_config(const struct usb_setup_packet* setup, struct usb_control_context* ctx,
                           void** data_out, size_t* data_len)
{
    (void)data_out;
    (void)data_len;

    uint8_t config_value = setup->wValue & 0xFF;

    /* 检查配置值是否有效 */
    if (config_value == 0 || config_value <= ctx->num_configs)
    {
        ctx->config_value = config_value;

        /* 回调通知 */
        if (ctx->config_handler)
        {
            ctx->config_handler(config_value, ctx->user_data);
        }

        return USB_CONTROL_OK_NO_DATA;
    }

    return USB_CONTROL_STALL;
}

int usb_control_get_config(const struct usb_setup_packet* setup, struct usb_control_context* ctx,
                           void** data_out, size_t* data_len)
{
    (void)setup;

    uint8_t* p = malloc(1);
    if (p)
    {
        *p = ctx->config_value;
        *data_out = p;
        *data_len = 1;
        return USB_CONTROL_OK;
    }

    return USB_CONTROL_ERROR;
}

/*****************************************************************************
 * GET_STATUS 处理
 *****************************************************************************/

int usb_control_get_status(const struct usb_setup_packet* setup, struct usb_control_context* ctx,
                           void** data_out, size_t* data_len)
{
    (void)ctx;

    uint8_t recipient = USB_SETUP_RECIPIENT(setup);
    uint16_t* status = malloc(2);

    if (!status)
    {
        return USB_CONTROL_ERROR;
    }

    switch (recipient)
    {
        case USB_RECIP_DEVICE:
            /* Bus-powered, 不支持远程唤醒 */
            *status = 0x0000; /* Bus-powered */
            break;

        case USB_RECIP_INTERFACE:
            *status = 0x0000;
            break;

        case USB_RECIP_ENDPOINT:
            /* 端点状态（ halted 或 not） */
            *status = 0x0000; /* Not halted */
            break;

        default:
            free(status);
            return USB_CONTROL_STALL;
    }

    *data_out = status;
    *data_len = 2;
    return USB_CONTROL_OK;
}

/*****************************************************************************
 * 类请求处理（默认实现，可被覆盖）
 *****************************************************************************/

int usb_control_class_request(const struct usb_setup_packet* setup, struct usb_control_context* ctx,
                              void** data_out, size_t* data_len)
{
    /* 如果提供了类请求处理器，调用它 */
    if (ctx->class_handler)
    {
        return ctx->class_handler(setup, ctx->user_data, data_out, data_len);
    }

    return USB_CONTROL_STALL;
}