/* SPDX-License-Identifier: MIT */

#include "osal/claw_os.h"
#include "platform/linux/local_voice_endpoint.h"
#include "claw/services/voice/voice_endpoint.h"
#include "claw/services/voice/voice_service.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TAG "local_voice"
#define LOCAL_VOICE_SESSION_ID 2
#define LOCAL_VOICE_DEVICE_MAX 96
#define LOCAL_VOICE_CHUNK_SIZE 4096
#define LOCAL_VOICE_WAV_PATH "/tmp/rtclaw-voice-tts.wav"

struct playback_job {
    char path[64];
    char device[LOCAL_VOICE_DEVICE_MAX];
};

struct local_voice_ctx {
    int running;
    int capturing;
    int capture_fd;
    pid_t capture_pid;
    struct claw_thread *capture_thread;
    struct claw_thread *playback_thread;
    struct claw_mutex *lock;
    char input_device[LOCAL_VOICE_DEVICE_MAX];
    char output_device[LOCAL_VOICE_DEVICE_MAX];
};

static struct local_voice_ctx s_local_voice = {
    .capture_fd = -1,
    .capture_pid = -1,
};

static int local_voice_lock(void)
{
    if (!s_local_voice.lock) {
        return CLAW_ERR_STATE;
    }
    return claw_mutex_lock(s_local_voice.lock, CLAW_WAIT_FOREVER) == CLAW_OK ?
           CLAW_OK : CLAW_ERR_STATE;
}

static void local_voice_unlock(void)
{
    claw_mutex_unlock(s_local_voice.lock);
}

static void local_voice_close_fd(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void local_voice_stop_pid(pid_t *pid)
{
    int status;

    if (*pid <= 0) {
        return;
    }
    kill(*pid, SIGTERM);
    if (waitpid(*pid, &status, 0) < 0 && errno != ECHILD) {
        CLAW_LOGW(TAG, "waitpid failed: %d", errno);
    }
    *pid = -1;
}

static int local_voice_submit_event(int type,
                                    const struct voice_audio_format *format,
                                    const void *data,
                                    size_t data_len)
{
    struct voice_endpoint_event event;

    memset(&event, 0, sizeof(event));
    event.session_id = LOCAL_VOICE_SESSION_ID;
    event.type = type;
    if (format) {
        event.format = *format;
    }
    if (data && data_len > 0) {
        event.data_ptr = (uint8_t *)claw_malloc(data_len);
        if (!event.data_ptr) {
            return CLAW_ERR_NOMEM;
        }
        memcpy(event.data_ptr, data, data_len);
        event.data_len = data_len;
        event.data_owns = 1;
    }
    return voice_submit_event(&event);
}

static int local_voice_spawn_capture(const char *device,
                                     int sample_rate,
                                     int channels,
                                     int *read_fd,
                                     pid_t *pid_out)
{
    int pipefd[2];
    pid_t pid;
    char rate_arg[16];
    char channels_arg[8];

    if (pipe(pipefd) != 0) {
        return CLAW_ERR_IO;
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return CLAW_ERR_IO;
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        snprintf(rate_arg, sizeof(rate_arg), "%d", sample_rate);
        snprintf(channels_arg, sizeof(channels_arg), "%d", channels);
        if (device && device[0]) {
            execlp("arecord", "arecord", "-q", "-D", device,
                   "-f", "S16_LE", "-r", rate_arg, "-c", channels_arg,
                   "-t", "raw", NULL);
        } else {
            execlp("arecord", "arecord", "-q", "-f", "S16_LE",
                   "-r", rate_arg, "-c", channels_arg, "-t", "raw", NULL);
        }
        _exit(127);
    }

    close(pipefd[1]);
    *read_fd = pipefd[0];
    *pid_out = pid;
    return CLAW_OK;
}

static void local_voice_capture_thread(void *arg)
{
    uint8_t buf[LOCAL_VOICE_CHUNK_SIZE];
    int fd;
    pid_t pid;

    (void)arg;
    while (!claw_thread_should_exit()) {
        ssize_t nread;

        if (local_voice_lock() != CLAW_OK) {
            break;
        }
        fd = s_local_voice.capture_fd;
        pid = s_local_voice.capture_pid;
        local_voice_unlock();
        if (fd < 0 || pid <= 0) {
            break;
        }

        nread = read(fd, buf, sizeof(buf));
        if (nread > 0) {
            if (local_voice_submit_event(VOICE_ENDPOINT_EVENT_AUDIO_CHUNK,
                                         NULL, buf, (size_t)nread) != CLAW_OK) {
                CLAW_LOGW(TAG, "failed to submit local audio chunk");
            }
            continue;
        }
        if (nread == 0 || errno != EINTR) {
            break;
        }
    }
}

static int local_voice_write_file(const char *path,
                                  const void *data,
                                  size_t data_len)
{
    FILE *fp;

    fp = fopen(path, "wb");
    if (!fp) {
        return CLAW_ERR_IO;
    }
    if (data_len > 0 && fwrite(data, 1, data_len, fp) != data_len) {
        fclose(fp);
        return CLAW_ERR_IO;
    }
    if (fclose(fp) != 0) {
        return CLAW_ERR_IO;
    }
    return CLAW_OK;
}

static int local_voice_play_file(const char *path, const char *device)
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid < 0) {
        return CLAW_ERR_IO;
    }
    if (pid == 0) {
        if (device && device[0]) {
            execlp("aplay", "aplay", "-q", "-D", device, path, NULL);
        } else {
            execlp("aplay", "aplay", "-q", path, NULL);
        }
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        return CLAW_ERR_IO;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return CLAW_ERR_IO;
    }
    return CLAW_OK;
}

static void local_voice_playback_thread(void *arg)
{
    struct playback_job *job = (struct playback_job *)arg;

    if (job) {
        if (local_voice_play_file(job->path, job->device) != CLAW_OK) {
            CLAW_LOGE(TAG, "tts playback failed");
        }
        unlink(job->path);
        claw_free(job);
    }
    (void)local_voice_submit_event(VOICE_ENDPOINT_EVENT_PLAYBACK_DONE,
                                   NULL, NULL, 0);
}

static void local_voice_cleanup_playback(void)
{
    struct claw_thread *thread = NULL;

    if (local_voice_lock() == CLAW_OK) {
        thread = s_local_voice.playback_thread;
        s_local_voice.playback_thread = NULL;
        local_voice_unlock();
    }
    if (thread) {
        claw_thread_delete(thread);
    }
}

static claw_err_t send_state(int session_id, int state, const char *detail)
{
    (void)session_id;
    if (state != VOICE_ENDPOINT_PLAYING) {
        local_voice_cleanup_playback();
    }
    CLAW_LOGI(TAG, "state=%s%s%s", voice_state_name(state),
              detail ? " detail=" : "", detail ? detail : "");
    return CLAW_OK;
}

static claw_err_t send_transcript(int session_id, const char *text)
{
    (void)session_id;
    CLAW_LOGI(TAG, "transcript: %s", text ? text : "");
    return CLAW_OK;
}

static claw_err_t send_assistant_text(int session_id, const char *text)
{
    (void)session_id;
    CLAW_LOGI(TAG, "assistant: %s", text ? text : "");
    return CLAW_OK;
}

static claw_err_t send_tts_audio(int session_id,
                                 const void *data,
                                 size_t data_len,
                                 const char *mime_type)
{
    struct playback_job *job;
    struct claw_thread *thread;

    (void)session_id;
    CLAW_LOGI(TAG, "playing tts audio: bytes=%u mime=%s",
              (unsigned int)data_len, mime_type ? mime_type : "unknown");
    job = (struct playback_job *)claw_calloc(1, sizeof(*job));
    if (!job) {
        return CLAW_ERR_NOMEM;
    }
    snprintf(job->path, sizeof(job->path), "%s", LOCAL_VOICE_WAV_PATH);
    if (local_voice_lock() != CLAW_OK) {
        claw_free(job);
        return CLAW_ERR_STATE;
    }
    snprintf(job->device, sizeof(job->device), "%s",
             s_local_voice.output_device);
    local_voice_unlock();

    if (local_voice_write_file(job->path, data, data_len) != CLAW_OK) {
        claw_free(job);
        return CLAW_ERR_IO;
    }
    thread = claw_thread_create("voice_play", local_voice_playback_thread,
                                job, 8192, 20);
    if (!thread) {
        unlink(job->path);
        claw_free(job);
        return CLAW_ERR_NOMEM;
    }
    if (local_voice_lock() != CLAW_OK) {
        claw_thread_delete(thread);
        return CLAW_ERR_STATE;
    }
    s_local_voice.playback_thread = thread;
    local_voice_unlock();
    return CLAW_OK;
}

static claw_err_t send_error(int session_id, const char *message)
{
    (void)session_id;
    CLAW_LOGE(TAG, "error: %s", message ? message : "unknown");
    return CLAW_OK;
}

int local_voice_endpoint_init(void)
{
    if (!s_local_voice.lock) {
        s_local_voice.lock = claw_mutex_create("local_voice");
        if (!s_local_voice.lock) {
            return CLAW_ERR_NOMEM;
        }
    }
    return CLAW_OK;
}

int local_voice_endpoint_start(void)
{
    static const struct voice_endpoint_backend backend = {
        .send_state = send_state,
        .send_transcript = send_transcript,
        .send_assistant_text = send_assistant_text,
        .send_tts_audio = send_tts_audio,
        .send_error = send_error,
    };
    int err;

    if (s_local_voice.running) {
        return CLAW_OK;
    }
    err = voice_endpoint_attach(LOCAL_VOICE_SESSION_ID, &backend);
    if (err != CLAW_OK) {
        return err;
    }
    err = local_voice_submit_event(VOICE_ENDPOINT_EVENT_ATTACH, NULL, NULL, 0);
    if (err != CLAW_OK) {
        voice_endpoint_detach(LOCAL_VOICE_SESSION_ID);
        return err;
    }
    s_local_voice.running = 1;
    CLAW_LOGI(TAG, "local voice endpoint started");
    return CLAW_OK;
}

void local_voice_endpoint_stop(void)
{
    if (!s_local_voice.running) {
        return;
    }
    (void)local_voice_endpoint_cancel();
    (void)local_voice_submit_event(VOICE_ENDPOINT_EVENT_DETACH, NULL, NULL, 0);
    voice_endpoint_detach(LOCAL_VOICE_SESSION_ID);
    s_local_voice.running = 0;
    CLAW_LOGI(TAG, "local voice endpoint stopped");
}

int local_voice_endpoint_running(void)
{
    return s_local_voice.running;
}

int local_voice_endpoint_capture_start(void)
{
    struct voice_audio_format format;
    char input_device[LOCAL_VOICE_DEVICE_MAX];
    int sample_rate;
    int channels;
    int fd = -1;
    pid_t pid = -1;
    int err;

    if (!s_local_voice.running) {
        err = local_voice_endpoint_start();
        if (err != CLAW_OK) {
            return err;
        }
    }
    if (s_local_voice.capturing) {
        return CLAW_ERR_BUSY;
    }

    sample_rate = voice_config_get()->input_sample_rate;
    channels = voice_config_get()->input_channels;
    if (sample_rate <= 0) {
        sample_rate = 16000;
    }
    if (channels <= 0) {
        channels = 1;
    }
    if (local_voice_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    snprintf(input_device, sizeof(input_device), "%s",
             s_local_voice.input_device);
    local_voice_unlock();

    err = local_voice_spawn_capture(input_device, sample_rate, channels,
                                    &fd, &pid);
    if (err != CLAW_OK) {
        return err;
    }

    memset(&format, 0, sizeof(format));
    format.sample_rate = sample_rate;
    format.channels = channels;
    format.bits_per_sample = 16;
    snprintf(format.encoding, sizeof(format.encoding), "%s", "pcm_s16le");
    err = local_voice_submit_event(VOICE_ENDPOINT_EVENT_START_CAPTURE,
                                   &format, NULL, 0);
    if (err != CLAW_OK) {
        close(fd);
        local_voice_stop_pid(&pid);
        return err;
    }

    if (local_voice_lock() != CLAW_OK) {
        close(fd);
        local_voice_stop_pid(&pid);
        return CLAW_ERR_STATE;
    }
    s_local_voice.capture_fd = fd;
    s_local_voice.capture_pid = pid;
    s_local_voice.capturing = 1;
    local_voice_unlock();

    s_local_voice.capture_thread = claw_thread_create("voice_cap",
                                                      local_voice_capture_thread,
                                                      NULL, 8192, 20);
    if (!s_local_voice.capture_thread) {
        (void)local_voice_endpoint_cancel();
        return CLAW_ERR_NOMEM;
    }
    CLAW_LOGI(TAG, "capture started: device=%s %dHz/%dch",
              input_device[0] ? input_device : "default", sample_rate, channels);
    return CLAW_OK;
}

int local_voice_endpoint_capture_stop(void)
{
    struct claw_thread *thread;
    pid_t pid;
    int fd;

    if (!s_local_voice.capturing) {
        return CLAW_ERR_STATE;
    }
    if (local_voice_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    thread = s_local_voice.capture_thread;
    pid = s_local_voice.capture_pid;
    fd = s_local_voice.capture_fd;
    s_local_voice.capture_thread = NULL;
    s_local_voice.capture_pid = -1;
    s_local_voice.capture_fd = -1;
    s_local_voice.capturing = 0;
    local_voice_unlock();

    local_voice_close_fd(&fd);
    local_voice_stop_pid(&pid);
    if (thread) {
        claw_thread_delete(thread);
    }
    CLAW_LOGI(TAG, "capture stopped");
    return local_voice_submit_event(VOICE_ENDPOINT_EVENT_END_CAPTURE,
                                    NULL, NULL, 0);
}

int local_voice_endpoint_cancel(void)
{
    struct claw_thread *thread = NULL;
    pid_t pid = -1;
    int fd = -1;

    if (local_voice_lock() == CLAW_OK) {
        thread = s_local_voice.capture_thread;
        pid = s_local_voice.capture_pid;
        fd = s_local_voice.capture_fd;
        s_local_voice.capture_thread = NULL;
        s_local_voice.capture_pid = -1;
        s_local_voice.capture_fd = -1;
        s_local_voice.capturing = 0;
        local_voice_unlock();
    }
    local_voice_close_fd(&fd);
    local_voice_stop_pid(&pid);
    if (thread) {
        claw_thread_delete(thread);
    }
    return local_voice_submit_event(VOICE_ENDPOINT_EVENT_CANCEL,
                                    NULL, NULL, 0);
}

int local_voice_endpoint_set_input(const char *device)
{
    if (!device) {
        return CLAW_ERR_INVALID;
    }
    if (!s_local_voice.lock && local_voice_endpoint_init() != CLAW_OK) {
        return CLAW_ERR_NOMEM;
    }
    if (local_voice_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    snprintf(s_local_voice.input_device, sizeof(s_local_voice.input_device),
             "%s", device);
    local_voice_unlock();
    return CLAW_OK;
}

int local_voice_endpoint_set_output(const char *device)
{
    if (!device) {
        return CLAW_ERR_INVALID;
    }
    if (!s_local_voice.lock && local_voice_endpoint_init() != CLAW_OK) {
        return CLAW_ERR_NOMEM;
    }
    if (local_voice_lock() != CLAW_OK) {
        return CLAW_ERR_STATE;
    }
    snprintf(s_local_voice.output_device, sizeof(s_local_voice.output_device),
             "%s", device);
    local_voice_unlock();
    return CLAW_OK;
}

const char *local_voice_endpoint_get_input(void)
{
    return s_local_voice.input_device;
}

const char *local_voice_endpoint_get_output(void)
{
    return s_local_voice.output_device;
}
