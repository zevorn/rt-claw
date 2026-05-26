#ifndef CLAW_SERVICES_VOICE_PROVIDERS_H
#define CLAW_SERVICES_VOICE_PROVIDERS_H

#include "osal/claw_os.h"
#include "claw/core/errno.h"
#include "claw/services/voice/voice_endpoint.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_URL_MAX      256
#define VOICE_KEY_MAX      128
#define VOICE_MODEL_MAX     64
#define VOICE_NAME_MAX      64
#define VOICE_APP_ID_MAX    64
#define VOICE_PROMPT_MAX   512
#define VOICE_FORMAT_MAX    32

typedef struct {
    int  enabled;
    int  web_port;
    int  stt_timeout_ms;
    int  input_sample_rate;
    int  input_channels;
    int  input_bits_per_sample;
    char endpoint_backend[VOICE_NAME_MAX];
    char stt_provider[VOICE_NAME_MAX];
    char stt_url[VOICE_URL_MAX];
    char stt_key[VOICE_KEY_MAX];
    char stt_model[VOICE_MODEL_MAX];
    char stt_xfyun_app_id[VOICE_APP_ID_MAX];
    char stt_xfyun_api_key[VOICE_KEY_MAX];
    char stt_xfyun_api_secret[VOICE_KEY_MAX];
    char input_encoding[VOICE_ENCODING_MAX];
    char tts_provider[VOICE_NAME_MAX];
    char tts_url[VOICE_URL_MAX];
    char tts_key[VOICE_KEY_MAX];
    char tts_model[VOICE_MODEL_MAX];
    char tts_voice[VOICE_NAME_MAX];
    char tts_style_prompt[VOICE_PROMPT_MAX];
    char tts_format[VOICE_FORMAT_MAX];
    int  tts_stream;
} voice_runtime_config_t;

struct voice_stt_session;

typedef claw_err_t (*voice_tts_audio_cb_t)(const void *data,
                                           size_t data_len,
                                           const char *mime_type,
                                           void *user);

claw_err_t voice_stt_session_start(struct voice_stt_session **session_out,
                                   const voice_runtime_config_t *cfg,
                                   const struct voice_audio_format *format);
claw_err_t voice_stt_session_send(struct voice_stt_session *session,
                                  const void *audio,
                                  size_t audio_len);
claw_err_t voice_stt_session_finish(struct voice_stt_session *session,
                                    char *text,
                                    size_t text_size);
void voice_stt_session_abort(struct voice_stt_session *session);
void voice_stt_session_destroy(struct voice_stt_session *session);

claw_err_t voice_tts_synthesize(const voice_runtime_config_t *cfg,
                                const char *text,
                                void *audio,
                                size_t audio_size,
                                size_t *audio_len,
                                const char **mime_type);
claw_err_t voice_tts_synthesize_stream(const voice_runtime_config_t *cfg,
                                       const char *text,
                                       voice_tts_audio_cb_t cb,
                                       void *user,
                                       const char **mime_type);

#ifdef __cplusplus
}
#endif

#endif /* CLAW_SERVICES_VOICE_PROVIDERS_H */
