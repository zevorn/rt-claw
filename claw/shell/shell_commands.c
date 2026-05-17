/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Common shell command implementations shared across all platforms.
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/core/errno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "claw/shell/shell_commands.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/services/ai/ai_memory.h"
#include "claw/services/tools/tools.h"
#include "utils/list.h"
#include "claw/services/im/feishu.h"
#include "claw/services/im/telegram.h"
#include "claw/services/net/net_service.h"
#ifdef CONFIG_RTCLAW_VOICE_ENABLE
#include "claw/services/voice/voice_service.h"
#ifdef CONFIG_RTCLAW_LINUX_WEB_VOICE_ENABLE
#include "platform/linux/web_voice_server.h"
#endif
#ifdef CONFIG_RTCLAW_LINUX_LOCAL_VOICE_ENABLE
#include "platform/linux/local_voice_endpoint.h"
#endif
#endif

#ifdef CONFIG_RTCLAW_SKILL_ENABLE
#include "claw/services/ai/ai_skill.h"
#endif
#ifdef CONFIG_RTCLAW_SCHED_ENABLE
#include "claw/services/sched.h"
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
        claw_printf("[error] KV save failed\n");
    }
}

void shell_nvs_config_load(void)
{
    char buf[1024];

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

#ifdef CONFIG_RTCLAW_VOICE_ENABLE
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "enabled",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_enabled(strcmp(buf, "on") == 0 ||
                                 strcmp(buf, "1") == 0);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "web_port",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_web_port((int)atoi(buf));
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "endpoint_backend",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("endpoint_backend", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "stt_provider",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("stt_provider", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "stt_url",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("stt_url", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "stt_key",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("stt_key", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "stt_model",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("stt_model", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "stt_xfyun_app_id",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("stt_xfyun_app_id", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "stt_xfyun_api_key",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("stt_xfyun_api_key", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "stt_xfyun_api_secret",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("stt_xfyun_api_secret", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "stt_timeout_ms",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("stt_timeout_ms", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "input_sample_rate",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("input_sample_rate", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "input_channels",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("input_channels", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "input_bits_per_sample",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("input_bits_per_sample", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "input_encoding",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("input_encoding", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "tts_provider",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("tts_provider", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "tts_url",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("tts_url", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "tts_key",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("tts_key", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "tts_model",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("tts_model", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "tts_voice",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("tts_voice", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "tts_style_prompt",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("tts_style_prompt", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "tts_format",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("tts_format", buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "tts_stream",
                        buf, sizeof(buf)) == CLAW_OK) {
        voice_config_set_string("tts_stream", buf);
    }
#ifdef CONFIG_RTCLAW_LINUX_LOCAL_VOICE_ENABLE
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "local_input",
                        buf, sizeof(buf)) == CLAW_OK) {
        local_voice_endpoint_set_input(buf);
    }
    if (claw_kv_get_str(SHELL_NVS_NS_VOICE, "local_output",
                        buf, sizeof(buf)) == CLAW_OK) {
        local_voice_endpoint_set_output(buf);
    }
#endif
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
            claw_printf("Log level: %s\n",
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
            claw_printf("Usage: /log level <error|warn|info|debug>\n");
            return;
        }
        claw_log_set_level(lv);
        claw_printf("Log level: %s\n", log_level_name(lv));
        return;
    } else {
        claw_printf("Usage: /log [on|off|level <error|warn|info|debug>]\n");
        return;
    }
    claw_printf("Log output: %s\n",
           claw_log_get_enabled() ? "ON" : "OFF");
}

static void cmd_history(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    claw_printf("Conversation memory: %d messages\n", ai_memory_count());
}

static void cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ai_memory_clear();
    claw_printf("Conversation memory cleared.\n");
}

static void cmd_ai_set(int argc, char **argv)
{
    if (argc < 3) {
        claw_printf("Usage: /ai_set key|url|model <value>\n");
        return;
    }

    const char *field = argv[1];
    const char *value = argv[2];

    if (strcmp(field, "key") == 0) {
        ai_set_api_key(value);
        shell_nvs_save_str(SHELL_NVS_NS_AI, "api_key", value);
        claw_printf("API key saved (effective immediately).\n");
    } else if (strcmp(field, "url") == 0) {
        ai_set_api_url(value);
        shell_nvs_save_str(SHELL_NVS_NS_AI, "api_url", value);
        claw_printf("API URL saved: %s\n", value);
    } else if (strcmp(field, "model") == 0) {
        ai_set_model(value);
        shell_nvs_save_str(SHELL_NVS_NS_AI, "model", value);
        claw_printf("Model saved: %s\n", value);
    } else {
        claw_printf("Unknown field: %s (use key|url|model)\n", field);
    }
}

static void cmd_ai_status(int argc, char **argv)
{
    const char *key = ai_get_api_key();
    int klen = strlen(key);

    (void)argc;
    (void)argv;
    claw_printf("AI Engine:\n");
    claw_printf("  API Key: %s\n",
           klen == 0 ? "(not set)" :
           klen <= 8 ? "****" : "****...****");
    claw_printf("  API URL: %s\n", ai_get_api_url());
    claw_printf("  Model:   %s\n", ai_get_model());
}

#ifdef CONFIG_RTCLAW_VOICE_ENABLE
#if defined(CONFIG_RTCLAW_LINUX_WEB_VOICE_ENABLE) || \
    defined(CONFIG_RTCLAW_LINUX_LOCAL_VOICE_ENABLE)
static int voice_endpoint_backend_is(const char *backend)
{
    voice_runtime_config_t cfg;

    if (voice_config_snapshot(&cfg) != CLAW_OK) {
        return 0;
    }
    return strcmp(cfg.endpoint_backend, backend) == 0;
}
#endif

static const char *mask_secret(const char *value)
{
    size_t len;

    if (!value || !value[0]) {
        return "(not set)";
    }
    len = strlen(value);
    return len <= 8 ? "****" : "****...****";
}

static void cmd_voice_enable(int argc, char **argv)
{
    if (argc < 2) {
        claw_printf("Usage: /voice_enable on|off\n");
        return;
    }

    if (strcmp(argv[1], "on") == 0) {
        voice_config_set_enabled(1);
        shell_nvs_save_str(SHELL_NVS_NS_VOICE, "enabled", "on");
#ifdef CONFIG_RTCLAW_LINUX_WEB_VOICE_ENABLE
        if (voice_endpoint_backend_is("web")) {
            if (web_voice_server_init() != CLAW_OK) {
                claw_printf("Voice enabled, but web voice init failed.\n");
                return;
            }
            if (!web_voice_server_running() &&
                web_voice_server_start() != CLAW_OK) {
                claw_printf("Voice enabled, but web voice start failed.\n");
                return;
            }
        }
#endif
#ifdef CONFIG_RTCLAW_LINUX_LOCAL_VOICE_ENABLE
        if (voice_endpoint_backend_is("local")) {
            if (local_voice_endpoint_init() != CLAW_OK) {
                claw_printf("Voice enabled, but local voice init failed.\n");
                return;
            }
            if (!local_voice_endpoint_running() &&
                local_voice_endpoint_start() != CLAW_OK) {
                claw_printf("Voice enabled, but local voice start failed.\n");
                return;
            }
        }
#endif
        claw_printf("Voice enabled.\n");
    } else if (strcmp(argv[1], "off") == 0) {
#ifdef CONFIG_RTCLAW_LINUX_WEB_VOICE_ENABLE
        if (web_voice_server_running()) {
            web_voice_server_stop();
        }
#endif
#ifdef CONFIG_RTCLAW_LINUX_LOCAL_VOICE_ENABLE
        if (local_voice_endpoint_running()) {
            local_voice_endpoint_stop();
        }
#endif
        voice_config_set_enabled(0);
        shell_nvs_save_str(SHELL_NVS_NS_VOICE, "enabled", "off");
        claw_printf("Voice disabled.\n");
    } else {
        claw_printf("Usage: /voice_enable on|off\n");
    }
}

static void cmd_voice_set(int argc, char **argv)
{
    char value[VOICE_PROMPT_MAX] = "";
    int off = 0;
    int i;

    if (argc < 3) {
        claw_printf("Usage: /voice_set <field> <value>\n");
        return;
    }

    for (i = 2; i < argc && off < (int)sizeof(value) - 1; i++) {
        if (i > 2) {
            value[off++] = ' ';
        }
        off += snprintf(value + off, sizeof(value) - off, "%s", argv[i]);
    }

    if (strcmp(argv[1], "web_port") == 0) {
        int port = (int)atoi(value);
        if (voice_config_set_web_port(port) != CLAW_OK) {
            claw_printf("[error] invalid web_port\n");
            return;
        }
        shell_nvs_save_str(SHELL_NVS_NS_VOICE, "web_port", value);
    } else {
        if (voice_config_set_string(argv[1], value) != CLAW_OK) {
            claw_printf("[error] unknown field: %s\n", argv[1]);
            return;
        }
        shell_nvs_save_str(SHELL_NVS_NS_VOICE, argv[1], value);
    }
    claw_printf("Voice config saved: %s\n", argv[1]);
}

static void cmd_voice_status(int argc, char **argv)
{
    voice_runtime_config_t cfg;

    (void)argc;
    (void)argv;
    if (voice_config_snapshot(&cfg) != CLAW_OK) {
        claw_printf("Voice config unavailable.\n");
        return;
    }
    claw_printf("Voice:\n");
    claw_printf("  Enabled:          %s\n",
                voice_config_get_enabled() ? "on" : "off");
    claw_printf("  State:            %s\n",
                voice_state_name(voice_state_get()));
    claw_printf("  Endpoint backend: %s\n",
                cfg.endpoint_backend[0] ? cfg.endpoint_backend : "(not set)");
    claw_printf("  Web port:         %d\n", cfg.web_port);
#ifdef CONFIG_RTCLAW_LINUX_WEB_VOICE_ENABLE
    claw_printf("  Web running:      %s\n",
                web_voice_server_running() ? "yes" : "no");
#endif
#ifdef CONFIG_RTCLAW_LINUX_LOCAL_VOICE_ENABLE
    claw_printf("  Local running:    %s\n",
                local_voice_endpoint_running() ? "yes" : "no");
    claw_printf("  Local input:      %s\n",
                local_voice_endpoint_get_input()[0] ?
                local_voice_endpoint_get_input() : "default");
    claw_printf("  Local output:     %s\n",
                local_voice_endpoint_get_output()[0] ?
                local_voice_endpoint_get_output() : "default");
#endif
    claw_printf("  Max turn bytes:   %d\n", CONFIG_RTCLAW_VOICE_MAX_TURN_BYTES);
    claw_printf("  STT Provider:     %s\n",
                cfg.stt_provider[0] ? cfg.stt_provider : "(not set)");
    claw_printf("  STT URL:          %s\n",
                cfg.stt_url[0] ? cfg.stt_url : "(not set)");
    claw_printf("  STT Key:          %s\n", mask_secret(cfg.stt_key));
    claw_printf("  STT Model:        %s\n",
                cfg.stt_model[0] ? cfg.stt_model : "(not set)");
    claw_printf("  XFYUN App ID:     %s\n", mask_secret(cfg.stt_xfyun_app_id));
    claw_printf("  XFYUN API Key:    %s\n",
                mask_secret(cfg.stt_xfyun_api_key));
    claw_printf("  XFYUN API Secret: %s\n",
                mask_secret(cfg.stt_xfyun_api_secret));
    claw_printf("  STT Timeout:      %d ms\n", cfg.stt_timeout_ms);
    claw_printf("  Input Format:     %d Hz, %d ch, %d bit, %s\n",
                cfg.input_sample_rate,
                cfg.input_channels,
                cfg.input_bits_per_sample,
                cfg.input_encoding[0] ? cfg.input_encoding : "(not set)");
    claw_printf("  TTS Provider:     %s\n",
                cfg.tts_provider[0] ? cfg.tts_provider : "(not set)");
    claw_printf("  TTS URL:          %s\n",
                cfg.tts_url[0] ? cfg.tts_url : "(not set)");
    claw_printf("  TTS Key:          %s\n", mask_secret(cfg.tts_key));
    claw_printf("  TTS Model:        %s\n",
                cfg.tts_model[0] ? cfg.tts_model : "(not set)");
    claw_printf("  TTS Voice:        %s\n",
                cfg.tts_voice[0] ? cfg.tts_voice : "(not set)");
    claw_printf("  TTS Style Prompt: %s\n",
                cfg.tts_style_prompt[0] ? cfg.tts_style_prompt : "(not set)");
    claw_printf("  TTS Format:       %s\n",
                cfg.tts_format[0] ? cfg.tts_format : "(not set)");
    claw_printf("  TTS Stream:       %s\n",
                cfg.tts_stream ? "on" : "off");
}

#ifdef CONFIG_RTCLAW_LINUX_LOCAL_VOICE_ENABLE
static void cmd_voice_local(int argc, char **argv)
{
    int rc;

    if (argc < 2) {
        claw_printf("Usage: /voice_local start|stop|cancel|input|output\n");
        return;
    }
    if (strcmp(argv[1], "start") == 0) {
        rc = local_voice_endpoint_capture_start();
    } else if (strcmp(argv[1], "stop") == 0) {
        rc = local_voice_endpoint_capture_stop();
    } else if (strcmp(argv[1], "cancel") == 0) {
        rc = local_voice_endpoint_cancel();
    } else if (strcmp(argv[1], "input") == 0 && argc >= 3) {
        rc = local_voice_endpoint_set_input(argv[2]);
    } else if (strcmp(argv[1], "output") == 0 && argc >= 3) {
        rc = local_voice_endpoint_set_output(argv[2]);
    } else {
        claw_printf("Usage: /voice_local start|stop|cancel|input|output\n");
        return;
    }
    if (rc != CLAW_OK) {
        claw_printf("[error] local voice failed: %s\n", claw_strerror(rc));
        return;
    }
    if (strcmp(argv[1], "input") == 0) {
        shell_nvs_save_str(SHELL_NVS_NS_VOICE, "local_input", argv[2]);
    } else if (strcmp(argv[1], "output") == 0) {
        shell_nvs_save_str(SHELL_NVS_NS_VOICE, "local_output", argv[2]);
    }
    claw_printf("Local voice %s ok.\n", argv[1]);
}
#endif
#endif

static void cmd_feishu_set(int argc, char **argv)
{
    if (argc < 3) {
        claw_printf("Usage: /feishu_set <app_id> <app_secret>\n");
        return;
    }

    feishu_set_app_id(argv[1]);
    feishu_set_app_secret(argv[2]);
    shell_nvs_save_str(SHELL_NVS_NS_FEISHU, "app_id", argv[1]);
    shell_nvs_save_str(SHELL_NVS_NS_FEISHU, "app_secret", argv[2]);
    claw_printf("Feishu credentials saved (reboot to apply).\n");
}

static void cmd_feishu_status(int argc, char **argv)
{
    const char *id = feishu_get_app_id();

    (void)argc;
    (void)argv;
    claw_printf("Feishu:\n");
    claw_printf("  App ID:     %s\n", id[0] ? id : "(not set)");
    claw_printf("  App Secret: %s\n",
           feishu_get_app_secret()[0] ? "****" : "(not set)");
}

static void cmd_telegram_set(int argc, char **argv)
{
    if (argc < 2) {
        claw_printf("Usage: /telegram_set <bot_token>\n");
        return;
    }

    telegram_set_bot_token(argv[1]);
    shell_nvs_save_str(SHELL_NVS_NS_TELEGRAM, "bot_token",
                       argv[1]);
    claw_printf("Telegram token saved (reboot to apply).\n");
}

static void cmd_telegram_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const char *tok = telegram_get_bot_token();

    claw_printf("Telegram:\n");
    claw_printf("  Bot token: %s\n",
           tok[0] ? "****" : "(not set)");
}

static void cmd_remember(int argc, char **argv)
{
    if (argc < 3) {
        claw_printf("Usage: /remember <key> <value...>\n");
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
        claw_printf("Remembered: %s = %s\n", argv[1], value);
    } else {
        claw_printf("[error] failed to save\n");
    }
}

static void cmd_forget(int argc, char **argv)
{
    if (argc < 2) {
        claw_printf("Usage: /forget <key>\n");
        return;
    }

    if (ai_ltm_delete(argv[1]) == CLAW_OK) {
        claw_printf("Forgot: %s\n", argv[1]);
    } else {
        claw_printf("[error] key '%s' not found\n", argv[1]);
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
        claw_printf("%s", buf);
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
        claw_printf("[error] no memory\n");
        return;
    }

    if (ai_skill_execute(argv[1], params,
                         reply, SKILL_REPLY_SIZE) == CLAW_OK) {
        claw_printf("\n" CLR_GREEN "rt-claw> " CLR_RESET "%s\n", reply);
    } else {
        claw_printf("\n" CLR_RED "error> " CLR_RESET "%s\n", reply);
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
            claw_printf("task '%s' removed\n", argv[2]);
        } else {
            claw_printf("task '%s' not found\n", argv[2]);
        }
    } else {
        claw_printf("usage: /task [rm <name>]\n");
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
        claw_printf("Usage: /ota check | update [url]\n");
        return;
    }

    if (strcmp(argv[1], "version") == 0) {
        claw_printf("Running: %s\n", claw_ota_running_version());
    } else if (!claw_ota_supported()) {
        claw_printf("OTA not supported on this platform.\n");
    } else if (strcmp(argv[1], "check") == 0) {
        claw_ota_info_t info;
        int ret = ota_check_update(&info);
        if (ret == 1) {
            claw_printf("Update available: %s\n", info.version);
            claw_printf("  URL:  %s\n", info.url);
            claw_printf("  Size: %lu bytes\n",
                   (unsigned long)info.size);
            claw_printf("Run '/ota update' to install.\n");
        } else if (ret == 0) {
            claw_printf("Firmware is up to date (%s).\n",
                   claw_ota_running_version());
        } else {
            claw_printf("[error] version check failed\n");
        }
    } else if (strcmp(argv[1], "update") == 0) {
        if (argc >= 3) {
            if (ota_trigger_update(argv[2]) == CLAW_OK) {
                claw_printf("OTA update started.\n");
            } else {
                claw_printf("[error] failed to start OTA\n");
            }
        } else {
            claw_ota_info_t uinfo;
            int ur = ota_check_update(&uinfo);
            if (ur == 1) {
                if (ota_trigger_update(uinfo.url)
                        == CLAW_OK) {
                    claw_printf("Updating to %s ...\n",
                           uinfo.version);
                } else {
                    claw_printf("[error] OTA start failed\n");
                }
            } else if (ur == 0) {
                claw_printf("Already up to date (%s).\n",
                       claw_ota_running_version());
            } else {
                claw_printf("[error] version check failed\n");
            }
        }
    } else if (strcmp(argv[1], "rollback") == 0) {
        if (claw_ota_rollback() != CLAW_OK) {
            claw_printf("[error] rollback failed\n");
        } else {
            claw_printf("Rolling back firmware ...\n");
        }
    } else {
        claw_printf("Usage: /ota version|check|update|rollback\n");
    }
}
#endif

static void cmd_ip(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    claw_printf("Network:\n");
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

    claw_printf("Registered tools (%d):\n", claw_tool_core_count());
    claw_list_for_each(pos, head) {
        struct claw_tool *t = claw_list_entry(pos, struct claw_tool,
                                               node);
        claw_printf("  %d. %-20s %s\n", ++i, t->name,
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
#ifdef CONFIG_RTCLAW_VOICE_ENABLE
    SHELL_CMD("/voice_enable",  cmd_voice_enable,  "Enable or disable voice"),
    SHELL_CMD("/voice_set",     cmd_voice_set,     "Set voice config (NVS)"),
    SHELL_CMD("/voice_status",  cmd_voice_status,  "Show voice config"),
#ifdef CONFIG_RTCLAW_LINUX_LOCAL_VOICE_ENABLE
    SHELL_CMD("/voice_local",   cmd_voice_local,   "Control local voice I/O"),
#endif
#endif
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

/* ---- Extra command table registry ---- */

#define MAX_EXTRA_TABLES 4

static struct {
    const shell_cmd_t *table;
    int count;
} s_extra_tables[MAX_EXTRA_TABLES];
static int s_extra_count;

void shell_register_cmd_table(const shell_cmd_t *table, int count)
{
    if (s_extra_count < MAX_EXTRA_TABLES && table && count > 0) {
        s_extra_tables[s_extra_count].table = table;
        s_extra_tables[s_extra_count].count = count;
        s_extra_count++;
    }
}

/* ---- shell_exec_capture: find + run + capture ---- */

static const shell_cmd_t *find_shell_cmd(const char *name)
{
    /* Search extra tables (platform builtins, board commands) */
    for (int t = 0; t < s_extra_count; t++) {
        for (int i = 0; i < s_extra_tables[t].count; i++) {
            if (strcmp(s_extra_tables[t].table[i].name, name) == 0) {
                return &s_extra_tables[t].table[i];
            }
        }
    }
    /* Search common commands */
    int count = SHELL_CMD_COUNT(shell_common_commands);
    for (int i = 0; i < count; i++) {
        if (strcmp(shell_common_commands[i].name, name) == 0) {
            return &shell_common_commands[i];
        }
    }
    return NULL;
}

int shell_exec_capture(const char *cmd_name, int argc, char **argv,
                       char *buf, size_t buf_size)
{
    if (!cmd_name || !buf || buf_size == 0) {
        return CLAW_ERR_INVALID;
    }
    buf[0] = '\0';

    const shell_cmd_t *cmd = find_shell_cmd(cmd_name);
    if (!cmd) {
        return CLAW_ERR_NOENT;
    }

    claw_printf_capture_start(buf, buf_size);
    cmd->handler(argc, argv);
    claw_printf_capture_stop();

    return CLAW_OK;
}
