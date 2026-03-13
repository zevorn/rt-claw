/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * Platform entry for ESP32-S3 / ESP-IDF + FreeRTOS.
 * Chat-first shell: direct input goes to AI, /commands for system.
 */

#include "claw_os.h"
#include "claw_init.h"
#include "ai_engine.h"

#include <stdio.h>
#include <string.h>

#ifdef CLAW_PLATFORM_ESP_IDF
#include "nvs_flash.h"
#include "esp_log.h"
#endif

#ifdef CONFIG_CLAW_SHELL_ENABLE
#include "ai_memory.h"
#include "driver/uart.h"
#ifdef CONFIG_CLAW_SKILL_ENABLE
#include "ai_skill.h"
#endif
#ifdef CONFIG_CLAW_SCHED_ENABLE
#include "scheduler.h"
#endif
#ifdef CONFIG_CLAW_SWARM_ENABLE
#include "swarm.h"
#endif
#endif /* CONFIG_CLAW_SHELL_ENABLE */

#define TAG         "main"
#define REPLY_SIZE  4096
#define INPUT_SIZE  256
#define MAX_ARGS    8

/* ANSI color codes */
#define CLR_RESET   "\033[0m"
#define CLR_RED     "\033[0;31m"
#define CLR_GREEN   "\033[0;32m"
#define CLR_YELLOW  "\033[0;33m"
#define CLR_CYAN    "\033[0;36m"
#define CLR_MAGENTA "\033[0;35m"

#ifdef CONFIG_CLAW_SHELL_ENABLE

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
                    uart_write_bytes(UART_NUM_0, "\b \b\b \b", 6);
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

/* Split string into argv-style tokens (modifies input in place) */
static int tokenize(char *line, char **argv, int max_args)
{
    int argc = 0;

    while (*line && argc < max_args) {
        while (*line == ' ') {
            line++;
        }
        if (*line == '\0') {
            break;
        }
        argv[argc++] = line;
        while (*line && *line != ' ') {
            line++;
        }
        if (*line) {
            *line++ = '\0';
        }
    }
    return argc;
}

/* ---- Slash command handlers ---- */

static void cmd_help(void)
{
    printf("  /help                    Show this help\n");
    printf("  /log [on|off]            Toggle log output\n");
    printf("  /history                 Show conversation message count\n");
    printf("  /clear                   Clear conversation memory\n");
#ifdef CONFIG_CLAW_SKILL_ENABLE
    printf("  /skill [name] [args]     List or execute a skill\n");
#endif
#ifdef CONFIG_CLAW_SCHED_ENABLE
    printf("  /sched                   List scheduled tasks\n");
#endif
#ifdef CONFIG_CLAW_SWARM_ENABLE
    printf("  /nodes                   Show swarm node table\n");
#endif
    printf("  /remember <key> <val>    Save to long-term memory\n");
    printf("  /forget <key>            Delete from long-term memory\n");
    printf("  /memories                List long-term memories\n");
    printf("\n  Anything else is sent directly to AI.\n");
}

static void cmd_history(void)
{
    printf("Conversation memory: %d messages\n", ai_memory_count());
}

static void cmd_clear(void)
{
    ai_memory_clear();
    printf("Conversation memory cleared.\n");
}

#ifdef CONFIG_CLAW_SCHED_ENABLE
static void cmd_sched(void)
{
    sched_list();
}
#endif

#ifdef CONFIG_CLAW_SKILL_ENABLE
static void cmd_skill(int argc, char **argv)
{
    if (argc < 2) {
        ai_skill_list();
        return;
    }

    char params[256] = "";
    int off = 0;

    for (int i = 2; i < argc && off < (int)sizeof(params) - 1; i++) {
        if (i > 2) {
            params[off++] = ' ';
        }
        off += snprintf(params + off, sizeof(params) - off, "%s", argv[i]);
    }

    if (ai_skill_execute(argv[1], params, s_reply, REPLY_SIZE) == CLAW_OK) {
        printf("\n" CLR_GREEN "<rt-claw> " CLR_RESET "%s\n", s_reply);
    } else {
        printf("\n" CLR_RED "<error> " CLR_RESET "%s\n", s_reply);
    }
}
#endif

static void cmd_remember(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: /remember <key> <value...>\n");
        return;
    }

    char value[128] = "";
    int off = 0;

    for (int i = 2; i < argc && off < (int)sizeof(value) - 1; i++) {
        if (i > 2) {
            value[off++] = ' ';
        }
        off += snprintf(value + off, sizeof(value) - off, "%s", argv[i]);
    }

    if (ai_ltm_save(argv[1], value) == CLAW_OK) {
        printf("Remembered: %s = %s\n", argv[1], value);
    } else {
        printf("[error] failed to save\n");
    }
}

static void cmd_forget(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: /forget <key>\n");
        return;
    }

    if (ai_ltm_delete(argv[1]) == CLAW_OK) {
        printf("Forgot: %s\n", argv[1]);
    } else {
        printf("[error] key '%s' not found\n", argv[1]);
    }
}

static void cmd_memories(void)
{
    ai_ltm_list();
}

static void cmd_log(int argc, char **argv)
{
    if (argc < 2) {
        /* Toggle */
        claw_log_set_enabled(!claw_log_get_enabled());
    } else if (strcmp(argv[1], "on") == 0) {
        claw_log_set_enabled(1);
    } else if (strcmp(argv[1], "off") == 0) {
        claw_log_set_enabled(0);
    } else {
        printf("Usage: /log [on|off]\n");
        return;
    }
    printf("Log output: %s\n", claw_log_get_enabled() ? "ON" : "OFF");
}

#ifdef CONFIG_CLAW_SWARM_ENABLE
static void cmd_nodes(void)
{
    swarm_list_nodes();
}
#endif

/* Dispatch a /command */
static void dispatch_command(char *line)
{
    char *argv[MAX_ARGS];
    int argc = tokenize(line, argv, MAX_ARGS);

    if (argc == 0) {
        return;
    }

    const char *cmd = argv[0];

    if (strcmp(cmd, "/help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "/log") == 0) {
        cmd_log(argc, argv);
    } else if (strcmp(cmd, "/history") == 0) {
        cmd_history();
    } else if (strcmp(cmd, "/clear") == 0) {
        cmd_clear();
#ifdef CONFIG_CLAW_SCHED_ENABLE
    } else if (strcmp(cmd, "/sched") == 0) {
        cmd_sched();
#endif
#ifdef CONFIG_CLAW_SKILL_ENABLE
    } else if (strcmp(cmd, "/skill") == 0) {
        cmd_skill(argc, argv);
#endif
    } else if (strcmp(cmd, "/remember") == 0) {
        cmd_remember(argc, argv);
    } else if (strcmp(cmd, "/forget") == 0) {
        cmd_forget(argc, argv);
    } else if (strcmp(cmd, "/memories") == 0) {
        cmd_memories();
#ifdef CONFIG_CLAW_SWARM_ENABLE
    } else if (strcmp(cmd, "/nodes") == 0) {
        cmd_nodes();
#endif
    } else {
        printf("Unknown command: %s (type /help)\n", cmd);
    }
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
               "                    \n", detail ? detail : "?");
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

#endif /* CONFIG_CLAW_SHELL_ENABLE */

void app_main(void)
{
#ifdef CLAW_PLATFORM_ESP_IDF
    /* Initialize NVS flash for long-term memory storage */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

#ifdef CONFIG_CLAW_SHELL_ENABLE
    /* Shell mode: suppress log by default; use /log on to enable */
    esp_log_level_set("*", ESP_LOG_NONE);
#else
    /* Headless mode (Feishu etc.): enable log for serial diagnostics */
    claw_log_set_enabled(1);
#endif
#endif

    claw_init();

#ifdef CONFIG_CLAW_SHELL_ENABLE
    shell_loop();
#endif

    /* Keep main task alive */
    while (1) {
        claw_thread_delay_ms(1000);
    }
}
