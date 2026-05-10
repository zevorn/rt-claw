/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Coolse DIY ESP32-S3 SP V2 board — WiFi + PSRAM real hardware.
 */

#include "platform/board.h"
#include "claw/services/tools/tools.h"
#ifdef CLAW_PLATFORM_ESP_IDF
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "esp_log.h"
#endif

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "ui_control.h"
#include "lvgl.h"  
#include "ui.h"

#define TAG "board"

static esp_lcd_panel_handle_t s_lcd_panel = NULL;
static int s_oled_ready = 0;

#define DISPLAY_WIDTH    240
#define DISPLAY_HEIGHT   240

#define LCD_HOST        SPI2_HOST

#define LCD_MOSI_PIN    GPIO_NUM_47
#define LCD_SCLK_PIN    GPIO_NUM_21
#define LCD_CS_PIN      GPIO_NUM_14
#define LCD_DC_PIN      GPIO_NUM_45
#define LCD_BL_PIN      GPIO_NUM_48

#define AUDIO_I2S_PORT  I2S_NUM_0
#define AUDIO_BCLK_PIN  GPIO_NUM_41
#define AUDIO_WS_PIN    GPIO_NUM_42
#define AUDIO_DATA_PIN  GPIO_NUM_2


#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY  true
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

static void st7789_display_init(spi_host_device_t host);

static void lcd_backlight_on(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(LCD_BL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&cfg);
    gpio_set_level(LCD_BL_PIN, 1);
}

/* SPI 总线创建 */ 
static esp_err_t create_spi_bus(int mosi, int sclk, int host)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = mosi,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = sclk,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2,
    };

    esp_err_t err = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI init failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static lv_disp_t *s_lvgl_disp = NULL;

static void audio_i2s_delete(i2s_chan_handle_t tx, i2s_chan_handle_t rx)
{
    if (tx) {
        i2s_channel_disable(tx);
    }
    if (rx) {
        i2s_channel_disable(rx);
    }
    if (tx) {
        i2s_del_channel(tx);
    }
    if (rx) {
        i2s_del_channel(rx);
    }
}

static int audio_i2s_tx_init(int sample_rate, i2s_chan_handle_t *tx)
{
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 2;
    chan_cfg.dma_frame_num = 300;
    chan_cfg.auto_clear_after_cb = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, tx, NULL);
    if (err != ESP_OK) {
        printf("i2s_new_channel(tx): %s\n", esp_err_to_name(err));
        return -1;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = sample_rate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = AUDIO_BCLK_PIN,
            .ws = AUDIO_WS_PIN,
            .dout = AUDIO_DATA_PIN,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(*tx, &std_cfg);
    if (err != ESP_OK) {
        printf("i2s_channel_init_std_mode(tx): %s\n",
               esp_err_to_name(err));
        audio_i2s_delete(*tx, NULL);
        *tx = NULL;
        return -1;
    }

    err = i2s_channel_enable(*tx);
    if (err != ESP_OK) {
        printf("i2s_channel_enable(tx): %s\n", esp_err_to_name(err));
        audio_i2s_delete(*tx, NULL);
        *tx = NULL;
        return -1;
    }
    return 0;
}

static int audio_i2s_rx_init(int sample_rate, i2s_chan_handle_t *rx)
{
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 3;
    chan_cfg.dma_frame_num = 300;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, rx);
    if (err != ESP_OK) {
        printf("i2s_new_channel(rx): %s\n", esp_err_to_name(err));
        return -1;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = sample_rate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = AUDIO_BCLK_PIN,
            .ws = AUDIO_WS_PIN,
            .dout = GPIO_NUM_NC,
            .din = AUDIO_DATA_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(*rx, &std_cfg);
    if (err != ESP_OK) {
        printf("i2s_channel_init_std_mode(rx): %s\n",
               esp_err_to_name(err));
        audio_i2s_delete(NULL, *rx);
        *rx = NULL;
        return -1;
    }

    err = i2s_channel_enable(*rx);
    if (err != ESP_OK) {
        printf("i2s_channel_enable(rx): %s\n", esp_err_to_name(err));
        audio_i2s_delete(NULL, *rx);
        *rx = NULL;
        return -1;
    }
    return 0;
}

static void cmd_audio_beep(int argc, char **argv)
{
    int freq = 880;
    int duration_ms = 300;
    int sample_rate = 44100;
    i2s_chan_handle_t tx = NULL;
    int16_t frame[512];
    size_t written;
    size_t total_written = 0;
    size_t total_expected = 0;

    if (argc >= 2) {
        freq = atoi(argv[1]);
    }
    if (argc >= 3) {
        duration_ms = atoi(argv[2]);
    }
    if (freq < 100 || freq > 4000) {
        printf("Usage: /audio_beep [100..4000Hz] [ms]\n");
        return;
    }
    if (duration_ms < 50 || duration_ms > 5000) {
        printf("Usage: /audio_beep [freq] [50..5000ms]\n");
        return;
    }
    if (audio_i2s_tx_init(sample_rate, &tx) != 0) {
        return;
    }

    int half_period = sample_rate / (freq * 2);
    if (half_period <= 0) {
        half_period = 1;
    }
    int total = sample_rate * duration_ms / 1000;
    int phase = 0;

    while (total > 0) {
        int max_frames = (int)(sizeof(frame) / sizeof(frame[0])) / 2;
        int n = total > max_frames ? max_frames : total;
        for (int i = 0; i < n; i++) {
            int16_t sample = (phase / half_period) & 1 ? 16000 : -16000;
            frame[i * 2] = sample;
            frame[i * 2 + 1] = sample;
            phase++;
            if (phase >= half_period * 2) {
                phase = 0;
            }
        }
        size_t bytes = n * 2 * sizeof(frame[0]);
        written = 0;
        esp_err_t err = i2s_channel_write(tx, frame, bytes,
                                          &written, pdMS_TO_TICKS(1000));
        if (err != ESP_OK) {
            printf("i2s_channel_write(tx): %s\n", esp_err_to_name(err));
            break;
        }
        total_written += written;
        total_expected += bytes;
        if (written != bytes) {
            printf("i2s tx short write: %u/%u bytes\n",
                   (unsigned)written, (unsigned)bytes);
            break;
        }
        total -= n;
    }

    vTaskDelay(pdMS_TO_TICKS(30));
    audio_i2s_delete(tx, NULL);
    printf("audio beep done: %dHz %dms written=%u/%u bytes\n",
           freq, duration_ms,
           (unsigned)total_written, (unsigned)total_expected);
}

static void cmd_audio_level(int argc, char **argv)
{
    int duration_ms = 1000;
    i2s_chan_handle_t rx = NULL;
    int32_t frame[256];
    int64_t end_us;
    int32_t peak = 0;
    int64_t sum = 0;
    int samples = 0;
    size_t bytes_read;

    if (argc >= 2) {
        duration_ms = atoi(argv[1]);
    }
    if (duration_ms < 100 || duration_ms > 10000) {
        printf("Usage: /audio_level [100..10000ms]\n");
        return;
    }
    if (audio_i2s_rx_init(16000, &rx) != 0) {
        return;
    }

    end_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    while (esp_timer_get_time() < end_us) {
        esp_err_t err = i2s_channel_read(rx, frame, sizeof(frame),
                                         &bytes_read,
                                         pdMS_TO_TICKS(100));
        if (err != ESP_OK || bytes_read == 0) {
            continue;
        }
        int count = bytes_read / sizeof(frame[0]);
        for (int i = 0; i < count; i++) {
            int32_t v = frame[i] >> 14;
            if (v < 0) {
                v = -v;
            }
            if (v > peak) {
                peak = v;
            }
            sum += v;
            samples++;
        }
    }

    audio_i2s_delete(NULL, rx);
    printf("audio level: samples=%d peak=%ld avg=%lld\n",
           samples, (long)peak,
           samples ? (long long)(sum / samples) : 0);
}



static int SpiLcdDisplayInit(esp_lcd_panel_io_handle_t panel_io,
                             esp_lcd_panel_handle_t panel,
                             int width, int height,
                             int offset_x, int offset_y,
                             bool mirror_x, bool mirror_y, bool swap_xy)
{
    /* 清屏 */
    uint16_t *white_buf = heap_caps_malloc(width * sizeof(uint16_t),
                                           MALLOC_CAP_DMA);
    if (!white_buf) {
        ESP_LOGE(TAG, "display clear buffer alloc failed");
        return -1;
    }

    for (int i = 0; i < width; i++) {
        white_buf[i] = 0xFFFF;
    }
    for (int y = 0; y < height; y++) {
        esp_lcd_panel_draw_bitmap(panel, 0, y, width, y + 1, white_buf);
    }
    heap_caps_free(white_buf);

    /* 打开显示 */
    ESP_LOGI(TAG, "Turning display on");
    esp_err_t err = esp_lcd_panel_disp_on_off(panel, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display on failed: %s", esp_err_to_name(err));
    }

    /* 初始化 LVGL */
    ESP_LOGI(TAG, "Init LVGL");

    lv_init();

    size_t psram_mb = 0;
    psram_mb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) >> 20;
    if (psram_mb >= 8) {
        ESP_LOGI(TAG, "LVGL v8: PSRAM 8MB detected");
    } else if (psram_mb >= 2) {
        ESP_LOGI(TAG, "LVGL v8: PSRAM 2MB detected");
    }
    /* 初始化 LVGL_PORT 任务 */
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 5;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif

    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    /* 注册显示设备到 LVGL */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .buffer_size = width * 20,
        .double_buffer = false,
        .hres = width,
        .vres = height,
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
        },
    };

    s_lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (s_lvgl_disp == NULL) {
        ESP_LOGE(TAG, "Failed add LVGL display");
        return -1;
    }

    ESP_LOGI(TAG, "LVGL initialized OK");

    ui_init();
    ui_controller_init();
    return 0;
}

/* ST7789 屏幕初始化 */
static void st7789_display_init(spi_host_device_t host)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_err_t err;

    /* IO 配置 */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS_PIN,
        .dc_gpio_num = LCD_DC_PIN,
        .spi_mode = 0,
        .pclk_hz = 60 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi(host, &io_cfg, &io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel IO create failed: %s", esp_err_to_name(err));
        return;
    }

    /* ST7789 配置 */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
    };
    err = esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel create failed: %s", esp_err_to_name(err));
        return;
    }

    /* 复位 + 初始化 */
    err = esp_lcd_panel_reset(panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel reset failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_lcd_panel_init(panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_lcd_panel_set_gap(panel_handle, DISPLAY_OFFSET_X,
                                DISPLAY_OFFSET_Y);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel set gap failed: %s", esp_err_to_name(err));
        return;
    }
    lcd_backlight_on();

    /* 旋转 + 镜像 + 反色 */
    err = esp_lcd_panel_invert_color(panel_handle,
                                     DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel invert failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_lcd_panel_swap_xy(panel_handle, DISPLAY_SWAP_XY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel swap_xy failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_lcd_panel_mirror(panel_handle, DISPLAY_MIRROR_X,
                               DISPLAY_MIRROR_Y);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel mirror failed: %s", esp_err_to_name(err));
        return;
    }

    if (SpiLcdDisplayInit(io_handle, panel_handle,
                          DISPLAY_WIDTH, DISPLAY_HEIGHT,
                          DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                          DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
                          DISPLAY_SWAP_XY) != 0) {
        return;
    }

    s_lcd_panel = panel_handle;
    s_oled_ready = 1;

    ESP_LOGI(TAG, "ST7789 LCD initialized OK");
}


void board_early_init(void)
{
    wifi_board_early_init();

#ifdef CLAW_PLATFORM_ESP_IDF
    if (create_spi_bus(LCD_MOSI_PIN, LCD_SCLK_PIN, LCD_HOST) == ESP_OK) {
        st7789_display_init(LCD_HOST);
    }
#endif /* CLAW_PLATFORM_ESP_IDF */
}

static const shell_cmd_t s_audio_commands[] = {
    SHELL_CMD("/audio_beep",  cmd_audio_beep,  "Play I2S test tone"),
    SHELL_CMD("/audio_level", cmd_audio_level, "Read I2S microphone level"),
};

static int append_shell_cmds(shell_cmd_t *dst, int dst_count, int dst_max,
                             const shell_cmd_t *src, int src_count)
{
    for (int i = 0; i < src_count && dst_count < dst_max; i++) {
        dst[dst_count++] = src[i];
    }
    return dst_count;
}

const shell_cmd_t *board_platform_commands(int *count)
{
    static shell_cmd_t s_board_commands[6];
    static int s_board_command_count;

    if (s_board_command_count == 0) {
        /*
         * Espressif shell consumes a single board command table.
         * Start from the shared WiFi commands, then append the
         * Coolse board's audio bring-up diagnostics.
         */
        int wifi_count = 0;
        const shell_cmd_t *wifi_cmds =
            wifi_board_platform_commands(&wifi_count);

        s_board_command_count = append_shell_cmds(
            s_board_commands, s_board_command_count,
            SHELL_CMD_COUNT(s_board_commands),
            wifi_cmds, wifi_count);
        s_board_command_count = append_shell_cmds(
            s_board_commands, s_board_command_count,
            SHELL_CMD_COUNT(s_board_commands),
            s_audio_commands, SHELL_CMD_COUNT(s_audio_commands));
    }

    *count = s_board_command_count;
    return s_board_commands;
}
