/*
 * SPDX-FileCopyrightText: 2020-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Raspberry Pi 5 GPIO driver using mmap for high-performance DAP access
 * Target: BCM2712 + RP1 I/O controller
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "compiler.h"

/* Debug pins - JTAG (using Kconfig configuration) */
#define GPIO_TDI        CONFIG_GPIO_TDI
#define GPIO_TDO        CONFIG_GPIO_TDO
#define GPIO_TCK        CONFIG_GPIO_TCK
#define GPIO_TMS        CONFIG_GPIO_TMS

/* Debug pins - SWD (using Kconfig configuration) */
#define GPIO_SWCLK      CONFIG_GPIO_SWCLK
#define GPIO_SWDIO      CONFIG_GPIO_SWDIO

/* RP1 GPIO/RIO register addresses (physical) */
#define RP1_GPIO_BASE   0x1f000d0000ULL
#define RP1_RIO_BASE    0x1f000e0000ULL

/* RIO register offsets - RP1 RIO 寄存器 */
#define RIO_OUT         0x0000
#define RIO_OE          0x0004
#define RIO_IN          0x0008
/* RP1 没有 SET/CLR，需要用 read-modify-write */

/* GPIO control register offsets (每个GPIO一个32位寄存器) */
#define GPIO_CTRL(gpio) ((gpio) * 4)

/* GPIO function codes - RP1 */
#define GPIO_FUNC_SIO   5   /* SIO function (GPIO) */

/* GPIO control register bit fields - RP1 */
#define GPIO_CTRL_FUNCSEL_SHIFT   0
#define GPIO_CTRL_FUNCSEL_MASK    (0x1f << GPIO_CTRL_FUNCSEL_SHIFT)
#define GPIO_CTRL_DRIVE_SHIFT     13   /* Drive strength: 0=2mA, 1=4mA, 2=8mA, 3=12mA */
#define GPIO_CTRL_DRIVE_MASK      (0x3 << GPIO_CTRL_DRIVE_SHIFT)
#define GPIO_CTRL_DRIVE_12MA      (3 << GPIO_CTRL_DRIVE_SHIFT)   /* Maximum drive */
#define GPIO_CTRL_SLEWFAST        (1 << 16)   /* Fast slew rate */

/* GPIO context */
extern volatile uint32_t *rpi_rio;
extern volatile uint32_t *rpi_gpio;

/**
 * @brief Debug activity notification callback
 */
typedef void (*debug_activity_notify_cb_t)(bool active);

/**
 * @brief Initialize debug GPIO pins for JTAG mode
 */
void debug_probe_init_jtag_pins(void);

/**
 * @brief Initialize debug GPIO pins for SWD mode
 */
void debug_probe_init_swd_pins(void);

/**
 * @brief Reset and cleanup debug GPIO pins
 */
void debug_probe_reset_pins(void);

/**
 * @brief Notify debug activity
 */
void debug_probe_notify_activity(bool active);

/**
 * @brief Initialize mmap GPIO access
 */
int rpi_gpio_mmap_init(void);

/**
 * @brief Cleanup mmap GPIO access
 */
void rpi_gpio_mmap_cleanup(void);

/* Bit masks for each GPIO */
#define GPIO_SWCLK_BIT  (1U << GPIO_SWCLK)
#define GPIO_SWDIO_BIT  (1U << GPIO_SWDIO)
#define GPIO_TCK_BIT    (1U << GPIO_TCK)
#define GPIO_TMS_BIT    (1U << GPIO_TMS)
#define GPIO_TDI_BIT    (1U << GPIO_TDI)
#define GPIO_TDO_BIT    (1U << GPIO_TDO)

/* High-performance inline GPIO functions for debug operations */

__STATIC_FORCEINLINE void debug_probe_swdio_out_enable(void)
{
    if (rpi_rio) {
        rpi_rio[RIO_OE>> 2] |= GPIO_SWDIO_BIT;
    }
}

__STATIC_FORCEINLINE void debug_probe_swdio_out_disable(void)
{
    if (rpi_rio) {
        rpi_rio[RIO_OE>> 2] &= ~GPIO_SWDIO_BIT;
    }
}

__STATIC_FORCEINLINE int debug_probe_swdio_read(void)
{
    if (rpi_rio) {
        return (rpi_rio[RIO_IN>> 2] & GPIO_SWDIO_BIT) ? 1 : 0;
    }
    return 0;
}

__STATIC_FORCEINLINE void debug_probe_swclk_set(void)
{
    if (rpi_rio) {
        rpi_rio[RIO_OUT>> 2] |= GPIO_SWCLK_BIT;
    }
}

__STATIC_FORCEINLINE void debug_probe_swclk_clr(void)
{
    if (rpi_rio) {
        rpi_rio[RIO_OUT>> 2] &= ~GPIO_SWCLK_BIT;
    }
}

__STATIC_FORCEINLINE void debug_probe_swdio_set(void)
{
    if (rpi_rio) {
        rpi_rio[RIO_OUT>> 2] |= GPIO_SWDIO_BIT;
    }
}

__STATIC_FORCEINLINE void debug_probe_swdio_clr(void)
{
    if (rpi_rio) {
        rpi_rio[RIO_OUT>> 2] &= ~GPIO_SWDIO_BIT;
    }
}

__STATIC_FORCEINLINE void debug_probe_swdio_write(int val)
{
    if (rpi_rio) {
        if (val & 0x01) {
            rpi_rio[RIO_OUT>> 2] |= GPIO_SWDIO_BIT;
        } else {
            rpi_rio[RIO_OUT>> 2] &= ~GPIO_SWDIO_BIT;
        }
    }
}

__STATIC_FORCEINLINE void debug_probe_swd_blink(int on)
{
    debug_probe_notify_activity(on);
}

__STATIC_FORCEINLINE void debug_probe_jtag_led_off(void)
{
    debug_probe_notify_activity(false);
}

__STATIC_FORCEINLINE void debug_probe_jtag_led_on(void)
{
    debug_probe_notify_activity(true);
}

__STATIC_FORCEINLINE int debug_probe_tdo_read(void)
{
    if (rpi_rio) {
        return (rpi_rio[RIO_IN>> 2] & GPIO_TDO_BIT) ? 1 : 0;
    }
    return 0;
}

__STATIC_FORCEINLINE void debug_probe_tdi_write(int val)
{
    if (rpi_rio) {
        if (val & 0x01) {
            rpi_rio[RIO_OUT>> 2] |= GPIO_TDI_BIT;
        } else {
            rpi_rio[RIO_OUT>> 2] &= ~GPIO_TDI_BIT;
        }
    }
}

__STATIC_FORCEINLINE void debug_probe_tck_set(void)
{
    if (rpi_rio) {
        rpi_rio[RIO_OUT>> 2] |= GPIO_TCK_BIT;
    }
}

__STATIC_FORCEINLINE void debug_probe_tck_clr(void)
{
    if (rpi_rio) {
        rpi_rio[RIO_OUT>> 2] &= ~GPIO_TCK_BIT;
    }
}

__STATIC_FORCEINLINE void debug_probe_write_tmstck(uint8_t tms_tdi_mask)
{
    /* TMS is bit 2, TDI is bit 1 */
    uint32_t out = rpi_rio[RIO_OUT>> 2];

    if (tms_tdi_mask & 0x04) out |= GPIO_TMS_BIT;
    else out &= ~GPIO_TMS_BIT;

    if (tms_tdi_mask & 0x02) out |= GPIO_TDI_BIT;
    else out &= ~GPIO_TDI_BIT;

    rpi_rio[RIO_OUT>> 2] = out;
}

/* Basic GPIO mode functions */
__STATIC_FORCEINLINE void debug_probe_mode_input_enable(int gpio_num)
{
    if (rpi_rio) {
        rpi_rio[RIO_OE>> 2] &= ~(1U << gpio_num);
    }
}

__STATIC_FORCEINLINE void debug_probe_mode_out_enable(int gpio_num)
{
    if (rpi_rio) {
        rpi_rio[RIO_OE>> 2] |= (1U << gpio_num);
    }
}

__STATIC_FORCEINLINE void debug_probe_mode_in_out_enable(int gpio_num)
{
    debug_probe_mode_out_enable(gpio_num);
}

__STATIC_FORCEINLINE void debug_probe_set(int gpio_num)
{
    if (rpi_rio) {
        rpi_rio[RIO_OUT>> 2] |= (1U << gpio_num);
    }
}

__STATIC_FORCEINLINE void debug_probe_clear(int gpio_num)
{
    if (rpi_rio) {
        rpi_rio[RIO_OUT>> 2] &= ~(1U << gpio_num);
    }
}