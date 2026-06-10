#ifndef USBIP_UTIL_H
#define USBIP_UTIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Current build-time fallback defaults for USBIP HID/Bulk string descriptors. */

#define USBIP_STR_MANUFACTURER  "RPI5"
#define USBIP_STR_PRODUCT_BASE  "CMSIS-DAP"
#define USBIP_STR_SERIAL        "1234567890"
#define USBIP_STR_DAPLINK_INTF  "CMSIS-DAP"
#define USBIP_STR_PRODUCT_HID   "HID " USBIP_STR_PRODUCT_BASE
#define USBIP_STR_PRODUCT_BULK  "BLK " USBIP_STR_PRODUCT_BASE
#define USBIP_STR_BULK_INTF     USBIP_STR_DAPLINK_INTF " v2"

/* Build a USB string descriptor by converting an ASCII string to UTF-16LE. */
void ascii_string_to_utf16le(uint8_t* desc, size_t desc_size, const char* ascii);

#ifdef __cplusplus
}
#endif

#endif /* USBIP_UTIL_H */
