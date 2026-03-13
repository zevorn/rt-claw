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

/* ---------------- Types (opaque handles) ---------------- */

typedef void *claw_thread_t;
typedef void *claw_mutex_t;
typedef void *claw_sem_t;
typedef void *claw_mq_t;
typedef void *claw_timer_t;

/* ---------------- Constants ----------------------------- */

#define CLAW_WAIT_FOREVER   UINT32_MAX
#define CLAW_NO_WAIT        0

/* Return codes */
#define CLAW_OK             0
#define CLAW_ERROR          (-1)
#define CLAW_TIMEOUT        (-2)
#define CLAW_NOMEM          (-3)

/* Log levels */
#define CLAW_LOG_ERROR      0
#define CLAW_LOG_WARN       1
#define CLAW_LOG_INFO       2
#define CLAW_LOG_DEBUG      3

/* ---------------- Thread -------------------------------- */

claw_thread_t claw_thread_create(const char *name,
                                  void (*entry)(void *arg),
                                  void *arg,
                                  uint32_t stack_size,
                                  uint32_t priority);
void claw_thread_delete(claw_thread_t thread);
void claw_thread_delay_ms(uint32_t ms);
void claw_thread_yield(void);

/* ---------------- Mutex --------------------------------- */

claw_mutex_t claw_mutex_create(const char *name);
int  claw_mutex_lock(claw_mutex_t mutex, uint32_t timeout_ms);
void claw_mutex_unlock(claw_mutex_t mutex);
void claw_mutex_delete(claw_mutex_t mutex);

/* ---------------- Semaphore ----------------------------- */

claw_sem_t claw_sem_create(const char *name, uint32_t init_value);
int  claw_sem_take(claw_sem_t sem, uint32_t timeout_ms);
void claw_sem_give(claw_sem_t sem);
void claw_sem_delete(claw_sem_t sem);

/* ---------------- Message Queue ------------------------- */

claw_mq_t claw_mq_create(const char *name,
                           uint32_t msg_size,
                           uint32_t max_msgs);
int  claw_mq_send(claw_mq_t mq, const void *msg, uint32_t size,
                   uint32_t timeout_ms);
int  claw_mq_recv(claw_mq_t mq, void *msg, uint32_t size,
                   uint32_t timeout_ms);
void claw_mq_delete(claw_mq_t mq);

/* ---------------- Software Timer ------------------------ */

claw_timer_t claw_timer_create(const char *name,
                                void (*callback)(void *arg),
                                void *arg,
                                uint32_t period_ms,
                                int repeat);
void claw_timer_start(claw_timer_t timer);
void claw_timer_stop(claw_timer_t timer);
void claw_timer_delete(claw_timer_t timer);

/* ---------------- Memory -------------------------------- */

void *claw_malloc(size_t size);
void *claw_calloc(size_t nmemb, size_t size);
void  claw_free(void *ptr);

/* ---------------- Log ----------------------------------- */

void claw_log(int level, const char *tag, const char *fmt, ...);
void claw_log_raw(const char *fmt, ...);
void claw_log_set_enabled(int enabled);
int  claw_log_get_enabled(void);

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
