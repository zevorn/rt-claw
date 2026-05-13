#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/services/voice/voice_providers.h"
#include "cJSON.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef CLAW_PLATFORM_LINUX
#include <curl/curl.h>
#include <curl/websockets.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#elif defined(CLAW_PLATFORM_ESP_IDF)
#include "esp_websocket_client.h"
#include <mbedtls/md.h>
#endif

#define TAG "voice_stt"
#define VOICE_STT_RESP_SIZE       CONFIG_RTCLAW_VOICE_STT_RESP_SIZE
#define VOICE_STT_CHUNK_MS        40U
#define VOICE_STT_CHUNK_AUDIO_MAX 4096U
#define VOICE_STT_XFYUN_HOST      "iat-api.xfyun.cn"
#define VOICE_STT_XFYUN_PATH      "/v2/iat"
#define VOICE_STT_XFYUN_URL       "wss://iat-api.xfyun.cn/v2/iat"

enum voice_stt_provider_type {
    VOICE_STT_PROVIDER_XFYUN = 0,
};

struct voice_stt_session {
    enum voice_stt_provider_type provider;
    voice_runtime_config_t       cfg;
    struct voice_audio_format    format;
    char                         transcript[CONFIG_RTCLAW_VOICE_TEXT_BUF_SIZE];
    size_t                       transcript_len;
    int                          started;
    int                          finished;
#ifdef CLAW_PLATFORM_LINUX
    CURL                        *ws_curl;
#elif defined(CLAW_PLATFORM_ESP_IDF)
    esp_websocket_client_handle_t ws_client;
    struct claw_sem             *connect_sem;
    struct claw_sem             *done_sem;
    char                         rx_buf[VOICE_STT_RESP_SIZE];
    size_t                       rx_len;
    claw_err_t                   connect_result;
    claw_err_t                   final_result;
    int                          connect_pending;
#endif
};

static claw_err_t
voice_stt_parse_provider(const voice_runtime_config_t *cfg,
                         enum voice_stt_provider_type *provider)
{
    if (!cfg || !provider) {
        return CLAW_ERR_INVALID;
    }
    if (cfg->stt_provider[0] == '\0' ||
        strcmp(cfg->stt_provider, "xfyun") == 0) {
        *provider = VOICE_STT_PROVIDER_XFYUN;
        return CLAW_OK;
    }
    return CLAW_ERR_INVALID;
}

static claw_err_t voice_stt_rfc1123_now(char *buf, size_t buf_size)
{
    time_t now;
    struct tm tm_now;

    if (!buf || buf_size < 32) {
        return CLAW_ERR_INVALID;
    }
    time(&now);
    if (!gmtime_r(&now, &tm_now)) {
        return CLAW_ERR_GENERIC;
    }
    if (strftime(buf, buf_size, "%a, %d %b %Y %H:%M:%S GMT", &tm_now) == 0) {
        return CLAW_ERR_GENERIC;
    }
    return CLAW_OK;
}

static int voice_stt_url_is_unreserved(char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

static claw_err_t voice_stt_url_encode(const char *src,
                                       char *dst,
                                       size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    size_t j = 0;

    if (!src || !dst || dst_size == 0) {
        return CLAW_ERR_INVALID;
    }

    for (i = 0; src[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)src[i];

        if (voice_stt_url_is_unreserved((char)ch)) {
            if (j + 1 >= dst_size) {
                return CLAW_ERR_NOMEM;
            }
            dst[j++] = (char)ch;
            continue;
        }
        if (j + 3 >= dst_size) {
            return CLAW_ERR_NOMEM;
        }
        dst[j++] = '%';
        dst[j++] = hex[(ch >> 4) & 0x0f];
        dst[j++] = hex[ch & 0x0f];
    }
    dst[j] = '\0';
    return CLAW_OK;
}

static claw_err_t voice_stt_base64_encode(const uint8_t *data,
                                          size_t data_len,
                                          char *out,
                                          size_t out_size)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len;
    size_t i;
    size_t j = 0;

    if (!data || !out || out_size == 0) {
        return CLAW_ERR_INVALID;
    }

    out_len = ((data_len + 2U) / 3U) * 4U;
    if (out_len + 1U > out_size) {
        return CLAW_ERR_NOMEM;
    }

    for (i = 0; i < data_len; i += 3U) {
        uint32_t chunk = (uint32_t)data[i] << 16;
        size_t remain = data_len - i;

        if (remain > 1U) {
            chunk |= (uint32_t)data[i + 1U] << 8;
        }
        if (remain > 2U) {
            chunk |= data[i + 2U];
        }

        out[j++] = table[(chunk >> 18) & 0x3f];
        out[j++] = table[(chunk >> 12) & 0x3f];
        out[j++] = remain > 1U ? table[(chunk >> 6) & 0x3f] : '=';
        out[j++] = remain > 2U ? table[chunk & 0x3f] : '=';
    }
    out[j] = '\0';
    return CLAW_OK;
}

#if defined(CLAW_PLATFORM_LINUX)
static claw_err_t voice_stt_hmac_sha256_base64(const char *key,
                                               const char *text,
                                               char *out,
                                               size_t out_size)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    if (!key || !text || !out || out_size == 0) {
        return CLAW_ERR_INVALID;
    }
    if (!HMAC(EVP_sha256(), key, (int)strlen(key),
              (const unsigned char *)text, strlen(text),
              digest, &digest_len)) {
        return CLAW_ERR_GENERIC;
    }
    return voice_stt_base64_encode(digest, digest_len, out, out_size);
}
#elif defined(CLAW_PLATFORM_ESP_IDF)
static claw_err_t voice_stt_hmac_sha256_base64(const char *key,
                                               const char *text,
                                               char *out,
                                               size_t out_size)
{
    const mbedtls_md_info_t *info;
    unsigned char digest[32];
    size_t digest_len;

    if (!key || !text || !out || out_size == 0) {
        return CLAW_ERR_INVALID;
    }

    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) {
        return CLAW_ERR_GENERIC;
    }
    if (mbedtls_md_hmac(info,
                        (const unsigned char *)key, strlen(key),
                        (const unsigned char *)text, strlen(text),
                        digest) != 0) {
        return CLAW_ERR_GENERIC;
    }
    digest_len = mbedtls_md_get_size(info);
    return voice_stt_base64_encode(digest, digest_len, out, out_size);
}
#else
static claw_err_t voice_stt_hmac_sha256_base64(const char *key,
                                               const char *text,
                                               char *out,
                                               size_t out_size)
{
    (void)key;
    (void)text;
    (void)out;
    (void)out_size;
    return CLAW_ERR_NOENT;
}
#endif

static claw_err_t voice_stt_xfyun_build_url(const voice_runtime_config_t *cfg,
                                            char *url,
                                            size_t url_size)
{
    char date[64];
    char sign_origin[256];
    char signature[128];
    char auth_origin[512];
    char auth_base64[768];
    char auth_param[(sizeof(auth_base64) * 3U) + 1U];
    char date_param[(sizeof(date) * 3U) + 1U];
    int written;
    claw_err_t err;

    if (!cfg || !url || url_size == 0) {
        return CLAW_ERR_INVALID;
    }
    if (!cfg->stt_xfyun_app_id[0] || !cfg->stt_xfyun_api_key[0] ||
        !cfg->stt_xfyun_api_secret[0]) {
        CLAW_LOGE(TAG,
                  "missing XFYUN config: app_id=%s api_key=%s "
                  "api_secret=%s",
                  cfg->stt_xfyun_app_id[0] ? "set" : "unset",
                  cfg->stt_xfyun_api_key[0] ? "set" : "unset",
                  cfg->stt_xfyun_api_secret[0] ? "set" : "unset");
        return CLAW_ERR_INVALID;
    }

    err = voice_stt_rfc1123_now(date, sizeof(date));
    if (err != CLAW_OK) {
        return err;
    }
    snprintf(sign_origin, sizeof(sign_origin),
             "host: %s\n"
             "date: %s\n"
             "GET %s HTTP/1.1",
             VOICE_STT_XFYUN_HOST, date, VOICE_STT_XFYUN_PATH);
    err = voice_stt_hmac_sha256_base64(cfg->stt_xfyun_api_secret,
                                       sign_origin,
                                       signature,
                                       sizeof(signature));
    if (err != CLAW_OK) {
        return err;
    }
    snprintf(auth_origin, sizeof(auth_origin),
             "api_key=\"%s\", algorithm=\"hmac-sha256\", "
             "headers=\"host date request-line\", signature=\"%s\"",
             cfg->stt_xfyun_api_key, signature);
    err = voice_stt_base64_encode((const uint8_t *)auth_origin,
                                  strlen(auth_origin),
                                  auth_base64,
                                  sizeof(auth_base64));
    if (err != CLAW_OK) {
        return err;
    }
    err = voice_stt_url_encode(auth_base64, auth_param, sizeof(auth_param));
    if (err != CLAW_OK) {
        return err;
    }
    err = voice_stt_url_encode(date, date_param, sizeof(date_param));
    if (err != CLAW_OK) {
        return err;
    }

    written = snprintf(url, url_size,
                       "%s?authorization=%s&date=%s&host=%s",
                       VOICE_STT_XFYUN_URL,
                       auth_param,
                       date_param,
                       VOICE_STT_XFYUN_HOST);
    if (written <= 0 || (size_t)written >= url_size) {
        return CLAW_ERR_NOMEM;
    }
    CLAW_LOGI(TAG, "built XFYUN ws url (timeout=%dms)", cfg->stt_timeout_ms);
    return CLAW_OK;
}

static claw_err_t
voice_stt_xfyun_ws_send_text(struct voice_stt_session *session,
                             const char *text)
{
    size_t len;

    if (!session || !text) {
        return CLAW_ERR_INVALID;
    }
    len = strlen(text);

#ifdef CLAW_PLATFORM_LINUX
    {
        size_t sent = 0;
        CURLcode rc;

        if (!session->ws_curl) {
            return CLAW_ERR_INVALID;
        }
        rc = curl_ws_send(session->ws_curl, text, len, &sent, 0, CURLWS_TEXT);
        if (rc != CURLE_OK || sent != len) {
            CLAW_LOGE(TAG, "ws send failed: %s", curl_easy_strerror(rc));
            return CLAW_ERR_IO;
        }
        return CLAW_OK;
    }
#elif defined(CLAW_PLATFORM_ESP_IDF)
    {
        int sent;

        if (!session->ws_client ||
            !esp_websocket_client_is_connected(session->ws_client)) {
            return CLAW_ERR_STATE;
        }
        sent = esp_websocket_client_send_text(session->ws_client,
                                              text,
                                              (int)len,
                                              portMAX_DELAY);
        if (sent < 0 || (size_t)sent != len) {
            CLAW_LOGE(TAG, "ws send failed: sent=%d expect=%u",
                      sent, (unsigned int)len);
            return CLAW_ERR_IO;
        }
        return CLAW_OK;
    }
#else
    (void)len;
    return CLAW_ERR_NOENT;
#endif
}

static claw_err_t
voice_stt_xfyun_append_ws_result(struct voice_stt_session *session,
                                 cJSON *result)
{
    cJSON *ws;
    int i;

    if (!session || !result) {
        return CLAW_ERR_INVALID;
    }
    ws = cJSON_GetObjectItemCaseSensitive(result, "ws");
    if (!cJSON_IsArray(ws)) {
        return CLAW_OK;
    }
    for (i = 0; i < cJSON_GetArraySize(ws); i++) {
        cJSON *item = cJSON_GetArrayItem(ws, i);
        cJSON *cw;
        cJSON *word;
        const char *text;
        size_t remain;
        int written;

        if (!cJSON_IsObject(item)) {
            continue;
        }
        cw = cJSON_GetObjectItemCaseSensitive(item, "cw");
        if (!cJSON_IsArray(cw) || cJSON_GetArraySize(cw) <= 0) {
            continue;
        }
        word = cJSON_GetArrayItem(cw, 0);
        if (!cJSON_IsObject(word)) {
            continue;
        }
        text = cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(word, "w"));
        if (!text || !text[0]) {
            continue;
        }
        CLAW_LOGD(TAG, "xfyun partial text: %.80s", text);
        remain = sizeof(session->transcript) - session->transcript_len;
        if (remain <= 1) {
            return CLAW_ERR_FULL;
        }
        written = snprintf(session->transcript + session->transcript_len,
                           remain, "%s", text);
        if (written <= 0 || (size_t)written >= remain) {
            return CLAW_ERR_FULL;
        }
        session->transcript_len += (size_t)written;
    }
    return CLAW_OK;
}

static claw_err_t
voice_stt_xfyun_handle_message(struct voice_stt_session *session,
                               const char *message,
                               int *done)
{
    cJSON *root;
    cJSON *code;
    cJSON *msg;
    cJSON *data;
    cJSON *status;
    cJSON *result;
    claw_err_t err;

    if (!session || !message || !done) {
        return CLAW_ERR_INVALID;
    }
    root = cJSON_Parse(message);
    if (!root) {
        return CLAW_ERR_GENERIC;
    }
    code = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (cJSON_IsNumber(code) && code->valueint != 0) {
        msg = cJSON_GetObjectItemCaseSensitive(root, "message");
        CLAW_LOGE(TAG, "xfyun code=%d message=%s",
                  code->valueint,
                  cJSON_IsString(msg) ? msg->valuestring : "?");
        cJSON_Delete(root);
        return CLAW_ERR_GENERIC;
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsObject(data)) {
        result = cJSON_GetObjectItemCaseSensitive(data, "result");
        err = voice_stt_xfyun_append_ws_result(session, result);
        if (err != CLAW_OK) {
            cJSON_Delete(root);
            return err;
        }
        status = cJSON_GetObjectItemCaseSensitive(data, "status");
        if (cJSON_IsNumber(status)) {
            CLAW_LOGD(TAG, "xfyun response status=%d transcript_len=%u",
                      status->valueint,
                      (unsigned int)session->transcript_len);
        }
        if (cJSON_IsNumber(status) && status->valueint == 2) {
            *done = 1;
        }
    }
    cJSON_Delete(root);
    return CLAW_OK;
}

#ifdef CLAW_PLATFORM_LINUX
static claw_err_t
voice_stt_xfyun_recv_until_done(struct voice_stt_session *session)
{
    uint32_t deadline;

    if (!session || !session->ws_curl) {
        return CLAW_ERR_INVALID;
    }
    deadline = claw_tick_ms() + (uint32_t)session->cfg.stt_timeout_ms;
    while (1) {
        char buf[VOICE_STT_RESP_SIZE];
        const struct curl_ws_frame *meta = NULL;
        size_t nread = 0;
        CURLcode rc;
        int done = 0;
        claw_err_t err;

        rc = curl_ws_recv(session->ws_curl, buf, sizeof(buf) - 1,
                          &nread, &meta);
        if (rc == CURLE_AGAIN) {
            if ((int32_t)(claw_tick_ms() - deadline) >= 0) {
                CLAW_LOGE(TAG, "xfyun recv timed out after %dms",
                          session->cfg.stt_timeout_ms);
                return CLAW_ERR_TIMEOUT;
            }
            claw_thread_delay_ms(20);
            continue;
        }
        if (rc != CURLE_OK) {
            CLAW_LOGE(TAG, "ws recv failed: %s", curl_easy_strerror(rc));
            return CLAW_ERR_IO;
        }
        if (!meta || nread == 0) {
            continue;
        }
        buf[nread] = '\0';
        CLAW_LOGD(TAG, "xfyun ws recv flags=0x%x len=%u",
                  meta->flags, (unsigned int)nread);
        if (meta->flags & CURLWS_CLOSE) {
            CLAW_LOGE(TAG, "xfyun ws closed by peer");
            return CLAW_ERR_IO;
        }
        if (!(meta->flags & CURLWS_TEXT)) {
            continue;
        }
        err = voice_stt_xfyun_handle_message(session, buf, &done);
        if (err != CLAW_OK) {
            return err;
        }
        if (done) {
            session->finished = 1;
            return CLAW_OK;
        }
    }
}
#endif

#ifdef CLAW_PLATFORM_ESP_IDF
static void voice_stt_xfyun_signal_connect(struct voice_stt_session *session,
                                           claw_err_t result)
{
    if (!session || !session->connect_pending) {
        return;
    }
    session->connect_pending = 0;
    session->connect_result = result;
    if (session->connect_sem) {
        claw_sem_give(session->connect_sem);
    }
}

static void voice_stt_xfyun_signal_done(struct voice_stt_session *session,
                                        claw_err_t result)
{
    if (!session) {
        return;
    }
    if (result != CLAW_OK) {
        session->final_result = result;
    }
    if (session->done_sem) {
        claw_sem_give(session->done_sem);
    }
}

static claw_err_t
voice_stt_xfyun_handle_data_event(struct voice_stt_session *session,
                                  const esp_websocket_event_data_t *ws)
{
    size_t payload_len;
    size_t payload_offset;
    size_t data_len;
    int done = 0;
    claw_err_t err;

    if (!session || !ws || !ws->data_ptr || ws->payload_len <= 0 ||
        ws->payload_offset < 0 || ws->data_len < 0) {
        return CLAW_ERR_INVALID;
    }

    payload_len = (size_t)ws->payload_len;
    payload_offset = (size_t)ws->payload_offset;
    data_len = (size_t)ws->data_len;

    if (payload_len >= sizeof(session->rx_buf)) {
        CLAW_LOGE(TAG, "xfyun ESP payload too large: %u",
                  (unsigned int)payload_len);
        return CLAW_ERR_FULL;
    }
    if (payload_offset == 0) {
        session->rx_len = 0;
    }
    if (payload_offset + data_len > payload_len ||
        payload_offset + data_len >= sizeof(session->rx_buf)) {
        CLAW_LOGE(TAG, "xfyun ESP payload bounds invalid: off=%u len=%u total=%u",
                  (unsigned int)payload_offset,
                  (unsigned int)data_len,
                  (unsigned int)payload_len);
        return CLAW_ERR_FULL;
    }

    memcpy(session->rx_buf + payload_offset, ws->data_ptr, data_len);
    if (payload_offset + data_len > session->rx_len) {
        session->rx_len = payload_offset + data_len;
    }
    CLAW_LOGD(TAG, "xfyun esp recv offset=%u len=%u total=%u opcode=0x%x",
              (unsigned int)payload_offset,
              (unsigned int)data_len,
              (unsigned int)payload_len,
              ws->op_code);

    if (session->rx_len < payload_len) {
        return CLAW_OK;
    }

    session->rx_buf[payload_len] = '\0';
    session->rx_len = 0;
    err = voice_stt_xfyun_handle_message(session, session->rx_buf, &done);
    if (err != CLAW_OK) {
        return err;
    }
    if (done) {
        session->finished = 1;
        voice_stt_xfyun_signal_done(session, CLAW_OK);
    }
    return CLAW_OK;
}

static void voice_stt_xfyun_ws_event(void *arg,
                                     esp_event_base_t base,
                                     int32_t event_id,
                                     void *event_data)
{
    struct voice_stt_session *session = (struct voice_stt_session *)arg;
    esp_websocket_event_data_t *ws = (esp_websocket_event_data_t *)event_data;
    claw_err_t err;

    (void)base;

    if (!session) {
        return;
    }

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        CLAW_LOGI(TAG, "xfyun websocket connected");
        session->final_result = CLAW_OK;
        voice_stt_xfyun_signal_connect(session, CLAW_OK);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        CLAW_LOGW(TAG, "xfyun websocket disconnected");
        voice_stt_xfyun_signal_connect(session, CLAW_ERR_IO);
        voice_stt_xfyun_signal_done(session, CLAW_ERR_IO);
        break;
    case WEBSOCKET_EVENT_CLOSED:
        CLAW_LOGW(TAG, "xfyun websocket closed");
        voice_stt_xfyun_signal_connect(session, CLAW_ERR_IO);
        voice_stt_xfyun_signal_done(session, CLAW_ERR_IO);
        break;
    case WEBSOCKET_EVENT_ERROR:
        CLAW_LOGE(TAG, "xfyun websocket error");
        voice_stt_xfyun_signal_connect(session, CLAW_ERR_IO);
        voice_stt_xfyun_signal_done(session, CLAW_ERR_IO);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (!ws || !ws->data_ptr || ws->data_len <= 0) {
            break;
        }
        if (ws->op_code == 0x02) {
            CLAW_LOGD(TAG, "ignoring binary STT websocket frame");
            break;
        }
        err = voice_stt_xfyun_handle_data_event(session, ws);
        if (err != CLAW_OK) {
            CLAW_LOGE(TAG, "xfyun ESP frame handling failed: %s",
                      claw_strerror(err));
            voice_stt_xfyun_signal_done(session, err);
        }
        break;
    default:
        break;
    }
}
#endif

static claw_err_t voice_stt_xfyun_send_frame(struct voice_stt_session *session,
                                             int status,
                                             const void *audio,
                                             size_t audio_len)
{
    cJSON *root = NULL;
    cJSON *data;
    cJSON *common;
    cJSON *business;
    char format[32];
    char *audio_base64 = NULL;
    char *json = NULL;
    claw_err_t ret = CLAW_ERR_GENERIC;

    if (!session) {
        return CLAW_ERR_INVALID;
    }
    root = cJSON_CreateObject();
    if (!root) {
        return CLAW_ERR_NOMEM;
    }
    if (status == 0) {
        common = cJSON_AddObjectToObject(root, "common");
        business = cJSON_AddObjectToObject(root, "business");
        if (!common || !business) {
            ret = CLAW_ERR_NOMEM;
            goto out;
        }
        cJSON_AddStringToObject(common, "app_id",
                                session->cfg.stt_xfyun_app_id);
        cJSON_AddStringToObject(business, "language", "zh_cn");
        cJSON_AddStringToObject(business, "domain", "iat");
        cJSON_AddStringToObject(business, "accent", "mandarin");
    }
    data = cJSON_AddObjectToObject(root, "data");
    if (!data) {
        ret = CLAW_ERR_NOMEM;
        goto out;
    }
    cJSON_AddNumberToObject(data, "status", status);
    if (status != 2) {
        snprintf(format, sizeof(format), "audio/L16;rate=%d",
                 session->format.sample_rate > 0 ?
                 session->format.sample_rate : 16000);
        audio_base64 = (char *)claw_malloc(((audio_len + 2U) / 3U) * 4U + 4U);
        if (!audio_base64) {
            ret = CLAW_ERR_NOMEM;
            goto out;
        }
        ret = voice_stt_base64_encode((const uint8_t *)audio,
                                      audio_len,
                                      audio_base64,
                                      ((audio_len + 2U) / 3U) * 4U + 4U);
        if (ret != CLAW_OK) {
            goto out;
        }
        cJSON_AddStringToObject(data, "format", format);
        cJSON_AddStringToObject(data, "encoding", "raw");
        cJSON_AddStringToObject(data, "audio", audio_base64);
    }
    json = cJSON_PrintUnformatted(root);
    if (!json) {
        ret = CLAW_ERR_NOMEM;
        goto out;
    }
    CLAW_LOGD(TAG, "xfyun send frame status=%d audio_len=%u json_len=%u",
              status,
              (unsigned int)audio_len,
              (unsigned int)strlen(json));
    ret = voice_stt_xfyun_ws_send_text(session, json);
out:
    claw_free(audio_base64);
    cJSON_free(json);
    cJSON_Delete(root);
    return ret;
}

#if defined(CLAW_PLATFORM_LINUX)
static claw_err_t voice_stt_xfyun_session_start(struct voice_stt_session *session)
{
    char url[1024];
    CURLcode rc;
    claw_err_t err;

    err = voice_stt_xfyun_build_url(&session->cfg, url, sizeof(url));
    if (err != CLAW_OK) {
        CLAW_LOGE(TAG, "xfyun url build failed: %s", claw_strerror(err));
        return err;
    }
    CLAW_LOGI(TAG, "connecting XFYUN websocket for %dHz/%dch/%dbps %s",
              session->format.sample_rate,
              session->format.channels,
              session->format.bits_per_sample,
              session->format.encoding);
    session->ws_curl = curl_easy_init();
    if (!session->ws_curl) {
        return CLAW_ERR_NOMEM;
    }
    curl_easy_setopt(session->ws_curl, CURLOPT_URL, url);
    curl_easy_setopt(session->ws_curl, CURLOPT_CONNECT_ONLY, 2L);
    rc = curl_easy_perform(session->ws_curl);
    if (rc != CURLE_OK) {
        CLAW_LOGE(TAG, "xfyun ws connect failed: %s", curl_easy_strerror(rc));
        curl_easy_cleanup(session->ws_curl);
        session->ws_curl = NULL;
        return CLAW_ERR_IO;
    }
    CLAW_LOGI(TAG, "xfyun websocket connected");
    return CLAW_OK;
}
#elif defined(CLAW_PLATFORM_ESP_IDF)
static claw_err_t voice_stt_xfyun_session_start(struct voice_stt_session *session)
{
    char url[1024];
    esp_websocket_client_config_t ws_cfg = { 0 };
    esp_err_t ws_err;
    claw_err_t err;

    session->connect_sem = claw_sem_create("voice_stt_conn", 0);
    session->done_sem = claw_sem_create("voice_stt_done", 0);
    if (!session->connect_sem || !session->done_sem) {
        return CLAW_ERR_NOMEM;
    }

    err = voice_stt_xfyun_build_url(&session->cfg, url, sizeof(url));
    if (err != CLAW_OK) {
        CLAW_LOGE(TAG, "xfyun url build failed: %s", claw_strerror(err));
        return err;
    }

    CLAW_LOGI(TAG, "connecting XFYUN websocket for %dHz/%dch/%dbps %s",
              session->format.sample_rate,
              session->format.channels,
              session->format.bits_per_sample,
              session->format.encoding);

    session->connect_pending = 1;
    session->connect_result = CLAW_ERR_IO;
    session->final_result = CLAW_OK;
    session->rx_len = 0;

    ws_cfg.uri = url;
    ws_cfg.buffer_size = (int)VOICE_STT_RESP_SIZE;
    ws_cfg.task_stack = 8192;
    ws_cfg.disable_auto_reconnect = true;

    session->ws_client = esp_websocket_client_init(&ws_cfg);
    if (!session->ws_client) {
        return CLAW_ERR_NOMEM;
    }

    ws_err = esp_websocket_register_events(session->ws_client,
                                           WEBSOCKET_EVENT_ANY,
                                           voice_stt_xfyun_ws_event,
                                           session);
    if (ws_err != ESP_OK) {
        CLAW_LOGE(TAG, "xfyun ws register failed: %d", ws_err);
        return CLAW_ERR_IO;
    }

    ws_err = esp_websocket_client_start(session->ws_client);
    if (ws_err != ESP_OK) {
        CLAW_LOGE(TAG, "xfyun ws start failed: %d", ws_err);
        return CLAW_ERR_IO;
    }

    err = claw_sem_take(session->connect_sem,
                        (uint32_t)session->cfg.stt_timeout_ms);
    if (err != CLAW_OK) {
        CLAW_LOGE(TAG, "xfyun ws connect timed out after %dms",
                  session->cfg.stt_timeout_ms);
        return err == CLAW_ERR_TIMEOUT ? CLAW_ERR_TIMEOUT : CLAW_ERR_IO;
    }
    if (session->connect_result != CLAW_OK) {
        return session->connect_result;
    }
    return CLAW_OK;
}
#else
static claw_err_t voice_stt_xfyun_session_start(struct voice_stt_session *session)
{
    (void)session;
    return CLAW_ERR_NOENT;
}
#endif

claw_err_t voice_stt_session_start(struct voice_stt_session **session_out,
                                   const voice_runtime_config_t *cfg,
                                   const struct voice_audio_format *format)
{
    struct voice_stt_session *session;
    claw_err_t err;

    if (!session_out || !cfg || !format) {
        return CLAW_ERR_INVALID;
    }
    session = (struct voice_stt_session *)claw_calloc(1, sizeof(*session));
    if (!session) {
        return CLAW_ERR_NOMEM;
    }
    err = voice_stt_parse_provider(cfg, &session->provider);
    if (err != CLAW_OK) {
        voice_stt_session_destroy(session);
        return err;
    }
    session->cfg = *cfg;
    session->format = *format;
    CLAW_LOGI(TAG,
              "starting STT session: provider=%s format=%dHz/%dch/%dbps "
              "%s timeout=%dms",
              cfg->stt_provider[0] ? cfg->stt_provider : "(default)",
              format->sample_rate,
              format->channels,
              format->bits_per_sample,
              format->encoding,
              cfg->stt_timeout_ms);
    err = voice_stt_xfyun_session_start(session);
    if (err != CLAW_OK) {
        voice_stt_session_destroy(session);
        return err;
    }
    *session_out = session;
    return CLAW_OK;
}

claw_err_t voice_stt_session_send(struct voice_stt_session *session,
                                  const void *audio,
                                  size_t audio_len)
{
    size_t offset = 0;
    claw_err_t err;

    if (!session || !audio || audio_len == 0) {
        return CLAW_ERR_INVALID;
    }
    while (offset < audio_len) {
        size_t chunk_len = audio_len - offset;

        if (chunk_len > VOICE_STT_CHUNK_AUDIO_MAX) {
            chunk_len = VOICE_STT_CHUNK_AUDIO_MAX;
        }
        CLAW_LOGD(TAG, "streaming STT chunk offset=%u len=%u status=%d",
                  (unsigned int)offset,
                  (unsigned int)chunk_len,
                  session->started ? 1 : 0);
        err = voice_stt_xfyun_send_frame(session,
                                         session->started ? 1 : 0,
                                         (const uint8_t *)audio + offset,
                                         chunk_len);
        if (err != CLAW_OK) {
            return err;
        }
        session->started = 1;
        offset += chunk_len;
        claw_thread_delay_ms(VOICE_STT_CHUNK_MS);
    }
    return CLAW_OK;
}

claw_err_t voice_stt_session_finish(struct voice_stt_session *session,
                                    char *text,
                                    size_t text_size)
{
    claw_err_t err;

    if (!session || !text || text_size == 0) {
        return CLAW_ERR_INVALID;
    }
    if (session->finished) {
        snprintf(text, text_size, "%s", session->transcript);
        return CLAW_OK;
    }
    CLAW_LOGI(TAG, "finishing STT session (transcript_len=%u)",
              (unsigned int)session->transcript_len);
    err = voice_stt_xfyun_send_frame(session, 2, NULL, 0);
    if (err != CLAW_OK) {
        CLAW_LOGE(TAG, "xfyun final frame send failed: %s", claw_strerror(err));
        return err;
    }

#ifdef CLAW_PLATFORM_LINUX
    err = voice_stt_xfyun_recv_until_done(session);
#elif defined(CLAW_PLATFORM_ESP_IDF)
    if (!session->done_sem) {
        return CLAW_ERR_STATE;
    }
    err = claw_sem_take(session->done_sem,
                        (uint32_t)session->cfg.stt_timeout_ms);
    if (err != CLAW_OK) {
        CLAW_LOGE(TAG, "xfyun receive timed out after %dms",
                  session->cfg.stt_timeout_ms);
        return err == CLAW_ERR_TIMEOUT ? CLAW_ERR_TIMEOUT : CLAW_ERR_IO;
    }
    err = session->final_result;
#else
    return CLAW_ERR_NOENT;
#endif

    if (err != CLAW_OK) {
        CLAW_LOGE(TAG, "xfyun receive failed: %s", claw_strerror(err));
        return err;
    }
    if (session->transcript[0] == '\0') {
        CLAW_LOGE(TAG, "xfyun transcript empty after finish");
        return CLAW_ERR_NOENT;
    }
    snprintf(text, text_size, "%s", session->transcript);
    session->finished = 1;
    CLAW_LOGI(TAG, "stt transcript complete (%u bytes): %.120s",
              (unsigned int)strlen(text), text);
    return CLAW_OK;
}

void voice_stt_session_abort(struct voice_stt_session *session)
{
    if (!session) {
        return;
    }
#ifdef CLAW_PLATFORM_LINUX
    if (session->ws_curl) {
        CLAW_LOGI(TAG, "aborting STT session");
        (void)voice_stt_xfyun_send_frame(session, 2, NULL, 0);
    }
#elif defined(CLAW_PLATFORM_ESP_IDF)
    if (session->ws_client &&
        esp_websocket_client_is_connected(session->ws_client)) {
        CLAW_LOGI(TAG, "aborting STT session");
        (void)voice_stt_xfyun_send_frame(session, 2, NULL, 0);
    }
#endif
}

void voice_stt_session_destroy(struct voice_stt_session *session)
{
    if (!session) {
        return;
    }
#ifdef CLAW_PLATFORM_LINUX
    if (session->ws_curl) {
        curl_easy_cleanup(session->ws_curl);
        session->ws_curl = NULL;
    }
#elif defined(CLAW_PLATFORM_ESP_IDF)
    if (session->ws_client) {
        if (esp_websocket_client_is_connected(session->ws_client)) {
            (void)esp_websocket_client_close(session->ws_client,
                                             portMAX_DELAY);
        }
        (void)esp_websocket_client_stop(session->ws_client);
        esp_websocket_client_destroy(session->ws_client);
        session->ws_client = NULL;
    }
    if (session->connect_sem) {
        claw_sem_delete(session->connect_sem);
        session->connect_sem = NULL;
    }
    if (session->done_sem) {
        claw_sem_delete(session->done_sem);
        session->done_sem = NULL;
    }
#endif
    claw_free(session);
}
