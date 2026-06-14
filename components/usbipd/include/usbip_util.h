#ifndef USBIP_UTIL_H
#define USBIP_UTIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* String defaults shared by USBIP HID/Bulk descriptor code. */
#if defined(CONFIG_TINYUSB_DESC_MANUFACTURER_STRING)
#define USBIP_STR_MANUFACTURER  CONFIG_TINYUSB_DESC_MANUFACTURER_STRING
#else
#define USBIP_STR_MANUFACTURER  "RPI5"
#endif

#if defined(CONFIG_TINYUSB_DESC_PRODUCT_STRING)
#define USBIP_STR_PRODUCT_BASE  CONFIG_TINYUSB_DESC_PRODUCT_STRING
#else
#define USBIP_STR_PRODUCT_BASE  "CMSIS-DAP"
#endif

#if defined(CONFIG_TINYUSB_DESC_SERIAL_STRING)
#define USBIP_STR_SERIAL        CONFIG_TINYUSB_DESC_SERIAL_STRING
#else
#define USBIP_STR_SERIAL        "1234567890"
#endif

#if defined(CONFIG_DAPLINK_DESC_STRING)
#define USBIP_STR_DAPLINK_INTF  CONFIG_DAPLINK_DESC_STRING
#else
#define USBIP_STR_DAPLINK_INTF  "CMSIS-DAP"
#endif

#define USBIP_STR_PRODUCT_HID   "HID " USBIP_STR_PRODUCT_BASE
#define USBIP_STR_PRODUCT_BULK  "BLK " USBIP_STR_PRODUCT_BASE
#define USBIP_STR_BULK_INTF     USBIP_STR_DAPLINK_INTF " v2"

/*
 * Optional descriptor override hook.
 *
 * Return 1 when override data is provided; return 0 to use built-in defaults.
 */
int usbip_desc_get_serial_ascii(char* serial, uint8_t serial_size);

/* Build a USB string descriptor by converting an ASCII string to UTF-16LE. */
void ascii_string_to_utf16le(uint8_t* desc, size_t desc_size, const char* ascii);

#ifdef __cplusplus
}
#endif

#endif /* USBIP_UTIL_H */
