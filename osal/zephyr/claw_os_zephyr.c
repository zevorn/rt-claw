/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL implementation for Zephyr RTOS.
 * Uses struct embedding (Linux kernel OOP style): each OSAL
 * primitive has a private sub-struct with the base as its first
 * member, recovered via container_of().
 */

#include "osal/claw_os.h"
#include "utils/list.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(claw_osal, LOG_LEVEL_INF);

/* ---------- helpers ---------- */

static inline k_timeout_t ms_to_timeout(uint32_t ms)
{
    if (ms == CLAW_WAIT_FOREVER) {
        return K_FOREVER;
    }
    if (ms == CLAW_NO_WAIT) {
        return K_NO_WAIT;
    }
    return K_MSEC(ms);
}

/* ---------- private sub-structs ---------- */

struct zephyr_thread {
    struct claw_thread  base;
    struct k_thread     handle;
    k_thread_stack_t   *stack;
    void              (*entry)(void *arg);
    void               *arg;
    volatile int        exit_requested; /* set by delete caller */
    volatile int        exited;         /* set by wrapper on return */
};

struct zephyr_mutex {
    struct claw_mutex   base;
    struct k_mutex      handle;
};

struct zephyr_sem {
    struct claw_sem     base;
    struct k_sem        handle;
};

struct zephyr_mq {
    struct claw_mq      base;
    struct k_msgq       handle;
    char               *buffer;
};

struct zephyr_timer {
    struct claw_timer   base;
    struct k_timer      handle;
    void              (*callback)(void *arg);
    void               *arg;
};

/* ---------- TLS for should_exit ---------- */

static __thread struct zephyr_thread *s_tls_self;

/* ---------- Thread ---------- */

static void thread_wrapper(void *p1, void *p2, void *p3)
{
    struct zephyr_thread *zt = p1;

    (void)p2;
    (void)p3;

    s_tls_self = zt;
    zt->entry(zt->arg);
    zt->exited = 1;
}

struct claw_thread *claw_thread_create(const char *name,
                                       void (*entry)(void *arg),
                                       void *arg,
                                       uint32_t stack_size,
                                       uint32_t priority)
{
    struct zephyr_thread *zt = k_malloc(sizeof(*zt));

    if (!zt) {
        return NULL;
    }
    memset(zt, 0, sizeof(*zt));

    zt->stack = k_thread_stack_alloc(stack_size, 0);
    if (!zt->stack) {
        k_free(zt);
        return NULL;
    }

    zt->base.name       = name;
    zt->base.priority   = priority;
    zt->base.stack_size = stack_size;
    zt->entry           = entry;
    zt->arg             = arg;

    int prio = (int)priority;

    if (prio >= CONFIG_NUM_PREEMPT_PRIORITIES) {
        prio = CONFIG_NUM_PREEMPT_PRIORITIES - 1;
    }

    k_thread_create(&zt->handle, zt->stack, stack_size,
                    thread_wrapper, zt, NULL, NULL,
                    prio, 0, K_NO_WAIT);

    if (name) {
        k_thread_name_set(&zt->handle, name);
    }

    return &zt->base;
}

void claw_thread_delete(struct claw_thread *thread)
{
    if (!thread) {
        return;
    }

    struct zephyr_thread *zt =
        container_of(thread, struct zephyr_thread, base);

    zt->exit_requested = 1;

    if (!zt->exited) {
        k_thread_join(&zt->handle, K_FOREVER);
    }

    k_thread_stack_free(zt->stack);
    k_free(zt);
}

void claw_thread_delay_ms(uint32_t ms)
{
    k_msleep((int32_t)ms);
}

void claw_thread_yield(void)
{
    k_yield();
}

int claw_thread_should_exit(void)
{
    if (s_tls_self) {
        return s_tls_self->exit_requested;
    }
    return 0;
}

/* ---------- Mutex ---------- */

struct claw_mutex *claw_mutex_create(const char *name)
{
    struct zephyr_mutex *zm = k_malloc(sizeof(*zm));

    if (!zm) {
        return NULL;
    }
    memset(zm, 0, sizeof(*zm));
    zm->base.name = name;
    k_mutex_init(&zm->handle);
    return &zm->base;
}

int claw_mutex_lock(struct claw_mutex *mutex, uint32_t timeout_ms)
{
    struct zephyr_mutex *zm =
        container_of(mutex, struct zephyr_mutex, base);
    int ret = k_mutex_lock(&zm->handle, ms_to_timeout(timeout_ms));

    if (ret == -EAGAIN || ret == -ETIMEDOUT) {
        return CLAW_TIMEOUT;
    }
    return (ret == 0) ? CLAW_OK : CLAW_ERROR;
}

void claw_mutex_unlock(struct claw_mutex *mutex)
{
    struct zephyr_mutex *zm =
        container_of(mutex, struct zephyr_mutex, base);

    k_mutex_unlock(&zm->handle);
}

void claw_mutex_delete(struct claw_mutex *mutex)
{
    if (!mutex) {
        return;
    }
    struct zephyr_mutex *zm =
        container_of(mutex, struct zephyr_mutex, base);

    k_free(zm);
}

/* ---------- Semaphore ---------- */

struct claw_sem *claw_sem_create(const char *name, uint32_t init_value)
{
    struct zephyr_sem *zs = k_malloc(sizeof(*zs));

    if (!zs) {
        return NULL;
    }
    memset(zs, 0, sizeof(*zs));
    zs->base.name = name;
    k_sem_init(&zs->handle, init_value, K_SEM_MAX_LIMIT);
    return &zs->base;
}

int claw_sem_take(struct claw_sem *sem, uint32_t timeout_ms)
{
    struct zephyr_sem *zs =
        container_of(sem, struct zephyr_sem, base);
    int ret = k_sem_take(&zs->handle, ms_to_timeout(timeout_ms));

    if (ret == -EAGAIN || ret == -ETIMEDOUT) {
        return CLAW_TIMEOUT;
    }
    return (ret == 0) ? CLAW_OK : CLAW_ERROR;
}

void claw_sem_give(struct claw_sem *sem)
{
    struct zephyr_sem *zs =
        container_of(sem, struct zephyr_sem, base);

    k_sem_give(&zs->handle);
}

void claw_sem_delete(struct claw_sem *sem)
{
    if (!sem) {
        return;
    }
    struct zephyr_sem *zs =
        container_of(sem, struct zephyr_sem, base);

    k_free(zs);
}

/* ---------- Message Queue ---------- */

struct claw_mq *claw_mq_create(const char *name,
                                uint32_t msg_size,
                                uint32_t max_msgs)
{
    struct zephyr_mq *zmq = k_malloc(sizeof(*zmq));

    if (!zmq) {
        return NULL;
    }
    memset(zmq, 0, sizeof(*zmq));

    zmq->buffer = k_malloc(msg_size * max_msgs);
    if (!zmq->buffer) {
        k_free(zmq);
        return NULL;
    }

    zmq->base.name     = name;
    zmq->base.msg_size = msg_size;
    zmq->base.max_msgs = max_msgs;

    k_msgq_init(&zmq->handle, zmq->buffer, msg_size, max_msgs);
    return &zmq->base;
}

int claw_mq_send(struct claw_mq *mq, const void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    struct zephyr_mq *zmq =
        container_of(mq, struct zephyr_mq, base);

    (void)size;
    int ret = k_msgq_put(&zmq->handle, msg, ms_to_timeout(timeout_ms));

    if (ret == -ENOMSG || ret == -EAGAIN) {
        return CLAW_TIMEOUT;
    }
    return (ret == 0) ? CLAW_OK : CLAW_ERROR;
}

int claw_mq_recv(struct claw_mq *mq, void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    struct zephyr_mq *zmq =
        container_of(mq, struct zephyr_mq, base);

    (void)size;
    int ret = k_msgq_get(&zmq->handle, msg, ms_to_timeout(timeout_ms));

    if (ret == -ENOMSG || ret == -EAGAIN) {
        return CLAW_TIMEOUT;
    }
    return (ret == 0) ? CLAW_OK : CLAW_ERROR;
}

void claw_mq_delete(struct claw_mq *mq)
{
    if (!mq) {
        return;
    }
    struct zephyr_mq *zmq =
        container_of(mq, struct zephyr_mq, base);

    k_msgq_purge(&zmq->handle);
    k_free(zmq->buffer);
    k_free(zmq);
}

/* ---------- Software Timer ---------- */

static void timer_expiry_wrapper(struct k_timer *timer)
{
    struct zephyr_timer *zt =
        CONTAINER_OF(timer, struct zephyr_timer, handle);

    if (zt->callback) {
        zt->callback(zt->arg);
    }
}

struct claw_timer *claw_timer_create(const char *name,
                                     void (*callback)(void *arg),
                                     void *arg,
                                     uint32_t period_ms,
                                     int repeat)
{
    struct zephyr_timer *zt = k_malloc(sizeof(*zt));

    if (!zt) {
        return NULL;
    }
    memset(zt, 0, sizeof(*zt));

    zt->base.name      = name;
    zt->base.period_ms = period_ms;
    zt->base.repeat    = repeat;
    zt->callback       = callback;
    zt->arg            = arg;

    k_timer_init(&zt->handle, timer_expiry_wrapper, NULL);
    return &zt->base;
}

void claw_timer_start(struct claw_timer *timer)
{
    struct zephyr_timer *zt =
        container_of(timer, struct zephyr_timer, base);
    k_timeout_t duration = K_MSEC(zt->base.period_ms);
    k_timeout_t period = zt->base.repeat ? duration : K_NO_WAIT;

    k_timer_start(&zt->handle, duration, period);
}

void claw_timer_stop(struct claw_timer *timer)
{
    struct zephyr_timer *zt =
        container_of(timer, struct zephyr_timer, base);

    k_timer_stop(&zt->handle);
}

void claw_timer_delete(struct claw_timer *timer)
{
    if (!timer) {
        return;
    }
    struct zephyr_timer *zt =
        container_of(timer, struct zephyr_timer, base);

    k_timer_stop(&zt->handle);
    k_free(zt);
}

/* ---------- Memory ---------- */

void *claw_malloc(size_t size)
{
    return k_malloc(size);
}

void *claw_calloc(size_t nmemb, size_t size)
{
    return k_calloc(nmemb, size);
}

void claw_free(void *ptr)
{
    k_free(ptr);
}

/* ---------- Log ---------- */

static int  s_log_enabled = 1;
static int  s_log_level   = CLAW_LOG_INFO;

void claw_log(int level, const char *tag, const char *fmt, ...)
{
    if (!s_log_enabled || level > s_log_level) {
        return;
    }

    va_list ap;
    char buf[256];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    switch (level) {
    case CLAW_LOG_ERROR:
        LOG_ERR("[%s] %s", tag, buf);
        break;
    case CLAW_LOG_WARN:
        LOG_WRN("[%s] %s", tag, buf);
        break;
    case CLAW_LOG_INFO:
        LOG_INF("[%s] %s", tag, buf);
        break;
    case CLAW_LOG_DEBUG:
        LOG_DBG("[%s] %s", tag, buf);
        break;
    default:
        LOG_INF("[%s] %s", tag, buf);
        break;
    }
}

void claw_log_raw(const char *fmt, ...)
{
    va_list ap;
    char buf[256];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printk("%s", buf);
}

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
    s_log_level = level;
}

int claw_log_get_level(void)
{
    return s_log_level;
}

/* ---------- Time ---------- */

uint32_t claw_tick_get(void)
{
    return (uint32_t)k_uptime_ticks();
}

uint32_t claw_tick_ms(void)
{
    return (uint32_t)k_uptime_get();
}
