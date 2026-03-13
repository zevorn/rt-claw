/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Lightweight shell command framework — table-driven dispatch.
 * Each platform defines its own command table using SHELL_CMD().
 */

#ifndef CLAW_SHELL_CMD_H
#define CLAW_SHELL_CMD_H

#include <string.h>
#include <stdio.h>

/* ANSI color codes */
#define CLR_RESET   "\033[0m"
#define CLR_RED     "\033[0;31m"
#define CLR_GREEN   "\033[0;32m"
#define CLR_YELLOW  "\033[0;33m"
#define CLR_CYAN    "\033[0;36m"
#define CLR_MAGENTA "\033[0;35m"

typedef void (*shell_cmd_fn)(int argc, char **argv);

typedef struct {
    const char *name;
    shell_cmd_fn handler;
    const char *help;
} shell_cmd_t;

#define SHELL_CMD(n, fn, h) { (n), (fn), (h) }

#define SHELL_CMD_COUNT(table) \
    ((int)(sizeof(table) / sizeof((table)[0])))

static inline void shell_print_help(const shell_cmd_t *table, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        printf("  %-28s %s\n", table[i].name, table[i].help);
    }
}

static inline int shell_dispatch(const shell_cmd_t *table, int count,
                                 int argc, char **argv)
{
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(table[i].name, argv[0]) == 0) {
            table[i].handler(argc, argv);
            return 1;
        }
    }
    return 0;
}

/* Split string into argv-style tokens (modifies input in place) */
static inline int shell_tokenize(char *line, char **argv, int max_args)
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

#endif /* CLAW_SHELL_CMD_H */
