/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Common shell commands shared across all platforms.
 * Platform-specific commands (e.g. WiFi) stay in each platform's main.c.
 */

#ifndef CLAW_SHELL_COMMANDS_H
#define CLAW_SHELL_COMMANDS_H

#include "claw/shell/shell_cmd.h"

/* Common command table (defined in claw/shell/shell_commands.c) */
extern const shell_cmd_t shell_common_commands[];
int shell_common_command_count(void);

/*
 * Load runtime config from NVS (ESP-IDF only).
 * Call before claw_init() so services pick up persisted values.
 */
void shell_nvs_config_load(void);

/*
 * Save a string key-value pair to NVS (ESP-IDF only).
 * No-op on non-ESP-IDF platforms.
 */
void shell_nvs_save_str(const char *ns, const char *key, const char *val);

/* NVS namespace constants */
#define SHELL_NVS_NS_AI     "ai_config"
#define SHELL_NVS_NS_FEISHU "feishu_cfg"

#endif /* CLAW_SHELL_COMMANDS_H */
