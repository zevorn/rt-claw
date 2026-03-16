/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IM utility — platform-independent helpers shared by IM backends.
 */

#ifndef CLAW_SERVICES_IM_IM_UTIL_H
#define CLAW_SERVICES_IM_IM_UTIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Find the end position for one message chunk.
 *
 * If @remaining fits within @max_chunk, returns @remaining (no split).
 * Otherwise, scans backward from @max_chunk for a newline in the
 * range [max_chunk/2, max_chunk] and splits after it.  If no newline
 * is found, hard-splits at @max_chunk.
 *
 * @param text       Pointer to the remaining text.
 * @param remaining  Number of bytes remaining.
 * @param max_chunk  Maximum chunk size in bytes (must be > 0).
 *
 * @return Number of bytes to include in this chunk (always > 0
 *         when remaining > 0).
 */
size_t im_find_chunk_end(const char *text, size_t remaining,
                         size_t max_chunk);

#ifdef __cplusplus
}
#endif

#endif /* CLAW_SERVICES_IM_IM_UTIL_H */
