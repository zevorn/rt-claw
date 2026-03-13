/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Task scheduler — runs callbacks at fixed intervals.
 */

#ifndef CLAW_SCHEDULER_H
#define CLAW_SCHEDULER_H

#include "osal/claw_os.h"
#include "claw/claw_config.h"

typedef void (*sched_callback_t)(void *arg);

int  sched_init(void);
int  sched_add(const char *name, uint32_t interval_ms, int32_t count,
               sched_callback_t cb, void *arg);
int  sched_remove(const char *name);
void sched_list(void);
int  sched_task_count(void);

#endif /* CLAW_SCHEDULER_H */
