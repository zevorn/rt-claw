/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL - OS Abstraction Layer for rt-claw.
 * Platform-independent RTOS API. All rt-claw core code depends
 * only on this header. Implementations provided per-RTOS:
 *   - claw_os_freertos.c  (FreeRTOS / ESP-IDF)
 *   - claw_os_rtthread.c  (RT-Thread)
 */

#ifndef CLAW_OS_H
#define CLAW_OS_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OSAL object types — struct-based (Linux kernel style).
 * Each RTOS backend defines a private sub-struct embedding these
 * as the first member, recovered via container_of.
 */

struct claw_thread {
    const char *name;
    uint32_t    priority;
    uint32_t    stack_size;
};

struct claw_mutex {
    const char *name;
};

struct claw_sem {
    const char *name;
};

struct claw_mq {
    const char *name;
    uint32_t    msg_size;
    uint32_t    max_msgs;
};

struct claw_timer {
    const char *name;
    uint32_t    period_ms;
    int         repeat;
};

/* ---------------- Constants ----------------------------- */

#define CLAW_WAIT_FOREVER   UINT32_MAX
#define CLAW_NO_WAIT        0

/*
 * Return codes — canonical definitions in claw/core/claw_errno.h.
 * Legacy macros kept for existing code; values match claw_err_t.
 */
#include "claw/core/claw_errno.h"

#ifndef CLAW_OK
#define CLAW_OK             0
#endif
#define CLAW_ERROR          CLAW_ERR_GENERIC
#define CLAW_TIMEOUT        CLAW_ERR_TIMEOUT
#define CLAW_NOMEM          CLAW_ERR_NOMEM

/* Log levels */
#define CLAW_LOG_ERROR      0
#define CLAW_LOG_WARN       1
#define CLAW_LOG_INFO       2
#define CLAW_LOG_DEBUG      3

/* ---------------- Thread -------------------------------- */

struct claw_thread *claw_thread_create(const char *name,
                                       void (*entry)(void *arg),
                                       void *arg,
                                       uint32_t stack_size,
                                       uint32_t priority);
void claw_thread_delete(struct claw_thread *thread);
void claw_thread_delay_ms(uint32_t ms);
void claw_thread_yield(void);

/*
 * Check if the current thread has been requested to exit.
 * Long-running threads should poll this in their main loop
 * to support cooperative cancellation via claw_thread_delete().
 * Returns non-zero if exit requested, 0 otherwise.
 * On platforms without cooperative cancellation, always returns 0.
 */
int  claw_thread_should_exit(void);

/* ---------------- Mutex --------------------------------- */

struct claw_mutex *claw_mutex_create(const char *name);
int  claw_mutex_lock(struct claw_mutex *mutex, uint32_t timeout_ms);
void claw_mutex_unlock(struct claw_mutex *mutex);
void claw_mutex_delete(struct claw_mutex *mutex);

/* ---------------- Semaphore ----------------------------- */

struct claw_sem *claw_sem_create(const char *name, uint32_t init_value);
int  claw_sem_take(struct claw_sem *sem, uint32_t timeout_ms);
void claw_sem_give(struct claw_sem *sem);
void claw_sem_delete(struct claw_sem *sem);

/* ---------------- Message Queue ------------------------- */

struct claw_mq *claw_mq_create(const char *name,
                                uint32_t msg_size,
                                uint32_t max_msgs);
int  claw_mq_send(struct claw_mq *mq, const void *msg, uint32_t size,
                   uint32_t timeout_ms);
int  claw_mq_recv(struct claw_mq *mq, void *msg, uint32_t size,
                   uint32_t timeout_ms);
void claw_mq_delete(struct claw_mq *mq);

/* ---------------- Software Timer ------------------------ */

struct claw_timer *claw_timer_create(const char *name,
                                     void (*callback)(void *arg),
                                     void *arg,
                                     uint32_t period_ms,
                                     int repeat);
void claw_timer_start(struct claw_timer *timer);
void claw_timer_stop(struct claw_timer *timer);
void claw_timer_delete(struct claw_timer *timer);

/* ---------------- Memory -------------------------------- */

void *claw_malloc(size_t size);
void *claw_calloc(size_t nmemb, size_t size);
void  claw_free(void *ptr);

/* ---------------- Log ----------------------------------- */

void claw_log(int level, const char *tag, const char *fmt, ...);
void claw_log_raw(const char *fmt, ...);
void claw_log_set_enabled(int enabled);
int  claw_log_get_enabled(void);
void claw_log_set_level(int level);
int  claw_log_get_level(void);

/* Convenience macros */
#define CLAW_LOGE(tag, fmt, ...) claw_log(CLAW_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define CLAW_LOGW(tag, fmt, ...) claw_log(CLAW_LOG_WARN,  tag, fmt, ##__VA_ARGS__)
#define CLAW_LOGI(tag, fmt, ...) claw_log(CLAW_LOG_INFO,  tag, fmt, ##__VA_ARGS__)
#define CLAW_LOGD(tag, fmt, ...) claw_log(CLAW_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)

/* ---------------- Time ---------------------------------- */

uint32_t claw_tick_get(void);
uint32_t claw_tick_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* CLAW_OS_H */
