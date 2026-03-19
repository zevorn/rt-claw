/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Task scheduler — intrusive linked list, sorted by next_run_ms.
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/core/scheduler.h"
#include "utils/list.h"

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
    claw_list_node_t node;
} sched_task_t;

static CLAW_LIST_HEAD(s_tasks);
static int s_task_count;
static struct claw_mutex *s_lock;
static struct claw_thread *s_thread;

static sched_task_t *find_task(const char *name)
{
    claw_list_node_t *pos;

    claw_list_for_each(pos, &s_tasks) {
        sched_task_t *t = claw_list_entry(pos, sched_task_t, node);
        if (strcmp(t->name, name) == 0) {
            return t;
        }
    }
    return NULL;
}

static void insert_sorted(sched_task_t *task)
{
    claw_list_node_t *pos;

    claw_list_for_each(pos, &s_tasks) {
        sched_task_t *t = claw_list_entry(pos, sched_task_t, node);
        if ((int32_t)(task->next_run_ms - t->next_run_ms) < 0) {
            /* Insert before this node */
            claw_list__insert(&task->node, pos->prev, pos);
            return;
        }
    }
    claw_list_add_tail(&task->node, &s_tasks);
}

static void sched_thread(void *arg)
{
    (void)arg;
    claw_list_node_t *pos;

    while (!claw_thread_should_exit()) {
        claw_thread_delay_ms(CLAW_SCHED_TICK_MS);
        uint32_t now = claw_tick_ms();

        claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

        /*
         * Scan the sorted task list for due items.  After each
         * callback, restart from the head because the callback
         * may add/remove tasks, invalidating saved iterators.
         */
    rescan:
        for (pos = s_tasks.next; pos != &s_tasks; pos = pos->next) {
            sched_task_t *t = claw_list_entry(pos, sched_task_t, node);

            if (t->remaining == 0) {
                continue;
            }
            if ((int32_t)(now - t->next_run_ms) < 0) {
                break;  /* sorted: no more tasks due */
            }

            sched_callback_t cb = t->callback;
            void *cb_arg = t->arg;

            t->next_run_ms = now + t->interval_ms;
            if (t->remaining > 0) {
                t->remaining--;
                if (t->remaining == 0) {
                    claw_list_del(&t->node);
                    s_task_count--;
                    claw_mutex_unlock(s_lock);
                    cb(cb_arg);
                    claw_free(t);
                    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
                    goto rescan;
                }
            }

            /* Re-insert at sorted position */
            claw_list_del(&t->node);
            insert_sorted(t);

            claw_mutex_unlock(s_lock);
            cb(cb_arg);
            claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
            goto rescan;
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

    s_task_count = 0;

    s_thread = claw_thread_create("sched", sched_thread, NULL,
                                    CLAW_SCHED_THREAD_STACK,
                                    CLAW_SCHED_THREAD_PRIO);
    if (!s_thread) {
        CLAW_LOGE(TAG, "thread create failed");
        claw_mutex_delete(s_lock);
        s_lock = NULL;
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "started (intrusive list), tick=%dms",
              CLAW_SCHED_TICK_MS);
    return CLAW_OK;
}

void sched_stop(void)
{
    claw_thread_delete(s_thread);
    s_thread = NULL;

    /* Free remaining tasks */
    if (s_lock) {
        claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
        claw_list_node_t *pos;
        claw_list_node_t *tmp;

        claw_list_for_each_safe(pos, tmp, &s_tasks) {
            sched_task_t *t = claw_list_entry(pos, sched_task_t, node);
            claw_list_del(&t->node);
            claw_free(t);
        }
        s_task_count = 0;
        claw_mutex_unlock(s_lock);

        claw_mutex_delete(s_lock);
        s_lock = NULL;
    }

    CLAW_LOGI(TAG, "stopped");
}

int sched_add(const char *name, uint32_t interval_ms, int32_t count,
              sched_callback_t cb, void *arg)
{
    if (!name || name[0] == '\0' || !cb || interval_ms == 0) {
        return CLAW_ERROR;
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    if (find_task(name)) {
        claw_mutex_unlock(s_lock);
        CLAW_LOGW(TAG, "duplicate task '%s', rejected", name);
        return CLAW_ERROR;
    }

    if (s_task_count >= CLAW_SCHED_MAX_TASKS) {
        claw_mutex_unlock(s_lock);
        CLAW_LOGW(TAG, "task limit reached (%d), '%s' rejected",
                  CLAW_SCHED_MAX_TASKS, name);
        return CLAW_ERROR;
    }

    sched_task_t *t = claw_malloc(sizeof(*t));
    if (!t) {
        claw_mutex_unlock(s_lock);
        CLAW_LOGE(TAG, "no memory for '%s'", name);
        return CLAW_ERROR;
    }

    snprintf(t->name, sizeof(t->name), "%s", name);
    t->interval_ms = interval_ms;
    t->next_run_ms = claw_tick_ms() + interval_ms;
    t->remaining   = count;
    t->callback    = cb;
    t->arg         = arg;
    claw_list_init(&t->node);
    insert_sorted(t);
    s_task_count++;

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

    sched_task_t *t = find_task(name);
    if (t) {
        claw_list_del(&t->node);
        s_task_count--;
        claw_mutex_unlock(s_lock);
        CLAW_LOGI(TAG, "removed '%s'", name);
        claw_free(t);
        return CLAW_OK;
    }

    claw_mutex_unlock(s_lock);
    return CLAW_ERROR;
}

void sched_list(void)
{
    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    printf("tasks: %d\n", s_task_count);

    claw_list_node_t *pos;

    claw_list_for_each(pos, &s_tasks) {
        sched_task_t *t = claw_list_entry(pos, sched_task_t, node);
        printf("  %-20s  every %5ums  remaining=%d\n",
               t->name, (unsigned)t->interval_ms, (int)t->remaining);
    }

    claw_mutex_unlock(s_lock);
}

int sched_list_to_buf(char *buf, size_t size)
{
    if (!buf || size == 0) {
        return 0;
    }

    int off = 0;

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    off += snprintf(buf + off, size - off,
                    "%d active task(s):\n", s_task_count);

    claw_list_node_t *pos;

    claw_list_for_each(pos, &s_tasks) {
        sched_task_t *t = claw_list_entry(pos, sched_task_t, node);
        if ((size_t)off < size - 1) {
            off += snprintf(buf + off, size - off,
                            "- %s: every %us, remaining=%d\n",
                            t->name,
                            (unsigned)(t->interval_ms / 1000),
                            (int)t->remaining);
        }
    }

    claw_mutex_unlock(s_lock);
    return off;
}

int sched_task_count(void)
{
    int count;

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
    count = s_task_count;
    claw_mutex_unlock(s_lock);

    return count;
}

/* OOP service registration */
#include "claw/core/claw_service.h"
#ifdef CONFIG_RTCLAW_SCHED_ENABLE
static const char *sched_deps[] = { NULL };
CLAW_DEFINE_SIMPLE_SERVICE(sched, "sched",
    sched_init, NULL, sched_stop, sched_deps);
#endif
