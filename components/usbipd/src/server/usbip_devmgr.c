/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-3-24      hongquan.li   add license declaration
 * 2026-04-09     hongquan.li   Add connection binding for multi-client support
 */

/*****************************************************************************
 * USBIP Device Manager
 *
 * Device management at USBIP protocol layer: driver registration, device import/export state
 * Enhanced with connection binding for multi-client support
 *****************************************************************************/

#include "usbip_devmgr.h"
#include <stdio.h>
#include <string.h>
#include "hal/usbip_log.h"
#include "hal/usbip_osal.h"
#include "usbip_conn.h"

LOG_MODULE_REGISTER(devmgr, CONFIG_USBIP_LOG_LEVEL);

/*****************************************************************************
 * Forward Declarations
 *****************************************************************************/

struct usbip_connection;

/*****************************************************************************
 * Driver Registry
 *****************************************************************************/

#define MAX_DRIVERS 16

static struct usbip_device_driver* driver_registry[MAX_DRIVERS];
static int driver_count = 0;

/*****************************************************************************
 * Device State Enumeration
 *****************************************************************************/

/**
 * enum device_state - Device export state for multi-client support
 */
enum device_state {
    DEV_STATE_AVAILABLE = 0,    /* Device not exported */
    DEV_STATE_EXPORTED,         /* Device exported to a connection */
};

/*****************************************************************************
 * Device Registry (Enhanced with connection binding)
 *****************************************************************************/

#define MAX_BUSY_DEVICES 32

/**
 * struct device_registry_entry - Device registration entry
 *
 * Tracks device state and binding to connection for multi-client support.
 */
static struct
{
    char busid[SYSFS_BUS_ID_SIZE];
    enum device_state state;
    struct usbip_connection* owner;     /* Connection that owns this device */
} device_registry[MAX_BUSY_DEVICES];

/*****************************************************************************
 * Static Function Declarations
 *****************************************************************************/
/* No static functions in this module, all functions are public interfaces */

/*****************************************************************************
 * Driver Registration Interface
 *****************************************************************************/

int usbip_register_driver(struct usbip_device_driver* driver)
{
    if (driver_count >= MAX_DRIVERS)
    {
        LOG_ERR("Driver registry full");
        return -1;
    }

    /* Check if already registered */
    for (int i = 0; i < driver_count; i++)
    {
        if (driver_registry[i] == driver)
        {
            LOG_WRN("Driver already registered: %s", driver->name);
            return -1;
        }
    }

    driver_registry[driver_count++] = driver;

    /* Call driver initialization */
    if (driver->init && driver->init(driver) < 0)
    {
        LOG_ERR("Driver init failed: %s", driver->name);
        driver_count--;
        return -1;
    }

    LOG_DBG("Registered driver: %s", driver->name);

    return 0;
}

void usbip_unregister_driver(struct usbip_device_driver* driver)
{
    for (int i = 0; i < driver_count; i++)
    {
        if (driver_registry[i] == driver)
        {
            /* Call driver cleanup */
            if (driver->cleanup)
            {
                driver->cleanup(driver);
            }
            /* Move subsequent drivers */
            for (int j = i; j < driver_count - 1; j++)
            {
                driver_registry[j] = driver_registry[j + 1];
            }
            driver_count--;
            LOG_DBG("Unregistered driver: %s", driver->name);
            return;
        }
    }
}

/*****************************************************************************
 * Driver Iteration Interface
 *****************************************************************************/

struct usbip_device_driver* usbip_get_first_driver(void)
{
    if (driver_count == 0)
    {
        return NULL;
    }
    return driver_registry[0];
}

struct usbip_device_driver* usbip_get_next_driver(struct usbip_device_driver* current)
{
    for (int i = 0; i < driver_count - 1; i++)
    {
        if (driver_registry[i] == current)
        {
            return driver_registry[i + 1];
        }
    }
    return NULL;
}

/*****************************************************************************
 * Device Connection Binding (Multi-Client Support)
 *****************************************************************************/

/**
 * usbip_bind_device - Bind device to a connection
 * @busid: Device bus ID
 * @conn: Connection that owns the device
 *
 * Marks device as exported to a specific connection.
 *
 * Return: 0 on success, -1 if already exported or error
 */
int usbip_bind_device(const char* busid, struct usbip_connection* conn)
{
    int i;

    /* Check if already exported */
    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        if (strcmp(device_registry[i].busid, busid) == 0)
        {
            if (device_registry[i].state == DEV_STATE_EXPORTED)
            {
                LOG_WRN("Device %s already exported", busid);
                return -1;
            }
            device_registry[i].state = DEV_STATE_EXPORTED;
            device_registry[i].owner = conn;
            LOG_DBG("Device %s bound to connection %p", busid, (void*)conn);
            return 0;
        }
    }

    /* Create new entry */
    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        if (device_registry[i].busid[0] == '\0')
        {
            strncpy(device_registry[i].busid, busid, SYSFS_BUS_ID_SIZE - 1);
            device_registry[i].busid[SYSFS_BUS_ID_SIZE - 1] = '\0';
            device_registry[i].state = DEV_STATE_EXPORTED;
            device_registry[i].owner = conn;
            LOG_DBG("Device %s bound to connection %p", busid, (void*)conn);
            return 0;
        }
    }

    LOG_ERR("Device registry full");
    return -1;
}

/**
 * usbip_unbind_device - Unbind device from its connection
 * @busid: Device bus ID
 *
 * Marks device as available and clears connection binding.
 */
void usbip_unbind_device(const char* busid)
{
    int i;

    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        if (strcmp(device_registry[i].busid, busid) == 0)
        {
            device_registry[i].state = DEV_STATE_AVAILABLE;
            device_registry[i].owner = NULL;
            LOG_DBG("Device %s unbound", busid);
            return;
        }
    }
}

/**
 * usbip_get_device_owner - Get connection that owns the device
 * @busid: Device bus ID
 *
 * Return: Connection pointer, NULL if not exported
 */
struct usbip_connection* usbip_get_device_owner(const char* busid)
{
    int i;

    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        if (strcmp(device_registry[i].busid, busid) == 0)
        {
            return device_registry[i].owner;
        }
    }
    return NULL;
}

/**
 * usbip_is_device_available - Check if device is available for export
 * @busid: Device bus ID
 *
 * Return: 1 if available, 0 if exported
 */
int usbip_is_device_available(const char* busid)
{
    int i;

    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        if (strcmp(device_registry[i].busid, busid) == 0)
        {
            return device_registry[i].state == DEV_STATE_AVAILABLE;
        }
    }
    /* Device not in registry - consider available */
    return 1;
}

/*****************************************************************************
 * Legacy Device Busy Status Management (Backward Compatibility)
 *****************************************************************************/

void usbip_set_device_busy(const char* busid)
{
    int i;

    /* Find existing entry */
    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        if (strcmp(device_registry[i].busid, busid) == 0)
        {
            device_registry[i].state = DEV_STATE_EXPORTED;
            return;
        }
    }
    /* Create new entry */
    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        if (device_registry[i].busid[0] == '\0')
        {
            strncpy(device_registry[i].busid, busid, SYSFS_BUS_ID_SIZE - 1);
            device_registry[i].busid[SYSFS_BUS_ID_SIZE - 1] = '\0';
            device_registry[i].state = DEV_STATE_EXPORTED;
            device_registry[i].owner = NULL;
            return;
        }
    }
}

void usbip_set_device_available(const char* busid)
{
    int i;

    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        if (strcmp(device_registry[i].busid, busid) == 0)
        {
            device_registry[i].state = DEV_STATE_AVAILABLE;
            device_registry[i].owner = NULL;
            return;
        }
    }
}

int usbip_is_device_busy(const char* busid)
{
    int i;

    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        if (strcmp(device_registry[i].busid, busid) == 0)
        {
            return device_registry[i].state == DEV_STATE_EXPORTED;
        }
    }
    return 0;
}