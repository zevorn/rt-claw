/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Host Python bridge for Linux native platform.
 */

#include "platform/scripting.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PYTHON3_BIN            "python3"
#define SCRIPT_TEMPLATE        "/tmp/rtclaw-python-XXXXXX"
#define SCRIPT_LANGUAGE_PYTHON "python"
#define SCRIPT_LANGUAGE_MICROPYTHON "micropython"
#define PIPE_READ_SIZE         128

static int script_write_file(const char *code, char *path)
{
    size_t len = strlen(code);
    ssize_t wrote = 0;
    int fd;

    snprintf(path, sizeof(SCRIPT_TEMPLATE), "%s", SCRIPT_TEMPLATE);
    fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }

    while ((size_t)wrote < len) {
        ssize_t ret = write(fd, code + wrote, len - (size_t)wrote);

        if (ret < 0) {
            close(fd);
            unlink(path);
            return -1;
        }
        wrote += ret;
    }

    close(fd);
    return 0;
}

static void script_read_output(FILE *pipe, char *output, size_t output_size)
{
    char chunk[PIPE_READ_SIZE];
    size_t len = 0;

    if (output_size == 0) {
        return;
    }

    output[0] = '\0';
    while (!feof(pipe) && !ferror(pipe)) {
        size_t n = fread(chunk, 1, sizeof(chunk), pipe);
        size_t remain;
        size_t copy;

        if (n == 0) {
            break;
        }

        if (len >= output_size - 1) {
            continue;
        }

        remain = output_size - len - 1;
        copy = n < remain ? n : remain;
        memcpy(output + len, chunk, copy);
        len += copy;
        output[len] = '\0';
    }
}

static int script_status_error(int status, char *output, size_t output_size)
{
    if (output_size == 0) {
        return -1;
    }

    if (WIFEXITED(status)) {
        snprintf(output, output_size,
                 "python3 exited with status %d", WEXITSTATUS(status));
    } else {
        snprintf(output, output_size,
                 "python3 terminated abnormally");
    }

    return -1;
}

int claw_platform_script_supported(const char *language)
{
    if (!language) {
        return 0;
    }

    return strcmp(language, SCRIPT_LANGUAGE_PYTHON) == 0
        || strcmp(language, SCRIPT_LANGUAGE_MICROPYTHON) == 0;
}

int claw_platform_run_script(const char *language, const char *code,
                             char *output, size_t output_size)
{
    char path[] = SCRIPT_TEMPLATE;
    char cmd[sizeof(path) + 32];
    FILE *pipe;
    int status;

    if (!claw_platform_script_supported(language) || !code ||
        !output || output_size == 0) {
        return -1;
    }

    if (script_write_file(code, path) != 0) {
        snprintf(output, output_size,
                 "failed to create temporary script: %s",
                 strerror(errno));
        return -1;
    }

    snprintf(cmd, sizeof(cmd), PYTHON3_BIN " -I -B '%s' 2>&1", path);
    pipe = popen(cmd, "r");
    if (!pipe) {
        unlink(path);
        snprintf(output, output_size,
                 "failed to launch python3: %s", strerror(errno));
        return -1;
    }

    script_read_output(pipe, output, output_size);
    status = pclose(pipe);
    unlink(path);

    if (status == -1) {
        snprintf(output, output_size,
                 "failed to wait for python3: %s", strerror(errno));
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (output[0] == '\0') {
            return script_status_error(status, output, output_size);
        }
        return -1;
    }

    return 0;
}
