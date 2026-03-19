/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Console I/O driver for Espressif SoCs.
 * Selects UART or USB-JTAG CDC at compile time via sdkconfig.
 */

#include "drivers/serial/espressif/console.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#else
#include "driver/uart.h"
#endif

#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG

void claw_console_init(void)
{
    usb_serial_jtag_driver_config_t cfg =
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 1024;
    cfg.tx_buffer_size = 1024;
    usb_serial_jtag_driver_install(&cfg);
    /* Switch VFS to use the installed driver (not polling) */
    usb_serial_jtag_vfs_use_driver();
}

int claw_console_read(void *buf, uint32_t len, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == UINT32_MAX)
                        ? portMAX_DELAY
                        : pdMS_TO_TICKS(timeout_ms);
    return usb_serial_jtag_read_bytes(buf, len, ticks);
}

int claw_console_write(const void *buf, size_t len)
{
    return usb_serial_jtag_write_bytes(buf, len, pdMS_TO_TICKS(100));
}

#else /* UART console */

void claw_console_init(void)
{
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
}

int claw_console_read(void *buf, uint32_t len, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == UINT32_MAX)
                        ? portMAX_DELAY
                        : pdMS_TO_TICKS(timeout_ms);
    return uart_read_bytes(UART_NUM_0, buf, len, ticks);
}

int claw_console_write(const void *buf, size_t len)
{
    return uart_write_bytes(UART_NUM_0, buf, len);
}

#endif /* CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG */

/* OOP driver registration */
#include "claw/core/claw_driver.h"

static claw_err_t console_drv_probe(struct claw_driver *drv)
{
    (void)drv;
    claw_console_init();
    return CLAW_OK;
}

static const struct claw_driver_ops console_drv_ops = {
    .probe  = console_drv_probe,
    .remove = NULL,
};

static struct claw_driver console_drv = {
    .name  = "console",
    .ops   = &console_drv_ops,
    .state = CLAW_DRV_REGISTERED,
};

CLAW_DRIVER_REGISTER(console, &console_drv);
