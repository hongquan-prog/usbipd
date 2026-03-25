/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-9-8      hongquan.li   add license declaration
 */

#ifndef USBIP_DEVICE_DRIVER_H
#define USBIP_DEVICE_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include "hal/usbip_transport.h"
#include "usbip_protocol.h"

/*****************************************************************************
 * 设备驱动抽象接口
 * 说明：实现此接口即可创建自定义USB设备
 *****************************************************************************/

struct usbip_device_driver
{
    const char* name; /* 驱动名称 */

    /*--- 设备枚举接口 ---*/

    /**
     * get_device_list - 获取可导出的设备列表
     * @driver: 驱动实例
     * @devices: 输出设备数组指针（由驱动分配，框架释放）
     * @count: 输出设备数量
     * Return: 0成功，负数失败
     *
     * 调用者负责 free(*devices)
     */
    int (*get_device_list)(struct usbip_device_driver* driver, struct usbip_usb_device** devices,
                           int* count);

    /**
     * get_device - 根据busid获取设备信息
     * @driver: 驱动实例
     * @busid: 设备总线ID，如 "1-1"
     * Return: 设备指针，未找到返回NULL
     */
    const struct usbip_usb_device* (*get_device)(struct usbip_device_driver* driver,
                                                 const char* busid);

    /*--- 设备导入/导出接口 ---*/

    /**
     * export_device - 导出设备给远程客户端
     * @driver: 驱动实例
     * @busid: 设备总线ID
     * @ctx: 连接上下文
     * Return: 0成功，负数失败
     *
     * 调用后驱动接管该连接的URB处理
     */
    int (*export_device)(struct usbip_device_driver* driver, const char* busid,
                         struct usbip_conn_ctx* ctx);

    /**
     * unexport_device - 取消设备导出
     * @driver: 驱动实例
     * @busid: 设备总线ID
     * Return: 0成功，负数失败
     */
    int (*unexport_device)(struct usbip_device_driver* driver, const char* busid);

    /*--- URB处理接口 ---*/

    /**
     * handle_urb - 处理一个URB请求
     * @driver: 驱动实例
     * @urb_cmd: 输入，URB命令头
     * @urb_ret: 输出，URB返回头
     * @data_out: 输出，数据缓冲区指针（驱动分配，框架释放）
     * @data_len: 输出，数据长度
     * @urb_data: 输入，URB附加数据（OUT传输时有效）
     * @urb_data_len: 输入，URB附加数据长度
     * Return: 0成功继续，>0需要发送响应，<0错误断开连接
     *
     * IN传输：驱动填充 data_out 和 data_len
     * OUT传输：框架已将数据追加在 urb_cmd 后
     */
    int (*handle_urb)(struct usbip_device_driver* driver, const struct usbip_header* urb_cmd,
                      struct usbip_header* urb_ret, void** data_out, size_t* data_len,
                      const void* urb_data, size_t urb_data_len);

    /*--- 生命周期接口 ---*/

    /**
     * init - 初始化驱动
     * @driver: 驱动实例
     * Return: 0成功，负数失败
     */
    int (*init)(struct usbip_device_driver* driver);

    /**
     * cleanup - 清理驱动资源
     * @driver: 驱动实例
     */
    void (*cleanup)(struct usbip_device_driver* driver);
};

/*****************************************************************************
 * 驱动注册接口
 *****************************************************************************/

/**
 * usbip_register_driver - 注册设备驱动
 * @driver: 驱动实例指针（需静态分配或持久化）
 * Return: 0成功，负数失败
 */
int usbip_register_driver(struct usbip_device_driver* driver);

/**
 * usbip_unregister_driver - 注销设备驱动
 * @driver: 驱动实例指针
 */
void usbip_unregister_driver(struct usbip_device_driver* driver);

/*****************************************************************************
 * 驱动迭代接口
 *****************************************************************************/

/**
 * usbip_get_first_driver - 获取第一个注册的驱动
 * Return: 驱动指针，无驱动返回NULL
 */
struct usbip_device_driver* usbip_get_first_driver(void);

/**
 * usbip_get_next_driver - 获取下一个驱动
 * @current: 当前驱动
 * Return: 下一个驱动指针，无更多驱动返回NULL
 */
struct usbip_device_driver* usbip_get_next_driver(struct usbip_device_driver* current);

/*****************************************************************************
 * 设备状态管理
 *****************************************************************************/

/**
 * usbip_set_device_busy - 标记设备忙
 * @busid: 设备ID
 */
void usbip_set_device_busy(const char* busid);

/**
 * usbip_set_device_available - 标记设备可用
 * @busid: 设备ID
 */
void usbip_set_device_available(const char* busid);

/**
 * usbip_is_device_busy - 检查设备是否忙碌
 * @busid: 设备ID
 * Return: 1忙碌，0可用
 */
int usbip_is_device_busy(const char* busid);

#endif /* USBIP_DEVICE_DRIVER_H */