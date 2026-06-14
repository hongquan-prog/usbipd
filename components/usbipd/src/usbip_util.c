#include <string.h>

#include "usbip_common.h"
#include "usbip_util.h"

#if defined(__GNUC__)
#define USBIP_WEAK __attribute__((weak))
#else
#define USBIP_WEAK
#endif

USBIP_WEAK int usbip_desc_get_serial_ascii(char* serial, uint8_t serial_size)
{
    (void)serial;
    (void)serial_size;
    return 0;
}

void ascii_string_to_utf16le(uint8_t* desc, size_t desc_size, const char* ascii)
{
    size_t i;
    size_t chars;
    size_t max_chars;

    if (!desc || !ascii || desc_size < 4)
    {
        return;
    }

    chars = strlen(ascii);
    max_chars = (desc_size - 2) / 2;
    if (chars > max_chars)
    {
        chars = max_chars;
    }

    desc[0] = (uint8_t)(2 + (chars * 2));
    desc[1] = USB_DT_STRING;
    for (i = 0; i < chars; ++i)
    {
        desc[2 + (i * 2)] = (uint8_t)ascii[i];
        desc[3 + (i * 2)] = 0;
    }

    memset(desc + desc[0], 0, desc_size - desc[0]);
}
