/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Global compile-time configuration for rt-claw.
 *
 * Feature flags (CONFIG_CLAW_*_ENABLE, CONFIG_CLAW_TOOL_*) are NOT defined
 * here.  Each platform enables them via its own build config:
 *   - ESP-IDF:   Kconfig -> sdkconfig -> compiler flags
 *   - RT-Thread:  CFLAGS in rtconfig.py
 */

#ifndef CLAW_CONFIG_H
#define CLAW_CONFIG_H

#define RT_CLAW_VERSION         "0.1.0"

/* Gateway */
#define CLAW_GW_MSG_POOL_SIZE   16
#define CLAW_GW_MSG_MAX_LEN     256
#define CLAW_GW_MAX_HANDLERS    8
#define CLAW_GW_THREAD_STACK    4096
#define CLAW_GW_THREAD_PRIO     15

/* Swarm */
#define CLAW_SWARM_MAX_NODES        32
#define CLAW_SWARM_HEARTBEAT_MS     5000
#define CLAW_SWARM_TIMEOUT_MS       15000
#define CLAW_SWARM_PORT             5300
#define CLAW_SWARM_THREAD_STACK     4096
#define CLAW_SWARM_THREAD_PRIO      12

/* Scheduler */
#define CLAW_SCHED_MAX_TASKS        8
#define CLAW_SCHED_TICK_MS          1000
#define CLAW_SCHED_THREAD_STACK     8192
#define CLAW_SCHED_THREAD_PRIO      10

/* AI engine defaults — override per-platform via build flags */
#ifndef CONFIG_CLAW_AI_API_KEY
#define CONFIG_CLAW_AI_API_KEY      ""
#endif
#ifndef CONFIG_CLAW_AI_API_URL
#define CONFIG_CLAW_AI_API_URL      "http://10.0.2.2:8888/v1/messages"
#endif
#ifndef CONFIG_CLAW_AI_MODEL
#define CONFIG_CLAW_AI_MODEL        "claude-opus-4-6"
#endif
#ifndef CONFIG_CLAW_AI_MAX_TOKENS
#define CONFIG_CLAW_AI_MAX_TOKENS   1024
#endif
#ifndef CONFIG_CLAW_AI_MEMORY_MAX_MSGS
#define CONFIG_CLAW_AI_MEMORY_MAX_MSGS 20
#endif

#endif /* CLAW_CONFIG_H */
