/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Platform entry for ESP32-S3 real hardware (WiFi + PSRAM).
 * Chat-first shell: direct input goes to AI, /commands for system.
 */

#include "osal/claw_os.h"
#include "claw/claw_init.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/shell/shell_commands.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#ifdef CLAW_PLATFORM_ESP_IDF
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_wifi.h"
#endif

#ifdef CONFIG_RTCLAW_SHELL_ENABLE
#include "driver/uart.h"
#endif

#define TAG         "main"
#define REPLY_SIZE  4096
#define INPUT_SIZE  256
#define MAX_ARGS    8

#ifdef CONFIG_RTCLAW_SHELL_ENABLE

static char *s_reply;

/*
 * Read one line from UART with raw byte echo.
 * UTF-8 multi-byte characters (Chinese etc.) pass through correctly.
 */
static int uart_read_line(char *buf, int size)
{
    int pos = 0;
    uint8_t ch;

    while (pos < size - 1) {
        int n = uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY);

        if (n <= 0) {
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            uart_write_bytes(UART_NUM_0, "\r\n", 2);
            /* Consume trailing \n after \r (or vice versa) */
            uint8_t trail;
            uart_read_bytes(UART_NUM_0, &trail, 1, pdMS_TO_TICKS(20));
            break;
        }
        if (ch == '\b' || ch == 127) {
            if (pos > 0) {
                pos--;
                while (pos > 0 && (buf[pos] & 0xC0) == 0x80) {
                    pos--;
                }
                if ((uint8_t)buf[pos] >= 0xE0) {
                    /* 3/4-byte UTF-8 (CJK, emoji): 2 columns */
                    uart_write_bytes(UART_NUM_0,
                                     "\b \b\b \b", 6);
                } else {
                    uart_write_bytes(UART_NUM_0, "\b \b", 3);
                }
            }
            continue;
        }
        buf[pos++] = (char)ch;
        uart_write_bytes(UART_NUM_0, &ch, 1);
    }
    buf[pos] = '\0';
    return pos;
}

/* ---- Platform-specific commands (WiFi) ---- */

static void cmd_wifi_set(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: /wifi_set <SSID> <PASSWORD>\n");
        return;
    }
    wifi_manager_set_credentials(argv[1], argv[2]);
    printf("WiFi credentials saved. Reconnecting...\n");
    esp_wifi_disconnect();
}

static void cmd_wifi_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("WiFi: %s\n",
           wifi_manager_is_connected() ? "connected" : "disconnected");
    printf("IP:   %s\n", wifi_manager_get_ip());
}

static void cmd_wifi_scan(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    wifi_manager_scan_and_print();
}

/* ---- Command tables ---- */

static void cmd_help(int argc, char **argv);

static const shell_cmd_t s_platform_commands[] = {
    SHELL_CMD("/help",        cmd_help,        "Show this help"),
    SHELL_CMD("/wifi_set",    cmd_wifi_set,    "Save WiFi credentials"),
    SHELL_CMD("/wifi_status", cmd_wifi_status, "Show WiFi connection"),
    SHELL_CMD("/wifi_scan",   cmd_wifi_scan,   "Scan nearby APs"),
};

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_print_help(s_platform_commands,
                     SHELL_CMD_COUNT(s_platform_commands));
    shell_print_help(shell_common_commands,
                     shell_common_command_count());
    printf("\n  Anything else is sent directly to AI.\n");
}

/* Dispatch a /command */
static void dispatch_command(char *line)
{
    char *argv[MAX_ARGS];
    int argc = shell_tokenize(line, argv, MAX_ARGS);

    if (argc == 0) {
        return;
    }
    if (shell_dispatch(s_platform_commands,
                       SHELL_CMD_COUNT(s_platform_commands),
                       argc, argv)) {
        return;
    }
    if (shell_dispatch(shell_common_commands,
                       shell_common_command_count(),
                       argc, argv)) {
        return;
    }
    printf("Unknown command: %s (type /help)\n", argv[0]);
}

/* ---- Thinking animation ---- */

static volatile int s_anim_active;
static volatile int s_anim_phase; /* 0=thinking, 1=tool */

static void anim_thread_fn(void *arg)
{
    (void)arg;
    int dots = 0;
    const char *dot_str[] = { ".", "..", "..." };

    while (s_anim_active) {
        if (s_anim_phase == 0) {
            printf("\r  " CLR_MAGENTA "thinking %s"
                   CLR_RESET "   ", dot_str[dots]);
            fflush(stdout);
            dots = (dots + 1) % 3;
        }
        claw_thread_delay_ms(500);
    }
}

static void chat_status_cb(int status, const char *detail)
{
    if (status == AI_STATUS_THINKING) {
        s_anim_phase = 0;
    } else if (status == AI_STATUS_TOOL_CALL) {
        s_anim_phase = 1;
        printf("\r  " CLR_YELLOW "► %s" CLR_RESET
               "                    \n",
               detail ? detail : "?");
        fflush(stdout);
    } else if (status == AI_STATUS_DONE) {
        s_anim_active = 0;
    }
}

/* Chat with AI (direct input) */
static void do_chat(const char *msg)
{
    s_anim_active = 1;
    s_anim_phase = 0;
    ai_set_status_cb(chat_status_cb);

    claw_thread_create("anim", anim_thread_fn, NULL, 2048, 20);

    int ret = ai_chat(msg, s_reply, REPLY_SIZE);

    s_anim_active = 0;
    ai_set_status_cb(NULL);
    claw_thread_delay_ms(100);
    /* Clear animation line */
    printf("\r                              \r");

    if (ret == CLAW_OK) {
        printf(CLR_GREEN "<rt-claw> " CLR_RESET "%s\n", s_reply);
    } else {
        printf(CLR_RED "<error> " CLR_RESET "%s\n", s_reply);
    }
}

static void shell_loop(void)
{
    char input[INPUT_SIZE];

    /* Install UART driver (required for uart_read_bytes) */
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);

    s_reply = claw_malloc(REPLY_SIZE);
    if (!s_reply) {
        CLAW_LOGE(TAG, "no memory for reply buffer");
        return;
    }

    printf("\n");
    printf(CLR_CYAN "  rt-claw chat" CLR_RESET
           "  (type /help for commands)\n");
    printf("  Direct input sends to AI, /command for system.\n");
    printf("  WiFi: %s (%s)\n",
           wifi_manager_is_connected() ? "connected" : "offline",
           wifi_manager_get_ip());
    printf("\n");

    while (1) {
        printf("\n" CLR_CYAN "<You> " CLR_RESET);
        fflush(stdout);
        int len = uart_read_line(input, sizeof(input));

        if (len == 0) {
            continue;
        }

        if (input[0] == '/') {
            dispatch_command(input);
        } else {
            do_chat(input);
        }
    }
}

#endif /* CONFIG_RTCLAW_SHELL_ENABLE */

void app_main(void)
{
#ifdef CLAW_PLATFORM_ESP_IDF
    /* Initialize NVS flash */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS truncated, erasing...");
        nvs_flash_erase();
        nvs_flash_init();
    }

#ifdef CONFIG_RTCLAW_SHELL_ENABLE
    /* Shell mode: suppress log by default; use /log on to enable */
    esp_log_level_set("*", ESP_LOG_NONE);
#else
    /* Headless mode: enable log for serial diagnostics */
    claw_log_set_enabled(1);
#endif
#endif

    /* WiFi initialization */
    wifi_manager_init();
    esp_err_t wifi_err = wifi_manager_start();
    if (wifi_err == ESP_OK) {
        CLAW_LOGI(TAG, "waiting for WiFi (30s timeout)...");
        if (wifi_manager_wait_connected(30000) == ESP_OK) {
            CLAW_LOGI(TAG, "WiFi connected: %s",
                      wifi_manager_get_ip());
        } else {
            CLAW_LOGW(TAG, "WiFi timeout, continuing offline");
        }
    } else {
        CLAW_LOGW(TAG, "no WiFi credentials, use /wifi_set");
    }

    /*
     * Load runtime config from NVS (before claw_init so services
     * pick up NVS values instead of compile-time defaults).
     */
    shell_nvs_config_load();

    /* Boot rt-claw services */
    claw_init();

#ifdef CONFIG_RTCLAW_SHELL_ENABLE
    shell_loop();
#endif

    /* Keep main task alive */
    while (1) {
        claw_thread_delay_ms(1000);
    }
}
