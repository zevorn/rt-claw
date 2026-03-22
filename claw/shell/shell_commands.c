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
#include "utils/list.h"
#include "claw/services/im/feishu.h"
#include "claw/services/im/telegram.h"
#include "claw/services/net/net_service.h"

#ifdef CONFIG_RTCLAW_SKILL_ENABLE
#include "claw/services/ai/ai_skill.h"
#endif
#ifdef CONFIG_RTCLAW_SCHED_ENABLE
#include "claw/core/scheduler.h"
#endif
#ifdef CONFIG_RTCLAW_SWARM_ENABLE
#include "claw/services/swarm/swarm.h"
#endif
#ifdef CONFIG_RTCLAW_OTA_ENABLE
#include "claw/services/ota/ota_service.h"
#endif

#include "osal/claw_kv.h"

/* ---- KV persistence (platform-independent via OSAL) ---- */

void shell_nvs_save_str(const char *ns, const char *key, const char *val)
{
    if (claw_kv_set_str(ns, key, val) != CLAW_OK) {
        printf("[error] KV save failed\n");
    }
}

void shell_nvs_config_load(void)
{
    char buf[256];

    /* Load AI config from KV (overrides compile-time defaults) */
    if (claw_kv_get_str(SHELL_NVS_NS_AI, "api_key",
                        buf, sizeof(buf)) == CLAW_OK) {
        ai_set_api_key(buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_AI, "api_url",
                        buf, sizeof(buf)) == CLAW_OK) {
        ai_set_api_url(buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_AI, "model",
                        buf, sizeof(buf)) == CLAW_OK) {
        ai_set_model(buf);
    }

    /* Load Feishu config from KV */
    if (claw_kv_get_str(SHELL_NVS_NS_FEISHU, "app_id",
                        buf, sizeof(buf)) == CLAW_OK) {
        feishu_set_app_id(buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_FEISHU, "app_secret",
                        buf, sizeof(buf)) == CLAW_OK) {
        feishu_set_app_secret(buf);
    }

    /* Load Telegram config from KV */
    if (claw_kv_get_str(SHELL_NVS_NS_TELEGRAM, "bot_token",
                        buf, sizeof(buf)) == CLAW_OK) {
        telegram_set_bot_token(buf);
    }
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

static void cmd_telegram_set(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: /telegram_set <bot_token>\n");
        return;
    }

    telegram_set_bot_token(argv[1]);
    shell_nvs_save_str(SHELL_NVS_NS_TELEGRAM, "bot_token",
                       argv[1]);
    printf("Telegram token saved (reboot to apply).\n");
}

static void cmd_telegram_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const char *tok = telegram_get_bot_token();

    printf("Telegram:\n");
    printf("  Bot token: %s\n",
           tok[0] ? "****" : "(not set)");
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

static void cmd_skills(int argc, char **argv)
{
    if (argc < 2) {
        char buf[512];
        ai_skill_list_to_buf(buf, sizeof(buf));
        printf("%s", buf);
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
        printf("\n" CLR_GREEN "rt-claw> " CLR_RESET "%s\n", reply);
    } else {
        printf("\n" CLR_RED "error> " CLR_RESET "%s\n", reply);
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

#ifdef CONFIG_RTCLAW_OTA_ENABLE
static void cmd_ota(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: /ota check | update [url]\n");
        return;
    }

    if (strcmp(argv[1], "version") == 0) {
        printf("Running: %s\n", claw_ota_running_version());
    } else if (!claw_ota_supported()) {
        printf("OTA not supported on this platform.\n");
    } else if (strcmp(argv[1], "check") == 0) {
        claw_ota_info_t info;
        int ret = ota_check_update(&info);
        if (ret == 1) {
            printf("Update available: %s\n", info.version);
            printf("  URL:  %s\n", info.url);
            printf("  Size: %lu bytes\n",
                   (unsigned long)info.size);
            printf("Run '/ota update' to install.\n");
        } else if (ret == 0) {
            printf("Firmware is up to date (%s).\n",
                   claw_ota_running_version());
        } else {
            printf("[error] version check failed\n");
        }
    } else if (strcmp(argv[1], "update") == 0) {
        if (argc >= 3) {
            if (ota_trigger_update(argv[2]) == CLAW_OK) {
                printf("OTA update started.\n");
            } else {
                printf("[error] failed to start OTA\n");
            }
        } else {
            claw_ota_info_t uinfo;
            int ur = ota_check_update(&uinfo);
            if (ur == 1) {
                if (ota_trigger_update(uinfo.url)
                        == CLAW_OK) {
                    printf("Updating to %s ...\n",
                           uinfo.version);
                } else {
                    printf("[error] OTA start failed\n");
                }
            } else if (ur == 0) {
                printf("Already up to date (%s).\n",
                       claw_ota_running_version());
            } else {
                printf("[error] version check failed\n");
            }
        }
    } else if (strcmp(argv[1], "rollback") == 0) {
        if (claw_ota_rollback() != CLAW_OK) {
            printf("[error] rollback failed\n");
        } else {
            printf("Rolling back firmware ...\n");
        }
    } else {
        printf("Usage: /ota version|check|update|rollback\n");
    }
}
#endif

static void cmd_ip(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Network:\n");
    net_print_ipinfo();
}

/* ---- /tools: list registered AI tools ---- */

static void cmd_tools(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    claw_list_node_t *head = claw_tool_core_list();
    claw_list_node_t *pos;
    int i = 0;

    printf("Registered tools (%d):\n", claw_tool_core_count());
    claw_list_for_each(pos, head) {
        struct claw_tool *t = claw_list_entry(pos, struct claw_tool,
                                               node);
        printf("  %d. %-20s %s\n", ++i, t->name,
               t->description ? t->description : "");
    }
}

/* ---- Common command table ---- */

const shell_cmd_t shell_common_commands[] = {
    SHELL_CMD("/log",           cmd_log,           "Log [on|off|level <lvl>]"),
    SHELL_CMD("/history",       cmd_history,       "Show conversation count"),
    SHELL_CMD("/clear",         cmd_clear,         "Clear conversation memory"),
    SHELL_CMD("/ai_set",        cmd_ai_set,        "Set AI config (NVS)"),
    SHELL_CMD("/ai_status",     cmd_ai_status,     "Show AI config"),
    SHELL_CMD("/feishu_set",    cmd_feishu_set,    "Set Feishu creds (NVS)"),
    SHELL_CMD("/feishu_status", cmd_feishu_status, "Show Feishu config"),
    SHELL_CMD("/telegram_set",  cmd_telegram_set,  "Set Telegram token"),
    SHELL_CMD("/telegram_status", cmd_telegram_status, "Show Telegram config"),
    SHELL_CMD("/tools",         cmd_tools,         "List registered AI tools"),
    SHELL_CMD("/ip",            cmd_ip,            "Show IP address"),
    SHELL_CMD("/remember",      cmd_remember,      "Save to long-term memory"),
    SHELL_CMD("/forget",        cmd_forget,        "Delete a long-term memory"),
    SHELL_CMD("/memories",      cmd_memories,      "List long-term memories"),
#ifdef CONFIG_RTCLAW_SKILL_ENABLE
    SHELL_CMD("/skills",        cmd_skills,        "List or execute a skill"),
#endif
#ifdef CONFIG_RTCLAW_SCHED_ENABLE
    SHELL_CMD("/task",          cmd_task,           "Tasks [rm <name>]"),
#endif
#ifdef CONFIG_RTCLAW_SWARM_ENABLE
    SHELL_CMD("/nodes",         cmd_nodes,         "Show swarm node table"),
#endif
#ifdef CONFIG_RTCLAW_OTA_ENABLE
    SHELL_CMD("/ota",           cmd_ota,           "OTA update management"),
#endif
};

int shell_common_command_count(void)
{
    return SHELL_CMD_COUNT(shell_common_commands);
}
