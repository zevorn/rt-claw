#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/core/service.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/services/voice/voice_service.h"
#include "claw/services/voice/voice_endpoint.h"
#include "claw/services/voice/voice_providers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "voice"
#define VOICE_TEXT_BUF_SIZE    CONFIG_RTCLAW_VOICE_TEXT_BUF_SIZE
#define VOICE_TTS_BUF_SIZE     CONFIG_RTCLAW_VOICE_TTS_AUDIO_BUF_SIZE
#define VOICE_MQ_DEPTH         16
#ifdef CLAW_PLATFORM_LINUX
#define VOICE_TTS_BUF_LIMIT    (VOICE_TTS_BUF_SIZE * 8U)
#else
#define VOICE_TTS_BUF_LIMIT    VOICE_TTS_BUF_SIZE
#endif

struct voice_service_ctx {
    struct claw_service       base;
    struct claw_mq           *mq;
    struct claw_thread       *worker;
    struct claw_mutex        *lock;
    struct voice_stt_session *stt_session;
    int                       state;
    int                       active_session_id;
    int                       turn_truncated;
    size_t                    turn_bytes;
    struct voice_audio_format turn_format;
    char                      transcript[VOICE_TEXT_BUF_SIZE];
    char                      reply[VOICE_TEXT_BUF_SIZE];
    voice_runtime_config_t    cfg;
};

CLAW_ASSERT_EMBEDDED_FIRST(struct voice_service_ctx, base);

static struct voice_service_ctx s_voice;
static const char *voice_deps[] = { "ai_engine", NULL };

static const char *state_to_string(int state)
{
    switch (state) {
    case VOICE_ENDPOINT_DISABLED: return "disabled";
    case VOICE_ENDPOINT_IDLE: return "idle";
    case VOICE_ENDPOINT_SESSION_READY: return "ready";
    case VOICE_ENDPOINT_CAPTURING: return "capturing";
    case VOICE_ENDPOINT_TRANSCRIBING: return "transcribing";
    case VOICE_ENDPOINT_THINKING: return "thinking";
    case VOICE_ENDPOINT_SYNTHESIZING: return "synthesizing";
    case VOICE_ENDPOINT_PLAYING: return "playing";
    case VOICE_ENDPOINT_ERROR: return "error";
    default: return "unknown";
    }
}

static void voice_set_state(struct voice_service_ctx *ctx, int state,
                            const char *detail)
{
    const char *from = state_to_string(ctx->state);
    const char *to = state_to_string(state);

    ctx->state = state;
    CLAW_LOGI(TAG, "state %s -> %s%s%s",
              from, to,
              detail ? " (" : "",
              detail ? detail : "");
    if (detail) {
        CLAW_LOGD(TAG, "state detail: %s", detail);
    }
    (void)voice_endpoint_send_state(state, detail);
}

static void voice_reset_turn(struct voice_service_ctx *ctx)
{
    ctx->turn_bytes = 0;
    ctx->turn_truncated = 0;
    memset(&ctx->turn_format, 0, sizeof(ctx->turn_format));
    ctx->transcript[0] = '\0';
    ctx->reply[0] = '\0';
}

static void voice_destroy_session(struct voice_service_ctx *ctx)
{
    if (!ctx->stt_session) {
        return;
    }
    voice_stt_session_destroy(ctx->stt_session);
    ctx->stt_session = NULL;
}

static void voice_abort_turn(struct voice_service_ctx *ctx)
{
    if (ctx->stt_session) {
        CLAW_LOGI(TAG, "aborting turn (%u bytes, truncated=%d)",
                  (unsigned int)ctx->turn_bytes, ctx->turn_truncated);
        voice_stt_session_abort(ctx->stt_session);
        voice_destroy_session(ctx);
    }
    voice_reset_turn(ctx);
}

static void voice_set_ready_state(struct voice_service_ctx *ctx)
{
    if (!ctx->cfg.enabled) {
        voice_set_state(ctx, VOICE_ENDPOINT_DISABLED, NULL);
    } else if (ctx->active_session_id >= 0) {
        voice_set_state(ctx, VOICE_ENDPOINT_SESSION_READY, NULL);
    } else {
        voice_set_state(ctx, VOICE_ENDPOINT_IDLE, NULL);
    }
}

static void voice_fail(struct voice_service_ctx *ctx, const char *message)
{
    CLAW_LOGE(TAG, "voice flow failed: %s",
              message ? message : "unknown");
    voice_set_state(ctx, VOICE_ENDPOINT_ERROR, message);
    (void)voice_endpoint_send_error(message);
    voice_abort_turn(ctx);
    voice_set_ready_state(ctx);
}

static void voice_fill_turn_format(struct voice_service_ctx *ctx,
                                   const struct voice_endpoint_event *event)
{
    memset(&ctx->turn_format, 0, sizeof(ctx->turn_format));
    ctx->turn_format.sample_rate = ctx->cfg.input_sample_rate;
    ctx->turn_format.channels = ctx->cfg.input_channels;
    ctx->turn_format.bits_per_sample = ctx->cfg.input_bits_per_sample;
    snprintf(ctx->turn_format.encoding, sizeof(ctx->turn_format.encoding),
             "%s", ctx->cfg.input_encoding);

    if (event->format.sample_rate > 0) {
        ctx->turn_format.sample_rate = event->format.sample_rate;
    }
    if (event->format.channels > 0) {
        ctx->turn_format.channels = event->format.channels;
    }
    if (event->format.bits_per_sample > 0) {
        ctx->turn_format.bits_per_sample = event->format.bits_per_sample;
    }
    if (event->format.encoding[0]) {
        snprintf(ctx->turn_format.encoding,
                 sizeof(ctx->turn_format.encoding),
                 "%s", event->format.encoding);
    }
}

static claw_err_t voice_run_tts(struct voice_service_ctx *ctx,
                                 const char **mime_type,
                                 uint8_t **audio_out,
                                 size_t *audio_out_len)
{
    claw_err_t rc;
    size_t buf_size = VOICE_TTS_BUF_SIZE;

    if (!ctx || !mime_type || !audio_out || !audio_out_len) {
        return CLAW_ERR_INVALID;
    }

    while (1) {
        *audio_out = (uint8_t *)claw_malloc(buf_size);
        if (!*audio_out) {
            return CLAW_ERR_NOMEM;
        }
        CLAW_LOGI(TAG,
                  "starting TTS provider=%s model=%s format=%s stream=%d "
                  "voice=%s style_prompt=%s out_buf=%u",
                  ctx->cfg.tts_provider[0] ?
                  ctx->cfg.tts_provider : "(default)",
                  ctx->cfg.tts_model[0] ? ctx->cfg.tts_model : "(unset)",
                  ctx->cfg.tts_format[0] ? ctx->cfg.tts_format : "(default)",
                  ctx->cfg.tts_stream,
                  ctx->cfg.tts_voice[0] ? ctx->cfg.tts_voice : "(unset)",
                  ctx->cfg.tts_style_prompt[0] ? "set" : "unset",
                  (unsigned int)buf_size);
        rc = voice_tts_synthesize(&ctx->cfg,
                                  ctx->reply,
                                  *audio_out,
                                  buf_size,
                                  audio_out_len,
                                  mime_type);
        if (rc != CLAW_ERR_FULL || buf_size >= VOICE_TTS_BUF_LIMIT) {
            if (rc != CLAW_OK) {
                CLAW_LOGE(TAG, "tts failed: %s", claw_strerror(rc));
                claw_free(*audio_out);
                *audio_out = NULL;
            }
            return rc;
        }
        claw_free(*audio_out);
        *audio_out = NULL;
        {
            size_t old_size = buf_size;
            buf_size *= 2U;
            CLAW_LOGW(TAG,
                      "retrying TTS with larger decoded-audio buffer: "
                      "old=%u new=%u",
                      (unsigned int)old_size,
                      (unsigned int)buf_size);
        }
    }
}

static void voice_process_end_capture(struct voice_service_ctx *ctx)
{
    const char *hint =
        "Voice conversation. Keep replies concise and natural for speech.";
    size_t audio_out_len = 0;
    uint8_t *audio_out = NULL;
    const char *mime_type = NULL;
    claw_err_t rc;

    if (ctx->turn_bytes == 0 || !ctx->stt_session) {
        voice_fail(ctx, "no audio captured");
        return;
    }

    CLAW_LOGI(TAG,
              "finalizing capture: bytes=%u format=%dHz/%dch/%dbps %s "
              "truncated=%d",
              (unsigned int)ctx->turn_bytes,
              ctx->turn_format.sample_rate,
              ctx->turn_format.channels,
              ctx->turn_format.bits_per_sample,
              ctx->turn_format.encoding,
              ctx->turn_truncated);
    voice_set_state(ctx, VOICE_ENDPOINT_TRANSCRIBING, NULL);
    rc = voice_stt_session_finish(ctx->stt_session,
                                  ctx->transcript,
                                  sizeof(ctx->transcript));
    voice_destroy_session(ctx);
    if (rc == CLAW_ERR_INVALID) {
        CLAW_LOGE(TAG, "stt finish failed: %s", claw_strerror(rc));
        voice_fail(ctx, "stt config invalid");
        return;
    }
    if (rc == CLAW_ERR_TIMEOUT) {
        CLAW_LOGE(TAG, "stt finish failed: %s", claw_strerror(rc));
        voice_fail(ctx, "stt timed out");
        return;
    }
    if (rc != CLAW_OK) {
        CLAW_LOGE(TAG, "stt finish failed: %s", claw_strerror(rc));
        voice_fail(ctx, "stt transport failed");
        return;
    }
    CLAW_LOGI(TAG, "transcript ready (%u bytes): %.120s",
              (unsigned int)strlen(ctx->transcript), ctx->transcript);
    (void)voice_endpoint_send_transcript(ctx->transcript);

    voice_set_state(ctx, VOICE_ENDPOINT_THINKING, NULL);
    ai_set_channel(AI_CHANNEL_SHELL);
    ai_set_channel_hint(hint);
    CLAW_LOGI(TAG, "sending transcript to ai_chat (%u bytes)",
              (unsigned int)strlen(ctx->transcript));
    rc = ai_chat(ctx->transcript, ctx->reply, sizeof(ctx->reply));
    ai_set_channel_hint(NULL);
    if (rc != CLAW_OK) {
        CLAW_LOGE(TAG, "ai_chat failed: %s", claw_strerror(rc));
        voice_fail(ctx, "ai chat failed");
        return;
    }
    CLAW_LOGI(TAG, "assistant reply ready (%u bytes): %.120s",
              (unsigned int)strlen(ctx->reply), ctx->reply);
    (void)voice_endpoint_send_assistant_text(ctx->reply);

    voice_set_state(ctx, VOICE_ENDPOINT_SYNTHESIZING, NULL);
    rc = voice_run_tts(ctx, &mime_type, &audio_out, &audio_out_len);
    if (rc == CLAW_ERR_NOMEM) {
        voice_fail(ctx, "tts oom");
        return;
    }
    if (rc != CLAW_OK) {
        voice_fail(ctx, "tts failed");
        return;
    }

    CLAW_LOGI(TAG, "tts audio ready: %u bytes mime=%s",
              (unsigned int)audio_out_len,
              mime_type ? mime_type : "(null)");
    voice_set_state(ctx, VOICE_ENDPOINT_PLAYING, NULL);
    (void)voice_endpoint_send_tts_audio(audio_out, audio_out_len, mime_type);
    claw_free(audio_out);
}

static void voice_handle_audio_chunk(struct voice_service_ctx *ctx,
                                     const struct voice_endpoint_event *event)
{
    size_t allowed = event->data_len;
    claw_err_t rc;

    if (ctx->state != VOICE_ENDPOINT_CAPTURING || !ctx->stt_session) {
        return;
    }
    if (event->data_len == 0 || !event->data_ptr || ctx->turn_truncated) {
        return;
    }
    if (ctx->turn_bytes >= CONFIG_RTCLAW_VOICE_MAX_TURN_BYTES) {
        CLAW_LOGW(TAG, "turn byte limit reached at %u bytes",
                  (unsigned int)ctx->turn_bytes);
        ctx->turn_truncated = 1;
        voice_process_end_capture(ctx);
        return;
    }
    if (ctx->turn_bytes + allowed > CONFIG_RTCLAW_VOICE_MAX_TURN_BYTES) {
        allowed = CONFIG_RTCLAW_VOICE_MAX_TURN_BYTES - ctx->turn_bytes;
        ctx->turn_truncated = 1;
        CLAW_LOGW(TAG, "truncating chunk to %u bytes at max turn %u",
                  (unsigned int)allowed,
                  (unsigned int)CONFIG_RTCLAW_VOICE_MAX_TURN_BYTES);
    }
    if (allowed == 0) {
        voice_process_end_capture(ctx);
        return;
    }

    rc = voice_stt_session_send(ctx->stt_session, event->data_ptr, allowed);
    if (rc != CLAW_OK) {
        CLAW_LOGE(TAG, "stt stream send failed after %u bytes: %s",
                  (unsigned int)ctx->turn_bytes,
                  claw_strerror(rc));
        voice_fail(ctx, "stt stream failed");
        return;
    }
    ctx->turn_bytes += allowed;
    CLAW_LOGD(TAG, "streamed audio chunk=%u total=%u",
              (unsigned int)allowed,
              (unsigned int)ctx->turn_bytes);

    if (ctx->turn_truncated) {
        voice_process_end_capture(ctx);
    }
}

static void voice_handle_event(struct voice_service_ctx *ctx,
                               const struct voice_endpoint_event *event)
{
    claw_err_t rc;

    switch (event->type) {
    case VOICE_ENDPOINT_EVENT_ATTACH:
        CLAW_LOGI(TAG, "endpoint attached: session=%d", event->session_id);
        voice_abort_turn(ctx);
        ctx->active_session_id = event->session_id;
        voice_set_ready_state(ctx);
        break;
    case VOICE_ENDPOINT_EVENT_DETACH:
        CLAW_LOGI(TAG, "endpoint detached: session=%d", event->session_id);
        if (ctx->active_session_id == event->session_id) {
            voice_abort_turn(ctx);
            ctx->active_session_id = -1;
            voice_set_state(ctx, VOICE_ENDPOINT_IDLE, NULL);
        }
        break;
    case VOICE_ENDPOINT_EVENT_START_CAPTURE:
        if (!ctx->cfg.enabled) {
            voice_set_state(ctx, VOICE_ENDPOINT_DISABLED, NULL);
            break;
        }
        CLAW_LOGI(TAG, "start capture: session=%d active=%d provider=%s",
                  event->session_id,
                  ctx->active_session_id,
                  ctx->cfg.stt_provider[0] ?
                  ctx->cfg.stt_provider : "(default)");
        if (ctx->active_session_id != event->session_id) {
            voice_fail(ctx, "session mismatch");
            break;
        }
        voice_abort_turn(ctx);
        voice_fill_turn_format(ctx, event);
        CLAW_LOGI(TAG, "capture format: %dHz/%dch/%dbps %s",
                  ctx->turn_format.sample_rate,
                  ctx->turn_format.channels,
                  ctx->turn_format.bits_per_sample,
                  ctx->turn_format.encoding);
        rc = voice_stt_session_start(&ctx->stt_session,
                                     &ctx->cfg,
                                     &ctx->turn_format);
        if (rc == CLAW_ERR_INVALID) {
            CLAW_LOGE(TAG, "stt session start failed: %s", claw_strerror(rc));
            voice_fail(ctx, "stt config invalid");
            break;
        }
        if (rc != CLAW_OK) {
            CLAW_LOGE(TAG, "stt session start failed: %s", claw_strerror(rc));
            voice_fail(ctx, "stt connect failed");
            break;
        }
        voice_set_state(ctx, VOICE_ENDPOINT_CAPTURING, NULL);
        break;
    case VOICE_ENDPOINT_EVENT_AUDIO_CHUNK:
        if (!ctx->cfg.enabled) {
            voice_set_state(ctx, VOICE_ENDPOINT_DISABLED, NULL);
            break;
        }
        voice_handle_audio_chunk(ctx, event);
        break;
    case VOICE_ENDPOINT_EVENT_END_CAPTURE:
        if (!ctx->cfg.enabled) {
            voice_set_state(ctx, VOICE_ENDPOINT_DISABLED, NULL);
            break;
        }
        CLAW_LOGI(TAG, "end capture: session=%d total=%u",
                  event->session_id,
                  (unsigned int)ctx->turn_bytes);
        if (ctx->state != VOICE_ENDPOINT_CAPTURING) {
            break;
        }
        voice_process_end_capture(ctx);
        break;
    case VOICE_ENDPOINT_EVENT_CANCEL:
        CLAW_LOGI(TAG, "capture cancelled: session=%d", event->session_id);
        voice_abort_turn(ctx);
        voice_set_ready_state(ctx);
        break;
    case VOICE_ENDPOINT_EVENT_PLAYBACK_DONE:
        if (!ctx->cfg.enabled) {
            voice_set_state(ctx, VOICE_ENDPOINT_DISABLED, NULL);
            break;
        }
        CLAW_LOGI(TAG, "playback done: session=%d", event->session_id);
        if (ctx->state == VOICE_ENDPOINT_PLAYING) {
            voice_reset_turn(ctx);
            voice_set_ready_state(ctx);
        }
        break;
    default:
        break;
    }
}

static void voice_worker(void *arg)
{
    struct voice_service_ctx *ctx = (struct voice_service_ctx *)arg;
    struct voice_endpoint_event event;

    while (!claw_thread_should_exit()) {
        if (claw_mq_recv(ctx->mq, &event, sizeof(event), 100) != CLAW_OK) {
            continue;
        }
        voice_handle_event(ctx, &event);
        /* Release owned audio buffer after the event has been handled. */
        if (event.data_owns && event.data_ptr) {
            claw_free(event.data_ptr);
            event.data_ptr = NULL;
        }
    }
}

claw_err_t voice_config_set_enabled(int enabled)
{
    s_voice.cfg.enabled = enabled ? 1 : 0;
    if (!s_voice.cfg.enabled) {
        voice_abort_turn(&s_voice);
        s_voice.state = VOICE_ENDPOINT_DISABLED;
    } else if (s_voice.active_session_id >= 0) {
        s_voice.state = VOICE_ENDPOINT_SESSION_READY;
    } else {
        s_voice.state = VOICE_ENDPOINT_IDLE;
    }
    return CLAW_OK;
}

int voice_config_get_enabled(void)
{
    return s_voice.cfg.enabled;
}

claw_err_t voice_config_set_web_port(int port)
{
    if (port <= 0 || port > 65535) {
        return CLAW_ERR_INVALID;
    }
    s_voice.cfg.web_port = port;
    return CLAW_OK;
}

int voice_config_get_web_port(void)
{
    return s_voice.cfg.web_port;
}

static claw_err_t voice_config_set_int(int *dst, const char *value, int min)
{
    long parsed = 0;
    int i = 0;

    if (!dst || !value || !value[0]) {
        return CLAW_ERR_INVALID;
    }
    while (value[i] >= '0' && value[i] <= '9') {
        parsed = parsed * 10 + (value[i] - '0');
        if (parsed > 2147483647L) {
            return CLAW_ERR_INVALID;
        }
        i++;
    }
    if (value[i] != '\0' || parsed < min) {
        return CLAW_ERR_INVALID;
    }
    *dst = (int)parsed;
    return CLAW_OK;
}

claw_err_t voice_config_set_string(const char *key, const char *value)
{
    char *dst = NULL;
    size_t dst_size = 0;

    if (!key || !value) {
        return CLAW_ERR_INVALID;
    }

    if (strcmp(key, "endpoint_backend") == 0) {
        dst = s_voice.cfg.endpoint_backend;
        dst_size = sizeof(s_voice.cfg.endpoint_backend);
    } else if (strcmp(key, "stt_provider") == 0) {
        dst = s_voice.cfg.stt_provider;
        dst_size = sizeof(s_voice.cfg.stt_provider);
    } else if (strcmp(key, "stt_url") == 0) {
        dst = s_voice.cfg.stt_url;
        dst_size = sizeof(s_voice.cfg.stt_url);
    } else if (strcmp(key, "stt_key") == 0) {
        dst = s_voice.cfg.stt_key;
        dst_size = sizeof(s_voice.cfg.stt_key);
    } else if (strcmp(key, "stt_model") == 0) {
        dst = s_voice.cfg.stt_model;
        dst_size = sizeof(s_voice.cfg.stt_model);
    } else if (strcmp(key, "stt_xfyun_app_id") == 0) {
        dst = s_voice.cfg.stt_xfyun_app_id;
        dst_size = sizeof(s_voice.cfg.stt_xfyun_app_id);
    } else if (strcmp(key, "stt_xfyun_api_key") == 0) {
        dst = s_voice.cfg.stt_xfyun_api_key;
        dst_size = sizeof(s_voice.cfg.stt_xfyun_api_key);
    } else if (strcmp(key, "stt_xfyun_api_secret") == 0) {
        dst = s_voice.cfg.stt_xfyun_api_secret;
        dst_size = sizeof(s_voice.cfg.stt_xfyun_api_secret);
    } else if (strcmp(key, "input_encoding") == 0) {
        dst = s_voice.cfg.input_encoding;
        dst_size = sizeof(s_voice.cfg.input_encoding);
    } else if (strcmp(key, "tts_provider") == 0) {
        dst = s_voice.cfg.tts_provider;
        dst_size = sizeof(s_voice.cfg.tts_provider);
    } else if (strcmp(key, "tts_url") == 0) {
        dst = s_voice.cfg.tts_url;
        dst_size = sizeof(s_voice.cfg.tts_url);
    } else if (strcmp(key, "tts_key") == 0) {
        dst = s_voice.cfg.tts_key;
        dst_size = sizeof(s_voice.cfg.tts_key);
    } else if (strcmp(key, "tts_model") == 0) {
        dst = s_voice.cfg.tts_model;
        dst_size = sizeof(s_voice.cfg.tts_model);
    } else if (strcmp(key, "tts_voice") == 0) {
        dst = s_voice.cfg.tts_voice;
        dst_size = sizeof(s_voice.cfg.tts_voice);
    } else if (strcmp(key, "tts_style_prompt") == 0) {
        dst = s_voice.cfg.tts_style_prompt;
        dst_size = sizeof(s_voice.cfg.tts_style_prompt);
    } else if (strcmp(key, "tts_format") == 0) {
        dst = s_voice.cfg.tts_format;
        dst_size = sizeof(s_voice.cfg.tts_format);
    } else if (strcmp(key, "stt_timeout_ms") == 0) {
        return voice_config_set_int(&s_voice.cfg.stt_timeout_ms, value, 1);
    } else if (strcmp(key, "tts_stream") == 0) {
        return voice_config_set_int(&s_voice.cfg.tts_stream, value, 0);
    } else if (strcmp(key, "input_sample_rate") == 0) {
        return voice_config_set_int(&s_voice.cfg.input_sample_rate, value, 1);
    } else if (strcmp(key, "input_channels") == 0) {
        return voice_config_set_int(&s_voice.cfg.input_channels, value, 1);
    } else if (strcmp(key, "input_bits_per_sample") == 0) {
        return voice_config_set_int(&s_voice.cfg.input_bits_per_sample,
                                    value, 1);
    } else {
        return CLAW_ERR_NOENT;
    }

    snprintf(dst, dst_size, "%s", value);
    return CLAW_OK;
}

const voice_runtime_config_t *voice_config_get(void)
{
    return &s_voice.cfg;
}

int voice_state_get(void)
{
    return s_voice.state;
}

const char *voice_state_name(int state)
{
    return state_to_string(state);
}

claw_err_t voice_submit_event(const struct voice_endpoint_event *event)
{
    claw_err_t rc;

    if (!event || !s_voice.mq) {
        return CLAW_ERR_STATE;
    }
    rc = claw_mq_send(s_voice.mq, event, sizeof(*event), 0);
    if (rc != CLAW_OK) {
        /*
         * Queue is full.  If the caller transferred buffer ownership via
         * data_owns, free the buffer now so it is never leaked.
         */
        if (event->data_owns && event->data_ptr) {
            claw_free((void *)event->data_ptr);
        }
        return CLAW_ERR_FULL;
    }
    return CLAW_OK;
}

static claw_err_t voice_svc_init(struct claw_service *svc)
{
    struct voice_service_ctx *ctx =
        container_of(svc, struct voice_service_ctx, base);

    if (ctx->cfg.web_port <= 0) {
        ctx->cfg.web_port = 8080;
    }
    if (ctx->cfg.stt_timeout_ms <= 0) {
        ctx->cfg.stt_timeout_ms = CONFIG_RTCLAW_VOICE_STT_TIMEOUT_MS;
    }
    if (ctx->cfg.input_sample_rate <= 0) {
        ctx->cfg.input_sample_rate = 16000;
    }
    if (ctx->cfg.input_channels <= 0) {
        ctx->cfg.input_channels = 1;
    }
    if (ctx->cfg.input_bits_per_sample <= 0) {
        ctx->cfg.input_bits_per_sample = 16;
    }
    if (ctx->cfg.endpoint_backend[0] == '\0') {
        snprintf(ctx->cfg.endpoint_backend, sizeof(ctx->cfg.endpoint_backend),
                 "%s", "web");
    }
    if (ctx->cfg.stt_provider[0] == '\0') {
        snprintf(ctx->cfg.stt_provider, sizeof(ctx->cfg.stt_provider),
                 "%s", "xfyun");
    }
    if (ctx->cfg.input_encoding[0] == '\0') {
        snprintf(ctx->cfg.input_encoding, sizeof(ctx->cfg.input_encoding),
                 "%s", "pcm_s16le");
    }
    if (ctx->cfg.tts_provider[0] == '\0') {
        snprintf(ctx->cfg.tts_provider, sizeof(ctx->cfg.tts_provider),
                 "%s", "mimo");
    }
    if (ctx->cfg.tts_model[0] == '\0') {
        snprintf(ctx->cfg.tts_model, sizeof(ctx->cfg.tts_model),
                 "%s", "mimo-v2.5-tts");
    }
    if (ctx->cfg.tts_format[0] == '\0') {
        snprintf(ctx->cfg.tts_format, sizeof(ctx->cfg.tts_format),
                 "%s", "wav");
    }

    ctx->active_session_id = -1;
    ctx->state = ctx->cfg.enabled ? VOICE_ENDPOINT_IDLE :
                 VOICE_ENDPOINT_DISABLED;
    ctx->mq = claw_mq_create("voice_mq", sizeof(struct voice_endpoint_event),
                             VOICE_MQ_DEPTH);
    if (!ctx->mq) {
        return CLAW_ERR_NOMEM;
    }
    ctx->lock = claw_mutex_create("voice_lock");
    if (!ctx->lock) {
        claw_mq_delete(ctx->mq);
        ctx->mq = NULL;
        return CLAW_ERR_NOMEM;
    }
    return CLAW_OK;
}

static claw_err_t voice_svc_start(struct claw_service *svc)
{
    struct voice_service_ctx *ctx =
        container_of(svc, struct voice_service_ctx, base);

    ctx->worker = claw_thread_create("voice", voice_worker, ctx, 8192, 20);
    if (!ctx->worker) {
        return CLAW_ERR_NOMEM;
    }
    voice_set_state(ctx,
                    ctx->cfg.enabled ? VOICE_ENDPOINT_IDLE :
                    VOICE_ENDPOINT_DISABLED,
                    NULL);
    return CLAW_OK;
}

static void voice_svc_stop(struct claw_service *svc)
{
    struct voice_service_ctx *ctx =
        container_of(svc, struct voice_service_ctx, base);
    struct voice_endpoint_event event;

    voice_abort_turn(ctx);
    if (ctx->worker) {
        claw_thread_delete(ctx->worker);
        ctx->worker = NULL;
    }
    /* Drain any queued events so that owned audio buffers are freed. */
    if (ctx->mq) {
        while (claw_mq_recv(ctx->mq, &event, sizeof(event),
                            CLAW_NO_WAIT) == CLAW_OK) {
            if (event.data_owns && event.data_ptr) {
                claw_free(event.data_ptr);
            }
        }
        claw_mq_delete(ctx->mq);
        ctx->mq = NULL;
    }
    if (ctx->lock) {
        claw_mutex_delete(ctx->lock);
        ctx->lock = NULL;
    }
    ctx->state = VOICE_ENDPOINT_DISABLED;
}

int voice_service_init(void)
{
    return voice_svc_init(&s_voice.base) == CLAW_OK ? CLAW_OK : CLAW_ERROR;
}

int voice_service_start(void)
{
    return voice_svc_start(&s_voice.base) == CLAW_OK ? CLAW_OK : CLAW_ERROR;
}

void voice_service_stop(void)
{
    voice_svc_stop(&s_voice.base);
}

static const struct claw_service_ops voice_svc_ops = {
    .init = voice_svc_init,
    .start = voice_svc_start,
    .stop = voice_svc_stop,
};

static struct voice_service_ctx s_voice = {
    .base = {
        .name = "voice",
        .ops = &voice_svc_ops,
        .deps = voice_deps,
        .state = CLAW_SVC_CREATED,
    },
};

CLAW_SERVICE_REGISTER(voice, &s_voice.base);
