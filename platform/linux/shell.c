/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Interactive shell for Linux native platform.
 *
 * Chat-first REPL with Tab completion for /commands.
 * Raw terminal mode for character-by-character input.
 */

#include "osal/claw_os.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/shell/shell_commands.h"
#include "claw/shell/shell_history.h"
#include "platform/board.h"

#ifdef CONFIG_RTCLAW_SKILL_ENABLE
#include "claw/services/ai/ai_skill.h"
#endif

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>

#define TAG         "shell"
#define REPLY_SIZE  4096
#define INPUT_SIZE  256
#define MAX_ARGS    8
#define SKILL_REPLY_SIZE 2048

extern volatile int g_exit_flag;

static char *s_reply;
static struct termios s_orig_termios;
static int s_raw_mode;

/* Forward declarations */
static void cmd_help(int argc, char **argv);
static void cmd_quit(int argc, char **argv);

static const shell_cmd_t s_builtin_commands[] = {
    SHELL_CMD("/help", cmd_help, "Show this help"),
    SHELL_CMD("/quit", cmd_quit, "Exit rt-claw"),
};

/* ---- Raw terminal mode ---- */

static void enable_raw_mode(void)
{
    if (!isatty(STDIN_FILENO)) {
        return;
    }
    tcgetattr(STDIN_FILENO, &s_orig_termios);
    struct termios raw = s_orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    s_raw_mode = 1;
}

static void disable_raw_mode(void)
{
    if (s_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_orig_termios);
        s_raw_mode = 0;
    }
}

/* ---- Tab completion ---- */

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
            if (strncmp(board[i].name, prefix,
                        prefix_len) == 0) {
                *match = board[i].name;
                (*match_count)++;
            }
        }
    }

#ifdef CONFIG_RTCLAW_SKILL_ENABLE
    {
        static char s_skill_match[64];
        for (int i = 0; i < ai_skill_count(); i++) {
            const char *sname = ai_skill_get_name(i);
            if (!sname) {
                continue;
            }
            snprintf(s_skill_match, sizeof(s_skill_match),
                     "/%s", sname);
            if (strncmp(s_skill_match, prefix,
                        prefix_len) == 0) {
                *match = s_skill_match;
                (*match_count)++;
            }
        }
    }
#endif
}

/* ---- Line editing helpers ---- */

static void linux_redraw_tail(const char *buf, int len, int cursor)
{
    int tail = len - cursor;
    if (tail > 0) {
        write(STDOUT_FILENO, buf + cursor, tail);
    }
    write(STDOUT_FILENO, " ", 1);
    for (int i = 0; i < tail + 1; i++) {
        write(STDOUT_FILENO, "\b", 1);
    }
}

static void linux_line_replace(char *buf, int *len, int *cursor,
                               const char *text)
{
    while (*cursor > 0) {
        write(STDOUT_FILENO, "\b", 1);
        (*cursor)--;
    }
    for (int i = 0; i < *len; i++) {
        write(STDOUT_FILENO, " ", 1);
    }
    for (int i = 0; i < *len; i++) {
        write(STDOUT_FILENO, "\b", 1);
    }
    int nlen = (int)strlen(text);
    if (nlen >= INPUT_SIZE) {
        nlen = INPUT_SIZE - 1;
    }
    memcpy(buf, text, nlen);
    buf[nlen] = '\0';
    *len = nlen;
    *cursor = nlen;
    write(STDOUT_FILENO, buf, nlen);
}

/* ---- Line reader with tab completion, cursor, history ---- */

static int shell_read_line(char *buf, int size)
{
    int len = 0;
    int cursor = 0;
    unsigned char ch;

    if (!isatty(STDIN_FILENO)) {
        while (len < size - 1) {
            fd_set readfds;
            struct timeval timeout = { 0, 100000 };
            int ret;

            if (g_exit_flag) {
                return -1;
            }

            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return -1;
            }
            if (ret == 0) {
                continue;
            }

            ret = (int)read(STDIN_FILENO, &ch, 1);
            if (ret <= 0) {
                if (len == 0) {
                    return -1;
                }
                break;
            }
            if (ch == '\n' || ch == '\r') {
                break;
            }
            buf[len++] = (char)ch;
        }
        buf[len] = '\0';
        return len;
    }

    while (len < size - 1) {
        int n = (int)read(STDIN_FILENO, &ch, 1);
        if (n <= 0) {
            return -1;
        }

        /* Enter */
        if (ch == '\r' || ch == '\n') {
            write(STDOUT_FILENO, "\r\n", 2);
            break;
        }

        /* Ctrl-D — exit */
        if (ch == 4) {
            g_exit_flag = 1;
            return -1;
        }

        /* Ctrl-C — cancel current line */
        if (ch == 3) {
            write(STDOUT_FILENO, "^C\r\n", 4);
            buf[0] = '\0';
            return -2;
        }

        /* Tab completion */
        if (ch == '\t') {
            if (len > 0 && buf[0] == '/') {
                buf[len] = '\0';
                const char *match = NULL;
                int match_count = 0;
                find_completions(buf, len,
                                 &match, &match_count);
                if (match_count == 1 && match) {
                    linux_line_replace(buf, &len, &cursor,
                                       "");
                    int mlen = (int)strlen(match);
                    memcpy(buf, match, mlen);
                    buf[mlen] = ' ';
                    len = mlen + 1;
                    cursor = len;
                    write(STDOUT_FILENO, buf, len);
                }
            }
            continue;
        }

        /* Backspace */
        if (ch == 127 || ch == '\b') {
            if (cursor > 0) {
                memmove(buf + cursor - 1, buf + cursor,
                        len - cursor);
                cursor--;
                len--;
                write(STDOUT_FILENO, "\b", 1);
                linux_redraw_tail(buf, len, cursor);
            }
            continue;
        }

        /* Escape sequences */
        if (ch == 0x1b) {
            unsigned char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) {
                continue;
            }
            if (seq[0] != '[') {
                continue;
            }
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) {
                continue;
            }

            /* Numeric: ESC [ digit ~ */
            if (seq[1] >= '0' && seq[1] <= '9') {
                unsigned char tilde;
                read(STDIN_FILENO, &tilde, 1);
                if (seq[1] == '3' && tilde == '~') {
                    /* Delete key */
                    if (cursor < len) {
                        memmove(buf + cursor,
                                buf + cursor + 1,
                                len - cursor - 1);
                        len--;
                        linux_redraw_tail(buf, len, cursor);
                    }
                }
                continue;
            }

            switch (seq[1]) {
            case 'A': { /* Up — older history */
                buf[len] = '\0';
                const char *h =
                    shell_history_navigate(-1, buf);
                if (h) {
                    linux_line_replace(buf, &len,
                                       &cursor, h);
                }
                break;
            }
            case 'B': { /* Down — newer history */
                buf[len] = '\0';
                const char *h =
                    shell_history_navigate(1, buf);
                if (h) {
                    linux_line_replace(buf, &len,
                                       &cursor, h);
                }
                break;
            }
            case 'C': /* Right */
                if (cursor < len) {
                    cursor++;
                    write(STDOUT_FILENO, "\033[C", 3);
                }
                break;
            case 'D': /* Left */
                if (cursor > 0) {
                    cursor--;
                    write(STDOUT_FILENO, "\033[D", 3);
                }
                break;
            case 'H': /* Home */
                while (cursor > 0) {
                    cursor--;
                    write(STDOUT_FILENO, "\033[D", 3);
                }
                break;
            case 'F': /* End */
                while (cursor < len) {
                    cursor++;
                    write(STDOUT_FILENO, "\033[C", 3);
                }
                break;
            default:
                break;
            }
            continue;
        }

        /* Printable character — insert at cursor */
        if (ch >= 32) {
            if (cursor < len) {
                memmove(buf + cursor + 1, buf + cursor,
                        len - cursor);
            }
            buf[cursor] = (char)ch;
            len++;
            cursor++;
            write(STDOUT_FILENO, &ch, 1);
            if (cursor < len) {
                linux_redraw_tail(buf, len, cursor);
            }
        }
    }

    buf[len] = '\0';

    /* Drain remaining input until newline when buffer is full */
    if (len >= size - 1) {
        unsigned char discard;
        while (read(STDIN_FILENO, &discard, 1) > 0) {
            if (discard == '\r' || discard == '\n') {
                break;
            }
        }
        write(STDOUT_FILENO, "\r\n", 2);
    }

    return len;
}

/* ---- Command handlers ---- */

static void cmd_quit(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    g_exit_flag = 1;
}

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int board_cmd_count = 0;
    const shell_cmd_t *board_cmds =
        board_platform_commands(&board_cmd_count);

    shell_print_help(s_builtin_commands,
                     SHELL_CMD_COUNT(s_builtin_commands));
    if (board_cmds && board_cmd_count > 0) {
        shell_print_help(board_cmds, board_cmd_count);
    }
    shell_print_help(shell_common_commands,
                     shell_common_command_count());

#ifdef CONFIG_RTCLAW_SKILL_ENABLE
    if (ai_skill_count() > 0) {
        claw_printf("\n  Dynamic skills (invoke as /name [args]):\n");
        for (int i = 0; i < ai_skill_count(); i++) {
            claw_printf("    /%s\n", ai_skill_get_name(i));
        }
    }
#endif

    claw_printf("\n  Anything else is sent directly to AI.\n");
}

/* ---- Command dispatch ---- */

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
    const shell_cmd_t *board_cmds =
        board_platform_commands(&board_cmd_count);
    if (board_cmds && shell_dispatch(board_cmds, board_cmd_count,
                                     argc, argv)) {
        return;
    }

    if (shell_dispatch(shell_common_commands,
                       shell_common_command_count(),
                       argc, argv)) {
        return;
    }

#ifdef CONFIG_RTCLAW_SKILL_ENABLE
    {
        const char *sname = argv[0];
        if (sname[0] == '/') {
            sname++;
        }
        if (ai_skill_find(sname)) {
            char *skill_reply = claw_malloc(SKILL_REPLY_SIZE);
            if (skill_reply) {
                if (ai_skill_try_command(argv[0], argc, argv,
                                         skill_reply,
                                         SKILL_REPLY_SIZE)
                        == CLAW_OK) {
                    printf("\n" CLR_GREEN "rt-claw> " CLR_RESET
                           "%s\n", skill_reply);
                }
                claw_free(skill_reply);
            }
            return;
        }
    }
#endif

    printf("Unknown command: %s (type /help)\n", argv[0]);
}

/* ---- Thinking animation ---- */

static volatile int s_anim_active;
static volatile int s_anim_phase;
static volatile int s_chat_cancel;

static void anim_thread_fn(void *arg)
{
    (void)arg;
    int dots = 0;
    const char *dot_str[] = { ".", "..", "..." };

    while (s_anim_active && !claw_thread_should_exit()) {
        /*
         * Poll stdin for Ctrl-C (non-blocking).
         * Raw mode is kept active during chat so we can
         * detect single keystrokes.
         */
        if (s_raw_mode) {
            unsigned char key;
            struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            if (select(STDIN_FILENO + 1, &fds, NULL, NULL,
                       &tv) > 0) {
                if (read(STDIN_FILENO, &key, 1) == 1 &&
                    key == 3) {
                    s_chat_cancel = 1;
                    s_anim_active = 0;
                    printf("\r  ^C — cancelled"
                           "                    \n");
                    fflush(stdout);
                    break;
                }
            }
        }

        if (s_anim_phase == 0) {
            printf("\r  " CLR_MAGENTA "thinking %s"
                   CLR_RESET "   ", dot_str[dots]);
            fflush(stdout);
            dots = (dots + 1) % 3;
        }
        claw_thread_delay_ms(450);
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
    s_chat_cancel = 0;
    ai_set_status_cb(chat_status_cb);

    struct claw_thread *anim = claw_thread_create("anim",
        anim_thread_fn, NULL, 2048, 20);

    /*
     * Keep raw mode active so the animation thread can poll
     * stdin for Ctrl-C.  AI output still goes to stdout fine.
     */
    int ret = ai_chat(msg, s_reply, REPLY_SIZE);

    s_anim_active = 0;
    ai_set_status_cb(NULL);
    if (anim) {
        claw_thread_delete(anim);
    }
    printf("\r                              \r");

    if (s_chat_cancel) {
        return;
    }

    if (ret == CLAW_OK) {
        printf(CLR_GREEN "rt-claw> " CLR_RESET "%s\n", s_reply);
    } else {
        printf(CLR_RED "error> " CLR_RESET "%s\n", s_reply);
    }
}

/* ---- Public API ---- */

void linux_shell_loop(void)
{
    char input[INPUT_SIZE];

    /* Register command tables for IM shell_exec_capture() */
    shell_register_cmd_table(s_builtin_commands,
                             SHELL_CMD_COUNT(s_builtin_commands));
    int board_cmd_count = 0;
    const shell_cmd_t *board_cmds =
        board_platform_commands(&board_cmd_count);
    if (board_cmds && board_cmd_count > 0) {
        shell_register_cmd_table(board_cmds, board_cmd_count);
    }

    s_reply = claw_malloc(REPLY_SIZE);
    if (!s_reply) {
        CLAW_LOGE(TAG, "no memory for reply buffer");
        return;
    }

    enable_raw_mode();

    printf(CLR_CYAN "  rt-claw" CLR_RESET
           "  (type /help for commands, Tab to complete)\n");
    printf("  Direct input sends to AI, /command for system.\n");

    struct timeval last_ctrlc = { 0, 0 };

    while (!g_exit_flag) {
        shell_history_reset_nav();
        printf("\n" CLR_CYAN "you> " CLR_RESET);
        fflush(stdout);

        int len = shell_read_line(input, sizeof(input));
        if (len == -1) {
            /* Ctrl-D or EOF */
            break;
        }
        if (len == -2) {
            /* Ctrl-C — check for double-press within 1s */
            struct timeval now;
            gettimeofday(&now, NULL);
            long elapsed_ms = (now.tv_sec - last_ctrlc.tv_sec) * 1000
                + (now.tv_usec - last_ctrlc.tv_usec) / 1000;
            if (last_ctrlc.tv_sec > 0 && elapsed_ms < 1000) {
                printf("Interrupted.\n");
                break;
            }
            last_ctrlc = now;
            printf("(press Ctrl-C again to exit)\n");
            continue;
        }
        last_ctrlc.tv_sec = 0;

        if (len == 0) {
            continue;
        }

        shell_history_add(input);

        if (input[0] == '/') {
            /* Cooked mode for command output */
            disable_raw_mode();
            dispatch_command(input);
            enable_raw_mode();
        } else {
            do_chat(input);
        }
    }

    disable_raw_mode();
    claw_free(s_reply);
    printf("\nBye!\n");
}
