/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL implementation for FreeRTOS (ESP-IDF and standalone).
 * Uses struct embedding (Linux kernel OOP style): each OSAL
 * primitive has a private sub-struct with the base as its first
 * member, recovered via container_of().
 */

#include "osal/claw_os.h"
#include "utils/list.h"  /* container_of */

#ifdef CLAW_PLATFORM_ESP_IDF
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/semphr.h"
  #include "freertos/queue.h"
  #include "freertos/timers.h"
  #include "esp_log.h"
#else
  #include "FreeRTOS.h"
  #include "task.h"
  #include "semphr.h"
  #include "queue.h"
  #include "timers.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- helpers ---------- */

static inline TickType_t ms_to_ticks(uint32_t ms)
{
    if (ms == CLAW_WAIT_FOREVER) {
        return portMAX_DELAY;
    }
    return pdMS_TO_TICKS(ms);
}

/* ---------- private sub-structs ---------- */

struct freertos_thread {
    struct claw_thread  base;   /* MUST be first */
    TaskHandle_t        handle;
    void              (*entry)(void *arg);
    void               *arg;
};

struct freertos_mutex {
    struct claw_mutex   base;   /* MUST be first */
    SemaphoreHandle_t   handle;
};

struct freertos_sem {
    struct claw_sem     base;   /* MUST be first */
    SemaphoreHandle_t   handle;
};

struct freertos_mq {
    struct claw_mq      base;   /* MUST be first */
    QueueHandle_t       handle;
};

struct freertos_timer {
    struct claw_timer   base;   /* MUST be first */
    TimerHandle_t       handle;
    void              (*callback)(void *arg);
    void               *arg;
};

/* ---------- Thread ---------- */

static void thread_wrapper(void *param)
{
    struct freertos_thread *ft = param;
    ft->entry(ft->arg);
    /* Auto-delete when thread function returns */
    vTaskDelete(NULL);
}

struct claw_thread *claw_thread_create(const char *name,
                                       void (*entry)(void *arg),
                                       void *arg,
                                       uint32_t stack_size,
                                       uint32_t priority)
{
    struct freertos_thread *ft = pvPortMalloc(sizeof(*ft));
    if (!ft) {
        return NULL;
    }
    ft->base.name = name;
    ft->base.priority = priority;
    ft->base.stack_size = stack_size;
    ft->entry = entry;
    ft->arg = arg;

    BaseType_t ret = xTaskCreate(thread_wrapper, name,
                                 stack_size / sizeof(StackType_t),
                                 ft, priority, &ft->handle);
    if (ret != pdPASS) {
        vPortFree(ft);
        return NULL;
    }
    return &ft->base;
}

void claw_thread_delete(struct claw_thread *thread)
{
    if (!thread) {
        return;
    }
    struct freertos_thread *ft = container_of(thread,
                                              struct freertos_thread, base);
    vTaskDelete(ft->handle);
    vPortFree(ft);
}

void claw_thread_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void claw_thread_yield(void)
{
    taskYIELD();
}

int claw_thread_should_exit(void)
{
    return 0;
}

/* ---------- Mutex ---------- */

struct claw_mutex *claw_mutex_create(const char *name)
{
    struct freertos_mutex *fm = pvPortMalloc(sizeof(*fm));
    if (!fm) {
        return NULL;
    }
    fm->base.name = name;
    fm->handle = xSemaphoreCreateMutex();
    if (!fm->handle) {
        vPortFree(fm);
        return NULL;
    }
    return &fm->base;
}

int claw_mutex_lock(struct claw_mutex *mutex, uint32_t timeout_ms)
{
    if (!mutex) {
        return CLAW_ERR_INVALID;
    }
    struct freertos_mutex *fm = container_of(mutex,
                                             struct freertos_mutex, base);
    if (xSemaphoreTake(fm->handle, ms_to_ticks(timeout_ms)) == pdTRUE) {
        return CLAW_OK;
    }
    return CLAW_TIMEOUT;
}

void claw_mutex_unlock(struct claw_mutex *mutex)
{
    struct freertos_mutex *fm = container_of(mutex,
                                             struct freertos_mutex, base);
    xSemaphoreGive(fm->handle);
}

void claw_mutex_delete(struct claw_mutex *mutex)
{
    if (!mutex) {
        return;
    }
    struct freertos_mutex *fm = container_of(mutex,
                                             struct freertos_mutex, base);
    vSemaphoreDelete(fm->handle);
    vPortFree(fm);
}

/* ---------- Semaphore ---------- */

struct claw_sem *claw_sem_create(const char *name, uint32_t init_value)
{
    struct freertos_sem *fs = pvPortMalloc(sizeof(*fs));
    if (!fs) {
        return NULL;
    }
    fs->base.name = name;
    fs->handle = xSemaphoreCreateCounting(UINT32_MAX, init_value);
    if (!fs->handle) {
        vPortFree(fs);
        return NULL;
    }
    return &fs->base;
}

int claw_sem_take(struct claw_sem *sem, uint32_t timeout_ms)
{
    if (!sem) {
        return CLAW_ERR_INVALID;
    }
    struct freertos_sem *fs = container_of(sem,
                                           struct freertos_sem, base);
    if (xSemaphoreTake(fs->handle, ms_to_ticks(timeout_ms)) == pdTRUE) {
        return CLAW_OK;
    }
    return CLAW_TIMEOUT;
}

void claw_sem_give(struct claw_sem *sem)
{
    struct freertos_sem *fs = container_of(sem,
                                           struct freertos_sem, base);
    xSemaphoreGive(fs->handle);
}

void claw_sem_delete(struct claw_sem *sem)
{
    if (!sem) {
        return;
    }
    struct freertos_sem *fs = container_of(sem,
                                           struct freertos_sem, base);
    vSemaphoreDelete(fs->handle);
    vPortFree(fs);
}

/* ---------- Message Queue ---------- */

struct claw_mq *claw_mq_create(const char *name,
                                uint32_t msg_size,
                                uint32_t max_msgs)
{
    struct freertos_mq *fmq = pvPortMalloc(sizeof(*fmq));
    if (!fmq) {
        return NULL;
    }
    fmq->base.name = name;
    fmq->base.msg_size = msg_size;
    fmq->base.max_msgs = max_msgs;
    fmq->handle = xQueueCreate(max_msgs, msg_size);
    if (!fmq->handle) {
        vPortFree(fmq);
        return NULL;
    }
    return &fmq->base;
}

int claw_mq_send(struct claw_mq *mq, const void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    (void)size; /* FreeRTOS queue item size is fixed at creation */
    struct freertos_mq *fmq = container_of(mq,
                                            struct freertos_mq, base);
    if (xQueueSend(fmq->handle, msg, ms_to_ticks(timeout_ms)) == pdTRUE) {
        return CLAW_OK;
    }
    return CLAW_TIMEOUT;
}

int claw_mq_recv(struct claw_mq *mq, void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    (void)size;
    struct freertos_mq *fmq = container_of(mq,
                                            struct freertos_mq, base);
    if (xQueueReceive(fmq->handle, msg, ms_to_ticks(timeout_ms)) == pdTRUE) {
        return CLAW_OK;
    }
    return CLAW_TIMEOUT;
}

void claw_mq_delete(struct claw_mq *mq)
{
    if (!mq) {
        return;
    }
    struct freertos_mq *fmq = container_of(mq,
                                            struct freertos_mq, base);
    vQueueDelete(fmq->handle);
    vPortFree(fmq);
}

/* ---------- Timer ---------- */

static void timer_trampoline(TimerHandle_t xTimer)
{
    struct freertos_timer *ft =
        (struct freertos_timer *)pvTimerGetTimerID(xTimer);
    if (ft && ft->callback) {
        ft->callback(ft->arg);
    }
}

struct claw_timer *claw_timer_create(const char *name,
                                     void (*callback)(void *arg),
                                     void *arg,
                                     uint32_t period_ms,
                                     int repeat)
{
    struct freertos_timer *ft = pvPortMalloc(sizeof(*ft));
    if (!ft) {
        return NULL;
    }
    ft->base.name = name;
    ft->base.period_ms = period_ms;
    ft->base.repeat = repeat;
    ft->callback = callback;
    ft->arg = arg;

    ft->handle = xTimerCreate(name, pdMS_TO_TICKS(period_ms),
                               repeat ? pdTRUE : pdFALSE,
                               ft,
                               timer_trampoline);
    if (!ft->handle) {
        vPortFree(ft);
        return NULL;
    }
    return &ft->base;
}

void claw_timer_start(struct claw_timer *timer)
{
    struct freertos_timer *ft = container_of(timer,
                                             struct freertos_timer, base);
    xTimerStart(ft->handle, 0);
}

void claw_timer_stop(struct claw_timer *timer)
{
    struct freertos_timer *ft = container_of(timer,
                                             struct freertos_timer, base);
    xTimerStop(ft->handle, 0);
}

void claw_timer_delete(struct claw_timer *timer)
{
    if (!timer) {
        return;
    }
    struct freertos_timer *ft = container_of(timer,
                                             struct freertos_timer, base);
    xTimerDelete(ft->handle, 0);
    vPortFree(ft);
}

/* ---------- Memory ---------- */

void *claw_malloc(size_t size)
{
#ifdef CLAW_PLATFORM_ESP_IDF
    return malloc(size); /* ESP-IDF wraps heap_caps_malloc */
#else
    return pvPortMalloc(size);
#endif
}

void *claw_calloc(size_t nmemb, size_t size)
{
#ifdef CLAW_PLATFORM_ESP_IDF
    return calloc(nmemb, size);
#else
    void *p = pvPortMalloc(nmemb * size);
    if (p) {
        memset(p, 0, nmemb * size);
    }
    return p;
#endif
}

void claw_free(void *ptr)
{
#ifdef CLAW_PLATFORM_ESP_IDF
    free(ptr);
#else
    vPortFree(ptr);
#endif
}

/* ---------- Log ---------- */

static int s_log_enabled = 1;
static int s_log_level = CLAW_LOG_DEBUG;  /* show all by default */

void claw_log_set_enabled(int enabled)
{
    s_log_enabled = enabled;
#ifdef CLAW_PLATFORM_ESP_IDF
    if (enabled) {
        esp_log_level_set("*", ESP_LOG_INFO);
    } else {
        esp_log_level_set("*", ESP_LOG_NONE);
    }
#endif
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
#ifdef CLAW_PLATFORM_ESP_IDF
    static const esp_log_level_t map[] = {
        ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG
    };
    esp_log_level_set("*", map[level]);
#endif
}

int claw_log_get_level(void)
{
    return s_log_level;
}

#ifdef CLAW_PLATFORM_ESP_IDF

void claw_log(int level, const char *tag, const char *fmt, ...)
{
    if (!s_log_enabled || level > s_log_level) {
        return;
    }

    va_list ap;
    esp_log_level_t esp_level;

    static const char *letters[] = { "E", "W", "I", "D" };
    int idx = level;

    if (idx < 0) {
        idx = 0;
    }
    if (idx > 3) {
        idx = 3;
    }

    switch (level) {
    case CLAW_LOG_ERROR:
        esp_level = ESP_LOG_ERROR;
        break;
    case CLAW_LOG_WARN:
        esp_level = ESP_LOG_WARN;
        break;
    case CLAW_LOG_INFO:
        esp_level = ESP_LOG_INFO;
        break;
    default:
        esp_level = ESP_LOG_DEBUG;
        break;
    }

#ifdef CONFIG_LOG_COLORS
    static const char *colors[] = {
        "\033[0;31m",   /* E: red */
        "\033[0;33m",   /* W: yellow */
        "\033[0;32m",   /* I: green */
        "\033[0;36m",   /* D: cyan */
    };
    char fmtbuf[256];

    snprintf(fmtbuf, sizeof(fmtbuf),
             "%s%s (%lu) %s: %s\033[0m\n",
             colors[idx], letters[idx],
             (unsigned long)esp_log_timestamp(), tag, fmt);
#else
    char fmtbuf[256];

    snprintf(fmtbuf, sizeof(fmtbuf), "%s (%lu) %s: %s\n",
             letters[idx],
             (unsigned long)esp_log_timestamp(), tag, fmt);
#endif
    va_start(ap, fmt);
    esp_log_writev(esp_level, tag, fmtbuf, ap);
    va_end(ap);
}

#else /* standalone FreeRTOS */

static const char *level_str[] = { "E", "W", "I", "D" };

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
    printf("[%s/%s] ", level_str[level], tag);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

#endif

void claw_log_raw(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* ---------- Time ---------- */

uint32_t claw_tick_get(void)
{
    return (uint32_t)xTaskGetTickCount();
}

uint32_t claw_tick_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
