/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Platform entry for ESP32-C3 / ESP-IDF + FreeRTOS.
 * Chat-first shell: direct input goes to AI, /commands for system.
 */

#include "osal/claw_os.h"
#include "claw/claw_init.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/shell/shell_commands.h"
#include "claw_board.h"

#include <stdio.h>
#include <string.h>

#ifdef CLAW_PLATFORM_ESP_IDF
#include "nvs_flash.h"
#include "esp_log.h"
#endif

#ifdef CONFIG_RTCLAW_SHELL_ENABLE
#include "drivers/serial/espressif/console.h"
#endif

#define TAG         "main"
#define REPLY_SIZE  4096
#define INPUT_SIZE  256
#define MAX_ARGS    8

#ifdef CONFIG_RTCLAW_SHELL_ENABLE

static char *s_reply;

/* Forward declarations */
static void cmd_help(int argc, char **argv);

/* Forward declaration for tab completion (defined after cmd table) */
static void find_completions(const char *prefix, int prefix_len,
                             const char **match, int *match_count);

/* ---- Redraw line from cursor position ---- */

static void redraw_from(const char *buf, int len, int cursor)
{
    int tail = len - cursor;

    if (tail > 0) {
        claw_console_write(buf + cursor, tail);
    }
    /* Clear one trailing char (covers deleted character) */
    claw_console_write(" ", 1);
    /* Move cursor back to correct position */
    for (int i = 0; i < tail + 1; i++) {
        claw_console_write("\b", 1);
    }
}

/*
 * Read one line with insert-mode editing.
 * Supports: left/right arrows, backspace, delete, home, end,
 * tab completion for /commands, UTF-8 passthrough.
 */
static int shell_read_line(char *buf, int size)
{
    int len = 0;     /* total chars in buffer */
    int cursor = 0;  /* cursor position */
    uint8_t ch;

    while (len < size - 1) {
        int n = claw_console_read(&ch, 1, UINT32_MAX);
        if (n <= 0) {
            continue;
        }

        /* Enter */
        if (ch == '\r' || ch == '\n') {
            claw_console_write("\r\n", 2);
            uint8_t trail;
            claw_console_read(&trail, 1, 20);
            break;
        }

        /* Tab completion */
        if (ch == '\t') {
            if (len > 0 && buf[0] == '/') {
                buf[len] = '\0';
                const char *match = NULL;
                int match_count = 0;
                find_completions(buf, len, &match, &match_count);
                if (match_count == 1 && match) {
                    int mlen = (int)strlen(match);
                    /* Clear current line */
                    while (cursor > 0) {
                        claw_console_write("\b \b", 3);
                        cursor--;
                    }
                    for (int i = 0; i < len; i++) {
                        claw_console_write(" ", 1);
                    }
                    for (int i = 0; i < len; i++) {
                        claw_console_write("\b", 1);
                    }
                    /* Fill in match */
                    memcpy(buf, match, mlen);
                    buf[mlen] = ' ';
                    len = mlen + 1;
                    cursor = len;
                    claw_console_write(buf, len);
                }
            }
            continue;
        }

        /* Backspace */
        if (ch == '\b' || ch == 127) {
            if (cursor > 0) {
                memmove(buf + cursor - 1, buf + cursor, len - cursor);
                cursor--;
                len--;
                claw_console_write("\b", 1);
                redraw_from(buf, len, cursor);
            }
            continue;
        }

        /* Escape sequences (arrows, home, end, delete) */
        if (ch == 0x1B) {
            uint8_t seq[2];
            if (claw_console_read(&seq[0], 1, 50) <= 0) {
                continue;
            }
            if (seq[0] != '[') {
                continue;
            }
            if (claw_console_read(&seq[1], 1, 50) <= 0) {
                continue;
            }

            /* Numeric sequences: ESC [ <digit> ~ */
            if (seq[1] >= '0' && seq[1] <= '9') {
                uint8_t tilde;
                claw_console_read(&tilde, 1, 50);
                if (seq[1] == '3' && tilde == '~') {
                    /* Delete key */
                    if (cursor < len) {
                        memmove(buf + cursor, buf + cursor + 1,
                                len - cursor - 1);
                        len--;
                        redraw_from(buf, len, cursor);
                    }
                }
                /* Other numeric sequences silently consumed */
                continue;
            }

            switch (seq[1]) {
            case 'D': /* Left arrow */
                if (cursor > 0) {
                    cursor--;
                    claw_console_write("\033[D", 3);
                }
                break;
            case 'C': /* Right arrow */
                if (cursor < len) {
                    cursor++;
                    claw_console_write("\033[C", 3);
                }
                break;
            case 'H': /* Home */
                while (cursor > 0) {
                    cursor--;
                    claw_console_write("\033[D", 3);
                }
                break;
            case 'F': /* End */
                while (cursor < len) {
                    cursor++;
                    claw_console_write("\033[C", 3);
                }
                break;
            default:
                break;
            }
            continue;
        }

        /* Normal character — insert at cursor */
        if (cursor < len) {
            memmove(buf + cursor + 1, buf + cursor, len - cursor);
        }
        buf[cursor] = (char)ch;
        len++;
        cursor++;

        /* Echo: print from new char onward, move cursor back */
        claw_console_write(&ch, 1);
        if (cursor < len) {
            redraw_from(buf, len, cursor);
        }
    }

    buf[len] = '\0';
    return len;
}

/* ---- Command table ---- */

static const shell_cmd_t s_builtin_commands[] = {
    SHELL_CMD("/help", cmd_help, "Show this help"),
};

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int board_cmd_count = 0;
    const shell_cmd_t *board_cmds = board_platform_commands(&board_cmd_count);

    shell_print_help(s_builtin_commands,
                     SHELL_CMD_COUNT(s_builtin_commands));
    if (board_cmds && board_cmd_count > 0) {
        shell_print_help(board_cmds, board_cmd_count);
    }
    shell_print_help(shell_common_commands,
                     shell_common_command_count());
    printf("\n  Anything else is sent directly to AI.\n");
}

/* ---- Tab completion for /commands ---- */

static void find_completions(const char *prefix, int prefix_len,
                             const char **match, int *match_count)
{
    *match = NULL;
    *match_count = 0;

    const struct {
        const shell_cmd_t *table;
        int count;
    } tables[] = {
        { s_builtin_commands, SHELL_CMD_COUNT(s_builtin_commands) },
        { shell_common_commands, shell_common_command_count() },
    };
    int board_count = 0;
    const shell_cmd_t *board = board_platform_commands(&board_count);

    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < tables[t].count; i++) {
            if (strncmp(tables[t].table[i].name, prefix,
                        prefix_len) == 0) {
                *match = tables[t].table[i].name;
                (*match_count)++;
            }
        }
    }
    if (board) {
        for (int i = 0; i < board_count; i++) {
            if (strncmp(board[i].name, prefix, prefix_len) == 0) {
                *match = board[i].name;
                (*match_count)++;
            }
        }
    }
}

/* Dispatch a /command */
static void dispatch_command(char *line)
{
    char *argv[MAX_ARGS];
    int argc = shell_tokenize(line, argv, MAX_ARGS);

    if (argc == 0) {
        return;
    }
    if (shell_dispatch(s_builtin_commands,
                       SHELL_CMD_COUNT(s_builtin_commands),
                       argc, argv)) {
        return;
    }

    int board_cmd_count = 0;
    const shell_cmd_t *board_cmds = board_platform_commands(&board_cmd_count);
    if (board_cmds && shell_dispatch(board_cmds, board_cmd_count,
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
static volatile int s_anim_phase;

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

    claw_console_init();

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
        int len = shell_read_line(input, sizeof(input));

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
    esp_log_level_set("*", ESP_LOG_WARN);
#else
    claw_log_set_enabled(1);
#endif
#endif

    /* Board-specific initialization (WiFi, Ethernet, etc.) */
    board_early_init();

    /* Load runtime config from NVS */
    shell_nvs_config_load();

    /* Boot rt-claw services */
    claw_init();

#ifdef CONFIG_RTCLAW_SHELL_ENABLE
    shell_loop();
#endif

    while (1) {
        claw_thread_delay_ms(1000);
    }
}
