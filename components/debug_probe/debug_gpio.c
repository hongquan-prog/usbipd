/*
 * SPDX-FileCopyrightText: 2020-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Raspberry Pi 5 GPIO driver using mmap for high-performance DAP access
 * Target: BCM2712 + RP1 I/O controller
 */

#include "debug_gpio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

/* Memory mapped GPIO registers */
volatile uint32_t *rpi_rio = NULL;
volatile uint32_t *rpi_gpio = NULL;

/* File descriptors */
static int mem_fd = -1;
static void *gpio_map = NULL;
static void *rio_map = NULL;

/* Initialization state */
static int s_debug_gpio_init = 0;

/* Activity callback */
static debug_activity_notify_cb_t s_activity_callback = NULL;

/* Page size for mmap */
#define PAGE_SIZE       4096
#define PAGE_MASK       (~(PAGE_SIZE - 1))

/* GPIO control register bits */
#define GPIO_CTRL_FUNCSEL_SHIFT  0
#define GPIO_CTRL_FUNCSEL_MASK   (0x1f << GPIO_CTRL_FUNCSEL_SHIFT)

void debug_probe_register_activity_callback(debug_activity_notify_cb_t callback)
{
    s_activity_callback = callback;
}

void debug_probe_notify_activity(bool active)
{
    if (s_activity_callback) {
        s_activity_callback(active);
    }
}

static void set_gpio_func(int gpio, int func)
{
    if (rpi_gpio) {
        /* 设置功能、最大驱动强度(12mA)和快速压摆率 */
        uint32_t ctrl = (func & 0x1f) << GPIO_CTRL_FUNCSEL_SHIFT;
        ctrl |= GPIO_CTRL_DRIVE_12MA;   /* 最大驱动强度 */
        ctrl |= GPIO_CTRL_SLEWFAST;     /* 快速压摆率 */
        rpi_gpio[GPIO_CTRL(gpio)>> 2] = ctrl;
    }
}

int rpi_gpio_mmap_init(void)
{
    if (rpi_rio) {
        return 0;  /* Already initialized */
    }

    /* Open /dev/mem */
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("Failed to open /dev/mem");
        return -1;
    }

    /* Map GPIO control registers */
    gpio_map = mmap(NULL, PAGE_SIZE * 4, PROT_READ | PROT_WRITE, MAP_SHARED,
                    mem_fd, RP1_GPIO_BASE & PAGE_MASK);
    if (gpio_map == MAP_FAILED) {
        perror("Failed to mmap GPIO");
        close(mem_fd);
        mem_fd = -1;
        return -1;
    }
    rpi_gpio = (volatile uint32_t *)((char *)gpio_map + (RP1_GPIO_BASE & ~PAGE_MASK));

    /* Map RIO registers */
    rio_map = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                   mem_fd, RP1_RIO_BASE & PAGE_MASK);
    if (rio_map == MAP_FAILED) {
        perror("Failed to mmap RIO");
        munmap(gpio_map, PAGE_SIZE * 4);
        rpi_gpio = NULL;
        close(mem_fd);
        mem_fd = -1;
        return -1;
    }
    rpi_rio = (volatile uint32_t *)((char *)rio_map + (RP1_RIO_BASE & ~PAGE_MASK));

    printf("GPIO mmap initialized: GPIO=%p, RIO=%p\n",
           (void *)rpi_gpio, (void *)rpi_rio);

    return 0;
}

void rpi_gpio_mmap_cleanup(void)
{
    if (rio_map && rio_map != MAP_FAILED) {
        munmap(rio_map, PAGE_SIZE);
        rio_map = NULL;
    }
    rpi_rio = NULL;

    if (gpio_map && gpio_map != MAP_FAILED) {
        munmap(gpio_map, PAGE_SIZE * 4);
        gpio_map = NULL;
    }
    rpi_gpio = NULL;

    if (mem_fd >= 0) {
        close(mem_fd);
        mem_fd = -1;
    }
}

void debug_probe_init_jtag_pins(void)
{
    if (!s_debug_gpio_init) {
        if (rpi_gpio_mmap_init() < 0) {
            fprintf(stderr, "Failed to initialize GPIO mmap\n");
            return;
        }
        s_debug_gpio_init = 1;
    }

    /* Set GPIO function to SIO (GPIO mode) with max drive and fast slew */
    set_gpio_func(GPIO_TCK, GPIO_FUNC_SIO);
    set_gpio_func(GPIO_TDI, GPIO_FUNC_SIO);
    set_gpio_func(GPIO_TMS, GPIO_FUNC_SIO);
    set_gpio_func(GPIO_TDO, GPIO_FUNC_SIO);

    /* Configure initial state:
     * TCK, TDI, TMS - outputs, initial TCK=0, TDI=1, TMS=1
     * TDO - input
     */
    uint32_t out = rpi_rio[RIO_OUT>> 2];
    out &= ~GPIO_TCK_BIT;   /* TCK = 0 */
    out |= GPIO_TDI_BIT;    /* TDI = 1 */
    out |= GPIO_TMS_BIT;    /* TMS = 1 */
    rpi_rio[RIO_OUT>> 2] = out;

    /* Set output enables */
    rpi_rio[RIO_OE>> 2] = GPIO_TCK_BIT | GPIO_TDI_BIT | GPIO_TMS_BIT;
}

void debug_probe_init_swd_pins(void)
{
    if (!s_debug_gpio_init) {
        if (rpi_gpio_mmap_init() < 0) {
            fprintf(stderr, "Failed to initialize GPIO mmap\n");
            return;
        }
        s_debug_gpio_init = 1;
    }

    /* Set GPIO function to SIO (GPIO mode) with max drive and fast slew */
    set_gpio_func(GPIO_SWCLK, GPIO_FUNC_SIO);
    set_gpio_func(GPIO_SWDIO, GPIO_FUNC_SIO);

    /* Configure initial state:
     * SWCLK - output, initial high
     * SWDIO - output, initial high
     */
    uint32_t out = rpi_rio[RIO_OUT>> 2];
    out |= GPIO_SWCLK_BIT;  /* SWCLK = 1 */
    out |= GPIO_SWDIO_BIT;  /* SWDIO = 1 */
    rpi_rio[RIO_OUT>> 2] = out;

    /* Set output enables for both pins */
    rpi_rio[RIO_OE>> 2] = GPIO_SWCLK_BIT | GPIO_SWDIO_BIT;
}

void debug_probe_reset_pins(void)
{
    if (!s_debug_gpio_init) {
        return;
    }

    /* Set pins to input mode (high impedance) */
    if (rpi_rio) {
        rpi_rio[RIO_OE>> 2] &= ~(GPIO_SWCLK_BIT | GPIO_SWDIO_BIT);
    }

    /* Note: Don't unmap, keep initialized for subsequent commands */
}