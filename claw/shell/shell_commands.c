/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Common shell command implementations shared across all platforms.
 */

#include "osal/claw_os.h"

#include <stdio.h>
#include <string.h>

#include "claw/shell/shell_commands.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/services/ai/ai_memory.h"
#include "claw/tools/claw_tools.h"
#include "claw/services/im/feishu.h"

#ifdef CONFIG_RTCLAW_SKILL_ENABLE
#include "claw/services/ai/ai_skill.h"
#endif
#ifdef CONFIG_RTCLAW_SCHED_ENABLE
#include "claw/core/scheduler.h"
#endif
#ifdef CONFIG_RTCLAW_SWARM_ENABLE
#include "claw/services/swarm/swarm.h"
#endif

#ifdef CLAW_PLATFORM_ESP_IDF
#include "nvs.h"
#endif

/* ---- NVS persistence (ESP-IDF only) ---- */

void shell_nvs_save_str(const char *ns, const char *key, const char *val)
{
#ifdef CLAW_PLATFORM_ESP_IDF
    nvs_handle_t nvs;

    if (nvs_open(ns, NVS_READWRITE, &nvs) != ESP_OK) {
        printf("[error] NVS open failed\n");
        return;
    }
    nvs_set_str(nvs, key, val);
    nvs_commit(nvs);
    nvs_close(nvs);
#else
    (void)ns;
    (void)key;
    (void)val;
#endif
}

void shell_nvs_config_load(void)
{
#ifdef CLAW_PLATFORM_ESP_IDF
    nvs_handle_t nvs;
    char buf[256];
    size_t len;

    /* Load AI config from NVS (overrides compile-time defaults) */
    if (nvs_open(SHELL_NVS_NS_AI, NVS_READONLY, &nvs) == ESP_OK) {
        len = sizeof(buf);
        if (nvs_get_str(nvs, "api_key", buf, &len) == ESP_OK) {
            ai_set_api_key(buf);
        }
        len = sizeof(buf);
        if (nvs_get_str(nvs, "api_url", buf, &len) == ESP_OK) {
            ai_set_api_url(buf);
        }
        len = sizeof(buf);
        if (nvs_get_str(nvs, "model", buf, &len) == ESP_OK) {
            ai_set_model(buf);
        }
        nvs_close(nvs);
    }

    /* Load Feishu config from NVS */
    if (nvs_open(SHELL_NVS_NS_FEISHU, NVS_READONLY, &nvs) == ESP_OK) {
        len = sizeof(buf);
        if (nvs_get_str(nvs, "app_id", buf, &len) == ESP_OK) {
            feishu_set_app_id(buf);
        }
        len = sizeof(buf);
        if (nvs_get_str(nvs, "app_secret", buf, &len) == ESP_OK) {
            feishu_set_app_secret(buf);
        }
        nvs_close(nvs);
    }
#endif
}

/* ---- Common command handlers ---- */

static const char *log_level_name(int level)
{
    switch (level) {
    case CLAW_LOG_ERROR: return "error";
    case CLAW_LOG_WARN:  return "warn";
    case CLAW_LOG_INFO:  return "info";
    case CLAW_LOG_DEBUG: return "debug";
    default:             return "unknown";
    }
}

static void cmd_log(int argc, char **argv)
{
    if (argc < 2) {
        claw_log_set_enabled(!claw_log_get_enabled());
    } else if (strcmp(argv[1], "on") == 0) {
        claw_log_set_enabled(1);
    } else if (strcmp(argv[1], "off") == 0) {
        claw_log_set_enabled(0);
    } else if (strcmp(argv[1], "level") == 0) {
        if (argc < 3) {
            printf("Log level: %s\n",
                   log_level_name(claw_log_get_level()));
            return;
        }
        int lv = -1;
        if (strcmp(argv[2], "error") == 0) {
            lv = CLAW_LOG_ERROR;
        } else if (strcmp(argv[2], "warn") == 0) {
            lv = CLAW_LOG_WARN;
        } else if (strcmp(argv[2], "info") == 0) {
            lv = CLAW_LOG_INFO;
        } else if (strcmp(argv[2], "debug") == 0) {
            lv = CLAW_LOG_DEBUG;
        }
        if (lv < 0) {
            printf("Usage: /log level <error|warn|info|debug>\n");
            return;
        }
        claw_log_set_level(lv);
        printf("Log level: %s\n", log_level_name(lv));
        return;
    } else {
        printf("Usage: /log [on|off|level <error|warn|info|debug>]\n");
        return;
    }
    printf("Log output: %s\n",
           claw_log_get_enabled() ? "ON" : "OFF");
}

static void cmd_history(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Conversation memory: %d messages\n", ai_memory_count());
}

static void cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ai_memory_clear();
    printf("Conversation memory cleared.\n");
}

static void cmd_ai_set(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: /ai_set key|url|model <value>\n");
        return;
    }

    const char *field = argv[1];
    const char *value = argv[2];

    if (strcmp(field, "key") == 0) {
        ai_set_api_key(value);
        shell_nvs_save_str(SHELL_NVS_NS_AI, "api_key", value);
        printf("API key saved (effective immediately).\n");
    } else if (strcmp(field, "url") == 0) {
        ai_set_api_url(value);
        shell_nvs_save_str(SHELL_NVS_NS_AI, "api_url", value);
        printf("API URL saved: %s\n", value);
    } else if (strcmp(field, "model") == 0) {
        ai_set_model(value);
        shell_nvs_save_str(SHELL_NVS_NS_AI, "model", value);
        printf("Model saved: %s\n", value);
    } else {
        printf("Unknown field: %s (use key|url|model)\n", field);
    }
}

static void cmd_ai_status(int argc, char **argv)
{
    const char *key = ai_get_api_key();
    int klen = strlen(key);

    (void)argc;
    (void)argv;
    printf("AI Engine:\n");
    printf("  API Key: %s\n",
           klen == 0 ? "(not set)" :
           klen <= 8 ? "****" : "****...****");
    printf("  API URL: %s\n", ai_get_api_url());
    printf("  Model:   %s\n", ai_get_model());
}

static void cmd_feishu_set(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: /feishu_set <app_id> <app_secret>\n");
        return;
    }

    feishu_set_app_id(argv[1]);
    feishu_set_app_secret(argv[2]);
    shell_nvs_save_str(SHELL_NVS_NS_FEISHU, "app_id", argv[1]);
    shell_nvs_save_str(SHELL_NVS_NS_FEISHU, "app_secret", argv[2]);
    printf("Feishu credentials saved (reboot to apply).\n");
}

static void cmd_feishu_status(int argc, char **argv)
{
    const char *id = feishu_get_app_id();

    (void)argc;
    (void)argv;
    printf("Feishu:\n");
    printf("  App ID:     %s\n", id[0] ? id : "(not set)");
    printf("  App Secret: %s\n",
           feishu_get_app_secret()[0] ? "****" : "(not set)");
}

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
        off += snprintf(value + off, sizeof(value) - off,
                        "%s", argv[i]);
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

static void cmd_memories(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ai_ltm_list();
}

#ifdef CONFIG_RTCLAW_SKILL_ENABLE
#define SKILL_REPLY_SIZE 2048

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
        off += snprintf(params + off, sizeof(params) - off,
                        "%s", argv[i]);
    }

    char *reply = claw_malloc(SKILL_REPLY_SIZE);
    if (!reply) {
        printf("[error] no memory\n");
        return;
    }

    if (ai_skill_execute(argv[1], params,
                         reply, SKILL_REPLY_SIZE) == CLAW_OK) {
        printf("\n" CLR_GREEN "<rt-claw> " CLR_RESET "%s\n", reply);
    } else {
        printf("\n" CLR_RED "<error> " CLR_RESET "%s\n", reply);
    }
    claw_free(reply);
}
#endif

#ifdef CONFIG_RTCLAW_SCHED_ENABLE
static void cmd_task(int argc, char **argv)
{
    if (argc < 2) {
        sched_list();
        return;
    }
    if (strcmp(argv[1], "rm") == 0 && argc >= 3) {
        if (sched_tool_remove_by_name(argv[2]) == CLAW_OK) {
            printf("task '%s' removed\n", argv[2]);
        } else {
            printf("task '%s' not found\n", argv[2]);
        }
    } else {
        printf("usage: /task [rm <name>]\n");
    }
}
#endif

#ifdef CONFIG_RTCLAW_SWARM_ENABLE
static void cmd_nodes(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    swarm_list_nodes();
}
#endif

/* ---- Common command table ---- */

const shell_cmd_t shell_common_commands[] = {
    SHELL_CMD("/log",           cmd_log,           "Log [on|off|level <lvl>]"),
    SHELL_CMD("/history",       cmd_history,       "Show conversation count"),
    SHELL_CMD("/clear",         cmd_clear,         "Clear conversation memory"),
    SHELL_CMD("/ai_set",        cmd_ai_set,        "Set AI config (NVS)"),
    SHELL_CMD("/ai_status",     cmd_ai_status,     "Show AI config"),
    SHELL_CMD("/feishu_set",    cmd_feishu_set,    "Set Feishu creds (NVS)"),
    SHELL_CMD("/feishu_status", cmd_feishu_status, "Show Feishu config"),
    SHELL_CMD("/remember",      cmd_remember,      "Save to long-term memory"),
    SHELL_CMD("/forget",        cmd_forget,        "Delete a long-term memory"),
    SHELL_CMD("/memories",      cmd_memories,      "List long-term memories"),
#ifdef CONFIG_RTCLAW_SKILL_ENABLE
    SHELL_CMD("/skill",         cmd_skill,         "List or execute a skill"),
#endif
#ifdef CONFIG_RTCLAW_SCHED_ENABLE
    SHELL_CMD("/task",          cmd_task,           "Tasks [rm <name>]"),
#endif
#ifdef CONFIG_RTCLAW_SWARM_ENABLE
    SHELL_CMD("/nodes",         cmd_nodes,         "Show swarm node table"),
#endif
};

int shell_common_command_count(void)
{
    return SHELL_CMD_COUNT(shell_common_commands);
}
