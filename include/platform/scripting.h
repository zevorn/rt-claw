/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Platform hooks for embedded script execution.
 */

#ifndef PLATFORM_SCRIPTING_H
#define PLATFORM_SCRIPTING_H

#include <stddef.h>

/*
 * Return 1 when the given runtime is available on this platform.
 * Current users pass "micropython".
 */
int claw_platform_script_supported(const char *language);

/*
 * Execute script source with the selected runtime.
 * Returns 0 on success, -1 on failure.
 * output receives stdout / exception text when available.
 */
int claw_platform_run_script(const char *language, const char *code,
                             char *output, size_t output_size);

#endif /* PLATFORM_SCRIPTING_H */
