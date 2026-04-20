/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Audio tools — beep, play sound, volume control.
 */

#include "claw/services/tools/tools.h"
#include "claw/services/swarm/swarm.h"

#include <stdio.h>

#ifdef CONFIG_RTCLAW_AUDIO_ENABLE

#ifdef CONFIG_RTCLAW_ES8388
#include "drivers/audio/espressif/es8388_audio.h"
#else
#include "drivers/audio/espressif/es8311_audio.h"
#endif


typedef void (*audio_beep_t)(int freq, int duration, int volume);
typedef void (*audio_set_volume_t)(int vol);
typedef int  (*audio_play_sound_t)(const char *name);


#ifdef CONFIG_RTCLAW_ES8388
static audio_beep_t       board_audio_beep       = es8388_audio_beep;
static audio_set_volume_t board_audio_set_volume = es8388_audio_set_volume;
static audio_play_sound_t board_audio_play_sound = es8388_audio_play_sound;
#else
static audio_beep_t       board_audio_beep       = es8311_audio_beep;
static audio_set_volume_t board_audio_set_volume = es8311_audio_set_volume;
static audio_play_sound_t board_audio_play_sound = es8311_audio_play_sound;
#endif

static claw_err_t tool_audio_beep(struct claw_tool *tool,
                                  const cJSON *params, cJSON *result)
{
    (void)tool;
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

    /* es8311_audio_beep(freq, duration, volume); */
    board_audio_beep(freq, duration, volume);

    cJSON_AddStringToObject(result, "status", "ok");
    char msg[64];
    snprintf(msg, sizeof(msg), "played %dHz for %dms at vol %d",
             freq, duration, volume);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static claw_err_t tool_audio_volume(struct claw_tool *tool,
                                    const cJSON *params, cJSON *result)
{
    (void)tool;
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

    /*  es8311_audio_set_volume(vol); */
    board_audio_set_volume(vol);
    cJSON_AddStringToObject(result, "status", "ok");
    char msg[32];
    snprintf(msg, sizeof(msg), "volume set to %d", vol);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static claw_err_t tool_audio_play_sound(struct claw_tool *tool,
                                        const cJSON *params, cJSON *result)
{
    (void)tool;
    cJSON *name_j = cJSON_GetObjectItem(params, "name");
    if (!name_j || !cJSON_IsString(name_j)) {
        cJSON_AddStringToObject(result, "error", "missing name");
        return CLAW_ERROR;
    }

    /* if (es8311_audio_play_sound(name_j->valuestring) != 0) { */
     if (board_audio_play_sound(name_j->valuestring) != 0) {
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

static const char schema_play_sound[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"name\":{\"type\":\"string\","
    "\"enum\":[\"success\",\"error\",\"notify\","
    "\"alert\",\"startup\",\"click\"],"
    "\"description\":\"Preset sound effect name\"}},"
    "\"required\":[\"name\"]}";

/* ---- OOP tool registration ---- */

static const struct claw_tool_ops beep_ops = {
    .execute = tool_audio_beep,
};
static struct claw_tool beep_tool = {
    .name = "audio_beep",
    .description =
        "Play a single tone. Use for custom frequencies.",
    .input_schema_json = schema_beep,
    .ops = &beep_ops,
    .required_caps = SWARM_CAP_SPEAKER,
};
CLAW_TOOL_REGISTER(audio_beep, &beep_tool);

static const struct claw_tool_ops play_sound_ops = {
    .execute = tool_audio_play_sound,
};
static struct claw_tool play_sound_tool = {
    .name = "audio_play_sound",
    .description =
        "Play a preset sound effect. Available sounds: "
        "success (task done), error (something failed), "
        "notify (new message), alert (urgent warning), "
        "startup (boot jingle), click (button feedback). "
        "Use this for common feedback instead of audio_beep.",
    .input_schema_json = schema_play_sound,
    .ops = &play_sound_ops,
    .required_caps = SWARM_CAP_SPEAKER,
};
CLAW_TOOL_REGISTER(audio_play_sound, &play_sound_tool);

static const struct claw_tool_ops volume_ops = {
    .execute = tool_audio_volume,
};
static struct claw_tool volume_tool = {
    .name = "audio_volume",
    .description = "Set the speaker volume (0-100).",
    .input_schema_json = schema_volume,
    .ops = &volume_ops,
    .required_caps = SWARM_CAP_SPEAKER,
};
CLAW_TOOL_REGISTER(audio_volume, &volume_tool);

#endif /* CONFIG_RTCLAW_AUDIO_ENABLE */
