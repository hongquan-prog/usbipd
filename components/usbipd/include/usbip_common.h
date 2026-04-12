/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-3-24      hongquan.li   add license declaration
 */

#ifndef USB_COMMON_H
#define USB_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * USBIP Protocol Constants
 *****************************************************************************/

#define USBIP_VERSION 0x0111

/*****************************************************************************
 * USB Standard Definitions (portable, no platform dependency)
 *****************************************************************************/
#define SYSFS_PATH_MAX 256
#define SYSFS_BUS_ID_SIZE 32

/* Status code */
#define ST_OK 0x00
#define ST_NA 0x01
#define ST_DEV_BUSY 0x02
#define ST_DEV_ERR 0x03
#define ST_NODEV 0x04
#define ST_ERROR 0x05

/* Operation code */
#define OP_REQUEST (0x80 << 8)
#define OP_REPLY (0x00 << 8)

#define OP_REQ_DEVLIST (OP_REQUEST | 0x05)
#define OP_REP_DEVLIST (OP_REPLY | 0x05)
#define OP_REQ_IMPORT (OP_REQUEST | 0x03)
#define OP_REP_IMPORT (OP_REPLY | 0x03)

/* URB command */
#define USBIP_CMD_SUBMIT 0x0001
#define USBIP_CMD_UNLINK 0x0002
#define USBIP_RET_SUBMIT 0x0003
#define USBIP_RET_UNLINK 0x0004

/* Direction */
#define USBIP_DIR_OUT 0x00
#define USBIP_DIR_IN 0x01

/*****************************************************************************
 * USB Descriptor Types
 *****************************************************************************/

#define USB_DT_DEVICE 0x01
#define USB_DT_CONFIG 0x02
#define USB_DT_STRING 0x03
#define USB_DT_INTERFACE 0x04
#define USB_DT_ENDPOINT 0x05
#define USB_DT_DEVICE_QUALIFIER 0x06
#define USB_DT_OTHER_SPEED_CONFIG 0x07
#define USB_DT_INTERFACE_POWER 0x08
#define USB_DT_BOS 0x0F
#define USB_DT_DEVICE_CAPABILITY 0x10
#define USB_DT_HID 0x21
#define USB_DT_REPORT 0x22
#define USB_DT_PHYSICAL 0x23
#define USB_DT_HUB 0x29

/*****************************************************************************
 * USB Descriptor Sizes
 *****************************************************************************/

#define USB_DT_DEVICE_SIZE 18
#define USB_DT_CONFIG_SIZE 9
#define USB_DT_INTERFACE_SIZE 9
#define USB_DT_ENDPOINT_SIZE 7
#define USB_DT_HID_SIZE 9

/*****************************************************************************
 * USB Standard Requests
 *****************************************************************************/

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
#define USB_REQ_SYNCH_FRAME 0x0C

/*****************************************************************************
 * USB Request Types
 *****************************************************************************/

#define USB_TYPE_STANDARD (0x00 << 5)
#define USB_TYPE_CLASS (0x01 << 5)
#define USB_TYPE_VENDOR (0x02 << 5)
#define USB_TYPE_RESERVED (0x03 << 5)

/*****************************************************************************
 * USB Request Directions
 *****************************************************************************/

#define USB_DIR_OUT 0x00
#define USB_DIR_IN 0x80

/*****************************************************************************
 * USB Request Recipients
 *****************************************************************************/

#define USB_RECIP_DEVICE 0x00
#define USB_RECIP_INTERFACE 0x01
#define USB_RECIP_ENDPOINT 0x02
#define USB_RECIP_OTHER 0x03

/*****************************************************************************
 * USB Endpoint Types
 *****************************************************************************/

#define USB_ENDPOINT_XFER_CONTROL 0x00
#define USB_ENDPOINT_XFER_ISOC 0x01
#define USB_ENDPOINT_XFER_BULK 0x02
#define USB_ENDPOINT_XFER_INT 0x03

#define USB_ENDPOINT_NUMBER_MASK 0x0F
#define USB_ENDPOINT_DIR_MASK 0x80

/*****************************************************************************
 * USB Device Classes
 *****************************************************************************/

#define USB_CLASS_PER_INTERFACE 0x00
#define USB_CLASS_AUDIO 0x01
#define USB_CLASS_COMM 0x02
#define USB_CLASS_HID 0x03
#define USB_CLASS_PHYSICAL 0x05
#define USB_CLASS_STILL_IMAGE 0x06
#define USB_CLASS_PRINTER 0x07
#define USB_CLASS_MASS_STORAGE 0x08
#define USB_CLASS_HUB 0x09
#define USB_CLASS_CDC_DATA 0x0A
#define USB_CLASS_CSCID 0x0B
#define USB_CLASS_CONTENT_SEC 0x0D
#define USB_CLASS_VIDEO 0x0E
#define USB_CLASS_WIRELESS 0xE0
#define USB_CLASS_MISC 0xEF
#define USB_CLASS_APP_SPEC 0xFE
#define USB_CLASS_VENDOR_SPEC 0xFF

/*****************************************************************************
 * USB Speeds
 *****************************************************************************/

#define USB_SPEED_UNKNOWN 0
#define USB_SPEED_LOW 1
#define USB_SPEED_FULL 2
#define USB_SPEED_HIGH 3
#define USB_SPEED_WIRELESS 4
#define USB_SPEED_SUPER 5
#define USB_SPEED_SUPER_PLUS 6

/*****************************************************************************
 * USB Feature Selectors
 *****************************************************************************/

#define USB_FEATURE_ENDPOINT_HALT 0
#define USB_FEATURE_DEVICE_REMOTE_WAKEUP 1
#define USB_FEATURE_TEST_MODE 2

/*****************************************************************************
 * USB Standard Descriptor Structures (packed)
 *****************************************************************************/

struct usb_device_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed));

struct usb_config_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} __attribute__((packed));

struct usb_interface_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} __attribute__((packed));

struct usb_hid_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    uint8_t bDescriptorType2;
    uint16_t wDescriptorLength;
} __attribute__((packed));

/* BOS Descriptor (Binary Object Store) */
struct usb_bos_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumDeviceCaps;
} __attribute__((packed));

/* Device Capability Descriptor (Container for WebUSB, etc.) */
struct usb_dev_cap_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bDevCapabilityType;
    uint8_t capability[0]; /* Variable length */
} __attribute__((packed));

/* Platform Descriptor Capability Type */
#define USB_DC_PLATFORM 5

/*****************************************************************************
 * USB Setup Packet
 *****************************************************************************/

struct usb_setup_packet
{
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

/* Note: USB descriptor constants, request types, endpoint types and
 * recipient types are defined in usbip_common.h which is included above.
 * Definitions here are intentionally removed to avoid duplication.
 */
 
/* Operation common header (8 bytes) */
struct op_common
{
    uint16_t version;
    uint16_t code;
    uint32_t status;
} __attribute__((packed));

/* USB device descriptor (312 bytes) */
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

/* USB interface descriptor (4 bytes) */
struct usbip_usb_interface
{
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t padding;
} __attribute__((packed));

/* URB Base Header (20 bytes) */
struct usbip_header_basic
{
    uint32_t command;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
} __attribute__((packed));

/* SUBMIT Command (28 bytes) */
struct usbip_header_cmd_submit
{
    uint32_t transfer_flags;
    int32_t transfer_buffer_length;
    int32_t start_frame;
    int32_t number_of_packets;
    int32_t interval;
    uint8_t setup[8];
} __attribute__((packed));

/* RET_SUBMIT (20 bytes) */
struct usbip_header_ret_submit
{
    int32_t status;
    int32_t actual_length;
    int32_t start_frame;
    int32_t number_of_packets;
    int32_t error_count;
} __attribute__((packed));

/* UNLINK Command (4 bytes) */
struct usbip_header_cmd_unlink
{
    uint32_t seqnum;
} __attribute__((packed));

/* RET_UNLINK (4 bytes) */
struct usbip_header_ret_unlink
{
    int32_t status;
} __attribute__((packed));

/* URB Full Header (48 bytes) */
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
 * Setup Helper Macros
 *****************************************************************************/

#define USB_SETUP_TYPE(setup) (((setup)->bmRequestType >> 5) & 0x03)
#define USB_SETUP_DIR(setup) ((setup)->bmRequestType >> 7)
#define USB_SETUP_RECIPIENT(setup) ((setup)->bmRequestType & 0x0F)
#define USB_SETUP_IS_IN(setup) ((setup)->bmRequestType & USB_DIR_IN)

#ifdef __cplusplus
}
#endif

#endif /* USB_COMMON_H */