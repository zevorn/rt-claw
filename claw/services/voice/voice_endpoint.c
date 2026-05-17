#include "osal/claw_os.h"
#include "claw/services/voice/voice_endpoint.h"

#include <string.h>

#define TAG "voice_endpoint"

/* Hold the current transport backend behind one session binding. */
static struct {
    struct claw_mutex                *lock;
    int                               attached;
    int                               session_id;
    struct voice_endpoint_backend     backend;
} s_endpoint;

static claw_err_t endpoint_lock(void)
{
    if (!s_endpoint.lock) {
        return CLAW_ERR_STATE;
    }
    return claw_mutex_lock(s_endpoint.lock, CLAW_WAIT_FOREVER) == CLAW_OK ?
           CLAW_OK : CLAW_ERR_STATE;
}

static void endpoint_unlock(void)
{
    claw_mutex_unlock(s_endpoint.lock);
}

static claw_err_t endpoint_call_state(int session_id, int state,
                                      const char *detail)
{
    if (!s_endpoint.backend.send_state) {
        return CLAW_ERR_NOENT;
    }
    return s_endpoint.backend.send_state(session_id, state, detail);
}

/* Swap in the active endpoint backend for the voice service. */
claw_err_t voice_endpoint_attach(int session_id,
                                 const struct voice_endpoint_backend *backend)
{
    if (!backend) {
        return CLAW_ERR_INVALID;
    }
    if (!s_endpoint.lock) {
        s_endpoint.lock = claw_mutex_create("voice_ep");
        if (!s_endpoint.lock) {
            return CLAW_ERR_NOMEM;
        }
    }
    if (endpoint_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    s_endpoint.attached = 1;
    s_endpoint.session_id = session_id;
    memcpy(&s_endpoint.backend, backend, sizeof(*backend));
    endpoint_unlock();
    CLAW_LOGI(TAG, "attached session %d", session_id);
    return CLAW_OK;
}

void voice_endpoint_detach(int session_id)
{
    if (!s_endpoint.lock) {
        return;
    }
    if (endpoint_lock() != CLAW_OK) {
        return;
    }
    if (!s_endpoint.attached || s_endpoint.session_id != session_id) {
        endpoint_unlock();
        return;
    }
    memset(&s_endpoint.backend, 0, sizeof(s_endpoint.backend));
    s_endpoint.attached = 0;
    s_endpoint.session_id = -1;
    endpoint_unlock();
    CLAW_LOGI(TAG, "detached session %d", session_id);
}

int voice_endpoint_session_id(void)
{
    return s_endpoint.session_id;
}

int voice_endpoint_attached(void)
{
    return s_endpoint.attached;
}

claw_err_t voice_endpoint_send_state(int state, const char *detail)
{
    claw_err_t ret;

    if (endpoint_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    if (!s_endpoint.attached || !s_endpoint.backend.send_state) {
        endpoint_unlock();
        return CLAW_ERR_NOENT;
    }
    ret = endpoint_call_state(s_endpoint.session_id, state, detail);
    endpoint_unlock();
    return ret;
}

claw_err_t voice_endpoint_send_transcript(const char *text)
{
    claw_err_t ret;

    if (endpoint_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    if (!s_endpoint.attached || !s_endpoint.backend.send_transcript) {
        endpoint_unlock();
        return CLAW_ERR_NOENT;
    }
    ret = s_endpoint.backend.send_transcript(s_endpoint.session_id, text);
    endpoint_unlock();
    return ret;
}

claw_err_t voice_endpoint_send_assistant_text(const char *text)
{
    claw_err_t ret;

    if (endpoint_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    if (!s_endpoint.attached || !s_endpoint.backend.send_assistant_text) {
        endpoint_unlock();
        return CLAW_ERR_NOENT;
    }
    ret = s_endpoint.backend.send_assistant_text(s_endpoint.session_id, text);
    endpoint_unlock();
    return ret;
}

claw_err_t voice_endpoint_send_tts_audio(const void *data,
                                         size_t data_len,
                                         const char *mime_type)
{
    claw_err_t ret;

    if (endpoint_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    if (!s_endpoint.attached || !s_endpoint.backend.send_tts_audio) {
        endpoint_unlock();
        return CLAW_ERR_NOENT;
    }
    ret = s_endpoint.backend.send_tts_audio(s_endpoint.session_id,
                                            data, data_len, mime_type);
    endpoint_unlock();
    return ret;
}

claw_err_t voice_endpoint_send_tts_done(void)
{
    claw_err_t ret = CLAW_OK;

    if (endpoint_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    if (!s_endpoint.attached) {
        endpoint_unlock();
        return CLAW_ERR_NOENT;
    }
    if (s_endpoint.backend.send_tts_done) {
        ret = s_endpoint.backend.send_tts_done(s_endpoint.session_id);
    }
    endpoint_unlock();
    return ret;
}

claw_err_t voice_endpoint_send_error(const char *message)
{
    claw_err_t ret;

    if (endpoint_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    if (!s_endpoint.attached || !s_endpoint.backend.send_error) {
        endpoint_unlock();
        return CLAW_ERR_NOENT;
    }
    ret = s_endpoint.backend.send_error(s_endpoint.session_id, message);
    endpoint_unlock();
    return ret;
}
