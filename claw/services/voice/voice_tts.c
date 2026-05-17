#include "osal/claw_os.h"
#include "claw_config.h"
#include "osal/claw_net.h"
#include "claw/services/voice/voice_providers.h"
#include "cJSON.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef CLAW_PLATFORM_LINUX
#include <openssl/evp.h>
#endif

#define TAG "voice_tts"
#define VOICE_TTS_RESP_SIZE  CONFIG_RTCLAW_VOICE_TTS_RESP_SIZE
#define VOICE_TTS_STREAM_CHUNK_SIZE 4096U

enum voice_tts_provider_type {
    VOICE_TTS_PROVIDER_MIMO = 0,
};

static claw_err_t
voice_tts_parse_provider(const voice_runtime_config_t *cfg,
                         enum voice_tts_provider_type *provider)
{
    if (!cfg || !provider) {
        return CLAW_ERR_INVALID;
    }
    if (cfg->tts_provider[0] == '\0' ||
        strcmp(cfg->tts_provider, "mimo") == 0) {
        *provider = VOICE_TTS_PROVIDER_MIMO;
        return CLAW_OK;
    }
    return CLAW_ERR_INVALID;
}

static const char *voice_tts_format_mime(const char *format)
{
    if (!format || !format[0] || strcmp(format, "wav") == 0) {
        return "audio/wav";
    }
    if (strcmp(format, "pcm") == 0 || strcmp(format, "pcm16") == 0) {
        return "audio/L16";
    }
    if (strcmp(format, "mp3") == 0) {
        return "audio/mpeg";
    }
    return "application/octet-stream";
}

#ifdef CLAW_PLATFORM_LINUX
static claw_err_t voice_tts_base64_decode(const char *data,
                                          void *out,
                                          size_t out_size,
                                          size_t *out_len)
{
    uint8_t *dst = (uint8_t *)out;
    size_t input_len;
    int decoded_len;
    int pad = 0;

    if (!data || !out || !out_len) {
        return CLAW_ERR_INVALID;
    }
    input_len = strlen(data);
    if (input_len == 0 || (input_len % 4) != 0) {
        return CLAW_ERR_INVALID;
    }
    if (data[input_len - 1] == '=') {
        pad++;
    }
    if (input_len > 1 && data[input_len - 2] == '=') {
        pad++;
    }
    if (((input_len / 4) * 3) < (size_t)pad) {
        return CLAW_ERR_INVALID;
    }
    if (((input_len / 4) * 3) - (size_t)pad > out_size) {
        return CLAW_ERR_NOMEM;
    }

    decoded_len = EVP_DecodeBlock(dst, (const unsigned char *)data,
                                  (int)input_len);
    if (decoded_len < 0) {
        return CLAW_ERR_GENERIC;
    }
    *out_len = (size_t)decoded_len - (size_t)pad;
    return CLAW_OK;
}
#else
static claw_err_t voice_tts_base64_decode(const char *data,
                                          void *out,
                                          size_t out_size,
                                          size_t *out_len)
{
    static const int8_t table[256] = {
        ['A'] = 0,  ['B'] = 1,  ['C'] = 2,  ['D'] = 3,
        ['E'] = 4,  ['F'] = 5,  ['G'] = 6,  ['H'] = 7,
        ['I'] = 8,  ['J'] = 9,  ['K'] = 10, ['L'] = 11,
        ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15,
        ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19,
        ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
        ['Y'] = 24, ['Z'] = 25,
        ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29,
        ['e'] = 30, ['f'] = 31, ['g'] = 32, ['h'] = 33,
        ['i'] = 34, ['j'] = 35, ['k'] = 36, ['l'] = 37,
        ['m'] = 38, ['n'] = 39, ['o'] = 40, ['p'] = 41,
        ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45,
        ['u'] = 46, ['v'] = 47, ['w'] = 48, ['x'] = 49,
        ['y'] = 50, ['z'] = 51,
        ['0'] = 52, ['1'] = 53, ['2'] = 54, ['3'] = 55,
        ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59,
        ['8'] = 60, ['9'] = 61, ['+'] = 62, ['/'] = 63,
    };
    const unsigned char *src = (const unsigned char *)data;
    uint8_t *dst = (uint8_t *)out;
    size_t input_len;
    size_t written = 0;
    size_t i;

    if (!data || !out || !out_len) {
        return CLAW_ERR_INVALID;
    }
    input_len = strlen(data);
    if (input_len == 0 || (input_len % 4) != 0) {
        return CLAW_ERR_INVALID;
    }

    for (i = 0; i < input_len; i += 4) {
        int8_t a = table[src[i + 0]];
        int8_t b = table[src[i + 1]];
        int8_t c = src[i + 2] == '=' ? 0 : table[src[i + 2]];
        int8_t d = src[i + 3] == '=' ? 0 : table[src[i + 3]];
        uint32_t chunk;

        if (a < 0 || b < 0 || (src[i + 2] != '=' && c < 0) ||
            (src[i + 3] != '=' && d < 0)) {
            return CLAW_ERR_INVALID;
        }
        chunk = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                ((uint32_t)c << 6) | (uint32_t)d;
        if (written + 1 > out_size) {
            return CLAW_ERR_NOMEM;
        }
        dst[written++] = (uint8_t)((chunk >> 16) & 0xff);
        if (src[i + 2] != '=') {
            if (written + 1 > out_size) {
                return CLAW_ERR_NOMEM;
            }
            dst[written++] = (uint8_t)((chunk >> 8) & 0xff);
        }
        if (src[i + 3] != '=') {
            if (written + 1 > out_size) {
                return CLAW_ERR_NOMEM;
            }
            dst[written++] = (uint8_t)(chunk & 0xff);
        }
    }

    *out_len = written;
    return CLAW_OK;
}
#endif

static claw_err_t
voice_tts_mimo_build_request(const voice_runtime_config_t *cfg,
                             const char *text,
                             char **body_out)
{
    cJSON *root = NULL;
    cJSON *messages = NULL;
    cJSON *audio = NULL;
    cJSON *assistant = NULL;
    cJSON *user = NULL;
    char *body = NULL;

    if (!cfg || !text || !body_out) {
        return CLAW_ERR_INVALID;
    }
    if (!cfg->tts_url[0] || !cfg->tts_key[0] || !cfg->tts_model[0]) {
        CLAW_LOGE(TAG, "missing TTS config: url=%s key=%s model=%s",
                  cfg->tts_url[0] ? "set" : "unset",
                  cfg->tts_key[0] ? "set" : "unset",
                  cfg->tts_model[0] ? "set" : "unset");
        return CLAW_ERR_INVALID;
    }

    root = cJSON_CreateObject();
    messages = cJSON_CreateArray();
    audio = cJSON_CreateObject();
    assistant = cJSON_CreateObject();
    if (!root || !messages || !audio || !assistant) {
        cJSON_Delete(root);
        cJSON_Delete(messages);
        cJSON_Delete(audio);
        cJSON_Delete(assistant);
        return CLAW_ERR_NOMEM;
    }

    cJSON_AddStringToObject(root, "model", cfg->tts_model);
    cJSON_AddItemToObject(root, "messages", messages);
    cJSON_AddItemToObject(root, "audio", audio);
    cJSON_AddBoolToObject(root, "stream", cfg->tts_stream ? 1 : 0);

    if (cfg->tts_style_prompt[0]) {
        user = cJSON_CreateObject();
        if (!user) {
            cJSON_Delete(root);
            return CLAW_ERR_NOMEM;
        }
        cJSON_AddStringToObject(user, "role", "user");
        cJSON_AddStringToObject(user, "content", cfg->tts_style_prompt);
        cJSON_AddItemToArray(messages, user);
    }

    cJSON_AddStringToObject(assistant, "role", "assistant");
    cJSON_AddStringToObject(assistant, "content", text);
    cJSON_AddItemToArray(messages, assistant);

    if (cfg->tts_voice[0]) {
        cJSON_AddStringToObject(audio, "voice", cfg->tts_voice);
    }
    if (cfg->tts_format[0]) {
        cJSON_AddStringToObject(audio, "format", cfg->tts_format);
    } else {
        cJSON_AddStringToObject(audio, "format", "wav");
    }

    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return CLAW_ERR_NOMEM;
    }

    CLAW_LOGI(TAG,
              "MiMo request built: model=%s format=%s stream=%d voice=%s "
              "style_prompt=%s text_len=%u body_len=%u",
              cfg->tts_model,
              cfg->tts_format[0] ? cfg->tts_format : "wav",
              cfg->tts_stream,
              cfg->tts_voice[0] ? cfg->tts_voice : "(unset)",
              cfg->tts_style_prompt[0] ? "set" : "unset",
              (unsigned int)strlen(text),
              (unsigned int)strlen(body));
    *body_out = body;
    return CLAW_OK;
}

struct mimo_stream_ctx {
    const char           *format;
    const char           *mime_type;
    voice_tts_audio_cb_t  audio_cb;
    void                 *audio_user;
    uint8_t               audio_buf[VOICE_TTS_STREAM_CHUNK_SIZE];
    size_t                audio_len;
    char                  key_window[16];
    size_t                key_len;
    char                  b64[4];
    size_t                b64_len;
    int                   in_audio;
    int                   audio_done;
    int                   escape;
};

static int voice_tts_b64_value(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    if (ch == '=') {
        return 0;
    }
    return -1;
}

static claw_err_t mimo_stream_emit(struct mimo_stream_ctx *ctx)
{
    claw_err_t rc;

    if (ctx->audio_len == 0) {
        return CLAW_OK;
    }
    rc = ctx->audio_cb(ctx->audio_buf, ctx->audio_len,
                       ctx->mime_type, ctx->audio_user);
    if (rc != CLAW_OK) {
        return rc;
    }
    ctx->audio_len = 0;
    return CLAW_OK;
}

static claw_err_t mimo_stream_put_audio(struct mimo_stream_ctx *ctx,
                                        uint8_t byte)
{
    ctx->audio_buf[ctx->audio_len++] = byte;
    if (ctx->audio_len >= sizeof(ctx->audio_buf)) {
        return mimo_stream_emit(ctx);
    }
    return CLAW_OK;
}

static claw_err_t mimo_stream_decode_quad(struct mimo_stream_ctx *ctx)
{
    int a = voice_tts_b64_value(ctx->b64[0]);
    int b = voice_tts_b64_value(ctx->b64[1]);
    int c = voice_tts_b64_value(ctx->b64[2]);
    int d = voice_tts_b64_value(ctx->b64[3]);
    uint32_t chunk;
    claw_err_t rc;

    if (a < 0 || b < 0 || c < 0 || d < 0) {
        return CLAW_ERR_INVALID;
    }
    chunk = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
            ((uint32_t)c << 6) | (uint32_t)d;
    rc = mimo_stream_put_audio(ctx, (uint8_t)((chunk >> 16) & 0xff));
    if (rc != CLAW_OK || ctx->b64[2] == '=') {
        return rc;
    }
    rc = mimo_stream_put_audio(ctx, (uint8_t)((chunk >> 8) & 0xff));
    if (rc != CLAW_OK || ctx->b64[3] == '=') {
        return rc;
    }
    return mimo_stream_put_audio(ctx, (uint8_t)(chunk & 0xff));
}

static int mimo_stream_http_cb(const void *data, size_t len, void *user)
{
    struct mimo_stream_ctx *ctx = (struct mimo_stream_ctx *)user;
    const char *src = (const char *)data;
    size_t i;

    for (i = 0; i < len; i++) {
        char ch = src[i];

        if (!ctx->in_audio) {
            if (ctx->audio_done) {
                continue;
            }
            if (ctx->key_len < sizeof(ctx->key_window) - 1) {
                ctx->key_window[ctx->key_len++] = ch;
            } else {
                memmove(ctx->key_window, ctx->key_window + 1,
                        sizeof(ctx->key_window) - 2);
                ctx->key_window[sizeof(ctx->key_window) - 2] = ch;
            }
            ctx->key_window[ctx->key_len] = '\0';
            if (strstr(ctx->key_window, "\"data\":\"") != NULL ||
                strstr(ctx->key_window, "\"data\" : \"") != NULL) {
                ctx->in_audio = 1;
                ctx->mime_type = voice_tts_format_mime(ctx->format);
            }
            continue;
        }

        if (ctx->escape) {
            ctx->escape = 0;
            if (ch == '/') {
                ch = '/';
            } else if (ch == 'n' || ch == 'r' || ch == 't') {
                continue;
            } else {
                CLAW_LOGE(TAG, "invalid escaped MiMo audio byte: 0x%02x",
                          (unsigned int)(uint8_t)ch);
                return CLAW_ERR_INVALID;
            }
        } else if (ch == '\\') {
            ctx->escape = 1;
            continue;
        } else if (ch == '"') {
            ctx->in_audio = 0;
            ctx->audio_done = 1;
            continue;
        }
        if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') {
            continue;
        }
        if (voice_tts_b64_value(ch) < 0) {
            CLAW_LOGE(TAG, "invalid MiMo audio base64 byte: 0x%02x",
                      (unsigned int)(uint8_t)ch);
            return CLAW_ERR_INVALID;
        }
        ctx->b64[ctx->b64_len++] = ch;
        if (ctx->b64_len == 4) {
            claw_err_t rc = mimo_stream_decode_quad(ctx);
            if (rc != CLAW_OK) {
                return rc;
            }
            ctx->b64_len = 0;
        }
    }
    return CLAW_OK;
}

static claw_err_t voice_tts_mimo_parse_response(const char *resp,
                                                const char *format,
                                                void *audio,
                                                size_t audio_size,
                                                size_t *audio_len,
                                                const char **mime_type)
{
    cJSON *root;
    cJSON *choices;
    cJSON *choice;
    cJSON *message;
    cJSON *audio_obj;
    cJSON *audio_data;
    const char *audio_base64;
    claw_err_t rc;

    if (!resp || !audio || !audio_len || !mime_type) {
        return CLAW_ERR_INVALID;
    }

    root = cJSON_Parse(resp);
    if (!root) {
        CLAW_LOGE(TAG, "MiMo response JSON parse failed: %.200s", resp);
        return CLAW_ERR_GENERIC;
    }
    choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    choice = cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0 ?
             cJSON_GetArrayItem(choices, 0) : NULL;
    message = choice ?
              cJSON_GetObjectItemCaseSensitive(choice, "message") : NULL;
    audio_obj = message ?
                cJSON_GetObjectItemCaseSensitive(message, "audio") : NULL;
    audio_data = audio_obj ?
                 cJSON_GetObjectItemCaseSensitive(audio_obj, "data") : NULL;
    audio_base64 = cJSON_GetStringValue(audio_data);
    if (!audio_base64 || !audio_base64[0]) {
        CLAW_LOGE(TAG,
                  "MiMo response missing audio.data: %.200s",
                  resp);
        cJSON_Delete(root);
        return CLAW_ERR_NOENT;
    }

    CLAW_LOGI(TAG, "MiMo response audio payload: base64_len=%u out_buf=%u",
              (unsigned int)strlen(audio_base64),
              (unsigned int)audio_size);
    rc = voice_tts_base64_decode(audio_base64, audio, audio_size, audio_len);
    cJSON_Delete(root);
    if (rc != CLAW_OK) {
        if (rc == CLAW_ERR_NOMEM) {
            CLAW_LOGE(TAG, "MiMo audio decode exceeded output buffer");
            return CLAW_ERR_FULL;
        }
        CLAW_LOGE(TAG, "MiMo audio decode failed: %s", claw_strerror(rc));
        return rc;
    }

    *mime_type = voice_tts_format_mime(format);
    CLAW_LOGI(TAG, "MiMo audio decoded: %u bytes mime=%s",
              (unsigned int)*audio_len, *mime_type);
    return CLAW_OK;
}

static claw_err_t voice_tts_mimo_synthesize_stream(
    const voice_runtime_config_t *cfg,
    const char *text,
    voice_tts_audio_cb_t cb,
    void *user,
    const char **mime_type)
{
    char auth[VOICE_KEY_MAX + 8];
    claw_net_header_t headers[2];
    char *body = NULL;
    size_t resp_len = 0;
    int status;
    claw_err_t rc;
    struct mimo_stream_ctx stream_ctx;

    if (!cfg || !text || !cb || !mime_type) {
        return CLAW_ERR_INVALID;
    }

    rc = voice_tts_mimo_build_request(cfg, text, &body);
    if (rc != CLAW_OK) {
        return rc;
    }

    memset(&stream_ctx, 0, sizeof(stream_ctx));
    stream_ctx.format = cfg->tts_format;
    stream_ctx.mime_type = voice_tts_format_mime(cfg->tts_format);
    stream_ctx.audio_cb = cb;
    stream_ctx.audio_user = user;
    *mime_type = stream_ctx.mime_type;

    snprintf(auth, sizeof(auth), "Bearer %s", cfg->tts_key);
    headers[0] = (claw_net_header_t){ "Content-Type", "application/json" };
    headers[1] = (claw_net_header_t){ "Authorization", auth };

    status = claw_net_post_stream(cfg->tts_url, headers, 2,
                                  body, strlen(body),
                                  mimo_stream_http_cb, &stream_ctx,
                                  &resp_len);
    cJSON_free(body);
    if (status < 0) {
        CLAW_LOGE(TAG, "MiMo stream HTTP transport failed");
        return CLAW_ERR_IO;
    }
    CLAW_LOGI(TAG, "MiMo stream HTTP status=%d resp_len=%u",
              status, (unsigned int)resp_len);
    if (status != 200) {
        CLAW_LOGE(TAG, "MiMo TTS stream HTTP %d", status);
        return CLAW_ERR_GENERIC;
    }
    if (stream_ctx.b64_len != 0 || stream_ctx.in_audio ||
        !stream_ctx.audio_done || stream_ctx.mime_type == NULL) {
        return CLAW_ERR_INVALID;
    }
    rc = mimo_stream_emit(&stream_ctx);
    if (rc != CLAW_OK) {
        return rc;
    }
    return CLAW_OK;
}

static claw_err_t voice_tts_mimo_synthesize(const voice_runtime_config_t *cfg,
                                            const char *text,
                                            void *audio,
                                            size_t audio_size,
                                            size_t *audio_len,
                                            const char **mime_type)
{
    char auth[VOICE_KEY_MAX + 8];
    claw_net_header_t headers[2];
    char *body = NULL;
    char *resp_buf;
    size_t resp_len = 0;
    size_t resp_cap;
    size_t audio_base64_cap;
    size_t required_resp_cap;
    int status;
    claw_err_t rc;

    if (!cfg || !text || !audio || !audio_len || !mime_type) {
        return CLAW_ERR_INVALID;
    }

    /*
     * Compute the minimum response buffer that can hold the full MiMo JSON:
     *   base64(audio_size) + fixed JSON envelope overhead.
     * If this exceeds the configured cap, return CLAW_ERR_FULL immediately
     * so the caller can grow the decoded audio buffer before retrying —
     * without issuing a remote request that is guaranteed to be truncated.
     */
    audio_base64_cap = ((audio_size + 2U) / 3U) * 4U;
    if (audio_base64_cap > SIZE_MAX - 4096U) {
        return CLAW_ERR_NOMEM;
    }
    required_resp_cap = audio_base64_cap + 4096U;
#ifdef CLAW_PLATFORM_LINUX
    resp_cap = VOICE_TTS_RESP_SIZE;
    (void)required_resp_cap;
#else
    if (required_resp_cap > VOICE_TTS_RESP_SIZE) {
        CLAW_LOGE(TAG,
                  "MiMo resp cap too small: need=%u configured=%u "
                  "audio_buf=%u",
                  (unsigned int)required_resp_cap,
                  (unsigned int)VOICE_TTS_RESP_SIZE,
                  (unsigned int)audio_size);
        return CLAW_ERR_FULL;
    }
    resp_cap = required_resp_cap;
#endif

    CLAW_LOGI(TAG,
              "starting MiMo synth: url=%s model=%s format=%s stream=%d "
              "voice=%s text_len=%u audio_buf=%u resp_buf=%u",
              cfg->tts_url[0] ? cfg->tts_url : "(unset)",
              cfg->tts_model[0] ? cfg->tts_model : "(unset)",
              cfg->tts_format[0] ? cfg->tts_format : "wav",
              cfg->tts_stream,
              cfg->tts_voice[0] ? cfg->tts_voice : "(unset)",
              (unsigned int)strlen(text),
              (unsigned int)audio_size,
              (unsigned int)resp_cap);
    rc = voice_tts_mimo_build_request(cfg, text, &body);
    if (rc != CLAW_OK) {
        CLAW_LOGE(TAG, "MiMo request build failed: %s", claw_strerror(rc));
        return rc;
    }

    resp_buf = (char *)claw_malloc(resp_cap);
    if (!resp_buf) {
        cJSON_free(body);
        return CLAW_ERR_NOMEM;
    }
    resp_buf[0] = '\0';

    snprintf(auth, sizeof(auth), "Bearer %s", cfg->tts_key);
    headers[0] = (claw_net_header_t){ "Content-Type", "application/json" };
    headers[1] = (claw_net_header_t){ "Authorization", auth };

    status = claw_net_post(cfg->tts_url, headers, 2,
                           body, strlen(body),
                           resp_buf, resp_cap, &resp_len);
    cJSON_free(body);
    if (status < 0) {
        CLAW_LOGE(TAG, "MiMo HTTP transport failed");
        claw_free(resp_buf);
        return CLAW_ERR_IO;
    }
    CLAW_LOGI(TAG, "MiMo HTTP status=%d resp_len=%u",
              status, (unsigned int)resp_len);
    if (status != 200) {
        CLAW_LOGE(TAG, "MiMo TTS HTTP %d: %.200s", status, resp_buf);
        claw_free(resp_buf);
        return CLAW_ERR_GENERIC;
    }

    rc = voice_tts_mimo_parse_response(resp_buf,
                                       cfg->tts_format,
                                       audio,
                                       audio_size,
                                       audio_len,
                                       mime_type);
    if (rc != CLAW_OK) {
        if (resp_len + 1 >= resp_cap) {
            CLAW_LOGE(TAG,
                      "MiMo response truncated: resp_len=%u cap=%u "
                      "audio_buf=%u configured=%u; increase "
                      "voice_tts_resp_size",
                      (unsigned int)resp_len,
                      (unsigned int)resp_cap,
                      (unsigned int)audio_size,
                      (unsigned int)VOICE_TTS_RESP_SIZE);
            rc = CLAW_ERR_FULL;
        }
        CLAW_LOGE(TAG, "MiMo response parse failed: %s", claw_strerror(rc));
    }
    claw_free(resp_buf);
    return rc;
}

claw_err_t voice_tts_synthesize(const voice_runtime_config_t *cfg,
                                const char *text,
                                void *audio,
                                size_t audio_size,
                                size_t *audio_len,
                                const char **mime_type)
{
    enum voice_tts_provider_type provider;
    claw_err_t rc;

    if (!cfg || !text || !text[0] || !audio || !audio_len || !mime_type) {
        return CLAW_ERR_INVALID;
    }

    rc = voice_tts_parse_provider(cfg, &provider);
    if (rc != CLAW_OK) {
        return rc;
    }

    switch (provider) {
    case VOICE_TTS_PROVIDER_MIMO:
        return voice_tts_mimo_synthesize(cfg, text,
                                         audio, audio_size,
                                         audio_len, mime_type);
    default:
        return CLAW_ERR_INVALID;
    }
}

claw_err_t voice_tts_synthesize_stream(const voice_runtime_config_t *cfg,
                                       const char *text,
                                       voice_tts_audio_cb_t cb,
                                       void *user,
                                       const char **mime_type)
{
    enum voice_tts_provider_type provider;
    claw_err_t rc;

    if (!cfg || !text || !text[0] || !cb || !mime_type) {
        return CLAW_ERR_INVALID;
    }

    rc = voice_tts_parse_provider(cfg, &provider);
    if (rc != CLAW_OK) {
        return rc;
    }

    switch (provider) {
    case VOICE_TTS_PROVIDER_MIMO:
        return voice_tts_mimo_synthesize_stream(cfg, text, cb,
                                                user, mime_type);
    default:
        return CLAW_ERR_INVALID;
    }
}
