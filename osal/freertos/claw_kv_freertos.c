/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL KV storage — ESP-IDF NVS Flash backend.
 */

#include "osal/claw_os.h"
#include "osal/claw_kv.h"

#ifdef CLAW_PLATFORM_ESP_IDF

#include "nvs_flash.h"
#include "nvs.h"

#define TAG "kv"

int claw_kv_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

int claw_kv_set_str(const char *ns, const char *key, const char *value)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        return CLAW_ERROR;
    }
    esp_err_t err = nvs_set_str(h, key, value);
    if (err != ESP_OK) {
        nvs_close(h);
        return CLAW_ERROR;
    }
    err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

int claw_kv_get_str(const char *ns, const char *key,
                    char *buf, size_t size)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return CLAW_ERROR;
    }
    size_t len = size;
    esp_err_t err = nvs_get_str(h, key, buf, &len);
    nvs_close(h);
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

int claw_kv_set_blob(const char *ns, const char *key,
                     const void *data, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        return CLAW_ERROR;
    }
    esp_err_t err = nvs_set_blob(h, key, data, len);
    if (err != ESP_OK) {
        nvs_close(h);
        return CLAW_ERROR;
    }
    err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

int claw_kv_get_blob(const char *ns, const char *key,
                     void *data, size_t *len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return CLAW_ERROR;
    }
    esp_err_t err = nvs_get_blob(h, key, data, len);
    nvs_close(h);
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

int claw_kv_set_u8(const char *ns, const char *key, uint8_t val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        return CLAW_ERROR;
    }
    esp_err_t err = nvs_set_u8(h, key, val);
    if (err != ESP_OK) {
        nvs_close(h);
        return CLAW_ERROR;
    }
    err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

int claw_kv_get_u8(const char *ns, const char *key, uint8_t *val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return CLAW_ERROR;
    }
    esp_err_t err = nvs_get_u8(h, key, val);
    nvs_close(h);
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

int claw_kv_delete(const char *ns, const char *key)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        return CLAW_ERROR;
    }
    esp_err_t err = nvs_erase_key(h, key);
    if (err != ESP_OK) {
        nvs_close(h);
        return CLAW_ERROR;
    }
    err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

int claw_kv_erase_ns(const char *ns)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        return CLAW_ERROR;
    }
    esp_err_t err = nvs_erase_all(h);
    if (err != ESP_OK) {
        nvs_close(h);
        return CLAW_ERROR;
    }
    err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

#else /* standalone FreeRTOS without NVS */

int claw_kv_init(void) { return CLAW_OK; }
int claw_kv_set_str(const char *ns, const char *key,
                    const char *value)
{ (void)ns; (void)key; (void)value; return CLAW_ERROR; }
int claw_kv_get_str(const char *ns, const char *key,
                    char *buf, size_t size)
{ (void)ns; (void)key; (void)buf; (void)size; return CLAW_ERROR; }
int claw_kv_set_blob(const char *ns, const char *key,
                     const void *data, size_t len)
{ (void)ns; (void)key; (void)data; (void)len; return CLAW_ERROR; }
int claw_kv_get_blob(const char *ns, const char *key,
                     void *data, size_t *len)
{ (void)ns; (void)key; (void)data; (void)len; return CLAW_ERROR; }
int claw_kv_set_u8(const char *ns, const char *key, uint8_t val)
{ (void)ns; (void)key; (void)val; return CLAW_ERROR; }
int claw_kv_get_u8(const char *ns, const char *key, uint8_t *val)
{ (void)ns; (void)key; (void)val; return CLAW_ERROR; }
int claw_kv_delete(const char *ns, const char *key)
{ (void)ns; (void)key; return CLAW_ERROR; }
int claw_kv_erase_ns(const char *ns)
{ (void)ns; return CLAW_ERROR; }

#endif
