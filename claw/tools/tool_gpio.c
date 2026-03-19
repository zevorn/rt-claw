/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GPIO tools — expose GPIO control as LLM-callable tools.
 */

#include "claw/tools/claw_tools.h"
#include "claw/services/swarm/swarm.h"

#include <string.h>
#include <stdio.h>

#define TAG "tool_gpio"

#ifdef CLAW_PLATFORM_ESP_IDF

#include "driver/gpio.h"
#include "soc/soc_caps.h"

#define GPIO_PIN_MAX  SOC_GPIO_PIN_COUNT

/* --- GPIO safety policy --- */

#define GPIO_POLICY_INPUT       (1 << 0)
#define GPIO_POLICY_OUTPUT      (1 << 1)
#define GPIO_POLICY_INPUT_OUTPUT (GPIO_POLICY_INPUT | GPIO_POLICY_OUTPUT)

typedef struct {
    int      pin;
    uint32_t allowed;       /* bitmask of GPIO_POLICY_* */
    const char *label;      /* human-readable purpose */
} gpio_policy_entry_t;

/*
 * Static whitelist — only listed pins are accessible to the AI.
 * Pins not in this table are silently blocked with a friendly hint.
 * Platform-specific via #if defined(CONFIG_IDF_TARGET_*).
 */
#if defined(CONFIG_IDF_TARGET_ESP32C3)

static const gpio_policy_entry_t s_gpio_policy[] = {
    {  0, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    {  1, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    {  2, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    {  3, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    {  4, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC / LED" },
    {  5, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    {  6, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    {  7, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    {  8, GPIO_POLICY_INPUT,        "boot strapping (input only)" },
    {  9, GPIO_POLICY_INPUT,        "boot strapping (input only)" },
    { 10, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    /* 11-17: SPI flash — forbidden */
    /* 18-19: USB-JTAG — forbidden */
    { 20, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    { 21, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
};

#elif defined(CONFIG_IDF_TARGET_ESP32S3)

static const gpio_policy_entry_t s_gpio_policy[] = {
    {  1, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    {  2, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    {  3, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    {  4, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    {  5, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    {  6, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    {  7, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    {  8, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    {  9, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    { 10, GPIO_POLICY_INPUT_OUTPUT, "user GPIO / ADC" },
    { 11, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    { 12, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    { 13, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    { 14, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    { 15, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    { 16, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    { 17, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    { 18, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    { 21, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    /* 19-20: USB-JTAG — forbidden */
    /* 26-32: SPI flash/PSRAM — forbidden */
    { 38, GPIO_POLICY_INPUT_OUTPUT, "user GPIO" },
    { 46, GPIO_POLICY_INPUT,        "input only" },
};

#else

static const gpio_policy_entry_t s_gpio_policy[] = { {0, 0, NULL} };

#endif

#define GPIO_POLICY_COUNT \
    (sizeof(s_gpio_policy) / sizeof(s_gpio_policy[0]))

/*
 * Check if a pin+mode combination is allowed.
 * Returns the policy entry on success, NULL on denial.
 */
static const gpio_policy_entry_t *gpio_check_policy(int pin,
                                                     uint32_t mode_bits)
{
    for (int i = 0; i < (int)GPIO_POLICY_COUNT; i++) {
        if (s_gpio_policy[i].pin == pin) {
            if ((s_gpio_policy[i].allowed & mode_bits) == mode_bits) {
                return &s_gpio_policy[i];
            }
            return NULL;    /* pin found but mode not allowed */
        }
    }
    return NULL;            /* pin not in whitelist */
}

static int gpio_policy_deny(int pin, const char *op, cJSON *result)
{
    char msg[128];

    /* Look up label for a friendlier message */
    for (int i = 0; i < (int)GPIO_POLICY_COUNT; i++) {
        if (s_gpio_policy[i].pin == pin) {
            snprintf(msg, sizeof(msg),
                     "GPIO %d: %s denied — pin is %s",
                     pin, op, s_gpio_policy[i].label);
            cJSON_AddStringToObject(result, "error", msg);
            return CLAW_ERROR;
        }
    }

    snprintf(msg, sizeof(msg),
             "GPIO %d: access denied — pin reserved by hardware "
             "(flash, USB-JTAG, or PSRAM)", pin);
    cJSON_AddStringToObject(result, "error", msg);
    return CLAW_ERROR;
}

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

    if (pin < 0 || pin >= GPIO_PIN_MAX) {
        cJSON_AddStringToObject(result, "error", "pin out of range");
        return CLAW_ERROR;
    }

    if (!gpio_check_policy(pin, GPIO_POLICY_OUTPUT)) {
        return gpio_policy_deny(pin, "output", result);
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
    if (pin < 0 || pin >= GPIO_PIN_MAX) {
        cJSON_AddStringToObject(result, "error", "pin out of range");
        return CLAW_ERROR;
    }

    if (!gpio_check_policy(pin, GPIO_POLICY_INPUT)) {
        return gpio_policy_deny(pin, "input", result);
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
    if (pin < 0 || pin >= GPIO_PIN_MAX) {
        cJSON_AddStringToObject(result, "error", "pin out of range");
        return CLAW_ERROR;
    }

    gpio_mode_t mode;
    uint32_t policy_bits;
    const char *mode_str = mode_j->valuestring;
    if (strcmp(mode_str, "output") == 0) {
        mode = GPIO_MODE_OUTPUT;
        policy_bits = GPIO_POLICY_OUTPUT;
    } else if (strcmp(mode_str, "input") == 0) {
        mode = GPIO_MODE_INPUT;
        policy_bits = GPIO_POLICY_INPUT;
    } else if (strcmp(mode_str, "input_output") == 0) {
        mode = GPIO_MODE_INPUT_OUTPUT;
        policy_bits = GPIO_POLICY_INPUT_OUTPUT;
    } else {
        cJSON_AddStringToObject(result, "error",
                                "mode must be input/output/input_output");
        return CLAW_ERROR;
    }

    if (!gpio_check_policy(pin, policy_bits)) {
        return gpio_policy_deny(pin, mode_str, result);
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

/* ---- GPIO blink — local timer, no AI API calls ---- */

#define BLINK_MAX_STEPS 16

static struct {
    int      pin;
    int      active;
    int      step;
    int      count;
    int      repeat;
    uint32_t intervals[BLINK_MAX_STEPS];
} s_blink;

static struct claw_timer *s_blink_timer;

static void blink_timer_cb(void *arg)
{
    (void)arg;
    if (!s_blink.active) {
        return;
    }

    /* Toggle GPIO */
    int level = gpio_get_level(s_blink.pin);
    gpio_set_level(s_blink.pin, level ? 0 : 1);

    /* Advance to next interval */
    s_blink.step++;
    if (s_blink.step >= s_blink.count) {
        if (s_blink.repeat) {
            s_blink.step = 0;
        } else {
            s_blink.active = 0;
            claw_timer_stop(s_blink_timer);
            CLAW_LOGI(TAG, "blink finished on GPIO %d", s_blink.pin);
            return;
        }
    }

    /* Restart timer with next interval */
    claw_timer_stop(s_blink_timer);
    claw_timer_delete(s_blink_timer);
    s_blink_timer = claw_timer_create("blink", blink_timer_cb, NULL,
                                       s_blink.intervals[s_blink.step],
                                       0);
    if (s_blink_timer) {
        claw_timer_start(s_blink_timer);
    }
}

static int tool_gpio_blink(const cJSON *params, cJSON *result)
{
    cJSON *pin_j = cJSON_GetObjectItem(params, "pin");
    cJSON *intervals_j = cJSON_GetObjectItem(params, "intervals_ms");
    cJSON *repeat_j = cJSON_GetObjectItem(params, "repeat");

    if (!pin_j || !cJSON_IsNumber(pin_j) ||
        !intervals_j || !cJSON_IsArray(intervals_j)) {
        cJSON_AddStringToObject(result, "error",
                                "missing pin or intervals_ms");
        return CLAW_ERROR;
    }

    int pin = pin_j->valueint;
    if (pin < 0 || pin >= GPIO_PIN_MAX) {
        cJSON_AddStringToObject(result, "error", "pin out of range");
        return CLAW_ERROR;
    }
    if (!gpio_check_policy(pin, GPIO_POLICY_OUTPUT)) {
        return gpio_policy_deny(pin, "blink", result);
    }

    /* Stop any existing blink */
    if (s_blink.active && s_blink_timer) {
        claw_timer_stop(s_blink_timer);
        claw_timer_delete(s_blink_timer);
        s_blink_timer = NULL;
    }

    /* Parse interval array */
    int n = cJSON_GetArraySize(intervals_j);
    if (n <= 0 || n > BLINK_MAX_STEPS) {
        cJSON_AddStringToObject(result, "error",
                                "intervals_ms must have 1-16 entries");
        return CLAW_ERROR;
    }

    for (int i = 0; i < n; i++) {
        cJSON *v = cJSON_GetArrayItem(intervals_j, i);
        s_blink.intervals[i] = (v && cJSON_IsNumber(v))
                                ? (uint32_t)v->valueint : 500;
        if (s_blink.intervals[i] < 50) {
            s_blink.intervals[i] = 50;
        }
    }

    /* Configure pin as output */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    s_blink.pin = pin;
    s_blink.count = n;
    s_blink.step = 0;
    s_blink.repeat = (repeat_j && cJSON_IsTrue(repeat_j)) ? 1 : 1;
    s_blink.active = 1;

    /* Start with first interval */
    s_blink_timer = claw_timer_create("blink", blink_timer_cb, NULL,
                                       s_blink.intervals[0], 0);
    if (!s_blink_timer) {
        s_blink.active = 0;
        cJSON_AddStringToObject(result, "error",
                                "timer create failed");
        return CLAW_ERROR;
    }
    claw_timer_start(s_blink_timer);

    char msg[128];
    snprintf(msg, sizeof(msg),
             "GPIO %d blinking with %d intervals, repeat=%s",
             pin, n, s_blink.repeat ? "yes" : "no");
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddStringToObject(result, "message", msg);
    CLAW_LOGI(TAG, "%s", msg);
    return CLAW_OK;
}

static int tool_gpio_blink_stop(const cJSON *params, cJSON *result)
{
    (void)params;
    if (s_blink.active && s_blink_timer) {
        claw_timer_stop(s_blink_timer);
        claw_timer_delete(s_blink_timer);
        s_blink_timer = NULL;
        gpio_set_level(s_blink.pin, 0);
    }
    s_blink.active = 0;

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddStringToObject(result, "message", "blink stopped");
    return CLAW_OK;
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

static const char schema_gpio_blink[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"pin\":{\"type\":\"integer\","
    "\"description\":\"GPIO pin number\"},"
    "\"intervals_ms\":{\"type\":\"array\","
    "\"items\":{\"type\":\"integer\"},"
    "\"description\":\"Array of toggle intervals in milliseconds. "
    "Each entry is the delay before the next toggle. "
    "Example: [1000,1000,2000,3000,5000] for fibonacci-like "
    "blink pattern. Max 16 entries, min 50ms each.\"},"
    "\"repeat\":{\"type\":\"boolean\","
    "\"description\":\"Repeat the pattern (default true)\"}},"
    "\"required\":[\"pin\",\"intervals_ms\"]}";

static const char schema_gpio_blink_stop[] =
    "{\"type\":\"object\",\"properties\":{}}";

void claw_tools_register_gpio(void)
{
    memset(&s_blink, 0, sizeof(s_blink));

    claw_tool_register("gpio_set",
        "Set a GPIO pin output level (HIGH=1 or LOW=0). "
        "Automatically configures the pin as output.",
        schema_gpio_set, tool_gpio_set,
        SWARM_CAP_GPIO, 0);

    claw_tool_register("gpio_get",
        "Read the current level of a GPIO pin. Returns 0 (LOW) or 1 (HIGH).",
        schema_gpio_get, tool_gpio_get,
        SWARM_CAP_GPIO, 0);

    claw_tool_register("gpio_config",
        "Configure a GPIO pin direction mode (input, output, or input_output).",
        schema_gpio_config, tool_gpio_config,
        SWARM_CAP_GPIO, 0);

    claw_tool_register("gpio_blink",
        "Start a GPIO blink pattern. Runs locally with hardware "
        "timers — no AI API calls needed. The pin toggles at each "
        "interval. Use this for LED patterns, Fibonacci blink, "
        "SOS morse code, heartbeat effects, etc. "
        "Call gpio_blink_stop to stop.",
        schema_gpio_blink, tool_gpio_blink,
        SWARM_CAP_GPIO, 0);

    claw_tool_register("gpio_blink_stop",
        "Stop any active GPIO blink pattern.",
        schema_gpio_blink_stop, tool_gpio_blink_stop,
        SWARM_CAP_GPIO, 0);
}

#else /* non-ESP-IDF */

static int tool_gpio_unsupported(const cJSON *params,
                                  cJSON *result)
{
    (void)params;
    cJSON_AddStringToObject(result, "error",
        "GPIO not supported on this platform");
    return CLAW_OK;
}

static const char schema_gpio_empty[] =
    "{\"type\":\"object\",\"properties\":{}}";

void claw_tools_register_gpio(void)
{
    claw_tool_register("gpio_set",
        "Set GPIO pin (unsupported on this platform).",
        schema_gpio_empty, tool_gpio_unsupported,
        SWARM_CAP_GPIO, CLAW_TOOL_LOCAL_ONLY);

    claw_tool_register("gpio_get",
        "Read GPIO pin (unsupported on this platform).",
        schema_gpio_empty, tool_gpio_unsupported,
        SWARM_CAP_GPIO, CLAW_TOOL_LOCAL_ONLY);

    claw_tool_register("gpio_config",
        "Configure GPIO (unsupported on this platform).",
        schema_gpio_empty, tool_gpio_unsupported,
        SWARM_CAP_GPIO, CLAW_TOOL_LOCAL_ONLY);

    claw_tool_register("gpio_blink",
        "Blink GPIO (unsupported on this platform).",
        schema_gpio_empty, tool_gpio_unsupported,
        SWARM_CAP_GPIO, CLAW_TOOL_LOCAL_ONLY);

    claw_tool_register("gpio_blink_stop",
        "Stop blink (unsupported on this platform).",
        schema_gpio_empty, tool_gpio_unsupported,
        SWARM_CAP_GPIO, CLAW_TOOL_LOCAL_ONLY);
}

#endif
