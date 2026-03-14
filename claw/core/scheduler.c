/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Task scheduler — dedicated thread polls task array every tick.
 */

#include "osal/claw_os.h"
#include "claw/claw_config.h"
#include "claw/core/scheduler.h"

#include <string.h>
#include <stdio.h>

#define TAG "sched"

typedef struct {
    char             name[24];
    uint32_t         interval_ms;
    uint32_t         next_run_ms;
    int32_t          remaining;     /* -1 = infinite, 0 = done */
    sched_callback_t callback;
    void            *arg;
    int              active;
} sched_task_t;

static sched_task_t s_tasks[CLAW_SCHED_MAX_TASKS];
static claw_mutex_t s_lock;

static void sched_thread(void *arg)
{
    (void)arg;

    while (1) {
        claw_thread_delay_ms(CLAW_SCHED_TICK_MS);
        uint32_t now = claw_tick_ms();

        claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

        for (int i = 0; i < CLAW_SCHED_MAX_TASKS; i++) {
            sched_task_t *t = &s_tasks[i];

            if (!t->active || t->remaining == 0) {
                continue;
            }
            if ((int32_t)(now - t->next_run_ms) < 0) {
                continue;
            }

            /* Time to run */
            sched_callback_t cb = t->callback;
            void *cb_arg = t->arg;

            t->next_run_ms = now + t->interval_ms;
            if (t->remaining > 0) {
                t->remaining--;
                if (t->remaining == 0) {
                    t->active = 0;
                }
            }

            claw_mutex_unlock(s_lock);
            cb(cb_arg);
            claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
        }

        claw_mutex_unlock(s_lock);
    }
}

int sched_init(void)
{
    s_lock = claw_mutex_create("sched");
    if (!s_lock) {
        CLAW_LOGE(TAG, "mutex create failed");
        return CLAW_ERROR;
    }

    memset(s_tasks, 0, sizeof(s_tasks));

    claw_thread_t th = claw_thread_create("sched", sched_thread, NULL,
                                           CLAW_SCHED_THREAD_STACK,
                                           CLAW_SCHED_THREAD_PRIO);
    if (!th) {
        CLAW_LOGE(TAG, "thread create failed");
        claw_mutex_delete(s_lock);
        s_lock = NULL;
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "started, max_tasks=%d, tick=%dms",
              CLAW_SCHED_MAX_TASKS, CLAW_SCHED_TICK_MS);
    return CLAW_OK;
}

int sched_add(const char *name, uint32_t interval_ms, int32_t count,
              sched_callback_t cb, void *arg)
{
    if (!name || !cb || interval_ms == 0) {
        return CLAW_ERROR;
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < CLAW_SCHED_MAX_TASKS; i++) {
        if (!s_tasks[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        claw_mutex_unlock(s_lock);
        CLAW_LOGE(TAG, "no free slot for '%s'", name);
        return CLAW_ERROR;
    }

    sched_task_t *t = &s_tasks[slot];
    snprintf(t->name, sizeof(t->name), "%s", name);
    t->interval_ms = interval_ms;
    t->next_run_ms = claw_tick_ms() + interval_ms;
    t->remaining   = count;
    t->callback    = cb;
    t->arg         = arg;
    t->active      = 1;

    claw_mutex_unlock(s_lock);
    CLAW_LOGI(TAG, "added '%s' every %ums (count=%d)",
              name, (unsigned)interval_ms, (int)count);
    return CLAW_OK;
}

int sched_remove(const char *name)
{
    if (!name) {
        return CLAW_ERROR;
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    for (int i = 0; i < CLAW_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].active && strcmp(s_tasks[i].name, name) == 0) {
            s_tasks[i].active = 0;
            claw_mutex_unlock(s_lock);
            CLAW_LOGI(TAG, "removed '%s'", name);
            return CLAW_OK;
        }
    }

    claw_mutex_unlock(s_lock);
    return CLAW_ERROR;
}

void sched_list(void)
{
    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    int count = 0;
    for (int i = 0; i < CLAW_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].active) {
            count++;
        }
    }

    printf("tasks: %d/%d\n", count, CLAW_SCHED_MAX_TASKS);
    for (int i = 0; i < CLAW_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].active) {
            sched_task_t *t = &s_tasks[i];
            printf("  [%d] %-20s  every %5ums  remaining=%d\n",
                   i, t->name, (unsigned)t->interval_ms,
                   (int)t->remaining);
        }
    }

    claw_mutex_unlock(s_lock);
}

int sched_task_count(void)
{
    int count = 0;

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
    for (int i = 0; i < CLAW_SCHED_MAX_TASKS; i++) {
        if (s_tasks[i].active) {
            count++;
        }
    }
    claw_mutex_unlock(s_lock);

    return count;
}
