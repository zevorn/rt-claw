/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "claw/tools/claw_tools.h"
#include "claw_config.h"
#ifdef CONFIG_RTCLAW_SKILL_ENABLE
#include "claw/services/ai/ai_skill.h"
#endif
#ifdef CONFIG_RTCLAW_AUDIO_ENABLE
#include "claw/services/swarm/swarm.h"
#include "drivers/audio/espressif/es8311_audio.h"
#endif

#include <string.h>
#include <stdio.h>

#define TAG "tools"

#ifdef CONFIG_RTCLAW_AUDIO_ENABLE
static void claw_tools_register_audio(void);
#endif

static claw_tool_t s_tools[CLAW_TOOL_MAX];
static int s_tool_count;

int claw_tools_init(void)
{
    memset(s_tools, 0, sizeof(s_tools));
    s_tool_count = 0;

#ifdef CONFIG_RTCLAW_TOOL_GPIO
    claw_tools_register_gpio();
#endif
#ifdef CONFIG_RTCLAW_TOOL_SYSTEM
    claw_tools_register_system();
#endif
#ifdef CONFIG_RTCLAW_TOOL_LCD
    claw_tools_register_lcd();
#endif
#ifdef CONFIG_RTCLAW_TOOL_SCHED
    claw_tools_register_sched();
#endif
#ifdef CONFIG_RTCLAW_TOOL_NET
    claw_tools_register_net();
#endif
#ifdef CONFIG_RTCLAW_SKILL_ENABLE
    claw_tools_register_skill();
#endif
#ifdef CONFIG_RTCLAW_OTA_ENABLE
    claw_tools_register_ota();
#endif

    /* Audio tools (always register — stubs on non-ESP-IDF) */
#ifdef CONFIG_RTCLAW_AUDIO_ENABLE
    claw_tools_register_audio();
#endif

    CLAW_LOGI(TAG, "%d tools registered", s_tool_count);
    return CLAW_OK;
}

void claw_tools_stop(void)
{
#ifdef CONFIG_RTCLAW_TOOL_SCHED
    sched_tool_stop();
#endif
}

#ifdef CONFIG_RTCLAW_AUDIO_ENABLE
/* ---- Audio tools (requires CONFIG_RTCLAW_AUDIO_ENABLE) ---- */

static int tool_audio_beep(const cJSON *params, cJSON *result)
{
    int freq = 1000;
    int duration = 200;
    int volume = 60;

    cJSON *f = cJSON_GetObjectItem(params, "frequency_hz");
    cJSON *d = cJSON_GetObjectItem(params, "duration_ms");
    cJSON *v = cJSON_GetObjectItem(params, "volume");

    if (f && cJSON_IsNumber(f)) {
        freq = f->valueint;
    }
    if (d && cJSON_IsNumber(d)) {
        duration = d->valueint;
    }
    if (v && cJSON_IsNumber(v)) {
        volume = v->valueint;
    }

    if (freq < 100 || freq > 8000) {
        cJSON_AddStringToObject(result, "error",
                                "frequency must be 100-8000 Hz");
        return CLAW_ERROR;
    }
    if (duration < 50 || duration > 5000) {
        cJSON_AddStringToObject(result, "error",
                                "duration must be 50-5000 ms");
        return CLAW_ERROR;
    }

    es8311_audio_beep(freq, duration, volume);

    cJSON_AddStringToObject(result, "status", "ok");
    char msg[64];
    snprintf(msg, sizeof(msg), "played %dHz for %dms at vol %d",
             freq, duration, volume);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static int tool_audio_volume(const cJSON *params, cJSON *result)
{
    cJSON *v = cJSON_GetObjectItem(params, "volume");
    if (!v || !cJSON_IsNumber(v)) {
        cJSON_AddStringToObject(result, "error", "missing volume");
        return CLAW_ERROR;
    }

    int vol = v->valueint;
    if (vol < 0 || vol > 100) {
        cJSON_AddStringToObject(result, "error",
                                "volume must be 0-100");
        return CLAW_ERROR;
    }

    es8311_audio_set_volume(vol);
    cJSON_AddStringToObject(result, "status", "ok");
    char msg[32];
    snprintf(msg, sizeof(msg), "volume set to %d", vol);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static const char schema_beep[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"frequency_hz\":{\"type\":\"integer\","
    "\"description\":\"Tone frequency 100-8000 Hz (default 1000)\"},"
    "\"duration_ms\":{\"type\":\"integer\","
    "\"description\":\"Duration 50-5000 ms (default 200)\"},"
    "\"volume\":{\"type\":\"integer\","
    "\"description\":\"Volume 0-100 (default 60)\"}},"
    "\"required\":[]}";

static const char schema_volume[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"volume\":{\"type\":\"integer\","
    "\"description\":\"Speaker volume 0-100\"}},"
    "\"required\":[\"volume\"]}";

static int tool_audio_play_sound(const cJSON *params, cJSON *result)
{
    cJSON *name_j = cJSON_GetObjectItem(params, "name");
    if (!name_j || !cJSON_IsString(name_j)) {
        cJSON_AddStringToObject(result, "error", "missing name");
        return CLAW_ERROR;
    }

    if (es8311_audio_play_sound(name_j->valuestring) != 0) {
        cJSON_AddStringToObject(result, "error",
                                "unknown sound name");
        return CLAW_ERROR;
    }

    cJSON_AddStringToObject(result, "status", "ok");
    char msg[48];
    snprintf(msg, sizeof(msg), "played sound: %s",
             name_j->valuestring);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static const char schema_play_sound[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"name\":{\"type\":\"string\","
    "\"enum\":[\"success\",\"error\",\"notify\","
    "\"alert\",\"startup\",\"click\"],"
    "\"description\":\"Preset sound effect name\"}},"
    "\"required\":[\"name\"]}";

static void claw_tools_register_audio(void)
{
    claw_tool_register("audio_beep",
        "Play a single tone. Use for custom frequencies.",
        schema_beep, tool_audio_beep,
        SWARM_CAP_SPEAKER, 0);

    claw_tool_register("audio_play_sound",
        "Play a preset sound effect. Available sounds: "
        "success (task done), error (something failed), "
        "notify (new message), alert (urgent warning), "
        "startup (boot jingle), click (button feedback). "
        "Use this for common feedback instead of audio_beep.",
        schema_play_sound, tool_audio_play_sound,
        SWARM_CAP_SPEAKER, 0);

    claw_tool_register("audio_volume",
        "Set the speaker volume (0-100).",
        schema_volume, tool_audio_volume,
        SWARM_CAP_SPEAKER, 0);
}
#endif /* CONFIG_RTCLAW_AUDIO_ENABLE */

int claw_tool_register(const char *name, const char *description,
                       const char *input_schema_json,
                       claw_tool_fn execute,
                       uint8_t caps, uint8_t flags)
{
    if (s_tool_count >= CLAW_TOOL_MAX) {
        CLAW_LOGE(TAG, "tool registry full");
        return CLAW_ERROR;
    }
    if (!name || !execute) {
        return CLAW_ERROR;
    }

    claw_tool_t *t = &s_tools[s_tool_count];
    strncpy(t->name, name, CLAW_TOOL_NAME_MAX - 1);
    t->name[CLAW_TOOL_NAME_MAX - 1] = '\0';
    t->description = description;
    t->input_schema_json = input_schema_json;
    t->execute = execute;
    t->required_caps = caps;
    t->flags = flags;
    s_tool_count++;

    CLAW_LOGD(TAG, "registered: %s", name);
    return CLAW_OK;
}

int claw_tools_count(void)
{
    return s_tool_count;
}

const claw_tool_t *claw_tool_get(int index)
{
    if (index < 0 || index >= s_tool_count) {
        return NULL;
    }
    return &s_tools[index];
}

const claw_tool_t *claw_tool_find(const char *name)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            return &s_tools[i];
        }
    }
    return NULL;
}

cJSON *claw_tools_to_json(void)
{
    return claw_tools_to_json_exclude(NULL);
}

cJSON *claw_tools_to_json_exclude(const char *prefix)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }

    size_t prefix_len = prefix ? strlen(prefix) : 0;

    for (int i = 0; i < s_tool_count; i++) {
        const claw_tool_t *t = &s_tools[i];

        if (prefix_len > 0 &&
            strncmp(t->name, prefix, prefix_len) == 0) {
            continue;
        }

        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", t->name);
        cJSON_AddStringToObject(tool, "description", t->description);

        cJSON *schema = cJSON_Parse(t->input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }
        cJSON_AddItemToArray(arr, tool);
    }
    return arr;
}

/* OOP service registration */
#include "claw/core/claw_service.h"
static const char *tools_deps[] = { "ai_engine", NULL };
CLAW_DEFINE_SIMPLE_SERVICE(tools, "tools",
    claw_tools_init, NULL, claw_tools_stop, tools_deps);
