/* SPDX-License-Identifier: MIT */

#include "osal/claw_os.h"
#include "platform/linux/web_voice_server.h"
#include "claw/services/voice/voice_endpoint.h"
#include "claw/services/voice/voice_service.h"
#include "cJSON.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define TAG "web_voice"
#define WEB_VOICE_SESSION_ID 1
#define HTTP_BUF_SIZE 16384
#define HTTP_BODY_MAX 12288

struct http_request {
    char method[8];
    char path[128];
    char content_type[64];
    size_t header_len;
    size_t body_len;
    uint8_t buf[HTTP_BUF_SIZE];
};

struct web_voice_ctx {
    int running;
    int listen_fd;
    int event_fd;
    struct claw_thread *thread;
    struct claw_mutex *lock;
};

static struct web_voice_ctx s_web_voice = {
    .listen_fd = -1,
    .event_fd = -1,
};

static void web_voice_close_fd(int *fd)
{
    if (*fd >= 0) {
        shutdown(*fd, SHUT_RDWR);
        close(*fd);
        *fd = -1;
    }
}

static claw_err_t web_voice_lock(void)
{
    if (!s_web_voice.lock) {
        return CLAW_ERR_STATE;
    }
    return claw_mutex_lock(s_web_voice.lock, CLAW_WAIT_FOREVER) == CLAW_OK ?
           CLAW_OK : CLAW_ERR_STATE;
}

static void web_voice_unlock(void)
{
    claw_mutex_unlock(s_web_voice.lock);
}

static int web_voice_send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)buf;

    while (len > 0) {
        ssize_t sent = send(fd, ptr, len, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            return -1;
        }
        ptr += (size_t)sent;
        len -= (size_t)sent;
    }
    return 0;
}

static int web_voice_send_response(int fd,
                                   const char *status,
                                   const char *content_type,
                                   const void *body,
                                   size_t body_len)
{
    char header[256];
    int header_len;

    header_len = snprintf(header, sizeof(header),
                          "HTTP/1.1 %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %u\r\n"
                          "Cache-Control: no-store\r\n"
                          "Connection: close\r\n"
                          "\r\n",
                          status,
                          content_type,
                          (unsigned)body_len);
    if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
        return -1;
    }
    if (web_voice_send_all(fd, header, (size_t)header_len) != 0) {
        return -1;
    }
    if (body && body_len > 0) {
        if (web_voice_send_all(fd, body, body_len) != 0) {
            return -1;
        }
    }
    return 0;
}

static int web_voice_send_text(int fd,
                               const char *status,
                               const char *content_type,
                               const char *body)
{
    size_t body_len = body ? strlen(body) : 0;

    return web_voice_send_response(fd, status, content_type, body, body_len);
}

static int web_voice_send_file(int fd, const char *path)
{
    FILE *fp;
    char *buf;
    long file_len;
    int ret = -1;

    fp = fopen(path, "rb");
    if (!fp) {
        return web_voice_send_text(fd, "404 Not Found",
                                   "text/plain; charset=utf-8",
                                   "voice page not found\n");
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return web_voice_send_text(fd, "500 Internal Server Error",
                                   "text/plain; charset=utf-8",
                                   "seek failed\n");
    }
    file_len = ftell(fp);
    if (file_len < 0) {
        fclose(fp);
        return web_voice_send_text(fd, "500 Internal Server Error",
                                   "text/plain; charset=utf-8",
                                   "tell failed\n");
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return web_voice_send_text(fd, "500 Internal Server Error",
                                   "text/plain; charset=utf-8",
                                   "rewind failed\n");
    }

    buf = (char *)malloc((size_t)file_len);
    if (!buf) {
        fclose(fp);
        return web_voice_send_text(fd, "500 Internal Server Error",
                                   "text/plain; charset=utf-8",
                                   "oom\n");
    }
    if (file_len > 0 && fread(buf, 1, (size_t)file_len, fp) != (size_t)file_len) {
        goto out;
    }
    ret = web_voice_send_response(fd, "200 OK", "text/html; charset=utf-8",
                                  buf, (size_t)file_len);
out:
    free(buf);
    fclose(fp);
    return ret;
}

static const char *web_voice_base64_table(void)
{
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

static char *web_voice_base64_encode(const uint8_t *data, size_t len)
{
    const char *table = web_voice_base64_table();
    size_t out_len = ((len + 2) / 3) * 4;
    char *out = (char *)malloc(out_len + 1);
    size_t i;
    size_t j = 0;

    if (!out) {
        return NULL;
    }
    for (i = 0; i < len; i += 3) {
        uint32_t chunk = (uint32_t)data[i] << 16;
        size_t remain = len - i;

        if (remain > 1) {
            chunk |= (uint32_t)data[i + 1] << 8;
        }
        if (remain > 2) {
            chunk |= data[i + 2];
        }

        out[j++] = table[(chunk >> 18) & 0x3f];
        out[j++] = table[(chunk >> 12) & 0x3f];
        out[j++] = remain > 1 ? table[(chunk >> 6) & 0x3f] : '=';
        out[j++] = remain > 2 ? table[chunk & 0x3f] : '=';
    }
    out[j] = '\0';
    return out;
}

static int web_voice_send_sse_json(cJSON *root)
{
    char *json;
    char *msg;
    int fd = -1;
    int detach = 0;
    size_t msg_len;
    int ret = -1;

    json = cJSON_PrintUnformatted(root);
    if (!json) {
        return -1;
    }
    msg_len = strlen(json) + strlen("data: \n\n");
    msg = (char *)malloc(msg_len + 1);
    if (!msg) {
        cJSON_free(json);
        return -1;
    }
    snprintf(msg, msg_len + 1, "data: %s\n\n", json);
    cJSON_free(json);

    if (web_voice_lock() != CLAW_OK) {
        free(msg);
        return -1;
    }
    fd = s_web_voice.event_fd;
    if (fd >= 0 && web_voice_send_all(fd, msg, strlen(msg)) != 0) {
        CLAW_LOGW(TAG, "event stream disconnected");
        web_voice_close_fd(&s_web_voice.event_fd);
        detach = 1;
    } else if (fd >= 0) {
        ret = 0;
    }
    web_voice_unlock();

    free(msg);

    if (detach) {
        struct voice_endpoint_event event = {
            .session_id = WEB_VOICE_SESSION_ID,
            .type = VOICE_ENDPOINT_EVENT_DETACH,
            .data_len = 0,
        };
        (void)voice_submit_event(&event);
    }
    return ret;
}

static claw_err_t send_state(int session_id, int state, const char *detail)
{
    cJSON *root = cJSON_CreateObject();
    claw_err_t ret;

    if (!root) {
        return CLAW_ERR_NOMEM;
    }
    cJSON_AddStringToObject(root, "type", "state");
    cJSON_AddNumberToObject(root, "session_id", session_id);
    cJSON_AddStringToObject(root, "state", voice_state_name(state));
    if (detail) {
        cJSON_AddStringToObject(root, "detail", detail);
    }
    ret = web_voice_send_sse_json(root) == 0 ? CLAW_OK : CLAW_ERR_NOENT;
    cJSON_Delete(root);
    return ret;
}

static claw_err_t send_transcript(int session_id, const char *text)
{
    cJSON *root = cJSON_CreateObject();
    claw_err_t ret;

    if (!root) {
        return CLAW_ERR_NOMEM;
    }
    cJSON_AddStringToObject(root, "type", "transcript");
    cJSON_AddNumberToObject(root, "session_id", session_id);
    cJSON_AddStringToObject(root, "text", text ? text : "");
    ret = web_voice_send_sse_json(root) == 0 ? CLAW_OK : CLAW_ERR_NOENT;
    cJSON_Delete(root);
    return ret;
}

static claw_err_t send_assistant_text(int session_id, const char *text)
{
    cJSON *root = cJSON_CreateObject();
    claw_err_t ret;

    if (!root) {
        return CLAW_ERR_NOMEM;
    }
    cJSON_AddStringToObject(root, "type", "assistant_text");
    cJSON_AddNumberToObject(root, "session_id", session_id);
    cJSON_AddStringToObject(root, "text", text ? text : "");
    ret = web_voice_send_sse_json(root) == 0 ? CLAW_OK : CLAW_ERR_NOENT;
    cJSON_Delete(root);
    return ret;
}

static claw_err_t send_tts_audio(int session_id,
                                 const void *data,
                                 size_t data_len,
                                 const char *mime_type)
{
    cJSON *root = cJSON_CreateObject();
    char *audio_base64 = NULL;
    claw_err_t ret;

    if (!root) {
        return CLAW_ERR_NOMEM;
    }
    if (data && data_len > 0) {
        audio_base64 = web_voice_base64_encode((const uint8_t *)data, data_len);
        if (!audio_base64) {
            cJSON_Delete(root);
            return CLAW_ERR_NOMEM;
        }
    }

    cJSON_AddStringToObject(root, "type", "tts_audio");
    cJSON_AddNumberToObject(root, "session_id", session_id);
    cJSON_AddStringToObject(root, "mime_type", mime_type ? mime_type : "");
    cJSON_AddNumberToObject(root, "size", (double)data_len);
    if (audio_base64) {
        cJSON_AddStringToObject(root, "audio_base64", audio_base64);
    }
    ret = web_voice_send_sse_json(root) == 0 ? CLAW_OK : CLAW_ERR_NOENT;
    free(audio_base64);
    cJSON_Delete(root);
    return ret;
}

static claw_err_t send_tts_done(int session_id)
{
    cJSON *root = cJSON_CreateObject();
    claw_err_t ret;

    if (!root) {
        return CLAW_ERR_NOMEM;
    }
    cJSON_AddStringToObject(root, "type", "tts_done");
    cJSON_AddNumberToObject(root, "session_id", session_id);
    ret = web_voice_send_sse_json(root) == 0 ? CLAW_OK : CLAW_ERR_NOENT;
    cJSON_Delete(root);
    return ret;
}

static claw_err_t send_error(int session_id, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    claw_err_t ret;

    if (!root) {
        return CLAW_ERR_NOMEM;
    }
    cJSON_AddStringToObject(root, "type", "error");
    cJSON_AddNumberToObject(root, "session_id", session_id);
    cJSON_AddStringToObject(root, "message", message ? message : "");
    ret = web_voice_send_sse_json(root) == 0 ? CLAW_OK : CLAW_ERR_NOENT;
    cJSON_Delete(root);
    return ret;
}

static int web_voice_parse_content_length(const uint8_t *req,
                                          size_t *content_length)
{
    const char *hdr = strstr((const char *)req, "\r\nContent-Length:");
    char *end = NULL;
    unsigned long parsed;

    if (!content_length) {
        return -1;
    }
    *content_length = 0;
    if (!hdr) {
        hdr = strstr((const char *)req, "\r\ncontent-length:");
    }
    if (!hdr) {
        return 0;
    }
    hdr = strchr(hdr, ':');
    if (!hdr) {
        return -1;
    }
    errno = 0;
    parsed = strtoul(hdr + 1, &end, 10);
    if (errno != 0 || end == hdr + 1 || parsed > HTTP_BODY_MAX) {
        return -1;
    }
    *content_length = (size_t)parsed;
    return 0;
}

static void web_voice_parse_content_type(struct http_request *req)
{
    const char *hdr = strstr((const char *)req->buf, "\r\nContent-Type:");
    const char *start;
    size_t len = 0;

    req->content_type[0] = '\0';
    if (!hdr) {
        hdr = strstr((const char *)req->buf, "\r\ncontent-type:");
    }
    if (!hdr) {
        return;
    }
    start = strchr(hdr, ':');
    if (!start) {
        return;
    }
    start += 1;
    while (*start == ' ') {
        start++;
    }
    while (start[len] && start[len] != '\r' && len + 1 < sizeof(req->content_type)) {
        req->content_type[len] = start[len];
        len++;
    }
    req->content_type[len] = '\0';
}

static int web_voice_read_request(int fd, struct http_request *req)
{
    size_t used = 0;
    size_t content_length = 0;
    uint8_t *hdr_end;

    memset(req, 0, sizeof(*req));

    while (used + 1 < sizeof(req->buf)) {
        ssize_t rc = recv(fd, req->buf + used, sizeof(req->buf) - used - 1, 0);

        if (rc <= 0) {
            return -1;
        }
        used += (size_t)rc;
        req->buf[used] = '\0';

        hdr_end = (uint8_t *)strstr((const char *)req->buf, "\r\n\r\n");
        if (!hdr_end) {
            continue;
        }
        req->header_len = (size_t)(hdr_end - req->buf) + 4;
        if (web_voice_parse_content_length(req->buf, &content_length) != 0) {
            return -1;
        }
        if (content_length > sizeof(req->buf) - req->header_len) {
            return -1;
        }
        if (content_length > used - req->header_len) {
            continue;
        }
        req->body_len = content_length;
        if (sscanf((const char *)req->buf, "%7s %127s", req->method, req->path) != 2) {
            return -1;
        }
        web_voice_parse_content_type(req);
        return 0;
    }
    return -1;
}

static int web_voice_submit_event_with_data(int type,
                                            const struct voice_audio_format *format,
                                            const void *data,
                                            size_t data_len)
{
    struct voice_endpoint_event event;

    memset(&event, 0, sizeof(event));
    event.session_id = WEB_VOICE_SESSION_ID;
    event.type       = type;
    event.data_ptr   = NULL;
    event.data_len   = 0;
    event.data_owns  = 0;

    if (format) {
        event.format = *format;
    }

    if (data && data_len > 0) {
        if (type == VOICE_ENDPOINT_EVENT_AUDIO_CHUNK) {
            /* Transfer ownership of a heap copy to voice_service. */
            event.data_ptr = (uint8_t *)claw_malloc(data_len);
            if (!event.data_ptr) {
                return -1;
            }
            memcpy(event.data_ptr, data, data_len);
            event.data_len  = data_len;
            event.data_owns = 1;
        } else {
            /* Control events carry no payload in the new model. */
            (void)data;
            (void)data_len;
        }
    }

    return voice_submit_event(&event) == CLAW_OK ? 0 : -1;
}

static int web_voice_submit_named_event(const char *type,
                                        const struct voice_audio_format *format,
                                        const char *chunk_text)
{
    if (strcmp(type, "attach") == 0) {
        return web_voice_submit_event_with_data(VOICE_ENDPOINT_EVENT_ATTACH,
                                                NULL, NULL, 0);
    }
    if (strcmp(type, "detach") == 0) {
        return web_voice_submit_event_with_data(VOICE_ENDPOINT_EVENT_DETACH,
                                                NULL, NULL, 0);
    }
    if (strcmp(type, "start_capture") == 0) {
        return web_voice_submit_event_with_data(
            VOICE_ENDPOINT_EVENT_START_CAPTURE,
            format, NULL, 0);
    }
    if (strcmp(type, "audio_chunk_text") == 0) {
        if (!chunk_text) {
            return -1;
        }
        return web_voice_submit_event_with_data(VOICE_ENDPOINT_EVENT_AUDIO_CHUNK,
                                                NULL, chunk_text,
                                                strlen(chunk_text));
    }
    if (strcmp(type, "end_capture") == 0) {
        return web_voice_submit_event_with_data(VOICE_ENDPOINT_EVENT_END_CAPTURE,
                                                NULL, NULL, 0);
    }
    if (strcmp(type, "cancel") == 0) {
        return web_voice_submit_event_with_data(VOICE_ENDPOINT_EVENT_CANCEL,
                                                NULL, NULL, 0);
    }
    if (strcmp(type, "playback_done") == 0) {
        return web_voice_submit_event_with_data(VOICE_ENDPOINT_EVENT_PLAYBACK_DONE,
                                                NULL, NULL, 0);
    }
    return -1;
}

static int web_voice_handle_json_event_post(int fd,
                                            const uint8_t *body,
                                            size_t body_len)
{
    cJSON *root;
    cJSON *type_item;
    cJSON *chunk_item;
    cJSON *sample_rate_item;
    cJSON *channels_item;
    cJSON *bits_item;
    cJSON *encoding_item;
    char *body_text;
    const char *chunk_text = NULL;
    struct voice_audio_format format;
    int ret;

    memset(&format, 0, sizeof(format));
    body_text = (char *)malloc(body_len + 1);
    if (!body_text) {
        return web_voice_send_text(fd, "500 Internal Server Error",
                                   "text/plain; charset=utf-8",
                                   "oom\n");
    }
    memcpy(body_text, body, body_len);
    body_text[body_len] = '\0';

    root = cJSON_Parse(body_text);
    free(body_text);
    if (!root) {
        return web_voice_send_text(fd, "400 Bad Request",
                                   "text/plain; charset=utf-8",
                                   "invalid json\n");
    }
    type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    chunk_item = cJSON_GetObjectItemCaseSensitive(root, "chunk_text");
    sample_rate_item = cJSON_GetObjectItemCaseSensitive(root, "sample_rate");
    channels_item = cJSON_GetObjectItemCaseSensitive(root, "channels");
    bits_item = cJSON_GetObjectItemCaseSensitive(root, "bits_per_sample");
    encoding_item = cJSON_GetObjectItemCaseSensitive(root, "encoding");
    if (!cJSON_IsString(type_item) || !type_item->valuestring) {
        cJSON_Delete(root);
        return web_voice_send_text(fd, "400 Bad Request",
                                   "text/plain; charset=utf-8",
                                   "missing type\n");
    }
    if (cJSON_IsString(chunk_item) && chunk_item->valuestring) {
        chunk_text = chunk_item->valuestring;
    }
    if (cJSON_IsNumber(sample_rate_item)) {
        format.sample_rate = sample_rate_item->valueint;
    }
    if (cJSON_IsNumber(channels_item)) {
        format.channels = channels_item->valueint;
    }
    if (cJSON_IsNumber(bits_item)) {
        format.bits_per_sample = bits_item->valueint;
    }
    if (cJSON_IsString(encoding_item) && encoding_item->valuestring) {
        snprintf(format.encoding, sizeof(format.encoding), "%s",
                 encoding_item->valuestring);
    }
    ret = web_voice_submit_named_event(type_item->valuestring,
                                       &format, chunk_text);
    cJSON_Delete(root);
    if (ret != 0) {
        return web_voice_send_text(fd, "400 Bad Request",
                                   "text/plain; charset=utf-8",
                                   "invalid voice event\n");
    }
    return web_voice_send_text(fd, "200 OK", "application/json",
                               "{\"ok\":true}\n");
}

static int web_voice_handle_audio_post(int fd,
                                       const uint8_t *body,
                                       size_t body_len)
{
    if (body_len == 0 || body_len > VOICE_ENDPOINT_EVENT_DATA_MAX) {
        return web_voice_send_text(fd, "413 Payload Too Large",
                                   "text/plain; charset=utf-8",
                                   "audio chunk too large\n");
    }
    if (web_voice_submit_event_with_data(VOICE_ENDPOINT_EVENT_AUDIO_CHUNK,
                                         NULL, body, body_len) != 0) {
        return web_voice_send_text(fd, "500 Internal Server Error",
                                   "text/plain; charset=utf-8",
                                   "audio queue failed\n");
    }
    return web_voice_send_text(fd, "200 OK", "application/json",
                               "{\"ok\":true}\n");
}

static int web_voice_open_event_stream(int fd)
{
    static const char header[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    int old_fd = -1;
    cJSON *root;

    if (web_voice_send_all(fd, header, sizeof(header) - 1) != 0) {
        return -1;
    }
    if (web_voice_lock() != CLAW_OK) {
        return -1;
    }
    old_fd = s_web_voice.event_fd;
    s_web_voice.event_fd = fd;
    web_voice_unlock();

    if (old_fd >= 0) {
        web_voice_close_fd(&old_fd);
        (void)web_voice_submit_named_event("detach", NULL, NULL);
    }
    (void)web_voice_submit_named_event("attach", NULL, NULL);

    root = cJSON_CreateObject();
    if (!root) {
        /*
         * Failed after event_fd was already swapped in.
         * Reset event_fd so no stale descriptor lingers in global state.
         */
        if (web_voice_lock() == CLAW_OK) {
            if (s_web_voice.event_fd == fd) {
                s_web_voice.event_fd = -1;
            }
            web_voice_unlock();
        }
        return -1;
    }
    cJSON_AddStringToObject(root, "type", "ready");
    cJSON_AddNumberToObject(root, "session_id", WEB_VOICE_SESSION_ID);
    cJSON_AddNumberToObject(root, "port", voice_config_get_web_port());
    if (web_voice_send_sse_json(root) != 0) {
        cJSON_Delete(root);
        if (web_voice_lock() == CLAW_OK) {
            if (s_web_voice.event_fd == fd) {
                s_web_voice.event_fd = -1;
            }
            web_voice_unlock();
        }
        return -1;
    }
    cJSON_Delete(root);
    return 0;
}

static void web_voice_handle_client(int fd)
{
    struct http_request req;
    const uint8_t *body;

    if (web_voice_read_request(fd, &req) != 0) {
        (void)web_voice_send_text(fd, "400 Bad Request",
                                  "text/plain; charset=utf-8",
                                  "bad request\n");
        web_voice_close_fd(&fd);
        return;
    }
    body = req.buf + req.header_len;

    if (strcmp(req.method, "GET") == 0 &&
        (strcmp(req.path, "/") == 0 || strcmp(req.path, "/voice.html") == 0)) {
        (void)web_voice_send_file(fd, "website/voice.html");
        web_voice_close_fd(&fd);
        return;
    }
    if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/voice/events") == 0) {
        if (web_voice_open_event_stream(fd) != 0) {
            web_voice_close_fd(&fd);
        }
        return;
    }
    if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/voice/event") == 0) {
        (void)web_voice_handle_json_event_post(fd, body, req.body_len);
        web_voice_close_fd(&fd);
        return;
    }
    if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/voice/audio") == 0) {
        (void)web_voice_handle_audio_post(fd, body, req.body_len);
        web_voice_close_fd(&fd);
        return;
    }

    (void)web_voice_send_text(fd, "404 Not Found", "text/plain; charset=utf-8",
                              "not found\n");
    web_voice_close_fd(&fd);
}

static void web_voice_server_thread(void *arg)
{
    (void)arg;

    while (!claw_thread_should_exit()) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        int client_fd;

        client_fd = accept(s_web_voice.listen_fd,
                           (struct sockaddr *)&addr, &addr_len);
        if (client_fd < 0) {
            if (!s_web_voice.running) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            CLAW_LOGW(TAG, "accept failed: %d", errno);
            continue;
        }
        web_voice_handle_client(client_fd);
    }
}

int web_voice_server_init(void)
{
    if (!s_web_voice.lock) {
        s_web_voice.lock = claw_mutex_create("web_voice");
        if (!s_web_voice.lock) {
            return CLAW_ERR_NOMEM;
        }
    }
    return CLAW_OK;
}

int web_voice_server_start(void)
{
    static const struct voice_endpoint_backend backend = {
        .send_state = send_state,
        .send_transcript = send_transcript,
        .send_assistant_text = send_assistant_text,
        .send_tts_audio = send_tts_audio,
        .send_tts_done = send_tts_done,
        .send_error = send_error,
    };
    struct sockaddr_in addr;
    int reuse = 1;
    claw_err_t err;

    if (s_web_voice.running) {
        return CLAW_OK;
    }

    err = voice_endpoint_attach(WEB_VOICE_SESSION_ID, &backend);
    if (err != CLAW_OK) {
        return err;
    }

    s_web_voice.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_web_voice.listen_fd < 0) {
        voice_endpoint_detach(WEB_VOICE_SESSION_ID);
        return CLAW_ERROR;
    }
    (void)setsockopt(s_web_voice.listen_fd, SOL_SOCKET, SO_REUSEADDR,
                     &reuse, sizeof(reuse));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)voice_config_get_web_port());

    if (bind(s_web_voice.listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        CLAW_LOGE(TAG, "bind failed: %d", errno);
        web_voice_close_fd(&s_web_voice.listen_fd);
        voice_endpoint_detach(WEB_VOICE_SESSION_ID);
        return CLAW_ERROR;
    }
    if (listen(s_web_voice.listen_fd, 4) != 0) {
        CLAW_LOGE(TAG, "listen failed: %d", errno);
        web_voice_close_fd(&s_web_voice.listen_fd);
        voice_endpoint_detach(WEB_VOICE_SESSION_ID);
        return CLAW_ERROR;
    }

    s_web_voice.thread = claw_thread_create("voice_web",
                                            web_voice_server_thread,
                                            NULL, 8192, 20);
    if (!s_web_voice.thread) {
        web_voice_close_fd(&s_web_voice.listen_fd);
        voice_endpoint_detach(WEB_VOICE_SESSION_ID);
        return CLAW_ERR_NOMEM;
    }
    s_web_voice.running = 1;
    CLAW_LOGI(TAG, "voice web server listening on http://0.0.0.0:%d/",
              voice_config_get_web_port());
    return CLAW_OK;
}

void web_voice_server_stop(void)
{
    if (!s_web_voice.running) {
        return;
    }

    s_web_voice.running = 0;
    web_voice_close_fd(&s_web_voice.listen_fd);
    if (web_voice_lock() == CLAW_OK) {
        web_voice_close_fd(&s_web_voice.event_fd);
        web_voice_unlock();
    }
    if (s_web_voice.thread) {
        claw_thread_delete(s_web_voice.thread);
        s_web_voice.thread = NULL;
    }
    voice_endpoint_detach(WEB_VOICE_SESSION_ID);
}

int web_voice_server_running(void)
{
    return s_web_voice.running;
}
