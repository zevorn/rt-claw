/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL implementation for RT-Thread.
 * Uses struct embedding (Linux kernel OOP style): each OSAL primitive
 * has a private sub-struct with the base as the first member, recovered
 * via container_of().
 */

#include "osal/claw_os.h"
#include <rtthread.h>

#include <stdio.h>
#include <string.h>

#include "utils/list.h"  /* for container_of */

/* ---------- private sub-structs ---------- */

struct rtthread_thread {
    struct claw_thread base;
    rt_thread_t        handle;
};

struct rtthread_mutex {
    struct claw_mutex base;
    rt_mutex_t        handle;
};

struct rtthread_sem {
    struct claw_sem base;
    rt_sem_t        handle;
};

struct rtthread_mq {
    struct claw_mq base;
    rt_mq_t        handle;
};

struct rtthread_timer {
    struct claw_timer base;
    rt_timer_t        handle;
};

/* ---------- helpers ---------- */

static inline int32_t ms_to_tick(uint32_t ms)
{
    if (ms == CLAW_WAIT_FOREVER) {
        return RT_WAITING_FOREVER;
    }
    if (ms == CLAW_NO_WAIT) {
        return RT_WAITING_NO;
    }
    return rt_tick_from_millisecond(ms);
}

/* ---------- Thread ---------- */

struct claw_thread *claw_thread_create(const char *name,
                                       void (*entry)(void *arg),
                                       void *arg,
                                       uint32_t stack_size,
                                       uint32_t priority)
{
    struct rtthread_thread *rt = rt_malloc(sizeof(*rt));

    if (!rt) {
        return NULL;
    }
    rt->base.name = name;
    rt->base.priority = priority;
    rt->base.stack_size = stack_size;
    rt->handle = rt_thread_create(name, entry, arg,
                                  stack_size, priority, 20);
    if (rt->handle == RT_NULL) {
        rt_free(rt);
        return NULL;
    }
    rt_thread_startup(rt->handle);
    return &rt->base;
}

void claw_thread_delete(struct claw_thread *thread)
{
    if (!thread) {
        return;
    }
    struct rtthread_thread *rt = container_of(thread,
                                              struct rtthread_thread,
                                              base);
    rt_thread_delete(rt->handle);
    rt_free(rt);
}

void claw_thread_delay_ms(uint32_t ms)
{
    rt_thread_mdelay(ms);
}

void claw_thread_yield(void)
{
    rt_thread_yield();
}

int claw_thread_should_exit(void)
{
    return 0;
}

/* ---------- Mutex ---------- */

struct claw_mutex *claw_mutex_create(const char *name)
{
    struct rtthread_mutex *rt = rt_malloc(sizeof(*rt));

    if (!rt) {
        return NULL;
    }
    rt->base.name = name;
    rt->handle = rt_mutex_create(name, RT_IPC_FLAG_PRIO);
    if (rt->handle == RT_NULL) {
        rt_free(rt);
        return NULL;
    }
    return &rt->base;
}

int claw_mutex_lock(struct claw_mutex *mutex, uint32_t timeout_ms)
{
    if (!mutex) {
        return CLAW_ERR_INVALID;
    }
    struct rtthread_mutex *rt = container_of(mutex,
                                             struct rtthread_mutex,
                                             base);
    rt_err_t ret = rt_mutex_take(rt->handle, ms_to_tick(timeout_ms));

    if (ret == RT_EOK) {
        return CLAW_OK;
    }
    if (ret == -RT_ETIMEOUT) {
        return CLAW_TIMEOUT;
    }
    return CLAW_ERROR;
}

void claw_mutex_unlock(struct claw_mutex *mutex)
{
    struct rtthread_mutex *rt = container_of(mutex,
                                             struct rtthread_mutex,
                                             base);
    rt_mutex_release(rt->handle);
}

void claw_mutex_delete(struct claw_mutex *mutex)
{
    if (!mutex) {
        return;
    }
    struct rtthread_mutex *rt = container_of(mutex,
                                             struct rtthread_mutex,
                                             base);
    rt_mutex_delete(rt->handle);
    rt_free(rt);
}

/* ---------- Semaphore ---------- */

struct claw_sem *claw_sem_create(const char *name, uint32_t init_value)
{
    struct rtthread_sem *rt = rt_malloc(sizeof(*rt));

    if (!rt) {
        return NULL;
    }
    rt->base.name = name;
    rt->handle = rt_sem_create(name, init_value, RT_IPC_FLAG_PRIO);
    if (rt->handle == RT_NULL) {
        rt_free(rt);
        return NULL;
    }
    return &rt->base;
}

int claw_sem_take(struct claw_sem *sem, uint32_t timeout_ms)
{
    if (!sem) {
        return CLAW_ERR_INVALID;
    }
    struct rtthread_sem *rt = container_of(sem,
                                           struct rtthread_sem,
                                           base);
    rt_err_t ret = rt_sem_take(rt->handle, ms_to_tick(timeout_ms));

    if (ret == RT_EOK) {
        return CLAW_OK;
    }
    if (ret == -RT_ETIMEOUT) {
        return CLAW_TIMEOUT;
    }
    return CLAW_ERROR;
}

void claw_sem_give(struct claw_sem *sem)
{
    struct rtthread_sem *rt = container_of(sem,
                                           struct rtthread_sem,
                                           base);
    rt_sem_release(rt->handle);
}

void claw_sem_delete(struct claw_sem *sem)
{
    if (!sem) {
        return;
    }
    struct rtthread_sem *rt = container_of(sem,
                                           struct rtthread_sem,
                                           base);
    rt_sem_delete(rt->handle);
    rt_free(rt);
}

/* ---------- Message Queue ---------- */

struct claw_mq *claw_mq_create(const char *name,
                                uint32_t msg_size,
                                uint32_t max_msgs)
{
    struct rtthread_mq *rt = rt_malloc(sizeof(*rt));

    if (!rt) {
        return NULL;
    }
    rt->base.name     = name;
    rt->base.msg_size = msg_size;
    rt->base.max_msgs = max_msgs;
    rt->handle = rt_mq_create(name, msg_size, max_msgs, RT_IPC_FLAG_FIFO);
    if (rt->handle == RT_NULL) {
        rt_free(rt);
        return NULL;
    }
    return &rt->base;
}

int claw_mq_send(struct claw_mq *mq, const void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    struct rtthread_mq *rt = container_of(mq, struct rtthread_mq, base);
    rt_err_t ret = rt_mq_send_wait(rt->handle, msg, size,
                                    ms_to_tick(timeout_ms));

    if (ret == RT_EOK) {
        return CLAW_OK;
    }
    if (ret == -RT_ETIMEOUT) {
        return CLAW_TIMEOUT;
    }
    return CLAW_ERROR;
}

int claw_mq_recv(struct claw_mq *mq, void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    struct rtthread_mq *rt = container_of(mq, struct rtthread_mq, base);
    rt_err_t ret = rt_mq_recv(rt->handle, msg, size, ms_to_tick(timeout_ms));

    if (ret == RT_EOK) {
        return CLAW_OK;
    }
    if (ret == -RT_ETIMEOUT) {
        return CLAW_TIMEOUT;
    }
    return CLAW_ERROR;
}

void claw_mq_delete(struct claw_mq *mq)
{
    if (!mq) {
        return;
    }
    struct rtthread_mq *rt = container_of(mq, struct rtthread_mq, base);

    rt_mq_delete(rt->handle);
    rt_free(rt);
}

/* ---------- Timer ---------- */

struct claw_timer *claw_timer_create(const char *name,
                                     void (*callback)(void *arg),
                                     void *arg,
                                     uint32_t period_ms,
                                     int repeat)
{
    struct rtthread_timer *rt = rt_malloc(sizeof(*rt));

    if (!rt) {
        return NULL;
    }
    rt->base.name      = name;
    rt->base.period_ms = period_ms;
    rt->base.repeat    = repeat;

    rt_uint8_t flag = repeat ? RT_TIMER_FLAG_PERIODIC : RT_TIMER_FLAG_ONE_SHOT;
    flag |= RT_TIMER_FLAG_SOFT_TIMER;

    rt->handle = rt_timer_create(name, callback, arg,
                                 rt_tick_from_millisecond(period_ms), flag);
    if (rt->handle == RT_NULL) {
        rt_free(rt);
        return NULL;
    }
    return &rt->base;
}

void claw_timer_start(struct claw_timer *timer)
{
    struct rtthread_timer *rt = container_of(timer,
                                             struct rtthread_timer,
                                             base);
    rt_timer_start(rt->handle);
}

void claw_timer_stop(struct claw_timer *timer)
{
    struct rtthread_timer *rt = container_of(timer,
                                             struct rtthread_timer,
                                             base);
    rt_timer_stop(rt->handle);
}

void claw_timer_delete(struct claw_timer *timer)
{
    if (!timer) {
        return;
    }
    struct rtthread_timer *rt = container_of(timer,
                                             struct rtthread_timer,
                                             base);
    rt_timer_delete(rt->handle);
    rt_free(rt);
}

/* ---------- Memory ---------- */

void *claw_malloc(size_t size)
{
    return rt_malloc(size);
}

void *claw_calloc(size_t nmemb, size_t size)
{
    return rt_calloc(nmemb, size);
}

void claw_free(void *ptr)
{
    rt_free(ptr);
}

/* ---------- Log ---------- */

static int s_log_enabled = 1;
static int s_log_level = CLAW_LOG_DEBUG;  /* show all by default */
static const char *level_str[] = { "E", "W", "I", "D" };

void claw_log_set_enabled(int enabled)
{
    s_log_enabled = enabled;
}

int claw_log_get_enabled(void)
{
    return s_log_enabled;
}

void claw_log_set_level(int level)
{
    if (level < CLAW_LOG_ERROR) {
        level = CLAW_LOG_ERROR;
    }
    if (level > CLAW_LOG_DEBUG) {
        level = CLAW_LOG_DEBUG;
    }
    s_log_level = level;
}

int claw_log_get_level(void)
{
    return s_log_level;
}

void claw_log(int level, const char *tag, const char *fmt, ...)
{
    va_list ap;

    if (!s_log_enabled || level > s_log_level) {
        return;
    }
    if (level < 0) {
        level = 0;
    }
    if (level > 3) {
        level = 3;
    }
    rt_kprintf("[%s/%s] ", level_str[level], tag);
    va_start(ap, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    rt_kprintf("%s\n", buf);
}

void claw_log_raw(const char *fmt, ...)
{
    va_list ap;
    char buf[256];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    rt_kprintf("%s", buf);
}

/* ---------- Time ---------- */

uint32_t claw_tick_get(void)
{
    return (uint32_t)rt_tick_get();
}

uint32_t claw_tick_ms(void)
{
    return (uint32_t)(rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
}
