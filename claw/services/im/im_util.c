/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IM utility — platform-independent helpers shared by IM backends.
 */

#include "claw/services/im/im_util.h"

size_t im_find_chunk_end(const char *text, size_t remaining,
                         size_t max_chunk)
{
    if (remaining <= max_chunk) {
        return remaining;
    }

    /* Scan backward for newline in [max_chunk/2, max_chunk] */
    size_t half = max_chunk / 2;
    for (size_t i = max_chunk; i > half; i--) {
        if (text[i] == '\n') {
            return i + 1;   /* include the newline in this chunk */
        }
    }

    /* No newline found — hard split at max_chunk */
    return max_chunk;
}
