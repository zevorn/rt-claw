/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OTA service — periodic version check and update management.
 * OOP: private context struct embedding struct claw_service.
 */

#include "osal/claw_os.h"
#include "osal/claw_net.h"
#include "claw/core/claw_service.h"
#include "claw_config.h"
#include "claw_ota.h"
#include "claw/services/ota/ota_service.h"
#include "utils/list.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef CLAW_PLATFORM_ESP_IDF
#include "esp_system.h"
#endif

#ifdef CONFIG_RTCLAW_OTA_ENABLE

#define TAG "ota"

/* ------------------------------------------------------------------ */
/* OTA service context — all state lives here, no file-scope globals  */
/* ------------------------------------------------------------------ */

struct ota_service_ctx {
    struct claw_service    base;           /* MUST be first member */

    struct claw_thread    *check_thread;
    volatile Claw_OtaState state;          /* worker thread + main */
    char                   update_url[256];
};

CLAW_ASSERT_EMBEDDED_FIRST(struct ota_service_ctx, base);

static struct ota_service_ctx s_ota;

/* ---- Progress callback ---- */

static void ota_progress(uint32_t received, uint32_t total)
{
    if (total > 0) {
        uint32_t pct = received * 100 / total;
        CLAW_LOGI(TAG, "download: %lu / %lu (%lu%%)",
                  (unsigned long)received,
                  (unsigned long)total,
                  (unsigned long)pct);
    } else {
        CLAW_LOGI(TAG, "download: %lu bytes",
                  (unsigned long)received);
    }
}

/* ---- Version check ---- */

/*
 * Expected JSON from OTA server:
 * {
 *   "version": "0.2.0",
 *   "url": "https://example.com/firmware.bin",
 *   "size": 123456,
 *   "sha256": "abcdef..."
 * }
 */
int ota_parse_version_json(const char *json, claw_ota_info_t *info)
{
    const char *p;

    memset(info, 0, sizeof(*info));

    /* version */
    p = strstr(json, "\"version\"");
    if (p) {
        p = strchr(p + 9, '"');
        if (p) {
            p++;
            const char *end = strchr(p, '"');
            if (end) {
                size_t len = end - p;
                if (len >= sizeof(info->version)) {
                    len = sizeof(info->version) - 1;
                }
                memcpy(info->version, p, len);
                info->version[len] = '\0';
            }
        }
    }

    /* url */
    p = strstr(json, "\"url\"");
    if (p) {
        p = strchr(p + 5, '"');
        if (p) {
            p++;
            const char *end = strchr(p, '"');
            if (end) {
                size_t len = end - p;
                if (len >= sizeof(info->url)) {
                    len = sizeof(info->url) - 1;
                }
                memcpy(info->url, p, len);
                info->url[len] = '\0';
            }
        }
    }

    /* size */
    p = strstr(json, "\"size\"");
    if (p) {
        p += 6;
        while (*p == ' ' || *p == ':') {
            p++;
        }
        info->size = (uint32_t)atoi(p);
    }

    /* sha256 */
    p = strstr(json, "\"sha256\"");
    if (p) {
        p = strchr(p + 8, '"');
        if (p) {
            p++;
            const char *end = strchr(p, '"');
            if (end) {
                size_t len = end - p;
                if (len >= sizeof(info->sha256)) {
                    len = sizeof(info->sha256) - 1;
                }
                memcpy(info->sha256, p, len);
                info->sha256[len] = '\0';
            }
        }
    }

    if (info->version[0] == '\0' || info->url[0] == '\0') {
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

/*
 * Simple semantic version comparison: "major.minor.patch".
 * Returns >0 if a > b, 0 if equal, <0 if a < b.
 */
int ota_version_compare(const char *a, const char *b)
{
    int a_parts[3] = {0, 0, 0};
    int b_parts[3] = {0, 0, 0};

    sscanf(a, "%d.%d.%d", &a_parts[0], &a_parts[1], &a_parts[2]);
    sscanf(b, "%d.%d.%d", &b_parts[0], &b_parts[1], &b_parts[2]);

    for (int i = 0; i < 3; i++) {
        if (a_parts[i] != b_parts[i]) {
            return a_parts[i] - b_parts[i];
        }
    }
    return 0;
}

int ota_check_update(claw_ota_info_t *info)
{
    const char *check_url = CONFIG_RTCLAW_OTA_URL;

    if (!check_url || check_url[0] == '\0') {
        CLAW_LOGW(TAG, "OTA URL not configured");
        return CLAW_ERROR;
    }

    char *resp = claw_malloc(2048);
    if (!resp) {
        return CLAW_ERROR;
    }

    size_t resp_len = 0;
    int status = claw_net_get(check_url, NULL, 0,
                              resp, 2048, &resp_len);
    if (status < 200 || status >= 300) {
        CLAW_LOGE(TAG, "version check failed: HTTP %d", status);
        claw_free(resp);
        return CLAW_ERROR;
    }

    if (ota_parse_version_json(resp, info) != CLAW_OK) {
        CLAW_LOGE(TAG, "invalid version JSON");
        claw_free(resp);
        return CLAW_ERROR;
    }
    claw_free(resp);

    const char *running = claw_ota_running_version();
    CLAW_LOGI(TAG, "running: %s, available: %s", running, info->version);

    if (ota_version_compare(info->version, running) > 0) {
        CLAW_LOGI(TAG, "update available: %s -> %s",
                  running, info->version);
        return 1;
    }

    CLAW_LOGI(TAG, "firmware is up to date");
    return 0;
}

/* ---- Update worker ---- */

static void ota_worker_thread(void *arg)
{
    struct ota_service_ctx *ctx = (struct ota_service_ctx *)arg;

    CLAW_LOGI(TAG, "starting OTA update: %s", ctx->update_url);
    ctx->state = CLAW_OTA_DOWNLOADING;

    int ret = claw_ota_do_update(ctx->update_url, ota_progress);
    if (ret != CLAW_OK) {
        CLAW_LOGE(TAG, "OTA update failed");
        ctx->state = CLAW_OTA_ERROR;
        return;
    }

    ctx->state = CLAW_OTA_DONE;
    CLAW_LOGI(TAG, "OTA update complete, rebooting in 3s ...");
    claw_thread_delay_ms(3000);
#ifdef CLAW_PLATFORM_ESP_IDF
    esp_restart();
#endif
}

int ota_trigger_update(const char *url)
{
    struct ota_service_ctx *ctx = &s_ota;

    if (!claw_ota_supported()) {
        CLAW_LOGW(TAG, "OTA not supported on this platform");
        return CLAW_ERROR;
    }

    if (ctx->state == CLAW_OTA_DOWNLOADING) {
        CLAW_LOGW(TAG, "OTA already in progress");
        return CLAW_ERROR;
    }

    if (!url || url[0] == '\0') {
        return CLAW_ERROR;
    }

    size_t len = strlen(url);
    if (len >= sizeof(ctx->update_url)) {
        return CLAW_ERROR;
    }
    memcpy(ctx->update_url, url, len + 1);

    if (!claw_thread_create("ota_worker", ota_worker_thread,
                            ctx, 8192, 10)) {
        CLAW_LOGE(TAG, "failed to create OTA worker thread");
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

/* ---- Auto-check thread ---- */

#if CONFIG_RTCLAW_OTA_CHECK_INTERVAL_MS > 0
static void ota_check_thread(void *arg)
{
    struct ota_service_ctx *ctx = (struct ota_service_ctx *)arg;

    /* Wait for network to stabilize after boot */
    claw_thread_delay_ms(30000);

    while (!claw_thread_should_exit()) {
        if (ctx->state == CLAW_OTA_IDLE) {
            claw_ota_info_t info;
            int ret = ota_check_update(&info);
            if (ret == 1) {
                ota_trigger_update(info.url);
            }
        }
        claw_thread_delay_ms(CONFIG_RTCLAW_OTA_CHECK_INTERVAL_MS);
    }
}
#endif

/* ---- OOP lifecycle ops ---- */

static claw_err_t ota_svc_init(struct claw_service *svc)
{
    struct ota_service_ctx *ctx =
        container_of(svc, struct ota_service_ctx, base);

    ctx->check_thread = NULL;
    ctx->state = CLAW_OTA_IDLE;
    ctx->update_url[0] = '\0';

    int ret = claw_ota_platform_init();
    if (ret != CLAW_OK) {
        CLAW_LOGE(TAG, "platform OTA init failed");
        return CLAW_ERR_GENERIC;
    }

    /* Mark running firmware as valid on successful boot */
    claw_ota_mark_valid();

    CLAW_LOGI(TAG, "OTA service initialized (running: %s)",
              claw_ota_running_version());
    return CLAW_OK;
}

static claw_err_t ota_svc_start(struct claw_service *svc)
{
    struct ota_service_ctx *ctx =
        container_of(svc, struct ota_service_ctx, base);

#if CONFIG_RTCLAW_OTA_CHECK_INTERVAL_MS > 0
    ctx->check_thread = claw_thread_create("ota_check",
        ota_check_thread, ctx, 4096, 18);
    if (!ctx->check_thread) {
        CLAW_LOGW(TAG, "auto-check thread create failed");
    }
#else
    (void)ctx;
#endif
    return CLAW_OK;
}

static void ota_svc_stop(struct claw_service *svc)
{
    struct ota_service_ctx *ctx =
        container_of(svc, struct ota_service_ctx, base);

    if (ctx->check_thread) {
        claw_thread_delete(ctx->check_thread);
        ctx->check_thread = NULL;
    }
}

/* ---- Public API (delegates to singleton) ---- */

int ota_service_init(void)
{
    return ota_svc_init(&s_ota.base) == CLAW_OK ? CLAW_OK : CLAW_ERROR;
}

int ota_service_start(void)
{
    return ota_svc_start(&s_ota.base) == CLAW_OK ? CLAW_OK : CLAW_ERROR;
}

void ota_service_stop(void)
{
    ota_svc_stop(&s_ota.base);
}

/* ---- OOP service registration ---- */

static const struct claw_service_ops ota_svc_ops = {
    .init  = ota_svc_init,
    .start = ota_svc_start,
    .stop  = ota_svc_stop,
};

static struct ota_service_ctx s_ota = {
    .base = {
        .name  = "ota",
        .ops   = &ota_svc_ops,
        .deps  = NULL,
        .state = CLAW_SVC_CREATED,
    },
};

CLAW_SERVICE_REGISTER(ota, &s_ota.base);

#endif /* CONFIG_RTCLAW_OTA_ENABLE */
