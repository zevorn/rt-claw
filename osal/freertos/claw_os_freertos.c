/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL implementation for FreeRTOS (ESP-IDF and standalone).
 */

#include "osal/claw_os.h"

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
    if (ms == CLAW_WAIT_FOREVER)
        return portMAX_DELAY;
    return pdMS_TO_TICKS(ms);
}

/* ---------- Thread ---------- */

typedef struct {
    void (*entry)(void *arg);
    void *arg;
} thread_wrap_t;

static void thread_wrapper(void *param)
{
    thread_wrap_t w = *(thread_wrap_t *)param;
    vPortFree(param);
    w.entry(w.arg);
    /* Auto-delete when thread function returns */
    vTaskDelete(NULL);
}

claw_thread_t claw_thread_create(const char *name,
                                  void (*entry)(void *arg),
                                  void *arg,
                                  uint32_t stack_size,
                                  uint32_t priority)
{
    TaskHandle_t handle = NULL;
    thread_wrap_t *w = pvPortMalloc(sizeof(*w));
    if (!w) {
        return NULL;
    }
    w->entry = entry;
    w->arg = arg;

    BaseType_t ret = xTaskCreate(thread_wrapper, name,
                                 stack_size / sizeof(StackType_t),
                                 w, priority, &handle);
    if (ret != pdPASS) {
        vPortFree(w);
        return NULL;
    }
    return (claw_thread_t)handle;
}

void claw_thread_delete(claw_thread_t thread)
{
    vTaskDelete((TaskHandle_t)thread);
}

void claw_thread_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void claw_thread_yield(void)
{
    taskYIELD();
}

/* ---------- Mutex ---------- */

claw_mutex_t claw_mutex_create(const char *name)
{
    (void)name;
    return (claw_mutex_t)xSemaphoreCreateMutex();
}

int claw_mutex_lock(claw_mutex_t mutex, uint32_t timeout_ms)
{
    if (xSemaphoreTake((SemaphoreHandle_t)mutex, ms_to_ticks(timeout_ms)) == pdTRUE)
        return CLAW_OK;
    return CLAW_TIMEOUT;
}

void claw_mutex_unlock(claw_mutex_t mutex)
{
    xSemaphoreGive((SemaphoreHandle_t)mutex);
}

void claw_mutex_delete(claw_mutex_t mutex)
{
    vSemaphoreDelete((SemaphoreHandle_t)mutex);
}

/* ---------- Semaphore ---------- */

claw_sem_t claw_sem_create(const char *name, uint32_t init_value)
{
    (void)name;
    return (claw_sem_t)xSemaphoreCreateCounting(UINT32_MAX, init_value);
}

int claw_sem_take(claw_sem_t sem, uint32_t timeout_ms)
{
    if (xSemaphoreTake((SemaphoreHandle_t)sem, ms_to_ticks(timeout_ms)) == pdTRUE)
        return CLAW_OK;
    return CLAW_TIMEOUT;
}

void claw_sem_give(claw_sem_t sem)
{
    xSemaphoreGive((SemaphoreHandle_t)sem);
}

void claw_sem_delete(claw_sem_t sem)
{
    vSemaphoreDelete((SemaphoreHandle_t)sem);
}

/* ---------- Message Queue ---------- */

claw_mq_t claw_mq_create(const char *name,
                           uint32_t msg_size,
                           uint32_t max_msgs)
{
    (void)name;
    return (claw_mq_t)xQueueCreate(max_msgs, msg_size);
}

int claw_mq_send(claw_mq_t mq, const void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    (void)size; /* FreeRTOS queue item size is fixed at creation */
    if (xQueueSend((QueueHandle_t)mq, msg, ms_to_ticks(timeout_ms)) == pdTRUE)
        return CLAW_OK;
    return CLAW_TIMEOUT;
}

int claw_mq_recv(claw_mq_t mq, void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    (void)size;
    if (xQueueReceive((QueueHandle_t)mq, msg, ms_to_ticks(timeout_ms)) == pdTRUE)
        return CLAW_OK;
    return CLAW_TIMEOUT;
}

void claw_mq_delete(claw_mq_t mq)
{
    vQueueDelete((QueueHandle_t)mq);
}

/* ---------- Timer ---------- */

typedef struct {
    void (*callback)(void *arg);
    void *arg;
} timer_ctx_t;

static void timer_trampoline(TimerHandle_t xTimer)
{
    timer_ctx_t *ctx = (timer_ctx_t *)pvTimerGetTimerID(xTimer);
    if (ctx && ctx->callback) {
        ctx->callback(ctx->arg);
    }
}

claw_timer_t claw_timer_create(const char *name,
                                void (*callback)(void *arg),
                                void *arg,
                                uint32_t period_ms,
                                int repeat)
{
    timer_ctx_t *ctx = pvPortMalloc(sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }
    ctx->callback = callback;
    ctx->arg = arg;

    TimerHandle_t t = xTimerCreate(name, pdMS_TO_TICKS(period_ms),
                                    repeat ? pdTRUE : pdFALSE,
                                    ctx,
                                    timer_trampoline);
    if (!t) {
        vPortFree(ctx);
        return NULL;
    }
    return (claw_timer_t)t;
}

void claw_timer_start(claw_timer_t timer)
{
    xTimerStart((TimerHandle_t)timer, 0);
}

void claw_timer_stop(claw_timer_t timer)
{
    xTimerStop((TimerHandle_t)timer, 0);
}

void claw_timer_delete(claw_timer_t timer)
{
    TimerHandle_t t = (TimerHandle_t)timer;
    timer_ctx_t *ctx = (timer_ctx_t *)pvTimerGetTimerID(t);
    xTimerDelete(t, 0);
    if (ctx) {
        vPortFree(ctx);
    }
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
    if (p)
        memset(p, 0, nmemb * size);
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
    case CLAW_LOG_ERROR: esp_level = ESP_LOG_ERROR; break;
    case CLAW_LOG_WARN:  esp_level = ESP_LOG_WARN;  break;
    case CLAW_LOG_INFO:  esp_level = ESP_LOG_INFO;  break;
    default:             esp_level = ESP_LOG_DEBUG;  break;
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
