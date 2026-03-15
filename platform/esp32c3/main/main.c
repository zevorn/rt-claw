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

        /* Backspace — UTF-8 aware */
        if (ch == '\b' || ch == 127) {
            if (cursor > 0) {
                /*
                 * Walk back over a complete UTF-8 sequence.
                 * Continuation bytes are 10xxxxxx (0x80..0xBF).
                 */
                int del = 1;
                while (del < cursor &&
                       (buf[cursor - del] & 0xC0) == 0x80) {
                    del++;
                }

                /* Determine display width: CJK fullwidth = 2 cols */
                int cols = 1;
                uint8_t lead = (uint8_t)buf[cursor - del];
                if (del == 3 && (lead & 0xF0) == 0xE0) {
                    /* 3-byte UTF-8: U+0800..U+FFFF (CJK range) */
                    cols = 2;
                } else if (del == 4 && (lead & 0xF8) == 0xF0) {
                    /* 4-byte UTF-8: supplementary (emoji etc.) */
                    cols = 2;
                }

                memmove(buf + cursor - del, buf + cursor,
                        len - cursor);
                cursor -= del;
                len -= del;

                for (int i = 0; i < cols; i++) {
                    claw_console_write("\b", 1);
                }
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
                    /* Delete key — UTF-8 aware */
                    if (cursor < len) {
                        int del = 1;
                        uint8_t lead = (uint8_t)buf[cursor];
                        if ((lead & 0x80) != 0) {
                            if ((lead & 0xE0) == 0xC0) {
                                del = 2;
                            } else if ((lead & 0xF0) == 0xE0) {
                                del = 3;
                            } else if ((lead & 0xF8) == 0xF0) {
                                del = 4;
                            }
                            if (cursor + del > len) {
                                del = len - cursor;
                            }
                        }
                        memmove(buf + cursor, buf + cursor + del,
                                len - cursor - del);
                        len -= del;
                        redraw_from(buf, len, cursor);
                    }
                }
                /* Other numeric sequences silently consumed */
                continue;
            }

            switch (seq[1]) {
            case 'D': /* Left arrow — UTF-8 aware */
                if (cursor > 0) {
                    int skip = 1;
                    while (skip < cursor &&
                           (buf[cursor - skip] & 0xC0) == 0x80) {
                        skip++;
                    }
                    int cols = 1;
                    uint8_t ld = (uint8_t)buf[cursor - skip];
                    if (skip == 3 && (ld & 0xF0) == 0xE0) {
                        cols = 2;
                    } else if (skip == 4 && (ld & 0xF8) == 0xF0) {
                        cols = 2;
                    }
                    cursor -= skip;
                    char esc[8];
                    int elen = snprintf(esc, sizeof(esc),
                                        "\033[%dD", cols);
                    claw_console_write(esc, elen);
                }
                break;
            case 'C': /* Right arrow — UTF-8 aware */
                if (cursor < len) {
                    int skip = 1;
                    uint8_t ld = (uint8_t)buf[cursor];
                    if ((ld & 0xE0) == 0xC0) {
                        skip = 2;
                    } else if ((ld & 0xF0) == 0xE0) {
                        skip = 3;
                    } else if ((ld & 0xF8) == 0xF0) {
                        skip = 4;
                    }
                    if (cursor + skip > len) {
                        skip = len - cursor;
                    }
                    int cols = 1;
                    if (skip == 3 && (ld & 0xF0) == 0xE0) {
                        cols = 2;
                    } else if (skip == 4 && (ld & 0xF8) == 0xF0) {
                        cols = 2;
                    }
                    cursor += skip;
                    char esc[8];
                    int elen = snprintf(esc, sizeof(esc),
                                        "\033[%dC", cols);
                    claw_console_write(esc, elen);
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

        /*
         * Normal character — insert at cursor.
         * For UTF-8 multi-byte sequences, read all continuation
         * bytes before inserting so the character appears atomically.
         */
        int seq_len = 1;
        uint8_t mb[4];
        mb[0] = ch;

        if ((ch & 0xE0) == 0xC0) {
            seq_len = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            seq_len = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            seq_len = 4;
        }

        for (int i = 1; i < seq_len; i++) {
            if (claw_console_read(&mb[i], 1, 100) <= 0) {
                seq_len = i;
                break;
            }
        }

        if (len + seq_len >= size) {
            continue;
        }

        if (cursor < len) {
            memmove(buf + cursor + seq_len, buf + cursor,
                    len - cursor);
        }
        memcpy(buf + cursor, mb, seq_len);
        len += seq_len;
        cursor += seq_len;

        /* Echo the complete character */
        claw_console_write(mb, seq_len);
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
