#ifndef CLAW_SERVICES_VOICE_ENDPOINT_H
#define CLAW_SERVICES_VOICE_ENDPOINT_H

#include "osal/claw_os.h"
#include "claw/core/errno.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum voice_endpoint_state {
    VOICE_ENDPOINT_DISABLED = 0,
    VOICE_ENDPOINT_IDLE,
    VOICE_ENDPOINT_SESSION_READY,
    VOICE_ENDPOINT_CAPTURING,
    VOICE_ENDPOINT_TRANSCRIBING,
    VOICE_ENDPOINT_THINKING,
    VOICE_ENDPOINT_SYNTHESIZING,
    VOICE_ENDPOINT_PLAYING,
    VOICE_ENDPOINT_ERROR,
};

enum voice_endpoint_event_type {
    VOICE_ENDPOINT_EVENT_ATTACH = 0,
    VOICE_ENDPOINT_EVENT_DETACH,
    VOICE_ENDPOINT_EVENT_START_CAPTURE,
    VOICE_ENDPOINT_EVENT_AUDIO_CHUNK,
    VOICE_ENDPOINT_EVENT_END_CAPTURE,
    VOICE_ENDPOINT_EVENT_CANCEL,
    VOICE_ENDPOINT_EVENT_PLAYBACK_DONE,
};

#define VOICE_ENCODING_MAX 32
/* Maximum bytes accepted per AUDIO_CHUNK event. */
#define VOICE_ENDPOINT_EVENT_DATA_MAX 8192

struct voice_audio_format {
    int  sample_rate;
    int  channels;
    int  bits_per_sample;
    char encoding[VOICE_ENCODING_MAX];
};

struct voice_endpoint_event {
    int                       session_id;
    int                       type;
    struct voice_audio_format format;
    uint8_t                  *data_ptr;
    size_t                    data_len;
    int                       data_owns;
};

/* Callback pointers are borrow-only; async backends must copy before return. */
struct voice_endpoint_backend {
    claw_err_t (*send_state)(int session_id, int state, const char *detail);
    claw_err_t (*send_transcript)(int session_id, const char *text);
    claw_err_t (*send_assistant_text)(int session_id, const char *text);
    claw_err_t (*send_tts_audio)(int session_id,
                                 const void *data,
                                 size_t data_len,
                                 const char *mime_type);
    claw_err_t (*send_error)(int session_id, const char *message);
};

claw_err_t voice_endpoint_attach(int session_id,
                                 const struct voice_endpoint_backend *backend);
void voice_endpoint_detach(int session_id);
int voice_endpoint_session_id(void);
int voice_endpoint_attached(void);
claw_err_t voice_endpoint_send_state(int state, const char *detail);
claw_err_t voice_endpoint_send_transcript(const char *text);
claw_err_t voice_endpoint_send_assistant_text(const char *text);
claw_err_t voice_endpoint_send_tts_audio(const void *data,
                                         size_t data_len,
                                         const char *mime_type);
claw_err_t voice_endpoint_send_error(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* CLAW_SERVICES_VOICE_ENDPOINT_H */
