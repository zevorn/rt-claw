/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL implementation for Linux (POSIX pthreads).
 * Uses struct embedding (Linux kernel OOP style) with container_of.
 */

#include "osal/claw_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>

#include "utils/list.h"  /* for container_of */

/* ---------- helpers ---------- */

static void ms_to_abstime(uint32_t ms, struct timespec *ts)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec  += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

/* ---------- Thread ---------- */

struct linux_thread {
    struct claw_thread  base;       /* MUST be first */
    pthread_t           handle;
    void              (*entry)(void *arg);
    void               *arg;
    atomic_bool         exit_flag;
};

static _Thread_local struct linux_thread *s_tls_self;

static void *thread_wrapper(void *param)
{
    struct linux_thread *t = (struct linux_thread *)param;
    s_tls_self = t;
    t->entry(t->arg);
    return NULL;
}

int claw_thread_should_exit(void)
{
    struct linux_thread *t = s_tls_self;
    if (t) {
        return atomic_load(&t->exit_flag) ? 1 : 0;
    }
    return 0;
}

struct claw_thread *claw_thread_create(const char *name,
                                       void (*entry)(void *arg),
                                       void *arg,
                                       uint32_t stack_size,
                                       uint32_t priority)
{
    (void)stack_size;
    (void)priority;

    struct linux_thread *t = malloc(sizeof(*t));
    if (!t) {
        return NULL;
    }

    t->base.name = name;
    t->base.priority = priority;
    t->base.stack_size = stack_size;
    t->entry = entry;
    t->arg = arg;
    atomic_init(&t->exit_flag, false);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    int ret = pthread_create(&t->handle, &attr, thread_wrapper, t);
    pthread_attr_destroy(&attr);

    if (ret != 0) {
        free(t);
        return NULL;
    }

    return &t->base;
}

void claw_thread_delete(struct claw_thread *thread)
{
    if (!thread) {
        return;
    }

    struct linux_thread *t = container_of(thread,
                                          struct linux_thread, base);
    atomic_store(&t->exit_flag, true);
    pthread_join(t->handle, NULL);
    free(t);
}

void claw_thread_delay_ms(uint32_t ms)
{
    /*
     * Sleep in 200ms chunks so cooperative exit via
     * claw_thread_should_exit() is responsive.
     */
    uint32_t remaining = ms;

    while (remaining > 0 && !claw_thread_should_exit()) {
        uint32_t chunk = (remaining > 200) ? 200 : remaining;
        struct timespec ts;
        ts.tv_sec  = chunk / 1000;
        ts.tv_nsec = (long)(chunk % 1000) * 1000000L;
        nanosleep(&ts, NULL);
        remaining -= chunk;
    }
}

void claw_thread_yield(void)
{
    sched_yield();
}

/* ---------- Mutex ---------- */

struct linux_mutex {
    struct claw_mutex   base;       /* MUST be first */
    pthread_mutex_t     handle;
};

struct claw_mutex *claw_mutex_create(const char *name)
{
    struct linux_mutex *m = malloc(sizeof(*m));
    if (!m) {
        return NULL;
    }

    m->base.name = name;
    pthread_mutex_init(&m->handle, NULL);
    return &m->base;
}

int claw_mutex_lock(struct claw_mutex *mutex, uint32_t timeout_ms)
{
    if (!mutex) {
        return CLAW_ERROR;
    }
    if (!mutex) {
        return CLAW_ERR_INVALID;
    }
    struct linux_mutex *m = container_of(mutex,
                                         struct linux_mutex, base);

    if (timeout_ms == CLAW_WAIT_FOREVER) {
        return (pthread_mutex_lock(&m->handle) == 0)
               ? CLAW_OK : CLAW_ERROR;
    }
    if (timeout_ms == CLAW_NO_WAIT) {
        return (pthread_mutex_trylock(&m->handle) == 0)
               ? CLAW_OK : CLAW_TIMEOUT;
    }

    struct timespec ts;
    ms_to_abstime(timeout_ms, &ts);
    int ret = pthread_mutex_timedlock(&m->handle, &ts);
    if (ret == 0) {
        return CLAW_OK;
    }
    if (ret == ETIMEDOUT) {
        return CLAW_TIMEOUT;
    }
    return CLAW_ERROR;
}

void claw_mutex_unlock(struct claw_mutex *mutex)
{
    if (mutex) {
        struct linux_mutex *m = container_of(mutex,
                                             struct linux_mutex, base);
        pthread_mutex_unlock(&m->handle);
    }
}

void claw_mutex_delete(struct claw_mutex *mutex)
{
    if (mutex) {
        struct linux_mutex *m = container_of(mutex,
                                             struct linux_mutex, base);
        pthread_mutex_destroy(&m->handle);
        free(m);
    }
}

/* ---------- Semaphore ---------- */

struct linux_sem {
    struct claw_sem     base;       /* MUST be first */
    sem_t               handle;
};

struct claw_sem *claw_sem_create(const char *name, uint32_t init_value)
{
    struct linux_sem *s = malloc(sizeof(*s));
    if (!s) {
        return NULL;
    }

    s->base.name = name;
    sem_init(&s->handle, 0, init_value);
    return &s->base;
}

int claw_sem_take(struct claw_sem *sem, uint32_t timeout_ms)
{
    if (!sem) {
        return CLAW_ERROR;
    }
    if (!sem) {
        return CLAW_ERR_INVALID;
    }
    struct linux_sem *s = container_of(sem, struct linux_sem, base);

    if (timeout_ms == CLAW_WAIT_FOREVER) {
        while (sem_wait(&s->handle) != 0) {
            if (errno != EINTR) {
                return CLAW_ERROR;
            }
        }
        return CLAW_OK;
    }
    if (timeout_ms == CLAW_NO_WAIT) {
        return (sem_trywait(&s->handle) == 0) ? CLAW_OK : CLAW_TIMEOUT;
    }

    struct timespec ts;
    ms_to_abstime(timeout_ms, &ts);
    while (sem_timedwait(&s->handle, &ts) != 0) {
        if (errno == ETIMEDOUT) {
            return CLAW_TIMEOUT;
        }
        if (errno != EINTR) {
            return CLAW_ERROR;
        }
    }
    return CLAW_OK;
}

void claw_sem_give(struct claw_sem *sem)
{
    if (sem) {
        struct linux_sem *s = container_of(sem, struct linux_sem, base);
        sem_post(&s->handle);
    }
}

void claw_sem_delete(struct claw_sem *sem)
{
    if (sem) {
        struct linux_sem *s = container_of(sem, struct linux_sem, base);
        sem_destroy(&s->handle);
        free(s);
    }
}

/* ---------- Message Queue ---------- */

struct linux_mq {
    struct claw_mq      base;       /* MUST be first */
    pthread_mutex_t     lock;
    pthread_cond_t      not_empty;
    pthread_cond_t      not_full;
    uint8_t            *buf;
    uint32_t            count;
    uint32_t            head;
    uint32_t            tail;
};

struct claw_mq *claw_mq_create(const char *name,
                                uint32_t msg_size,
                                uint32_t max_msgs)
{
    struct linux_mq *mq = malloc(sizeof(*mq));
    if (!mq) {
        return NULL;
    }

    mq->buf = malloc(msg_size * max_msgs);
    if (!mq->buf) {
        free(mq);
        return NULL;
    }

    mq->base.name     = name;
    mq->base.msg_size = msg_size;
    mq->base.max_msgs = max_msgs;

    pthread_mutex_init(&mq->lock, NULL);
    pthread_cond_init(&mq->not_empty, NULL);
    pthread_cond_init(&mq->not_full, NULL);
    mq->count = 0;
    mq->head = 0;
    mq->tail = 0;

    return &mq->base;
}

int claw_mq_send(struct claw_mq *mq_handle, const void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    if (!mq_handle) {
        return CLAW_ERROR;
    }
    (void)size;
    struct linux_mq *mq = container_of(mq_handle,
                                        struct linux_mq, base);

    pthread_mutex_lock(&mq->lock);

    if (mq->count >= mq->base.max_msgs) {
        if (timeout_ms == CLAW_NO_WAIT) {
            pthread_mutex_unlock(&mq->lock);
            return CLAW_TIMEOUT;
        }
        if (timeout_ms == CLAW_WAIT_FOREVER) {
            while (mq->count >= mq->base.max_msgs) {
                pthread_cond_wait(&mq->not_full, &mq->lock);
            }
        } else {
            struct timespec ts;
            ms_to_abstime(timeout_ms, &ts);
            while (mq->count >= mq->base.max_msgs) {
                int ret = pthread_cond_timedwait(&mq->not_full,
                                                  &mq->lock, &ts);
                if (ret == ETIMEDOUT) {
                    pthread_mutex_unlock(&mq->lock);
                    return CLAW_TIMEOUT;
                }
            }
        }
    }

    memcpy(mq->buf + mq->tail * mq->base.msg_size,
           msg, mq->base.msg_size);
    mq->tail = (mq->tail + 1) % mq->base.max_msgs;
    mq->count++;

    pthread_cond_signal(&mq->not_empty);
    pthread_mutex_unlock(&mq->lock);
    return CLAW_OK;
}

int claw_mq_recv(struct claw_mq *mq_handle, void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    if (!mq_handle) {
        return CLAW_ERROR;
    }
    (void)size;
    struct linux_mq *mq = container_of(mq_handle,
                                        struct linux_mq, base);

    pthread_mutex_lock(&mq->lock);

    if (mq->count == 0) {
        if (timeout_ms == CLAW_NO_WAIT) {
            pthread_mutex_unlock(&mq->lock);
            return CLAW_TIMEOUT;
        }
        if (timeout_ms == CLAW_WAIT_FOREVER) {
            while (mq->count == 0) {
                pthread_cond_wait(&mq->not_empty, &mq->lock);
            }
        } else {
            struct timespec ts;
            ms_to_abstime(timeout_ms, &ts);
            while (mq->count == 0) {
                int ret = pthread_cond_timedwait(&mq->not_empty,
                                                  &mq->lock, &ts);
                if (ret == ETIMEDOUT) {
                    pthread_mutex_unlock(&mq->lock);
                    return CLAW_TIMEOUT;
                }
            }
        }
    }

    memcpy(msg, mq->buf + mq->head * mq->base.msg_size,
           mq->base.msg_size);
    mq->head = (mq->head + 1) % mq->base.max_msgs;
    mq->count--;

    pthread_cond_signal(&mq->not_full);
    pthread_mutex_unlock(&mq->lock);
    return CLAW_OK;
}

void claw_mq_delete(struct claw_mq *mq_handle)
{
    if (!mq_handle) {
        return;
    }
    struct linux_mq *mq = container_of(mq_handle,
                                        struct linux_mq, base);
    pthread_mutex_destroy(&mq->lock);
    pthread_cond_destroy(&mq->not_empty);
    pthread_cond_destroy(&mq->not_full);
    free(mq->buf);
    free(mq);
}

/* ---------- Timer ---------- */

struct linux_timer {
    struct claw_timer   base;       /* MUST be first */
    void              (*callback)(void *arg);
    void               *arg;
    atomic_bool         running;
    atomic_bool         deleted;
    pthread_t           thread;
    pthread_mutex_t     lock;
    pthread_cond_t      cond;
};

static void *timer_thread_fn(void *param)
{
    struct linux_timer *t = (struct linux_timer *)param;

    pthread_mutex_lock(&t->lock);
    while (!atomic_load(&t->deleted)) {
        /* Wait until running */
        while (!atomic_load(&t->running) && !atomic_load(&t->deleted)) {
            pthread_cond_wait(&t->cond, &t->lock);
        }
        if (atomic_load(&t->deleted)) {
            break;
        }

        /* Sleep for period */
        struct timespec ts;
        ms_to_abstime(t->base.period_ms, &ts);
        int ret = pthread_cond_timedwait(&t->cond, &t->lock, &ts);

        if (atomic_load(&t->deleted)) {
            break;
        }
        if (!atomic_load(&t->running)) {
            continue;
        }
        if (ret == ETIMEDOUT) {
            /* Fire callback */
            pthread_mutex_unlock(&t->lock);
            t->callback(t->arg);
            pthread_mutex_lock(&t->lock);

            if (!t->base.repeat) {
                atomic_store(&t->running, false);
            }
        }
    }
    pthread_mutex_unlock(&t->lock);
    return NULL;
}

struct claw_timer *claw_timer_create(const char *name,
                                     void (*callback)(void *arg),
                                     void *arg,
                                     uint32_t period_ms,
                                     int repeat)
{
    struct linux_timer *t = malloc(sizeof(*t));
    if (!t) {
        return NULL;
    }

    t->base.name      = name;
    t->base.period_ms = period_ms;
    t->base.repeat    = repeat;
    t->callback  = callback;
    t->arg       = arg;
    atomic_init(&t->running, false);
    atomic_init(&t->deleted, false);
    pthread_mutex_init(&t->lock, NULL);
    pthread_cond_init(&t->cond, NULL);

    if (pthread_create(&t->thread, NULL, timer_thread_fn, t) != 0) {
        pthread_mutex_destroy(&t->lock);
        pthread_cond_destroy(&t->cond);
        free(t);
        return NULL;
    }

    return &t->base;
}

void claw_timer_start(struct claw_timer *timer)
{
    if (!timer) {
        return;
    }
    struct linux_timer *t = container_of(timer,
                                         struct linux_timer, base);
    pthread_mutex_lock(&t->lock);
    atomic_store(&t->running, true);
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->lock);
}

void claw_timer_stop(struct claw_timer *timer)
{
    if (!timer) {
        return;
    }
    struct linux_timer *t = container_of(timer,
                                         struct linux_timer, base);
    pthread_mutex_lock(&t->lock);
    atomic_store(&t->running, false);
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->lock);
}

void claw_timer_delete(struct claw_timer *timer)
{
    if (!timer) {
        return;
    }
    struct linux_timer *t = container_of(timer,
                                         struct linux_timer, base);
    pthread_mutex_lock(&t->lock);
    atomic_store(&t->deleted, true);
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->lock);
    pthread_join(t->thread, NULL);
    pthread_mutex_destroy(&t->lock);
    pthread_cond_destroy(&t->cond);
    free(t);
}

/* ---------- Memory ---------- */

void *claw_malloc(size_t size)
{
    return malloc(size);
}

void *claw_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void claw_free(void *ptr)
{
    free(ptr);
}

/* ---------- Log ---------- */

static int s_log_enabled;  /* default 0: logs off */
static int s_log_level = CLAW_LOG_INFO;

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

static const char *s_level_letters[] = { "E", "W", "I", "D" };
static const char *s_level_colors[]  = {
    "\033[0;31m",   /* E: red */
    "\033[0;33m",   /* W: yellow */
    "\033[0;32m",   /* I: green */
    "\033[0;36m",   /* D: cyan */
};

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

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long ms = (unsigned long)ts.tv_sec * 1000
                     + (unsigned long)ts.tv_nsec / 1000000;

    fprintf(stderr, "%s%s (%lu) %s: ",
            s_level_colors[level], s_level_letters[level], ms, tag);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\033[0m\n");
}

void claw_log_raw(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* ---------- Time ---------- */

static struct timespec s_boot_time;
static pthread_once_t s_boot_once = PTHREAD_ONCE_INIT;

static void init_boot_time(void)
{
    clock_gettime(CLOCK_MONOTONIC, &s_boot_time);
}

uint32_t claw_tick_get(void)
{
    return claw_tick_ms();
}

uint32_t claw_tick_ms(void)
{
    pthread_once(&s_boot_once, init_boot_time);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    uint32_t ms = (uint32_t)((now.tv_sec - s_boot_time.tv_sec) * 1000
                  + (now.tv_nsec - s_boot_time.tv_nsec) / 1000000);
    return ms;
}
