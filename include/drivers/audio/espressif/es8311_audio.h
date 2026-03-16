/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ES8311 audio codec driver — I2C control + I2S audio data.
 * Supports playback (speaker) and capture (microphone).
 */

#ifndef CLAW_DRIVERS_AUDIO_ESPRESSIF_ES8311_AUDIO_H
#define CLAW_DRIVERS_AUDIO_ESPRESSIF_ES8311_AUDIO_H

#include <stdint.h>
#include <stddef.h>

/**
 * Initialize ES8311 codec + I2S + PA on an existing I2C bus.
 *
 * @param i2c_bus   I2C master bus handle (shared with OLED etc.)
 * @param pa_pin    Power amplifier enable GPIO (-1 to skip)
 * @return 0 on success
 */
int es8311_audio_init(void *i2c_bus, int pa_pin);

/** Set output volume (0-100). */
void es8311_audio_set_volume(int vol);

/** Enable or disable speaker output (controls PA GPIO). */
void es8311_audio_enable_output(int enable);

/**
 * Play a tone (blocking).
 * @param freq_hz   Tone frequency in Hz
 * @param duration_ms  Duration in milliseconds
 * @param volume    Volume 0-100
 */
void es8311_audio_beep(int freq_hz, int duration_ms, int volume);

/**
 * Write raw PCM data to the speaker.
 * @param data      16-bit signed PCM samples, 24kHz mono
 * @param samples   Number of samples
 */
void es8311_audio_write(const int16_t *data, size_t samples);

/**
 * Play a named preset sound effect.
 * Available names: "success", "error", "notify", "alert",
 *                  "startup", "click"
 * @return 0 on success, -1 if name unknown
 */
int es8311_audio_play_sound(const char *name);

#endif /* CLAW_DRIVERS_AUDIO_ESPRESSIF_ES8311_AUDIO_H */
