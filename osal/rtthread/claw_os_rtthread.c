/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL implementation for RT-Thread.
 */

#include "osal/claw_os.h"
#include <rtthread.h>

#include <stdio.h>
#include <string.h>

/* ---------- helpers ---------- */

static inline int32_t ms_to_tick(uint32_t ms)
{
    if (ms == CLAW_WAIT_FOREVER)
        return RT_WAITING_FOREVER;
    if (ms == CLAW_NO_WAIT)
        return RT_WAITING_NO;
    return rt_tick_from_millisecond(ms);
}

/* ---------- Thread ---------- */

claw_thread_t claw_thread_create(const char *name,
                                  void (*entry)(void *arg),
                                  void *arg,
                                  uint32_t stack_size,
                                  uint32_t priority)
{
    rt_thread_t t = rt_thread_create(name, entry, arg,
                                      stack_size, priority, 20);
    if (t == RT_NULL)
        return NULL;
    rt_thread_startup(t);
    return (claw_thread_t)t;
}

void claw_thread_delete(claw_thread_t thread)
{
    if (thread)
        rt_thread_delete((rt_thread_t)thread);
}

void claw_thread_delay_ms(uint32_t ms)
{
    rt_thread_mdelay(ms);
}

void claw_thread_yield(void)
{
    rt_thread_yield();
}

/* ---------- Mutex ---------- */

claw_mutex_t claw_mutex_create(const char *name)
{
    return (claw_mutex_t)rt_mutex_create(name, RT_IPC_FLAG_PRIO);
}

int claw_mutex_lock(claw_mutex_t mutex, uint32_t timeout_ms)
{
    rt_err_t ret = rt_mutex_take((rt_mutex_t)mutex, ms_to_tick(timeout_ms));
    if (ret == RT_EOK)
        return CLAW_OK;
    if (ret == -RT_ETIMEOUT)
        return CLAW_TIMEOUT;
    return CLAW_ERROR;
}

void claw_mutex_unlock(claw_mutex_t mutex)
{
    rt_mutex_release((rt_mutex_t)mutex);
}

void claw_mutex_delete(claw_mutex_t mutex)
{
    if (mutex)
        rt_mutex_delete((rt_mutex_t)mutex);
}

/* ---------- Semaphore ---------- */

claw_sem_t claw_sem_create(const char *name, uint32_t init_value)
{
    return (claw_sem_t)rt_sem_create(name, init_value, RT_IPC_FLAG_PRIO);
}

int claw_sem_take(claw_sem_t sem, uint32_t timeout_ms)
{
    rt_err_t ret = rt_sem_take((rt_sem_t)sem, ms_to_tick(timeout_ms));
    if (ret == RT_EOK)
        return CLAW_OK;
    if (ret == -RT_ETIMEOUT)
        return CLAW_TIMEOUT;
    return CLAW_ERROR;
}

void claw_sem_give(claw_sem_t sem)
{
    rt_sem_release((rt_sem_t)sem);
}

void claw_sem_delete(claw_sem_t sem)
{
    if (sem)
        rt_sem_delete((rt_sem_t)sem);
}

/* ---------- Message Queue ---------- */

claw_mq_t claw_mq_create(const char *name,
                           uint32_t msg_size,
                           uint32_t max_msgs)
{
    return (claw_mq_t)rt_mq_create(name, msg_size, max_msgs,
                                     RT_IPC_FLAG_FIFO);
}

int claw_mq_send(claw_mq_t mq, const void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    rt_err_t ret = rt_mq_send_wait((rt_mq_t)mq, msg, size,
                                    ms_to_tick(timeout_ms));

    if (ret == RT_EOK)
        return CLAW_OK;
    if (ret == -RT_ETIMEOUT)
        return CLAW_TIMEOUT;
    return CLAW_ERROR;
}

int claw_mq_recv(claw_mq_t mq, void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    rt_err_t ret = rt_mq_recv((rt_mq_t)mq, msg, size, ms_to_tick(timeout_ms));
    if (ret == RT_EOK)
        return CLAW_OK;
    if (ret == -RT_ETIMEOUT)
        return CLAW_TIMEOUT;
    return CLAW_ERROR;
}

void claw_mq_delete(claw_mq_t mq)
{
    if (mq)
        rt_mq_delete((rt_mq_t)mq);
}

/* ---------- Timer ---------- */

claw_timer_t claw_timer_create(const char *name,
                                void (*callback)(void *arg),
                                void *arg,
                                uint32_t period_ms,
                                int repeat)
{
    rt_uint8_t flag = repeat ? RT_TIMER_FLAG_PERIODIC : RT_TIMER_FLAG_ONE_SHOT;
    flag |= RT_TIMER_FLAG_SOFT_TIMER;

    rt_timer_t t = rt_timer_create(name, callback, arg,
                                    rt_tick_from_millisecond(period_ms),
                                    flag);
    return (claw_timer_t)t;
}

void claw_timer_start(claw_timer_t timer)
{
    rt_timer_start((rt_timer_t)timer);
}

void claw_timer_stop(claw_timer_t timer)
{
    rt_timer_stop((rt_timer_t)timer);
}

void claw_timer_delete(claw_timer_t timer)
{
    if (timer)
        rt_timer_delete((rt_timer_t)timer);
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
