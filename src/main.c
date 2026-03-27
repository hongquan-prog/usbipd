/*
 * Copyright (c) 2026-2026, hongquan.li
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-3-24      hongquan.li   add license declaration
 */

/*
 * USBIP Server Main Program
 *
 * Simplified entry point using modular server core
 */

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/usbip_log.h"
#include "hal/usbip_transport.h"
#include "usbip_devmgr.h"
#include "usbip_server.h"

LOG_MODULE_REGISTER(main, CONFIG_MAIN_LOG_LEVEL);

/*****************************************************************************
 * Signal Handling
 *****************************************************************************/
static void signal_handler(int sig)
{
    (void)sig;
    usbip_server_stop();
}

static void setup_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGPIPE, &(struct sigaction){.sa_handler = SIG_IGN}, NULL);
}

static void print_usage(const char* prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -p, --port PORT   Listen port (default: %d)\n", CONFIG_USBIP_SERVER_PORT);
    printf("  -h, --help        Show this help\n");
    printf("\nExample:\n");
    printf("  %s -p %d\n", prog, CONFIG_USBIP_SERVER_PORT);
    printf("\nTo connect from a client:\n");
    printf("  usbip list -r <server_ip>\n");
    printf("  usbip attach -r <server_ip> -b 2-1\n");
}

int main(int argc, char* argv[])
{
    int port = CONFIG_USBIP_SERVER_PORT;
    int opt;

    static struct option long_options[] = {{"port", required_argument, 0, 'p'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    while ((opt = getopt_long(argc, argv, "p:dh", long_options, NULL)) != -1)
    {
        switch (opt)
        {
            case 'p':
                port = atoi(optarg);
                if (port <= 0 || port > 65535)
                {
                    LOG_ERR("Invalid port: %s", optarg);
                    return 1;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    LOG_INF("USBIP Server v0.2");

    /* Setup signal handling */
    setup_signals();

    /* Initialize server */
    if (usbip_server_init(port) < 0)
    {
        usbip_server_cleanup();
        return 1;
    }

    /* Run server */
    usbip_server_run();

    /* Cleanup */
    usbip_server_cleanup();

    LOG_INF("Server stopped");

    return 0;
}