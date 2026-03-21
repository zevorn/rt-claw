/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * WiFi STA manager — NVS credentials, exponential backoff reconnect.
 * Shared across all Espressif SoCs (ESP32-C3, ESP32-S3, etc.).
 */

#include "drivers/net/espressif/wifi_manager.h"

#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define TAG "wifi"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

#define WIFI_MAX_RETRY      10
#define WIFI_RETRY_BASE_MS  1000
#define WIFI_RETRY_MAX_MS   30000

#define NVS_NAMESPACE       "wifi_config"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASS        "password"

/* Build-time defaults from Kconfig (may be empty) */
#ifndef CONFIG_RTCLAW_WIFI_SSID
#define CONFIG_RTCLAW_WIFI_SSID ""
#endif
#ifndef CONFIG_RTCLAW_WIFI_PASS
#define CONFIG_RTCLAW_WIFI_PASS ""
#endif

static EventGroupHandle_t s_wifi_events;
static int  s_retry_count;
static char s_ip_str[16] = "0.0.0.0";
static bool s_connected;

static const char *wifi_reason_to_str(wifi_err_reason_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:            return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_FAIL:              return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_EXPIRE:           return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_FAIL:             return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:      return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND:            return "NO_AP_FOUND";
    case WIFI_REASON_BEACON_TIMEOUT:         return "BEACON_TIMEOUT";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL:        return "CONNECTION_FAIL";
    default:                                 return "UNKNOWN";
    }
}

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT &&
               id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        wifi_event_sta_disconnected_t *disc =
            (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "disconnected (reason=%d:%s)",
                 disc->reason, wifi_reason_to_str(disc->reason));

        if (s_retry_count < WIFI_MAX_RETRY) {
            /* Exponential backoff: 1s, 2s, 4s, ... capped at 30s */
            uint32_t delay = WIFI_RETRY_BASE_MS << s_retry_count;
            if (delay > WIFI_RETRY_MAX_MS) {
                delay = WIFI_RETRY_MAX_MS;
            }
            ESP_LOGW(TAG, "retry %d/%d in %" PRIu32 "ms",
                     s_retry_count + 1, WIFI_MAX_RETRY, delay);
            vTaskDelay(pdMS_TO_TICKS(delay));
            esp_wifi_connect();
            s_retry_count++;
        } else {
            ESP_LOGE(TAG, "failed after %d retries", WIFI_MAX_RETRY);
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str),
                 IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "connected, IP: %s", s_ip_str);
        s_retry_count = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    wifi_config_t wifi_cfg = {0};
    bool found = false;

    /* NVS credentials (set via CLI) take priority */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(wifi_cfg.sta.ssid);
        if (nvs_get_str(nvs, NVS_KEY_SSID,
                        (char *)wifi_cfg.sta.ssid, &len) == ESP_OK) {
            len = sizeof(wifi_cfg.sta.password);
            nvs_get_str(nvs, NVS_KEY_PASS,
                        (char *)wifi_cfg.sta.password, &len);
            found = true;
        }
        nvs_close(nvs);
    }

    /* Fall back to Kconfig build-time defaults */
    if (!found && CONFIG_RTCLAW_WIFI_SSID[0] != '\0') {
        strncpy((char *)wifi_cfg.sta.ssid,
                CONFIG_RTCLAW_WIFI_SSID,
                sizeof(wifi_cfg.sta.ssid) - 1);
        strncpy((char *)wifi_cfg.sta.password,
                CONFIG_RTCLAW_WIFI_PASS,
                sizeof(wifi_cfg.sta.password) - 1);
        found = true;
    }

    if (!found) {
        ESP_LOGW(TAG, "no WiFi credentials configured");
        ESP_LOGW(TAG, "use shell: /wifi_set <SSID> <PASS>");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "connecting to SSID: %s",
             (const char *)wifi_cfg.sta.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == UINT32_MAX)
                       ? portMAX_DELAY
                       : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, ticks);

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

const char *wifi_manager_get_ip(void)
{
    return s_ip_str;
}

esp_err_t wifi_manager_set_credentials(const char *ssid,
                                       const char *password)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set SSID failed: %s",
                 esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_set_str(nvs, NVS_KEY_PASS, password ? password : "");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set password failed: %s",
                 esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_commit(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s",
                 esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "credentials saved for SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_reconnect(const char *ssid, const char *password)
{
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid,
            sizeof(wifi_cfg.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_cfg.sta.password, password,
                sizeof(wifi_cfg.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    s_retry_count = 0;
    esp_wifi_disconnect();

    ESP_LOGI(TAG, "reconnecting to SSID: %s", ssid);
    return ESP_OK;
}

void wifi_manager_scan_and_print(void)
{
    /*
     * Scan while staying connected — no disconnect needed.
     * ESP-IDF supports STA background scan; the current
     * connection is briefly paused during channel hopping
     * but not dropped.
     */
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    printf("scanning nearby APs...\n");

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        printf("scan failed: %s\n", esp_err_to_name(err));
        return;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        printf("no APs found\n");
        return;
    }

    if (ap_count > 20) {
        ap_count = 20;
    }

    wifi_ap_record_t *ap_list = calloc(ap_count,
                                       sizeof(wifi_ap_record_t));
    if (!ap_list) {
        printf("out of memory for AP list\n");
        return;
    }

    uint16_t ap_max = ap_count;
    if (esp_wifi_scan_get_ap_records(&ap_max, ap_list) != ESP_OK) {
        printf("failed to get AP records\n");
        free(ap_list);
        return;
    }

    printf("Found %u APs:\n", ap_max);
    for (uint16_t i = 0; i < ap_max; i++) {
        const wifi_ap_record_t *ap = &ap_list[i];
        printf("  [%u] %-32s  RSSI=%d  CH=%d\n",
               i + 1, (const char *)ap->ssid,
               ap->rssi, ap->primary);
    }

    free(ap_list);
}

/* OOP driver registration */
#include "claw/core/claw_driver.h"

static claw_err_t wifi_drv_probe(struct claw_driver *drv)
{
    (void)drv;
    /*
     * WiFi init is handled by board_early_init() which runs before
     * claw_init().  Calling wifi_manager_init() again would hit
     * ESP_ERROR_CHECK on already-initialized netif/event loop.
     */
    return CLAW_OK;
}

static void wifi_drv_remove(struct claw_driver *drv)
{
    (void)drv;
    /* WiFi teardown handled by ESP-IDF on reboot */
}

static const struct claw_driver_ops wifi_drv_ops = {
    .probe  = wifi_drv_probe,
    .remove = wifi_drv_remove,
};

static struct claw_driver wifi_drv = {
    .name  = "wifi_manager",
    .ops   = &wifi_drv_ops,
    .state = CLAW_DRV_REGISTERED,
};

CLAW_DRIVER_REGISTER(wifi, &wifi_drv);
