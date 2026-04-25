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
#include "usbip_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Forward Declarations
 *****************************************************************************/

struct usbip_connection;

/*****************************************************************************
 * Device Driver Abstract Interface
 * Note: Implement this interface to create custom USB devices
 *****************************************************************************/

struct usbip_device_driver
{
    const char* name; /* Driver name */

    /*--- Device Enumeration Interface ---*/

    /**
     * get_device_count - Get number of exportable devices
     * @driver: Driver instance
     * Return: Number of devices, 0 if none
     */
    int (*get_device_count)(struct usbip_device_driver* driver);

    /**
     * get_device_by_index - Get device by index
     * @driver: Driver instance
     * @index: Device index (0 to get_device_count()-1)
     * @device: Output device info
     * Return: 0 on success, -1 if index invalid
     */
    int (*get_device_by_index)(struct usbip_device_driver* driver, int index,
                               struct usbip_usb_device* device);

    /**
     * get_interface - Get interface info for device enumeration
     * @driver: Driver instance
     * @index: Device index
     * @iface: Output interface info
     * Return: 0 on success, -1 if index invalid
     */
    int (*get_interface)(struct usbip_device_driver* driver, int index,
                         struct usbip_usb_interface* iface);

    /**
     * get_device - Get device info by busid
     * @driver: Driver instance
     * @busid: Device bus ID, e.g., "1-1"
     * Return: Device pointer, NULL if not found
     */
    const struct usbip_usb_device* (*get_device)(struct usbip_device_driver* driver,
                                                 const char* busid);

    /*--- Device Import/Export Interface ---*/

    /**
     * export_device - Export device to remote client
     * @driver: Driver instance
     * @busid: Device bus ID
     * @conn: Connection context (for multi-client support)
     * Return: 0 on success, negative on failure
     *
     * After calling, driver takes over URB handling for this connection
     */
    int (*export_device)(struct usbip_device_driver* driver, const char* busid,
                         struct usbip_connection* conn);

    /**
     * unexport_device - Unexport device
     * @driver: Driver instance
     * @busid: Device bus ID
     * Return: 0 on success, negative on failure
     */
    int (*unexport_device)(struct usbip_device_driver* driver, const char* busid);

    /*--- URB Handling Interface ---*/

    /**
     * handle_urb - Handle a URB request
     * @driver: Driver instance
     * @urb_cmd: Input, URB command header
     * @urb_ret: Output, URB return header
     * @data_out: Output, data buffer pointer (driver allocates, framework frees)
     * @data_len: Output, data length
     * @urb_data: Input, URB additional data (valid for OUT transfers)
     * @urb_data_len: Input, URB additional data length
     * Return: 0 continue, >0 need to send response, <0 error disconnect
     *
     * IN transfer: driver fills data_out and data_len
     * OUT transfer: framework appends data after urb_cmd
     */
    int (*handle_urb)(struct usbip_device_driver* driver, const struct usbip_header* urb_cmd,
                      struct usbip_header* urb_ret, void** data_out, size_t* data_len,
                      const void* urb_data, size_t urb_data_len);

    /*--- Lifecycle Interface ---*/

    /**
     * init - Initialize driver
     * @driver: Driver instance
     * Return: 0 on success, negative on failure
     */
    int (*init)(struct usbip_device_driver* driver);

    /**
     * cleanup - Cleanup driver resources
     * @driver: Driver instance
     */
    void (*cleanup)(struct usbip_device_driver* driver);
};

/*****************************************************************************
 * Device Manager Initialization
 *****************************************************************************/

/**
 * usbip_devmgr_init - Initialize device manager
 * Return: 0 on success, -1 on failure
 */
int usbip_devmgr_init(void);

/*****************************************************************************
 * Driver Registration Interface
 *****************************************************************************/

/**
 * usbip_register_driver - Register device driver
 * @driver: Driver instance pointer (must be statically allocated or persistent)
 * Return: 0 on success, negative on failure
 */
int usbip_register_driver(struct usbip_device_driver* driver);

/**
 * usbip_unregister_driver - Unregister device driver
 * @driver: Driver instance pointer
 */
void usbip_unregister_driver(struct usbip_device_driver* driver);

/*****************************************************************************
 * Driver Iteration Interface
 *****************************************************************************/

/**
 * usbip_get_first_driver - Get first registered driver
 * Return: Driver pointer, NULL if no drivers
 */
struct usbip_device_driver* usbip_get_first_driver(void);

/**
 * usbip_get_next_driver - Get next driver
 * @current: Current driver
 * Return: Next driver pointer, NULL if no more drivers
 */
struct usbip_device_driver* usbip_get_next_driver(struct usbip_device_driver* current);

/*****************************************************************************
 * Device Query Interface (Wrapper Functions)
 * These wrap driver function pointers for better readability and error handling
 *****************************************************************************/

/**
 * usbip_driver_get_device_count - Get number of devices from driver
 * @driver: Driver instance
 * Return: Device count, 0 if error or no devices
 */
int usbip_driver_get_device_count(struct usbip_device_driver* driver);

/**
 * usbip_driver_get_device_by_index - Get device by index
 * @driver: Driver instance
 * @index: Device index (0 to get_device_count()-1)
 * @device: Output device info
 * Return: 0 on success, -1 if index invalid
 */
int usbip_driver_get_device_by_index(struct usbip_device_driver* driver, int index,
                                    struct usbip_usb_device* device);

/**
 * usbip_driver_get_interface - Get interface info for device
 * @driver: Driver instance
 * @index: Device index
 * @iface: Output interface info
 * Return: 0 on success, -1 if index invalid
 */
int usbip_driver_get_interface(struct usbip_device_driver* driver, int index,
                               struct usbip_usb_interface* iface);

/**
 * usbip_driver_get_device - Get device by busid
 * @driver: Driver instance
 * @busid: Device bus ID
 * Return: Device pointer, NULL if not found
 */
const struct usbip_usb_device* usbip_driver_get_device(struct usbip_device_driver* driver,
                                                       const char* busid);

/**
 * usbip_driver_export_device - Export device to remote client
 * @driver: Driver instance
 * @busid: Device bus ID
 * @conn: Connection context
 * Return: 0 on success, negative on failure
 */
int usbip_driver_export_device(struct usbip_device_driver* driver, const char* busid,
                               struct usbip_connection* conn);

/**
 * usbip_driver_unexport_device - Unexport device
 * @driver: Driver instance
 * @busid: Device bus ID
 * Return: 0 on success, negative on failure
 */
int usbip_driver_unexport_device(struct usbip_device_driver* driver, const char* busid);

/**
 * usbip_driver_handle_urb - Handle a URB request
 * @driver: Driver instance
 * @urb_cmd: Input, URB command header
 * @urb_ret: Output, URB return header
 * @data_out: Output, data buffer pointer (driver allocates, framework frees)
 * @data_len: Output, data length
 * @urb_data: Input, URB additional data (valid for OUT transfers)
 * @urb_data_len: Input, URB additional data length
 * Return: 0 continue, >0 need to send response, <0 error disconnect
 */
int usbip_driver_handle_urb(struct usbip_device_driver* driver,
                           const struct usbip_header* urb_cmd,
                           struct usbip_header* urb_ret,
                           void** data_out, size_t* data_len,
                           const void* urb_data, size_t urb_data_len);

/*****************************************************************************
 * Device Status Management
 *****************************************************************************/

/**
 * usbip_set_device_busy - Mark device as busy
 * @busid: Device ID
 */
void usbip_set_device_busy(const char* busid);

/**
 * usbip_set_device_available - Mark device as available
 * @busid: Device ID
 */
void usbip_set_device_available(const char* busid);

/**
 * usbip_is_device_busy - Check if device is busy
 * @busid: Device ID
 * Return: 1 busy, 0 available
 */
int usbip_is_device_busy(const char* busid);

/*****************************************************************************
 * Device Connection Binding (Multi-Client Support)
 *****************************************************************************/

/* Forward declaration */
struct usbip_connection;

/**
 * usbip_bind_device - Bind device to a connection
 * @busid: Device bus ID
 * @conn: Connection that owns the device
 * Return: 0 on success, -1 if already exported or error
 */
int usbip_bind_device(const char* busid, struct usbip_connection* conn);

/**
 * usbip_unbind_device - Unbind device from its connection
 * @busid: Device bus ID
 */
void usbip_unbind_device(const char* busid);

/**
 * usbip_get_device_owner - Get connection that owns the device
 * @busid: Device bus ID
 * Return: Connection pointer, NULL if not exported
 */
struct usbip_connection* usbip_get_device_owner(const char* busid);

/**
 * usbip_is_device_available - Check if device is available for export
 * @busid: Device bus ID
 * Return: 1 if available, 0 if exported
 */
int usbip_is_device_available(const char* busid);

#ifdef __cplusplus
}
#endif

#endif /* USBIP_DEVICE_DRIVER_H */