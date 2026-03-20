/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Global compile-time configuration for rt-claw.
 *
 * This is the single source of truth for all rt-claw business configuration.
 * Credentials and tunable parameters are set by the Meson build system
 * (via meson options or environment variables) and written into
 * claw_config_generated.h.  The #ifndef guards below provide defaults
 * when a value is not set by the build system.
 *
 * Configuration methods (priority high to low):
 *   1. Meson option:   meson setup ... -Dai_api_key='sk-...'
 *   2. Environment var: export RTCLAW_AI_API_KEY='sk-...'
 *   3. Defaults below
 *
 * Feature flags (CONFIG_RTCLAW_*_ENABLE, CONFIG_RTCLAW_TOOL_*) are set
 * per-platform:
 *   - ESP-IDF:   Kconfig -> sdkconfig -> compiler flags
 *   - RT-Thread:  CFLAGS in rtconfig.py
 */

#ifndef CLAW_CONFIG_H
#define CLAW_CONFIG_H

/*
 * Include build-system generated overrides.
 * This header is produced by Meson's configure_file() and lives in the
 * build directory.  Values set here take precedence over the #ifndef
 * defaults below.
 */
#ifdef CLAW_HAS_GENERATED_CONFIG
#include "claw_gen_config.h"
#endif

#ifndef RT_CLAW_VERSION
#define RT_CLAW_VERSION         "0.2.0"
#endif

/* ---- Gateway ---- */
#define CLAW_GW_MSG_POOL_SIZE   16
#define CLAW_GW_MSG_MAX_LEN     256
#define CLAW_GW_THREAD_STACK    4096
#define CLAW_GW_THREAD_PRIO     15

/* ---- Swarm ---- */
#define CLAW_SWARM_MAX_NODES        32
#define CLAW_SWARM_HEARTBEAT_MS     5000
#define CLAW_SWARM_TIMEOUT_MS       15000
#define CLAW_SWARM_PORT             5300
#define CLAW_SWARM_THREAD_STACK     4096
#define CLAW_SWARM_THREAD_PRIO      12

/* ---- Scheduler ---- */
#define CLAW_SCHED_MAX_TASKS        8
#define CLAW_SCHED_TICK_MS          1000
#define CLAW_SCHED_THREAD_STACK     8192
#define CLAW_SCHED_THREAD_PRIO      10

/* ---- Heartbeat (periodic AI check-in) ---- */
#define CLAW_HEARTBEAT_INTERVAL_MS  300000  /* 5 minutes */
#define CLAW_HEARTBEAT_MAX_EVENTS   8
#define CLAW_HEARTBEAT_MSG_MAX      128
#define CLAW_HEARTBEAT_PROMPT_MAX   1024
#define CLAW_HEARTBEAT_REPLY_MAX    512
#define CLAW_HEARTBEAT_THREAD_STACK 8192

/* ---- AI Engine (worker thread) ---- */
#define CLAW_AI_QUEUE_DEPTH         4
#define CLAW_AI_WORKER_STACK        8192
#define CLAW_AI_WORKER_PRIO         15

/* ---- AI Engine (API) ---- */
#ifndef CONFIG_RTCLAW_AI_API_KEY
#define CONFIG_RTCLAW_AI_API_KEY      ""
#endif
#ifndef CONFIG_RTCLAW_AI_API_URL
#define CONFIG_RTCLAW_AI_API_URL      "http://10.0.2.2:8888/v1/messages"
#endif
#ifndef CONFIG_RTCLAW_AI_MODEL
#define CONFIG_RTCLAW_AI_MODEL        "claude-opus-4-6"
#endif
#ifndef CONFIG_RTCLAW_AI_MAX_TOKENS
#define CONFIG_RTCLAW_AI_MAX_TOKENS   1024
#endif
#ifndef CONFIG_RTCLAW_AI_CONTEXT_SIZE
#define CONFIG_RTCLAW_AI_CONTEXT_SIZE 8192
#endif
#ifndef CONFIG_RTCLAW_AI_MEMORY_MAX_MSGS
#define CONFIG_RTCLAW_AI_MEMORY_MAX_MSGS 20
#endif

/* ---- Feishu (Lark) IM ---- */
#ifndef CONFIG_RTCLAW_FEISHU_APP_ID
#define CONFIG_RTCLAW_FEISHU_APP_ID   ""
#endif
#ifndef CONFIG_RTCLAW_FEISHU_APP_SECRET
#define CONFIG_RTCLAW_FEISHU_APP_SECRET ""
#endif

/* ---- Telegram Bot IM ---- */
#ifndef CONFIG_RTCLAW_TELEGRAM_BOT_TOKEN
#define CONFIG_RTCLAW_TELEGRAM_BOT_TOKEN ""
#endif
#ifndef CONFIG_RTCLAW_TELEGRAM_API_URL
#define CONFIG_RTCLAW_TELEGRAM_API_URL   "https://api.telegram.org"
#endif

/* ---- OTA (Over-The-Air update) ---- */
#define CLAW_OTA_THREAD_STACK       8192
#define CLAW_OTA_THREAD_PRIO        10
#ifndef CONFIG_RTCLAW_OTA_URL
#define CONFIG_RTCLAW_OTA_URL       ""
#endif
#ifndef CONFIG_RTCLAW_OTA_CHECK_INTERVAL_MS
#define CONFIG_RTCLAW_OTA_CHECK_INTERVAL_MS  0   /* 0 = manual only */
#endif

#endif /* CLAW_CONFIG_H */
