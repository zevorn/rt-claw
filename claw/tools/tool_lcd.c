/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LCD tools — framebuffer drawing on QEMU virtual display.
 * Uses esp_lcd_qemu_rgb with direct framebuffer access.
 */

#include "claw/tools/claw_tools.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "tool_lcd"

#if defined(CLAW_PLATFORM_ESP_IDF) && defined(CONFIG_RTCLAW_LCD_ENABLE)

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_qemu_rgb.h"

#define LCD_WIDTH   320
#define LCD_HEIGHT  240
#define LCD_BPP     16  /* RGB565 */

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb;      /* QEMU framebuffer pointer */
static int s_lcd_active;    /* Skip rendering until first tool use */

/* -------------------- 8x8 ASCII font (chars 32-126) -------------------- */

static const uint8_t font8x8[][8] = {
    /* 32 ' ' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 33 '!' */ {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    /* 34 '"' */ {0x36, 0x36, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 35 '#' */ {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    /* 36 '$' */ {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},
    /* 37 '%' */ {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
    /* 38 '&' */ {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},
    /* 39 ''' */ {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 40 '(' */ {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},
    /* 41 ')' */ {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
    /* 42 '*' */ {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    /* 43 '+' */ {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
    /* 44 ',' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    /* 45 '-' */ {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
    /* 46 '.' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    /* 47 '/' */ {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
    /* 48-57: 0-9 */
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x30, 0x00},
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
    /* 58 ':' */ {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    /* 59 ';' */ {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    /* 60 '<' */ {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},
    /* 61 '=' */ {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
    /* 62 '>' */ {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
    /* 63 '?' */ {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
    /* 64 '@' */ {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},
    /* 65-90: A-Z */
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},
    /* 91 '[' */ {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
    /* 92 '\' */ {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},
    /* 93 ']' */ {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
    /* 94 '^' */ {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},
    /* 95 '_' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    /* 96 '`' */ {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 97-122: a-z */
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
    {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00},
    {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00},
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},
    /* 123 '{' */ {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
    /* 124 '|' */ {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
    /* 125 '}' */ {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
    /* 126 '~' */ {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

/* -------------------- Color helpers -------------------- */

static uint16_t rgb888_to_565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static uint16_t parse_color(const char *s)
{
    if (!s) {
        return 0xFFFF; /* white */
    }

    /* Named colors */
    if (strcmp(s, "black") == 0) {
        return 0x0000;
    } else if (strcmp(s, "white") == 0) {
        return 0xFFFF;
    } else if (strcmp(s, "red") == 0) {
        return rgb888_to_565(255, 0, 0);
    } else if (strcmp(s, "green") == 0) {
        return rgb888_to_565(0, 255, 0);
    } else if (strcmp(s, "blue") == 0) {
        return rgb888_to_565(0, 0, 255);
    } else if (strcmp(s, "yellow") == 0) {
        return rgb888_to_565(255, 255, 0);
    } else if (strcmp(s, "cyan") == 0) {
        return rgb888_to_565(0, 255, 255);
    } else if (strcmp(s, "magenta") == 0) {
        return rgb888_to_565(255, 0, 255);
    } else if (strcmp(s, "orange") == 0) {
        return rgb888_to_565(255, 165, 0);
    } else if (strcmp(s, "gray") == 0) {
        return rgb888_to_565(128, 128, 128);
    }

    /* #RRGGBB hex */
    if (s[0] == '#' && strlen(s) == 7) {
        unsigned int hex;
        if (sscanf(s + 1, "%06x", &hex) == 1) {
            return rgb888_to_565((hex >> 16) & 0xFF,
                                (hex >> 8) & 0xFF,
                                hex & 0xFF);
        }
    }

    return 0xFFFF; /* default white */
}

/* -------------------- Drawing primitives -------------------- */

static inline void put_pixel(int x, int y, uint16_t color)
{
    if (x >= 0 && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT) {
        s_fb[y * LCD_WIDTH + x] = color;
    }
}

static void draw_hline(int x, int y, int w, uint16_t color)
{
    for (int i = 0; i < w; i++) {
        put_pixel(x + i, y, color);
    }
}

static void lcd_fill(uint16_t color)
{
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
        s_fb[i] = color;
    }
    esp_lcd_rgb_qemu_refresh(s_panel);
}

static void lcd_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg,
                          int scale)
{
    int idx = c - 32;
    if (idx < 0 || idx >= (int)(sizeof(font8x8) / sizeof(font8x8[0]))) {
        idx = 0; /* space for unprintable */
    }

    for (int row = 0; row < 8; row++) {
        uint8_t bits = font8x8[idx][row];
        for (int col = 0; col < 8; col++) {
            uint16_t clr = (bits & (1 << col)) ? fg : bg;
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    put_pixel(x + col * scale + sx,
                              y + row * scale + sy, clr);
                }
            }
        }
    }
}

static void lcd_draw_text(int x, int y, const char *text, uint16_t fg,
                          uint16_t bg, int scale)
{
    int cx = x;
    int cy = y;
    int char_w = 8 * scale;
    int char_h = 8 * scale;

    while (*text) {
        if (*text == '\n') {
            cx = x;
            cy += char_h;
        } else {
            lcd_draw_char(cx, cy, *text, fg, bg, scale);
            cx += char_w;
            if (cx + char_w > LCD_WIDTH) {
                cx = x;
                cy += char_h;
            }
        }
        text++;
    }
    esp_lcd_rgb_qemu_refresh(s_panel);
}

static void lcd_draw_rect(int x, int y, int w, int h, uint16_t color,
                          int filled)
{
    if (filled) {
        for (int row = y; row < y + h; row++) {
            draw_hline(x, row, w, color);
        }
    } else {
        draw_hline(x, y, w, color);
        draw_hline(x, y + h - 1, w, color);
        for (int row = y; row < y + h; row++) {
            put_pixel(x, row, color);
            put_pixel(x + w - 1, row, color);
        }
    }
    esp_lcd_rgb_qemu_refresh(s_panel);
}

static void lcd_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    /* Bresenham's line algorithm */
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    esp_lcd_rgb_qemu_refresh(s_panel);
}

static void lcd_draw_circle(int cx, int cy, int r, uint16_t color,
                            int filled)
{
    /* Midpoint circle algorithm */
    int x = 0;
    int y = r;
    int d = 1 - r;

    while (x <= y) {
        if (filled) {
            draw_hline(cx - x, cy + y, 2 * x + 1, color);
            draw_hline(cx - x, cy - y, 2 * x + 1, color);
            draw_hline(cx - y, cy + x, 2 * y + 1, color);
            draw_hline(cx - y, cy - x, 2 * y + 1, color);
        } else {
            put_pixel(cx + x, cy + y, color);
            put_pixel(cx - x, cy + y, color);
            put_pixel(cx + x, cy - y, color);
            put_pixel(cx - x, cy - y, color);
            put_pixel(cx + y, cy + x, color);
            put_pixel(cx - y, cy + x, color);
            put_pixel(cx + y, cy - x, color);
            put_pixel(cx - y, cy - x, color);
        }
        x++;
        if (d < 0) {
            d += 2 * x + 1;
        } else {
            y--;
            d += 2 * (x - y) + 1;
        }
    }
    esp_lcd_rgb_qemu_refresh(s_panel);
}

/* -------------------- LCD initialization -------------------- */

int claw_lcd_init(void)
{
    esp_lcd_rgb_qemu_config_t cfg = {
        .width = LCD_WIDTH,
        .height = LCD_HEIGHT,
        .bpp = RGB_QEMU_BPP_16,
    };

    esp_err_t err = esp_lcd_new_rgb_qemu(&cfg, &s_panel);
    if (err != ESP_OK) {
        CLAW_LOGW(TAG, "QEMU LCD not available (real hardware?)");
        return CLAW_ERROR;
    }

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);

    void *fb = NULL;
    esp_lcd_rgb_qemu_get_frame_buffer(s_panel, &fb);
    s_fb = (uint16_t *)fb;

    CLAW_LOGI(TAG, "LCD initialized (%dx%d RGB565)", LCD_WIDTH, LCD_HEIGHT);

    /*
     * Skip initial fill and text drawing at boot — QEMU MMIO writes
     * are extremely slow (>10min for 320x240 fill). LCD tools still
     * work on demand after boot completes.
     */
    return CLAW_OK;
}

/* -------------------- Tool implementations -------------------- */

static int tool_lcd_fill(const cJSON *params, cJSON *result)
{
    if (!s_fb) {
        cJSON_AddStringToObject(result, "error", "LCD not initialized");
        return CLAW_ERROR;
    }
    s_lcd_active = 1;

    cJSON *color_j = cJSON_GetObjectItem(params, "color");
    const char *color_str = (color_j && cJSON_IsString(color_j))
                            ? color_j->valuestring : "black";

    lcd_fill(parse_color(color_str));
    cJSON_AddStringToObject(result, "status", "ok");
    char msg[64];
    snprintf(msg, sizeof(msg), "Screen filled with %s", color_str);
    cJSON_AddStringToObject(result, "message", msg);
    CLAW_LOGI(TAG, "%s", msg);
    return CLAW_OK;
}

static int tool_lcd_text(const cJSON *params, cJSON *result)
{
    if (!s_fb) {
        cJSON_AddStringToObject(result, "error", "LCD not initialized");
        return CLAW_ERROR;
    }
    s_lcd_active = 1;

    cJSON *x_j = cJSON_GetObjectItem(params, "x");
    cJSON *y_j = cJSON_GetObjectItem(params, "y");
    cJSON *text_j = cJSON_GetObjectItem(params, "text");
    cJSON *color_j = cJSON_GetObjectItem(params, "color");
    cJSON *bg_j = cJSON_GetObjectItem(params, "bg_color");
    cJSON *size_j = cJSON_GetObjectItem(params, "size");

    if (!text_j || !cJSON_IsString(text_j)) {
        cJSON_AddStringToObject(result, "error", "missing text");
        return CLAW_ERROR;
    }

    int x = (x_j && cJSON_IsNumber(x_j)) ? x_j->valueint : 0;
    int y = (y_j && cJSON_IsNumber(y_j)) ? y_j->valueint : 0;
    int scale = (size_j && cJSON_IsNumber(size_j)) ? size_j->valueint : 1;
    if (scale < 1) {
        scale = 1;
    } else if (scale > 4) {
        scale = 4;
    }

    const char *color_str = (color_j && cJSON_IsString(color_j))
                            ? color_j->valuestring : "white";
    const char *bg_str = (bg_j && cJSON_IsString(bg_j))
                         ? bg_j->valuestring : "black";

    lcd_draw_text(x, y, text_j->valuestring,
                  parse_color(color_str), parse_color(bg_str), scale);

    cJSON_AddStringToObject(result, "status", "ok");
    char msg[128];
    snprintf(msg, sizeof(msg), "Text \"%.*s\" drawn at (%d,%d)",
             30, text_j->valuestring, x, y);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static int tool_lcd_rect(const cJSON *params, cJSON *result)
{
    if (!s_fb) {
        cJSON_AddStringToObject(result, "error", "LCD not initialized");
        return CLAW_ERROR;
    }

    cJSON *x_j = cJSON_GetObjectItem(params, "x");
    cJSON *y_j = cJSON_GetObjectItem(params, "y");
    cJSON *w_j = cJSON_GetObjectItem(params, "width");
    cJSON *h_j = cJSON_GetObjectItem(params, "height");
    cJSON *color_j = cJSON_GetObjectItem(params, "color");
    cJSON *filled_j = cJSON_GetObjectItem(params, "filled");

    int x = (x_j && cJSON_IsNumber(x_j)) ? x_j->valueint : 0;
    int y = (y_j && cJSON_IsNumber(y_j)) ? y_j->valueint : 0;
    int w = (w_j && cJSON_IsNumber(w_j)) ? w_j->valueint : 50;
    int h = (h_j && cJSON_IsNumber(h_j)) ? h_j->valueint : 50;
    const char *color_str = (color_j && cJSON_IsString(color_j))
                            ? color_j->valuestring : "white";
    int filled = (filled_j && cJSON_IsBool(filled_j))
                 ? cJSON_IsTrue(filled_j) : 0;

    lcd_draw_rect(x, y, w, h, parse_color(color_str), filled);
    cJSON_AddStringToObject(result, "status", "ok");
    return CLAW_OK;
}

static int tool_lcd_line(const cJSON *params, cJSON *result)
{
    if (!s_fb) {
        cJSON_AddStringToObject(result, "error", "LCD not initialized");
        return CLAW_ERROR;
    }

    cJSON *x1_j = cJSON_GetObjectItem(params, "x1");
    cJSON *y1_j = cJSON_GetObjectItem(params, "y1");
    cJSON *x2_j = cJSON_GetObjectItem(params, "x2");
    cJSON *y2_j = cJSON_GetObjectItem(params, "y2");
    cJSON *color_j = cJSON_GetObjectItem(params, "color");

    int x1 = (x1_j && cJSON_IsNumber(x1_j)) ? x1_j->valueint : 0;
    int y1 = (y1_j && cJSON_IsNumber(y1_j)) ? y1_j->valueint : 0;
    int x2 = (x2_j && cJSON_IsNumber(x2_j)) ? x2_j->valueint : 100;
    int y2 = (y2_j && cJSON_IsNumber(y2_j)) ? y2_j->valueint : 100;
    const char *color_str = (color_j && cJSON_IsString(color_j))
                            ? color_j->valuestring : "white";

    lcd_draw_line(x1, y1, x2, y2, parse_color(color_str));
    cJSON_AddStringToObject(result, "status", "ok");
    return CLAW_OK;
}

static int tool_lcd_circle(const cJSON *params, cJSON *result)
{
    if (!s_fb) {
        cJSON_AddStringToObject(result, "error", "LCD not initialized");
        return CLAW_ERROR;
    }

    cJSON *x_j = cJSON_GetObjectItem(params, "x");
    cJSON *y_j = cJSON_GetObjectItem(params, "y");
    cJSON *r_j = cJSON_GetObjectItem(params, "radius");
    cJSON *color_j = cJSON_GetObjectItem(params, "color");
    cJSON *filled_j = cJSON_GetObjectItem(params, "filled");

    int x = (x_j && cJSON_IsNumber(x_j)) ? x_j->valueint : 160;
    int y = (y_j && cJSON_IsNumber(y_j)) ? y_j->valueint : 120;
    int r = (r_j && cJSON_IsNumber(r_j)) ? r_j->valueint : 30;
    const char *color_str = (color_j && cJSON_IsString(color_j))
                            ? color_j->valuestring : "white";
    int filled = (filled_j && cJSON_IsBool(filled_j))
                 ? cJSON_IsTrue(filled_j) : 0;

    lcd_draw_circle(x, y, r, parse_color(color_str), filled);
    cJSON_AddStringToObject(result, "status", "ok");
    return CLAW_OK;
}

/* -------------------- JSON schemas -------------------- */

static const char schema_lcd_fill[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"color\":{\"type\":\"string\","
    "\"description\":\"Color name (red,green,blue,white,black,yellow,cyan,"
    "magenta,orange,gray) or #RRGGBB hex\"}},"
    "\"required\":[\"color\"]}";

static const char schema_lcd_text[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"x\":{\"type\":\"integer\",\"description\":\"X position in pixels\"},"
    "\"y\":{\"type\":\"integer\",\"description\":\"Y position in pixels\"},"
    "\"text\":{\"type\":\"string\",\"description\":\"ASCII text to display\"},"
    "\"color\":{\"type\":\"string\",\"description\":\"Text color name or #RRGGBB\"},"
    "\"bg_color\":{\"type\":\"string\",\"description\":\"Background color\"},"
    "\"size\":{\"type\":\"integer\",\"description\":\"Font scale 1-4 (1=8px)\"}},"
    "\"required\":[\"text\"]}";

static const char schema_lcd_rect[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"x\":{\"type\":\"integer\",\"description\":\"X position\"},"
    "\"y\":{\"type\":\"integer\",\"description\":\"Y position\"},"
    "\"width\":{\"type\":\"integer\",\"description\":\"Rectangle width\"},"
    "\"height\":{\"type\":\"integer\",\"description\":\"Rectangle height\"},"
    "\"color\":{\"type\":\"string\",\"description\":\"Color name or #RRGGBB\"},"
    "\"filled\":{\"type\":\"boolean\",\"description\":\"true=filled, false=outline\"}},"
    "\"required\":[\"x\",\"y\",\"width\",\"height\"]}";

static const char schema_lcd_line[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"x1\":{\"type\":\"integer\"},\"y1\":{\"type\":\"integer\"},"
    "\"x2\":{\"type\":\"integer\"},\"y2\":{\"type\":\"integer\"},"
    "\"color\":{\"type\":\"string\",\"description\":\"Color name or #RRGGBB\"}},"
    "\"required\":[\"x1\",\"y1\",\"x2\",\"y2\"]}";

static const char schema_lcd_circle[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"x\":{\"type\":\"integer\",\"description\":\"Center X\"},"
    "\"y\":{\"type\":\"integer\",\"description\":\"Center Y\"},"
    "\"radius\":{\"type\":\"integer\",\"description\":\"Radius in pixels\"},"
    "\"color\":{\"type\":\"string\",\"description\":\"Color name or #RRGGBB\"},"
    "\"filled\":{\"type\":\"boolean\",\"description\":\"true=filled, false=outline\"}},"
    "\"required\":[\"x\",\"y\",\"radius\"]}";

/* -------------------- Hardware detection -------------------- */

int claw_lcd_available(void)
{
    return (s_fb != NULL) ? 1 : 0;
}

/* -------------------- Registration -------------------- */

void claw_tools_register_lcd(void)
{
    if (!claw_lcd_available()) {
        CLAW_LOGW(TAG, "LCD not available, skipping tool registration");
        return;
    }

    claw_tool_register("lcd_fill",
        "Fill the entire LCD screen (320x240) with a solid color.",
        schema_lcd_fill, tool_lcd_fill);

    claw_tool_register("lcd_text",
        "Draw ASCII text on the LCD at position (x,y). "
        "Size 1=8px, 2=16px, 3=24px. Screen is 320x240 pixels.",
        schema_lcd_text, tool_lcd_text);

    claw_tool_register("lcd_rect",
        "Draw a rectangle on the LCD. Set filled=true for solid fill.",
        schema_lcd_rect, tool_lcd_rect);

    claw_tool_register("lcd_line",
        "Draw a line on the LCD from (x1,y1) to (x2,y2).",
        schema_lcd_line, tool_lcd_line);

    claw_tool_register("lcd_circle",
        "Draw a circle on the LCD at center (x,y) with given radius.",
        schema_lcd_circle, tool_lcd_circle);
}

/* -------------------- Status bar API -------------------- */

#define STATUS_Y    220
#define STATUS_H    20
#define BAR_Y       232
#define BAR_H       6
#define BAR_X       4
#define BAR_W       (LCD_WIDTH - 8)

void claw_lcd_status(const char *msg)
{
    if (!msg) {
        return;
    }

    /* Skip slow MMIO framebuffer writes until user explicitly uses LCD */
    if (!s_fb || !s_lcd_active) {
        return;
    }

    /* Clear status strip */
    lcd_draw_rect(0, STATUS_Y, LCD_WIDTH, STATUS_H,
                  rgb888_to_565(0, 0, 40), 1);

    lcd_draw_text(BAR_X, STATUS_Y + 2, msg,
                  rgb888_to_565(0, 220, 255),
                  rgb888_to_565(0, 0, 40), 1);
}

void claw_lcd_progress(int percent)
{
    if (!s_fb || !s_lcd_active) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    /* Background bar (dark gray) */
    lcd_draw_rect(BAR_X, BAR_Y, BAR_W, BAR_H,
                  rgb888_to_565(40, 40, 40), 1);

    /* Filled portion (cyan) */
    int filled_w = BAR_W * percent / 100;

    if (filled_w > 0) {
        lcd_draw_rect(BAR_X, BAR_Y, filled_w, BAR_H,
                      rgb888_to_565(0, 200, 255), 1);
    }
}

#else /* non-ESP-IDF */

int claw_lcd_init(void)
{
    return CLAW_ERROR;
}

int claw_lcd_available(void)
{
    return 0;
}

void claw_tools_register_lcd(void)
{
}

void claw_lcd_status(const char *msg)
{
    (void)msg;
}

void claw_lcd_progress(int percent)
{
    (void)percent;
}

#endif
