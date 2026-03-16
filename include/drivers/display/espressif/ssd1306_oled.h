/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SSD1306 OLED driver — lightweight 128x64 monochrome display.
 * Uses ESP-IDF esp_lcd + I2C.  No LVGL dependency.
 */

#ifndef CLAW_DRIVERS_DISPLAY_ESPRESSIF_SSD1306_OLED_H
#define CLAW_DRIVERS_DISPLAY_ESPRESSIF_SSD1306_OLED_H

#include <stdint.h>

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   (SSD1306_HEIGHT / 8)

/**
 * Initialize SSD1306 over I2C.
 * @param sda_pin  I2C SDA GPIO number
 * @param scl_pin  I2C SCL GPIO number
 * @return 0 on success
 */
int ssd1306_init(int sda_pin, int scl_pin);

/**
 * Initialize SSD1306 on an existing I2C master bus.
 * Use this when the I2C bus is shared with other devices
 * (e.g. ES8311 audio codec on xmini-c3).
 * @param bus  I2C master bus handle (from i2c_new_master_bus)
 * @return 0 on success
 */
int ssd1306_init_on_bus(void *bus);

/** Clear the entire display. */
void ssd1306_clear(void);

/** Flush the internal framebuffer to the display. */
void ssd1306_flush(void);

/**
 * Write a text string at the given row (0-7) using built-in 8x8 font.
 * Clears the row first, then renders the text.
 */
void ssd1306_write_line(int row, const char *text);

/**
 * Draw a horizontal progress bar on the given row.
 * @param row      Row (0-7)
 * @param percent  0-100
 */
void ssd1306_progress_bar(int row, int percent);

/** Set pixel at (x, y). Call ssd1306_flush() to update display. */
void ssd1306_set_pixel(int x, int y, int on);

#endif /* CLAW_DRIVERS_DISPLAY_ESPRESSIF_SSD1306_OLED_H */
