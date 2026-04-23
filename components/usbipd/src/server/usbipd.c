#include "usbip_server.h"
#include "hal/usbip_log.h"

LOG_MODULE_REGISTER(usbipd, CONFIG_USBIP_LOG_LEVEL);

extern void hid_dap_driver_register(void);
extern void bulk_dap_driver_register(void);
extern void default_transport_register(void);
extern void default_os_register(void);
extern void dap_lock_init(void);

void usbipd_init(uint16_t port)
{
    default_os_register();
    usbip_log_init();
    dap_lock_init();
    hid_dap_driver_register();
    bulk_dap_driver_register();
    default_transport_register();

    /* Initialize server on configured port */
    if (usbip_server_init(port) == 0)
    {
        LOG_INF("USBIP server listening on port %d", CONFIG_USBIP_SERVER_PORT);
        /* Run server main loop (blocking) */
        usbip_server_run();
        usbip_server_cleanup();
    }
    else
    {
        LOG_ERR("Failed to initialize USBIP server");
    }
}