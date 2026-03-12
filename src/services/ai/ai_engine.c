/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * AI engine — LLM API client with Tool Use support and conversation memory.
 */

#include "claw_os.h"
#include "ai_engine.h"
#include "ai_memory.h"
#include "claw_tools.h"

#include <string.h>
#include <stdio.h>

#define TAG "ai"

#ifdef CLAW_PLATFORM_ESP_IDF

#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"

#define AI_API_KEY      CONFIG_CLAW_AI_API_KEY
#define AI_API_URL      CONFIG_CLAW_AI_API_URL
#define AI_MODEL        CONFIG_CLAW_AI_MODEL
#define AI_MAX_TOKENS   CONFIG_CLAW_AI_MAX_TOKENS

#define RESP_BUF_SIZE      8192
#define MAX_TOOL_ROUNDS    5
#define API_MAX_RETRIES    3
#define API_RETRY_BASE_MS  3000

static claw_mutex_t s_api_lock;

static const char *SYSTEM_PROMPT =
    "You are rt-claw, an AI assistant running on an ESP32-C3 microcontroller. "
    "You can control hardware peripherals (GPIO, sensors, etc.) through tools. "
    "When the user asks to control hardware, use the appropriate tool. "
    "Be concise — this is an embedded device with limited display. "
    "Respond in the same language the user uses.";

/* HTTP response collection context */
typedef struct {
    char *buf;
    size_t size;
    size_t len;
} resp_ctx_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    resp_ctx_t *ctx = (resp_ctx_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        size_t avail = ctx->size - ctx->len - 1;
        size_t copy = (evt->data_len < avail) ? evt->data_len : avail;
        if (copy > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, copy);
            ctx->len += copy;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

static int is_retryable_status(int status)
{
    return status == 429 || status == 500 || status == 502 ||
           status == 503 || status == 529;
}

static cJSON *do_api_call(cJSON *req_body, resp_ctx_t *ctx)
{
    char *body_str = cJSON_PrintUnformatted(req_body);
    if (!body_str) {
        return NULL;
    }

    for (int attempt = 0; attempt <= API_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            int delay = API_RETRY_BASE_MS * attempt;
            CLAW_LOGW(TAG, "retry %d/%d in %dms ...",
                      attempt, API_MAX_RETRIES, delay);
            claw_lcd_status("Retrying ...");
            claw_thread_delay_ms(delay);
        }

        ctx->len = 0;
        ctx->buf[0] = '\0';

        esp_http_client_config_t http_cfg = {
            .url = AI_API_URL,
            .method = HTTP_METHOD_POST,
            .timeout_ms = 60000,
            .event_handler = on_http_event,
            .user_data = ctx,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .buffer_size = 4096,
            .buffer_size_tx = 4096,
        };

        esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
        if (!client) {
            continue;
        }

        esp_http_client_set_header(client, "Content-Type",
                                   "application/json");
        esp_http_client_set_header(client, "x-api-key", AI_API_KEY);
        esp_http_client_set_header(client, "anthropic-version",
                                   "2023-06-01");
        esp_http_client_set_post_field(client, body_str, strlen(body_str));

        esp_err_t err = esp_http_client_perform(client);
        int status = 0;
        if (err == ESP_OK) {
            status = esp_http_client_get_status_code(client);
        }
        esp_http_client_cleanup(client);

        if (err != ESP_OK) {
            CLAW_LOGE(TAG, "HTTP failed: %s", esp_err_to_name(err));
            continue;
        }

        if (is_retryable_status(status)) {
            CLAW_LOGW(TAG, "HTTP %d (transient)", status);
            continue;
        }

        if (status != 200) {
            CLAW_LOGE(TAG, "HTTP %d: %.200s", status, ctx->buf);
            cJSON_free(body_str);
            return NULL;
        }

        cJSON_free(body_str);
        return cJSON_Parse(ctx->buf);
    }

    CLAW_LOGE(TAG, "API call failed after %d retries", API_MAX_RETRIES);
    cJSON_free(body_str);
    return NULL;
}

/* Build the base request object (reused across tool-use rounds) */
static cJSON *build_request(const char *system_prompt,
                            cJSON *messages, cJSON *tools)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", AI_MODEL);
    cJSON_AddNumberToObject(req, "max_tokens", AI_MAX_TOKENS);
    cJSON_AddStringToObject(req, "system", system_prompt);

    /* Attach references (not copies) — caller manages lifetime */
    cJSON_AddItemReferenceToObject(req, "messages", messages);
    if (tools && cJSON_GetArraySize(tools) > 0) {
        cJSON_AddItemReferenceToObject(req, "tools", tools);
    }

    return req;
}

/* Build system prompt with long-term memory context appended */
static char *build_system_prompt(void)
{
    char *ltm_ctx = ai_ltm_build_context();
    if (!ltm_ctx) {
        /* No LTM, return a copy of the base prompt */
        size_t len = strlen(SYSTEM_PROMPT);
        char *p = claw_malloc(len + 1);
        if (p) {
            memcpy(p, SYSTEM_PROMPT, len + 1);
        }
        return p;
    }

    size_t base_len = strlen(SYSTEM_PROMPT);
    size_t ctx_len = strlen(ltm_ctx);
    char *p = claw_malloc(base_len + ctx_len + 1);
    if (p) {
        memcpy(p, SYSTEM_PROMPT, base_len);
        memcpy(p + base_len, ltm_ctx, ctx_len + 1);
    }
    claw_free(ltm_ctx);
    return p;
}

/* Extract concatenated text blocks from response content array */
static void extract_text(const cJSON *content, char *reply,
                         size_t reply_size)
{
    size_t offset = 0;

    for (int i = 0; i < cJSON_GetArraySize(content); i++) {
        cJSON *block = cJSON_GetArrayItem(content, i);
        cJSON *type = cJSON_GetObjectItem(block, "type");
        if (!type || !cJSON_IsString(type)) {
            continue;
        }
        if (strcmp(type->valuestring, "text") == 0) {
            cJSON *text = cJSON_GetObjectItem(block, "text");
            if (text && cJSON_IsString(text)) {
                size_t avail = reply_size - offset - 1;
                if (avail > 0) {
                    size_t n = snprintf(reply + offset, avail + 1,
                                        "%s", text->valuestring);
                    offset += (n < avail) ? n : avail;
                }
            }
        }
    }
    if (offset == 0 && reply_size > 0) {
        reply[0] = '\0';
    }
}

/* Execute all tool_use blocks, return a tool_result content array */
static cJSON *execute_tool_calls(const cJSON *content)
{
    cJSON *results = cJSON_CreateArray();

    for (int i = 0; i < cJSON_GetArraySize(content); i++) {
        cJSON *block = cJSON_GetArrayItem(content, i);
        cJSON *type = cJSON_GetObjectItem(block, "type");
        if (!type || strcmp(type->valuestring, "tool_use") != 0) {
            continue;
        }

        cJSON *id = cJSON_GetObjectItem(block, "id");
        cJSON *name = cJSON_GetObjectItem(block, "name");
        cJSON *input = cJSON_GetObjectItem(block, "input");

        const char *tool_name = name ? name->valuestring : "unknown";
        const char *tool_id = id ? id->valuestring : "";

        CLAW_LOGI(TAG, "tool_use: %s", tool_name);

        cJSON *result_obj = cJSON_CreateObject();
        const claw_tool_t *tool = claw_tool_find(tool_name);

        if (tool) {
            tool->execute(input, result_obj);
        } else {
            cJSON_AddStringToObject(result_obj, "error",
                                    "tool not found");
            CLAW_LOGE(TAG, "unknown tool: %s", tool_name);
        }

        /* Wrap as tool_result */
        char *result_str = cJSON_PrintUnformatted(result_obj);
        cJSON_Delete(result_obj);

        cJSON *tr = cJSON_CreateObject();
        cJSON_AddStringToObject(tr, "type", "tool_result");
        cJSON_AddStringToObject(tr, "tool_use_id", tool_id);
        cJSON_AddStringToObject(tr, "content",
                                result_str ? result_str : "{}");
        cJSON_AddItemToArray(results, tr);

        if (result_str) {
            cJSON_free(result_str);
        }
    }
    return results;
}

/*
 * Core API call loop with tool-use support.
 * Takes a pre-built messages array and runs the tool-use loop.
 * Does NOT touch conversation memory.
 */
static int ai_chat_with_messages(const char *system_prompt,
                                 cJSON *messages, cJSON *tools,
                                 char *reply, size_t reply_size)
{
    char *resp_buf = claw_malloc(RESP_BUF_SIZE);
    if (!resp_buf) {
        CLAW_LOGE(TAG, "no memory for response buffer");
        return CLAW_ERROR;
    }

    resp_ctx_t ctx = { .buf = resp_buf, .size = RESP_BUF_SIZE, .len = 0 };
    int ret = CLAW_ERROR;
    reply[0] = '\0';

    claw_lcd_status("Thinking ...");
    claw_lcd_progress(0);

    for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {
        cJSON *req = build_request(system_prompt, messages, tools);
        cJSON *resp = do_api_call(req, &ctx);
        cJSON_Delete(req);

        if (!resp) {
            snprintf(reply, reply_size, "[API request failed]");
            claw_lcd_status("API request failed");
            claw_lcd_progress(0);
            break;
        }

        /* Check for API error */
        cJSON *err_obj = cJSON_GetObjectItem(resp, "error");
        if (err_obj) {
            cJSON *err_msg = cJSON_GetObjectItem(err_obj, "message");
            if (err_msg && cJSON_IsString(err_msg)) {
                snprintf(reply, reply_size, "[API error: %s]",
                         err_msg->valuestring);
            }
            cJSON_Delete(resp);
            break;
        }

        cJSON *content = cJSON_GetObjectItem(resp, "content");
        cJSON *stop = cJSON_GetObjectItem(resp, "stop_reason");
        const char *stop_reason = (stop && cJSON_IsString(stop))
                                  ? stop->valuestring : "";

        /* Extract any text in this response */
        extract_text(content, reply, reply_size);

        if (strcmp(stop_reason, "tool_use") == 0) {
            CLAW_LOGI(TAG, "tool_use round %d", round + 1);
            claw_lcd_status("Tool call ...");
            claw_lcd_progress((round + 1) * 100 / MAX_TOOL_ROUNDS);

            /* Append assistant message (with full content) */
            cJSON *asst_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(asst_msg, "role", "assistant");
            cJSON_AddItemToObject(asst_msg, "content",
                                  cJSON_Duplicate(content, 1));
            cJSON_AddItemToArray(messages, asst_msg);

            /* Execute tools and append result message */
            cJSON *tool_results = execute_tool_calls(content);
            cJSON *result_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(result_msg, "role", "user");
            cJSON_AddItemToObject(result_msg, "content", tool_results);
            cJSON_AddItemToArray(messages, result_msg);

            cJSON_Delete(resp);
            continue;
        }

        /* end_turn or other stop reason — done */
        ret = CLAW_OK;
        claw_lcd_status("Done");
        claw_lcd_progress(100);
        cJSON_Delete(resp);
        break;
    }

    claw_free(resp_buf);
    return ret;
}

int ai_chat(const char *user_msg, char *reply, size_t reply_size)
{
    if (!user_msg || !reply || reply_size == 0) {
        return CLAW_ERROR;
    }

    if (strlen(AI_API_KEY) == 0) {
        snprintf(reply, reply_size,
                 "[no API key — set via idf.py menuconfig]");
        return CLAW_ERROR;
    }

    claw_mutex_lock(s_api_lock, CLAW_WAIT_FOREVER);

    /* Add user message to memory */
    ai_memory_add_message("user", user_msg);

    /* Build system prompt with long-term memory context */
    char *sys_prompt = build_system_prompt();
    if (!sys_prompt) {
        claw_mutex_unlock(s_api_lock);
        return CLAW_ERROR;
    }

    /* Build messages from conversation memory */
    cJSON *messages = ai_memory_build_messages();
    cJSON *tools = claw_tools_to_json();

    /* Track original message count to detect tool-round additions */
    int orig_msg_count = cJSON_GetArraySize(messages);

    int ret = ai_chat_with_messages(sys_prompt, messages, tools,
                                     reply, reply_size);

    /*
     * Store tool-use intermediate messages in memory first.
     * Messages appended during tool rounds start at orig_msg_count.
     */
    int msg_count = cJSON_GetArraySize(messages);
    for (int i = orig_msg_count; i < msg_count; i++) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (!role || !content) {
            continue;
        }
        char *content_str = cJSON_PrintUnformatted(content);
        if (content_str) {
            ai_memory_add_message(role->valuestring, content_str);
            cJSON_free(content_str);
        }
    }

    /* Store final assistant text reply in memory (after tool messages) */
    if (ret == CLAW_OK && reply[0] != '\0') {
        ai_memory_add_message("assistant", reply);
    }

    claw_free(sys_prompt);
    cJSON_Delete(messages);
    if (tools) {
        cJSON_Delete(tools);
    }

    claw_mutex_unlock(s_api_lock);
    return ret;
}

int ai_chat_raw(const char *prompt, char *reply, size_t reply_size)
{
    if (!prompt || !reply || reply_size == 0) {
        return CLAW_ERROR;
    }

    if (strlen(AI_API_KEY) == 0) {
        snprintf(reply, reply_size,
                 "[no API key — set via idf.py menuconfig]");
        return CLAW_ERROR;
    }

    claw_mutex_lock(s_api_lock, CLAW_WAIT_FOREVER);

    char *sys_prompt = build_system_prompt();
    if (!sys_prompt) {
        claw_mutex_unlock(s_api_lock);
        return CLAW_ERROR;
    }

    /* Build single-message array (no memory) */
    cJSON *messages = cJSON_CreateArray();
    cJSON *user_m = cJSON_CreateObject();
    cJSON_AddStringToObject(user_m, "role", "user");
    cJSON_AddStringToObject(user_m, "content", prompt);
    cJSON_AddItemToArray(messages, user_m);

    cJSON *tools = claw_tools_to_json();

    int ret = ai_chat_with_messages(sys_prompt, messages, tools,
                                     reply, reply_size);

    claw_free(sys_prompt);
    cJSON_Delete(messages);
    if (tools) {
        cJSON_Delete(tools);
    }

    claw_mutex_unlock(s_api_lock);
    return ret;
}

int ai_engine_init(void)
{
    s_api_lock = claw_mutex_create("ai_api");
    ai_memory_init();
    ai_ltm_init();

    if (strlen(AI_API_KEY) == 0) {
        CLAW_LOGW(TAG, "no API key — set via: idf.py menuconfig");
        claw_lcd_status("No API key configured");
    } else {
        CLAW_LOGI(TAG, "engine ready (model: %s, tools: %d)",
                  AI_MODEL, claw_tools_count());
        claw_lcd_status("AI ready - waiting for input");
    }
    return CLAW_OK;
}

#else /* non-ESP-IDF platforms */

int ai_chat(const char *user_msg, char *reply, size_t reply_size)
{
    (void)user_msg;
    snprintf(reply, reply_size, "[AI not available on this platform]");
    return CLAW_ERROR;
}

int ai_chat_raw(const char *prompt, char *reply, size_t reply_size)
{
    (void)prompt;
    snprintf(reply, reply_size, "[AI not available on this platform]");
    return CLAW_ERROR;
}

int ai_engine_init(void)
{
    ai_memory_init();
    ai_ltm_init();
    CLAW_LOGI(TAG, "engine initialized (inference backend pending)");
    return CLAW_OK;
}

#endif
