/*
 * Copyright (c) 2026, Tang Sun <714858993@qq.com>
 * SPDX-License-Identifier: MIT
 *
 * es8388 audio codec driver for xmini-c3.
 * Uses ESP-IDF esp_codec_dev framework (C API).
 *
 * Hardware:
 *   I2S: MCLK=GPIO10, BCLK=GPIO8, WS=GPIO6, DOUT=GPIO5, DIN=GPIO7
 *   Codec I2C: 0x18 on shared I2C bus
 *   PA enable: GPIO11 (active high)
 *   Format: 24kHz, 16-bit, mono
 */



/*
 * es8388 requires esp_codec_dev managed component which is only
 * available on boards that declare the dependency (e.g. xiaozhi-xmini).
 * Fall through to stubs when the header is absent.
 */
#if defined(CLAW_PLATFORM_ESP_IDF) && __has_include("esp_codec_dev.h")

#include "drivers/audio/espressif/es8388_audio.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <math.h>

#define TAG "es8388"

#define I2S_MCLK_PIN    GPIO_NUM_3
#define I2S_BCLK_PIN    GPIO_NUM_46
#define I2S_WS_PIN      GPIO_NUM_9
#define I2S_DOUT_PIN    GPIO_NUM_10
#define I2S_DIN_PIN     GPIO_NUM_14


#define ES8388_CODEC_DEFAULT_ADDR    (0x20)
#define ES8388_CODEC_DEFAULT_ADDR_1  (0x22)
#define CODEC_I2C_ADDR  ES8388_CODEC_DEFAULT_ADDR

#define SAMPLE_RATE     24000
#define BITS_PER_SAMPLE 16


static esp_codec_dev_handle_t s_output_dev;
static esp_codec_dev_handle_t s_input_dev;
static int s_pa_pin = -1;
static int s_initialized;

int es8388_audio_init(void *i2c_bus, int pa_pin)
{
    
    s_pa_pin = pa_pin;

    /* Configure PA GPIO */
    if (pa_pin >= 0) {
        gpio_config_t pa_cfg = {
            .pin_bit_mask = (1ULL << pa_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&pa_cfg);
        gpio_set_level(pa_pin, 0);

    }

    /* Create I2S channel (full-duplex) */
    i2s_chan_handle_t tx_handle = NULL;
    i2s_chan_handle_t rx_handle = NULL;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear_after_cb = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return -1;
    }

    /* I2S standard mode config */
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_WS_PIN,
            .dout = I2S_DOUT_PIN,
            .din  = I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    i2s_channel_init_std_mode(tx_handle, &std_cfg);
    i2s_channel_init_std_mode(rx_handle, &std_cfg);
    i2s_channel_enable(tx_handle);
    i2s_channel_enable(rx_handle);


    /*
     * es8388 needs MCLK running before it accepts I2C register
     * writes (probe ACKs the address but register writes NACK
     * without clock).  Wait for MCLK to stabilize.
     */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Create codec control interface (I2C) */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = CODEC_I2C_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if =
        audio_codec_new_i2c_ctrl(&i2c_cfg);

    if (!ctrl_if) {
        ESP_LOGE(TAG,"[ES8388] I2C 控制接口创建失败！\n");
        return -1;
    }

    /* Create codec data interface (I2S) */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
    };
    const audio_codec_data_if_t *data_if =
        audio_codec_new_i2s_data(&i2s_cfg);

    if (!data_if) {
        ESP_LOGE(TAG,"[ES8388] I2S 数据接口创建失败！\n");
        return -1;
    }

    /* Create GPIO interface for PA control */
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    /* Create es8388 codec */
    es8388_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = true,
        .pa_pin = (pa_pin >= 0) ? pa_pin : -1,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .pa_reverted = false,
    };
    const audio_codec_if_t *codec_if = es8388_codec_new(&es_cfg);

    /* ====================== 调试打印 ======================  */
    if (!codec_if) {
        ESP_LOGE(TAG, "es8388_codec_new failed");
        return -1;
    }

    esp_codec_dev_cfg_t out_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_output_dev = esp_codec_dev_new(&out_cfg);

    esp_codec_dev_cfg_t in_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_input_dev = esp_codec_dev_new(&in_cfg);

    esp_codec_set_disable_when_closed(s_output_dev, false);
    esp_codec_set_disable_when_closed(s_input_dev, false);

    if (!s_output_dev) {
        ESP_LOGE(TAG, "esp_codec_dev_new failed");
        return -1;
    }

    /* Open device with default sample format */
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = BITS_PER_SAMPLE,
        .channel = 1,
        .sample_rate = SAMPLE_RATE,
    };
    err = esp_codec_dev_open(s_output_dev, &fs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "codec open: %s", esp_err_to_name(err));
        return -1;
    }

    uint8_t reg_val = 30;
    uint8_t regs[] = {46, 47, 48, 49};
    for (int i = 0; i < 4; i++) {
        ctrl_if->write_reg(ctrl_if, regs[i], 1, &reg_val, 1);
    }

    esp_codec_dev_set_out_vol(s_output_dev, 70);
    esp_codec_dev_set_in_gain(s_input_dev, 24.0);

    s_initialized = 1;
    ESP_LOGI(TAG, "ES8388 ready (24kHz/16bit/mono, PA=GPIO)");

    ESP_LOGI(TAG,"[ES8388] ====  初始化成功 ====\n\n");

    return 0;
}


void es8388_audio_set_volume(int vol)
{
    if (s_initialized) {
        esp_codec_dev_set_out_vol(s_output_dev, vol);
    }
}

void es8388_audio_enable_output(int enable)
{
    if (enable) {
        /* Let PA stabilize before sending audio */
        vTaskDelay(pdMS_TO_TICKS(50));
    } else {
        /* Flush silence to drain I2S DMA before PA off */
        int16_t silence[240] = {0};
        es8388_audio_write(silence, 240);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void es8388_audio_write(const int16_t *data, size_t samples)
{
    if (!s_initialized || !data || samples == 0) {
        return;
    }
    esp_codec_dev_write(s_output_dev, (void *)data,
                        samples * sizeof(int16_t));
}

/*
 * Generate a sine-wave tone.  Caller must manage PA enable/disable
 * to avoid repeated PA toggling (pop noise) during melodies.
 */
static void generate_tone(int freq_hz, int duration_ms)
{
    int total_samples = SAMPLE_RATE * duration_ms / 1000;
    int fade_samples = SAMPLE_RATE / 100; /* 10ms fade */
    int16_t frame[240];
    int pos = 0;

    if (fade_samples > total_samples / 2) {
        fade_samples = total_samples / 2;
    }

    while (pos < total_samples) {
        int chunk = total_samples - pos;
        if (chunk > 240) {
            chunk = 240;
        }
        for (int i = 0; i < chunk; i++) {
            int s = pos + i;
            float t = (float)s / SAMPLE_RATE;
            float val = sinf(2.0f * 3.14159f * freq_hz * t);
            float env = 1.0f;
            if (s < fade_samples) {
                env = (float)s / fade_samples;
            } else if (s >= total_samples - fade_samples) {
                env = (float)(total_samples - 1 - s) / fade_samples;
            }
            frame[i] = (int16_t)(val * env * 16000);
        }
        es8388_audio_write(frame, chunk);
        pos += chunk;
    }
}

void es8388_audio_beep(int freq_hz, int duration_ms, int volume)
{
    if (!s_initialized) {
        return;
    }

    es8388_audio_set_volume(volume);
    es8388_audio_enable_output(1);
    generate_tone(freq_hz, duration_ms);
    es8388_audio_enable_output(0);
}

/* ---- Preset sound effects ---- */

typedef struct {
    int freq;
    int ms;
} note_t;

static void play_melody(const note_t *notes, int count, int vol)
{
    es8388_audio_set_volume(vol);
    es8388_audio_enable_output(1);

    for (int i = 0; i < count; i++) {
        if (notes[i].freq == 0) {
            vTaskDelay(pdMS_TO_TICKS(notes[i].ms));
        } else {
            generate_tone(notes[i].freq, notes[i].ms);
        }
    }

    es8388_audio_enable_output(0);
}

int es8388_audio_play_sound(const char *name)
{
    if (!s_initialized || !name) {
        return -1;
    }

    /* Success: ascending C5-E5 */
    if (strcmp(name, "success") == 0) {
        static const note_t melody[] = {
            {523, 120}, {0, 30}, {659, 200},
        };
        play_melody(melody, 3, 60);
        return 0;
    }

    /* Error: descending buzz */
    if (strcmp(name, "error") == 0) {
        static const note_t melody[] = {
            {400, 150}, {0, 30}, {250, 300},
        };
        play_melody(melody, 3, 60);
        return 0;
    }

    /* Notify: triple short beep */
    if (strcmp(name, "notify") == 0) {
        static const note_t melody[] = {
            {880, 80}, {0, 60},
            {880, 80}, {0, 60},
            {880, 80},
        };
        play_melody(melody, 5, 50);
        return 0;
    }

    /* Alert: urgent siren */
    if (strcmp(name, "alert") == 0) {
        static const note_t melody[] = {
            {1200, 150}, {800, 150},
            {1200, 150}, {800, 150},
            {1200, 150}, {800, 150},
        };
        play_melody(melody, 6, 70);
        return 0;
    }

    /* Startup: musical boot jingle C-E-G-C' */
    if (strcmp(name, "startup") == 0) {
        static const note_t melody[] = {
            {523, 100}, {0, 20},
            {659, 100}, {0, 20},
            {784, 100}, {0, 20},
            {1047, 200},
        };
        play_melody(melody, 7, 55);
        return 0;
    }

    /* Click: short tick */
    if (strcmp(name, "click") == 0) {
        static const note_t melody[] = {
            {1500, 30},
        };
        play_melody(melody, 1, 40);
        return 0;
    }

    return -1;
}

#else /* stubs — no esp_codec_dev or non-ESP-IDF */



#endif

/*
 * OOP driver registration — only on ESP-IDF with codec support.
 * Falls through to no-op on other platforms (stubs above).
 */
#if defined(CLAW_PLATFORM_ESP_IDF) && __has_include("esp_codec_dev.h")

#include "claw/core/driver.h"

static claw_err_t es8388_drv_probe(struct claw_driver *drv)
{
    (void)drv;
    return CLAW_OK;
}

static const struct claw_driver_ops es8388_drv_ops = {
    .probe  = es8388_drv_probe,
    .remove = NULL,
};

static struct claw_driver es8388_drv = {
    .name  = "es8388_audio",
    .ops   = &es8388_drv_ops,
    .state = CLAW_DRV_REGISTERED,
};

CLAW_DRIVER_REGISTER(es8388_audio, &es8388_drv);
#endif
