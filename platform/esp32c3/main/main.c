/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * Platform entry for ESP32-C3 / ESP-IDF + FreeRTOS.
 */

#include "claw_os.h"
#include "claw_init.h"
#include "ai_engine.h"

#include <stdio.h>
#include <string.h>

#ifdef CLAW_PLATFORM_ESP_IDF
#include "esp_console.h"
#include "driver/uart.h"
#endif

#define TAG         "main"
#define REPLY_SIZE  4096

#ifdef CLAW_PLATFORM_ESP_IDF

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
            break;
        }
        if (ch == '\b' || ch == 127) {
            if (pos > 0) {
                /* UTF-8 backspace: remove trailing continuation bytes */
                pos--;
                while (pos > 0 && (buf[pos] & 0xC0) == 0x80) {
                    pos--;
                }
                /* Erase character on screen */
                uart_write_bytes(UART_NUM_0, "\b \b", 3);
            }
            continue;
        }
        buf[pos++] = (char)ch;
        uart_write_bytes(UART_NUM_0, &ch, 1);
    }
    buf[pos] = '\0';
    return pos;
}

/* "ask" command — send message to AI */
static int cmd_ask(int argc, char **argv)
{
    char msg[256];

    if (argc < 2) {
        /* Interactive mode: raw UART read for UTF-8 support */
        printf("You> ");
        if (uart_read_line(msg, sizeof(msg)) == 0) {
            return 0;
        }
    } else {
        /* Inline mode: concatenate args */
        int off = 0;

        for (int i = 1; i < argc && off < (int)sizeof(msg) - 1; i++) {
            if (i > 1) {
                msg[off++] = ' ';
            }
            int n = snprintf(msg + off, sizeof(msg) - off,
                             "%s", argv[i]);
            off += n;
        }
    }

    if (ai_chat(msg, s_reply, REPLY_SIZE) == CLAW_OK) {
        printf("\nAssistant> %s\n", s_reply);
    } else {
        printf("\n[error] %s\n", s_reply);
    }
    return 0;
}

static void register_commands(void)
{
    const esp_console_cmd_t ask_cmd = {
        .command = "ask",
        .help = "Chat with AI. 'ask' for interactive input, or 'ask <msg>'",
        .func = &cmd_ask,
    };
    esp_console_cmd_register(&ask_cmd);
    esp_console_register_help_command();
}

static void console_start(void)
{
    s_reply = claw_malloc(REPLY_SIZE);
    if (!s_reply) {
        CLAW_LOGE(TAG, "no memory for reply buffer");
        return;
    }

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config =
        ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "rt-claw> ";
    repl_config.task_stack_size = 8192;

    esp_console_dev_uart_config_t uart_config =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    register_commands();

    printf("\n");
    printf("  rt-claw shell  (type 'help' for commands)\n");
    printf("  AI chat:  ask <your message>\n");
    printf("\n");

    esp_console_start_repl(repl);
}

#endif /* CLAW_PLATFORM_ESP_IDF */

void app_main(void)
{
    claw_init();

#ifdef CLAW_PLATFORM_ESP_IDF
    console_start();
#endif

    /* Keep main task alive */
    while (1) {
        claw_thread_delay_ms(1000);
    }
}
