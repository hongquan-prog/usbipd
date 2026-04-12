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
 * Device Registry with Hash Table (O(1) lookup)
 *****************************************************************************/

#define MAX_BUSY_DEVICES 32
#define DEV_HASH_TABLE_SIZE 16  /* Power of 2 for efficient masking */

/**
 * struct device_registry_entry - Device registration entry
 *
 * Tracks device state and binding to connection for multi-client support.
 * Uses chaining for hash collision resolution.
 */
struct device_registry_entry
{
    char busid[SYSFS_BUS_ID_SIZE];
    enum device_state state;
    struct usbip_connection* owner;
    struct device_registry_entry* next;  /* Hash chain */
};

static struct device_registry_entry device_registry[MAX_BUSY_DEVICES];
static struct device_registry_entry* hash_table[DEV_HASH_TABLE_SIZE];
static int device_count = 0;

/*****************************************************************************
 * Hash Table Implementation
 *****************************************************************************/

/**
 * dev_hash - Compute hash value for busid (djb2 algorithm)
 * @busid: Device bus ID string
 * Return: Hash value (0 to DEV_HASH_TABLE_SIZE-1)
 */
static unsigned int dev_hash(const char* busid)
{
    unsigned int hash = 5381;
    int c;

    while ((c = *busid++) != '\0')
    {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash & (DEV_HASH_TABLE_SIZE - 1); /* Fast modulo for power of 2 */
}

/**
 * dev_hash_find - Find device entry in hash table
 * @busid: Device bus ID
 * Return: Entry pointer if found, NULL otherwise
 */
static struct device_registry_entry* dev_hash_find(const char* busid)
{
    unsigned int idx = dev_hash(busid);
    struct device_registry_entry* entry = hash_table[idx];

    while (entry != NULL)
    {
        if (strcmp(entry->busid, busid) == 0)
        {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

/**
 * dev_hash_insert - Insert device entry into hash table
 * @entry: Device entry to insert
 * Return: 0 on success, -1 if already exists
 */
static int dev_hash_insert(struct device_registry_entry* entry)
{
    unsigned int idx = dev_hash(entry->busid);
    struct device_registry_entry* curr = hash_table[idx];

    /* Check for duplicate */
    while (curr != NULL)
    {
        if (strcmp(curr->busid, entry->busid) == 0)
        {
            return -1; /* Already exists */
        }
        curr = curr->next;
    }

    /* Insert at head of chain */
    entry->next = hash_table[idx];
    hash_table[idx] = entry;

    return 0;
}

/**
 * dev_hash_remove - Remove device entry from hash table
 * @busid: Device bus ID to remove
 */
static void dev_hash_remove(const char* busid)
{
    unsigned int idx = dev_hash(busid);
    struct device_registry_entry** curr = &hash_table[idx];

    while (*curr != NULL)
    {
        if (strcmp((*curr)->busid, busid) == 0)
        {
            *curr = (*curr)->next;
            return;
        }
        curr = &(*curr)->next;
    }
}

/**
 * dev_alloc_entry - Allocate a free device registry entry
 * Return: Pointer to free entry, NULL if full
 */
static struct device_registry_entry* dev_alloc_entry(void)
{
    int i;

    if (device_count >= MAX_BUSY_DEVICES)
    {
        return NULL;
    }

    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        if (device_registry[i].busid[0] == '\0')
        {
            device_count++;
            return &device_registry[i];
        }
    }

    return NULL;
}

/**
 * dev_free_entry - Free a device registry entry
 * @entry: Entry to free
 */
static void dev_free_entry(struct device_registry_entry* entry)
{
    if (entry != NULL && entry->busid[0] != '\0')
    {
        entry->busid[0] = '\0';
        entry->state = DEV_STATE_AVAILABLE;
        entry->owner = NULL;
        entry->next = NULL;
        device_count--;
    }
}

/*****************************************************************************
 * Static Function Declarations
 *****************************************************************************/
static void dev_hash_init(void);

/*****************************************************************************
 * Driver Registration Interface
 *****************************************************************************/

/**
 * dev_hash_init - Initialize hash table
 */
static void dev_hash_init(void)
{
    int i;

    for (i = 0; i < DEV_HASH_TABLE_SIZE; i++)
    {
        hash_table[i] = NULL;
    }

    for (i = 0; i < MAX_BUSY_DEVICES; i++)
    {
        device_registry[i].busid[0] = '\0';
        device_registry[i].state = DEV_STATE_AVAILABLE;
        device_registry[i].owner = NULL;
        device_registry[i].next = NULL;
    }

    device_count = 0;
}

/**
 * usbip_devmgr_init - Initialize device manager
 * Return: 0 on success, -1 on failure
 */
int usbip_devmgr_init(void)
{
    dev_hash_init();

    return 0;
}

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
    struct device_registry_entry* entry;

    /* Check if already exists */
    entry = dev_hash_find(busid);
    if (entry != NULL)
    {
        if (entry->state == DEV_STATE_EXPORTED)
        {
            LOG_WRN("Device %s already exported", busid);
            return -1;
        }
        /* Reuse existing entry */
        entry->state = DEV_STATE_EXPORTED;
        entry->owner = conn;
        LOG_DBG("Device %s bound to connection %p", busid, (void*)conn);
        return 0;
    }

    /* Create new entry */
    entry = dev_alloc_entry();
    if (entry == NULL)
    {
        LOG_ERR("Device registry full");
        return -1;
    }

    strncpy(entry->busid, busid, SYSFS_BUS_ID_SIZE - 1);
    entry->busid[SYSFS_BUS_ID_SIZE - 1] = '\0';
    entry->state = DEV_STATE_EXPORTED;
    entry->owner = conn;

    if (dev_hash_insert(entry) < 0)
    {
        dev_free_entry(entry);
        return -1;
    }

    LOG_DBG("Device %s bound to connection %p", busid, (void*)conn);
    return 0;
}

/**
 * usbip_unbind_device - Unbind device from its connection
 * @busid: Device bus ID
 *
 * Marks device as available and clears connection binding.
 */
void usbip_unbind_device(const char* busid)
{
    struct device_registry_entry* entry = dev_hash_find(busid);

    if (entry != NULL)
    {
        dev_hash_remove(busid);
        entry->state = DEV_STATE_AVAILABLE;
        entry->owner = NULL;
        LOG_DBG("Device %s unbound", busid);
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
    struct device_registry_entry* entry = dev_hash_find(busid);

    return entry != NULL ? entry->owner : NULL;
}

/**
 * usbip_is_device_available - Check if device is available for export
 * @busid: Device bus ID
 *
 * Return: 1 if available, 0 if exported
 */
int usbip_is_device_available(const char* busid)
{
    struct device_registry_entry* entry = dev_hash_find(busid);

    if (entry != NULL)
    {
        return entry->state == DEV_STATE_AVAILABLE;
    }
    /* Device not in registry - consider available */
    return 1;
}

/*****************************************************************************
 * Legacy Device Busy Status Management (Backward Compatibility)
 *****************************************************************************/

void usbip_set_device_busy(const char* busid)
{
    struct device_registry_entry* entry = dev_hash_find(busid);

    if (entry != NULL)
    {
        entry->state = DEV_STATE_EXPORTED;
        return;
    }

    /* Create new entry */
    entry = dev_alloc_entry();
    if (entry != NULL)
    {
        strncpy(entry->busid, busid, SYSFS_BUS_ID_SIZE - 1);
        entry->busid[SYSFS_BUS_ID_SIZE - 1] = '\0';
        entry->state = DEV_STATE_EXPORTED;
        entry->owner = NULL;
        dev_hash_insert(entry);
    }
}

void usbip_set_device_available(const char* busid)
{
    struct device_registry_entry* entry = dev_hash_find(busid);

    if (entry != NULL)
    {
        entry->state = DEV_STATE_AVAILABLE;
        entry->owner = NULL;
    }
}

int usbip_is_device_busy(const char* busid)
{
    struct device_registry_entry* entry = dev_hash_find(busid);

    return entry != NULL && entry->state == DEV_STATE_EXPORTED;
}