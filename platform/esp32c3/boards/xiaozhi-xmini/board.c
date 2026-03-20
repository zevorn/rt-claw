/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * XiaoZhi xmini-c3 board — WiFi + SSD1306 OLED + ES8311 Audio.
 *
 * Supports multiple board revisions:
 *   V1: I2C SDA=3 SCL=4, PA=11
 *   V3: I2C SDA=0 SCL=1, PA=10
 * Auto-detects by probing ES8311 on each I2C bus.
 */

#include "claw_board.h"
#include "drivers/display/espressif/ssd1306_oled.h"
#ifdef CONFIG_RTCLAW_AUDIO_ENABLE
#include "drivers/audio/espressif/es8311_audio.h"
#endif
#include "claw/tools/claw_tools.h"

#ifdef CLAW_PLATFORM_ESP_IDF
#include "driver/i2c_master.h"
#include "esp_log.h"
#endif

#include <string.h>

#define TAG "board"

/* Board revision pin configs */
#define V1_SDA  3
#define V1_SCL  4
#define V1_PA   11

#define V3_SDA  0
#define V3_SCL  1
#define V3_PA   10

#define ES8311_ADDR  0x18
#define SSD1306_ADDR 0x3C

#define STATUS_ROW_START 2
#define STATUS_ROWS      4
#define PROGRESS_ROW     6

static int s_oled_ready;
static int s_audio_ready;

#ifdef CLAW_PLATFORM_ESP_IDF

static i2c_master_bus_handle_t create_i2c_bus(int sda, int scl,
                                               int port)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = port,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus (SDA=%d SCL=%d) init: %s",
                 sda, scl, esp_err_to_name(err));
        return NULL;
    }
    return bus;
}

static void i2c_scan(i2c_master_bus_handle_t bus, const char *label)
{
    printf("I2C scan [%s]:", label);
    int found = 0;
    for (int addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 100) == ESP_OK) {
            printf(" 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        printf(" (none)");
    }
    printf("\n");
}

#endif /* CLAW_PLATFORM_ESP_IDF */

void board_early_init(void)
{
    wifi_board_early_init();

#ifdef CLAW_PLATFORM_ESP_IDF
    /*
     * Auto-detect board revision by trying both I2C pin configs.
     * V1: SDA=3 SCL=4, V3: SDA=0 SCL=1.
     * OLED (0x3C) and ES8311 (0x18) may be on the same or
     * different buses depending on revision.
     */

    /*
     * Auto-detect board revision by probing OLED on V1 pins.
     * If not found, try V3 pins.
     */
    i2c_master_bus_handle_t bus0 = create_i2c_bus(V1_SDA, V1_SCL,
                                                   I2C_NUM_0);
    int oled_on_v1 = 0;
    int pa_pin __attribute__((unused)) = V1_PA;

    if (bus0) {
        i2c_scan(bus0, "V1 SDA=3 SCL=4");
        oled_on_v1 = (i2c_master_probe(bus0, SSD1306_ADDR, 200)
                      == ESP_OK);
    }

    if (!oled_on_v1) {
        /* V1 pins didn't find OLED, try V3 */
        if (bus0) {
            i2c_del_master_bus(bus0);
            bus0 = NULL;
        }
        bus0 = create_i2c_bus(V3_SDA, V3_SCL, I2C_NUM_0);
        pa_pin = V3_PA;
        if (bus0) {
            i2c_scan(bus0, "V3 SDA=0 SCL=1");
            printf("Detected V3 board\n");
        }
    } else {
        printf("Detected V1 board\n");
    }

#ifdef CONFIG_RTCLAW_AUDIO_ENABLE
    /*
     * Initialize ES8311 BEFORE SSD1306.
     *
     * esp_codec_dev's audio_codec_new_i2c_ctrl() registers an
     * I2C device on the bus.  If SSD1306's esp_lcd_panel_io_i2c
     * is created first, the two frameworks conflict on the same
     * bus, causing NACK errors on the codec address.
     */
    if (bus0 &&
        i2c_master_probe(bus0, ES8311_ADDR, 200) == ESP_OK) {
        if (es8311_audio_init(bus0, pa_pin) == 0) {
            s_audio_ready = 1;
            es8311_audio_play_sound("startup");
        }
    } else {
        printf("ES8311 not found, audio disabled\n");
    }
#endif /* CONFIG_RTCLAW_AUDIO_ENABLE */

    /* Initialize OLED */
    if (bus0) {
        if (ssd1306_init_on_bus(bus0) == 0) {
            s_oled_ready = 1;
            ssd1306_write_line(0, "  rt-claw v" RT_CLAW_VERSION);
            ssd1306_write_line(1, "  xmini-c3");
            ssd1306_write_line(2, s_audio_ready
                               ? "  Audio: OK" : "  Audio: N/A");
        }
    }
#endif
}

const shell_cmd_t *board_platform_commands(int *count)
{
    return wifi_board_platform_commands(count);
}

/* ---- Override weak claw_lcd_* stubs ---- */

int claw_lcd_init(void)
{
    return s_oled_ready ? 0 : -1;
}

int claw_lcd_available(void)
{
    return s_oled_ready;
}

void claw_lcd_status(const char *msg)
{
    if (!s_oled_ready || !msg) {
        return;
    }

    for (int r = STATUS_ROW_START;
         r < STATUS_ROW_START + STATUS_ROWS; r++) {
        ssd1306_write_line(r, "");
    }

    int len = (int)strlen(msg);
    int chars_per_line = SSD1306_WIDTH / 8;

    for (int r = 0; r < STATUS_ROWS && len > 0; r++) {
        char line[17];
        int n = len > chars_per_line ? chars_per_line : len;
        memcpy(line, msg, n);
        line[n] = '\0';
        ssd1306_write_line(STATUS_ROW_START + r, line);
        msg += n;
        len -= n;
    }
}

void claw_lcd_progress(int percent)
{
    if (!s_oled_ready) {
        return;
    }
    ssd1306_progress_bar(PROGRESS_ROW, percent);
}
