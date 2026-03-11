/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * GPIO tools — expose GPIO control as LLM-callable tools.
 */

#include "claw_tools.h"

#include <string.h>
#include <stdio.h>

#define TAG "tool_gpio"

#ifdef CLAW_PLATFORM_ESP_IDF

#include "driver/gpio.h"

#define GPIO_PIN_MAX  21

static int tool_gpio_set(const cJSON *params, cJSON *result)
{
    cJSON *pin_j = cJSON_GetObjectItem(params, "pin");
    cJSON *level_j = cJSON_GetObjectItem(params, "level");

    if (!pin_j || !cJSON_IsNumber(pin_j) ||
        !level_j || !cJSON_IsNumber(level_j)) {
        cJSON_AddStringToObject(result, "error", "missing pin or level");
        return CLAW_ERROR;
    }

    int pin = pin_j->valueint;
    int level = level_j->valueint;

    if (pin < 0 || pin > GPIO_PIN_MAX) {
        cJSON_AddStringToObject(result, "error", "pin out of range (0-21)");
        return CLAW_ERROR;
    }

    /* Ensure pin is configured as output */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    esp_err_t err = gpio_set_level(pin, level ? 1 : 0);

    if (err == ESP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "GPIO %d set to %s",
                 pin, level ? "HIGH" : "LOW");
        cJSON_AddStringToObject(result, "status", "ok");
        cJSON_AddStringToObject(result, "message", msg);
        CLAW_LOGI(TAG, "%s", msg);
    } else {
        cJSON_AddStringToObject(result, "error", "gpio_set_level failed");
    }
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

static int tool_gpio_get(const cJSON *params, cJSON *result)
{
    cJSON *pin_j = cJSON_GetObjectItem(params, "pin");

    if (!pin_j || !cJSON_IsNumber(pin_j)) {
        cJSON_AddStringToObject(result, "error", "missing pin");
        return CLAW_ERROR;
    }

    int pin = pin_j->valueint;
    if (pin < 0 || pin > GPIO_PIN_MAX) {
        cJSON_AddStringToObject(result, "error", "pin out of range (0-21)");
        return CLAW_ERROR;
    }

    int level = gpio_get_level(pin);
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "pin", pin);
    cJSON_AddNumberToObject(result, "level", level);
    cJSON_AddStringToObject(result, "level_str", level ? "HIGH" : "LOW");
    return CLAW_OK;
}

static int tool_gpio_config(const cJSON *params, cJSON *result)
{
    cJSON *pin_j = cJSON_GetObjectItem(params, "pin");
    cJSON *mode_j = cJSON_GetObjectItem(params, "mode");

    if (!pin_j || !cJSON_IsNumber(pin_j) ||
        !mode_j || !cJSON_IsString(mode_j)) {
        cJSON_AddStringToObject(result, "error",
                                "missing pin or mode");
        return CLAW_ERROR;
    }

    int pin = pin_j->valueint;
    if (pin < 0 || pin > GPIO_PIN_MAX) {
        cJSON_AddStringToObject(result, "error", "pin out of range (0-21)");
        return CLAW_ERROR;
    }

    gpio_mode_t mode;
    const char *mode_str = mode_j->valuestring;
    if (strcmp(mode_str, "output") == 0) {
        mode = GPIO_MODE_OUTPUT;
    } else if (strcmp(mode_str, "input") == 0) {
        mode = GPIO_MODE_INPUT;
    } else if (strcmp(mode_str, "input_output") == 0) {
        mode = GPIO_MODE_INPUT_OUTPUT;
    } else {
        cJSON_AddStringToObject(result, "error",
                                "mode must be input/output/input_output");
        return CLAW_ERROR;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);

    if (err == ESP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "GPIO %d configured as %s", pin, mode_str);
        cJSON_AddStringToObject(result, "status", "ok");
        cJSON_AddStringToObject(result, "message", msg);
        CLAW_LOGI(TAG, "%s", msg);
    } else {
        cJSON_AddStringToObject(result, "error", "gpio_config failed");
    }
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

/* JSON schema strings (static, compile-time) */

static const char schema_gpio_set[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number (0-21)\"},"
    "\"level\":{\"type\":\"integer\",\"enum\":[0,1],"
    "\"description\":\"0=LOW, 1=HIGH\"}},"
    "\"required\":[\"pin\",\"level\"]}";

static const char schema_gpio_get[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number (0-21)\"}},"
    "\"required\":[\"pin\"]}";

static const char schema_gpio_config[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number (0-21)\"},"
    "\"mode\":{\"type\":\"string\",\"enum\":[\"input\",\"output\",\"input_output\"],"
    "\"description\":\"GPIO direction mode\"}},"
    "\"required\":[\"pin\",\"mode\"]}";

void claw_tools_register_gpio(void)
{
    claw_tool_register("gpio_set",
        "Set a GPIO pin output level (HIGH=1 or LOW=0). "
        "Automatically configures the pin as output.",
        schema_gpio_set, tool_gpio_set);

    claw_tool_register("gpio_get",
        "Read the current level of a GPIO pin. Returns 0 (LOW) or 1 (HIGH).",
        schema_gpio_get, tool_gpio_get);

    claw_tool_register("gpio_config",
        "Configure a GPIO pin direction mode (input, output, or input_output).",
        schema_gpio_config, tool_gpio_config);
}

#else /* non-ESP-IDF */

void claw_tools_register_gpio(void)
{
    /* GPIO tools not available on this platform */
}

#endif
