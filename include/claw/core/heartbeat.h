/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Heartbeat — periodic AI check-in inspired by OpenClaw.
 * Collects events from services, skips LLM if nothing pending,
 * otherwise summarizes and pushes to the user via IM or console.
 */

#ifndef CLAW_CORE_HEARTBEAT_H
#define CLAW_CORE_HEARTBEAT_H

#include "osal/claw_os.h"

typedef void (*heartbeat_reply_fn_t)(const char *target,
                                     const char *text);

/**
 * Initialize heartbeat and register with scheduler.
 * Requires scheduler to be initialized first.
 */
int heartbeat_init(void);

/**
 * Post an event for the next heartbeat check.
 * Thread-safe. Events are buffered until the next heartbeat tick.
 * Oldest events are dropped if the buffer is full.
 *
 * @param category  Short tag, e.g. "swarm", "sensor", "system"
 * @param message   Event description
 */
void heartbeat_post(const char *category, const char *message);

/**
 * Set the IM reply destination.  When set, heartbeat summaries
 * are pushed via the callback.  Pass NULL to revert to console.
 */
void heartbeat_set_reply(heartbeat_reply_fn_t fn,
                         const char *target);

#endif /* CLAW_CORE_HEARTBEAT_H */
