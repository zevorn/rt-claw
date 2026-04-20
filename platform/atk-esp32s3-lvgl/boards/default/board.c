/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ESP32-S3 default board — WiFi + PSRAM real hardware.
 */

#include "platform/board.h"
#ifdef CONFIG_RTCLAW_AUDIO_ENABLE
#include "drivers/audio/espressif/es8388_audio.h"
#endif
#include "claw/services/tools/tools.h"
#ifdef CLAW_PLATFORM_ESP_IDF
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/spi_master.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "esp_log.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "ui_control.h"
#include "lvgl.h"  
#include "ui.h"

#define TAG "board"

static esp_lcd_panel_handle_t s_lcd_panel = NULL;
static int s_oled_ready = 0;
static int s_audio_ready = 0;

#define V1_PA   GPIO_NUM_NC
#define AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_41
#define AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_42
#define ES8388_CODEC_DEFAULT_ADDR    (0x20)
#define ES8388_CODEC_DEFAULT_ADDR_1  (0x22)
#define AUDIO_CODEC_ES8388_ADDR ES8388_CODEC_DEFAULT_ADDR

#define DISPLAY_WIDTH    320
#define DISPLAY_HEIGHT   240

#define LCD_HOST        SPI2_HOST

#define LCD_MOSI_PIN    GPIO_NUM_11   
#define LCD_SCLK_PIN    GPIO_NUM_12   
#define LCD_CS_PIN      GPIO_NUM_21   
#define LCD_DC_PIN      GPIO_NUM_40   


#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY  true
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

#define XL9555_ADDR 0x20 
static i2c_master_dev_handle_t g_xl9555_dev = NULL;
static void st7789_display_init(spi_host_device_t host);

/* 初始化 XL9555 */
static void xl9555_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = XL9555_ADDR,
        .scl_speed_hz = 400*1000,
    };
    i2c_master_bus_add_device(bus, &cfg, &g_xl9555_dev);

    uint8_t buf1[] = {0x06, 0x03};
    uint8_t buf2[] = {0x07, 0xF0};
    i2c_master_transmit(g_xl9555_dev, buf1, 2, 100);
    i2c_master_transmit(g_xl9555_dev, buf2, 2, 100);
}

static void xl9555_set_output(uint8_t bit, uint8_t level)
{
    uint8_t data;
    int index = bit;

    if (bit < 8) {
        i2c_master_transmit_receive(g_xl9555_dev, &(uint8_t){0x02}, 1, &data, 1, 100);
    } else {
        i2c_master_transmit_receive(g_xl9555_dev, &(uint8_t){0x03}, 1, &data, 1, 100);
        index -= 8;
    }

    data = (data & ~(1 << index)) | (level << index);

    uint8_t reg = (bit < 8) ? 0x02 : 0x03;
    uint8_t buf[] = {reg, data};
    i2c_master_transmit(g_xl9555_dev, buf, 2, 100);
}


/* SPI 总线创建 */ 
static void create_spi_bus(int mosi, int sclk, int host)
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
    }
}

static lv_disp_t *s_lvgl_disp = NULL;



static void SpiLcdDisplayInit(esp_lcd_panel_io_handle_t panel_io,
                             esp_lcd_panel_handle_t panel,
                             int width, int height,
                             int offset_x, int offset_y,
                             bool mirror_x, bool mirror_y, bool swap_xy)
{
    /* 清屏 */ 
    uint16_t *white_buf = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!white_buf) return;

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
        return;
    }

    ESP_LOGI(TAG, "LVGL initialized OK");

    ui_init();
    ui_controller_init();
}

/* ST7789 屏幕初始化 */
static void st7789_display_init(spi_host_device_t host)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;

    /* IO 配置 */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS_PIN,
        .dc_gpio_num = LCD_DC_PIN,
        .spi_mode = 0,
        .pclk_hz = 20 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_new_panel_io_spi(host, &io_cfg, &io_handle);

    /* ST7789 配置 */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
    };
    esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle);

    /* 复位 + 初始化 */
    esp_lcd_panel_reset(panel_handle);
    if(g_xl9555_dev)
        xl9555_set_output(8, 1);

    esp_lcd_panel_init(panel_handle);

    /* 旋转 + 镜像 + 反色 */
    esp_lcd_panel_invert_color(panel_handle, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    esp_lcd_panel_swap_xy(panel_handle, DISPLAY_SWAP_XY);
    esp_lcd_panel_mirror(panel_handle, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

    s_lcd_panel = panel_handle;
    s_oled_ready = 1;
    SpiLcdDisplayInit(io_handle, panel_handle, 
        DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);

    ESP_LOGI(TAG, "ST7789 LCD initialized OK");
}




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
    i2c_master_bus_handle_t bus0 = create_i2c_bus(AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN,
                                                   I2C_NUM_0);
    create_spi_bus(LCD_MOSI_PIN, LCD_SCLK_PIN, LCD_HOST);

    
    

    int pa_pin __attribute__((unused)) = V1_PA;

    if (bus0) {
        xl9555_init(bus0);
        xl9555_set_output(2, 0);   
        printf("xl9555: set pin 2 = 0\n");
    }


    st7789_display_init(LCD_HOST);

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
        i2c_master_probe(bus0, AUDIO_CODEC_ES8388_ADDR, 200) == ESP_OK) {
        if (es8388_audio_init(bus0, pa_pin) == 0) {
            s_audio_ready = 1;
            es8388_audio_play_sound("startup");
        }
    } else {
        printf("ES8388 not found, audio disabled\n");
    }
#endif /* CONFIG_RTCLAW_AUDIO_ENABLE */


#endif
}

const shell_cmd_t *board_platform_commands(int *count)
{
    return wifi_board_platform_commands(count);
}
