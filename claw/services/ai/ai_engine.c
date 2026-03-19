/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AI engine — LLM API client with Tool Use support and conversation memory.
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "osal/claw_net.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/services/ai/ai_memory.h"
#include "claw/tools/claw_tools.h"
#include "cJSON.h"

#ifdef CONFIG_RTCLAW_SCHED_ENABLE
#include "claw/core/scheduler.h"
#endif
#ifdef CONFIG_RTCLAW_SWARM_ENABLE
#include "claw/services/swarm/swarm.h"
#endif
#ifdef CONFIG_RTCLAW_SKILL_ENABLE
#include "claw/services/ai/ai_skill.h"
#endif
#ifdef CLAW_PLATFORM_ESP_IDF
#include "esp_system.h"
#endif

#include <string.h>
#include <stdio.h>

#define TAG "ai"

#define AI_MAX_TOKENS   CONFIG_RTCLAW_AI_MAX_TOKENS
#define AI_KEY_MAX      128
#define AI_URL_MAX      256
#define AI_MODEL_MAX    64

/*
 * Mutable config buffers — initialized from compile-time defaults,
 * overridden at runtime via ai_set_*() (e.g. from NVS).
 */
static char s_api_key[AI_KEY_MAX];
static char s_api_url[AI_URL_MAX];
static char s_model[AI_MODEL_MAX];

#ifdef CONFIG_RTCLAW_AI_CONTEXT_SIZE
#define RESP_BUF_SIZE      CONFIG_RTCLAW_AI_CONTEXT_SIZE
#else
#define RESP_BUF_SIZE      4096
#endif
#define MAX_TOOL_ROUNDS    3
#define API_MAX_RETRIES    2
#define API_RETRY_BASE_MS  2000

/*
 * Per-caller state: callers set these before ai_chat(), which
 * snapshots them into the request struct for the worker thread.
 */
static ai_status_cb_t s_status_cb;
static char s_channel_hint[512];
static int s_channel = AI_CHANNEL_SHELL;

/*
 * API format: 0 = Claude (Anthropic), 1 = OpenAI-compatible.
 * Auto-detected from model name in ai_engine_init().
 */
static int s_openai_compat;

/* ---- Request queue ---- */

struct ai_request {
    const char     *input;
    char           *reply;
    size_t          reply_size;
    int             channel;
    char            channel_hint[512];
    ai_status_cb_t  status_cb;
    int             raw;           /* 0 = ai_chat, 1 = ai_chat_raw */
    struct claw_sem *done;
    int             result;
};

static struct claw_mq *s_ai_queue;
static struct claw_thread *s_worker_thread;

/* Worker-local active callback (set per request) */
static ai_status_cb_t s_active_cb;

static inline void notify_status(int st, const char *detail)
{
    if (s_active_cb) {
        s_active_cb(st, detail);
    }
}

void ai_set_status_cb(ai_status_cb_t cb)
{
    s_status_cb = cb;
}

void ai_set_channel_hint(const char *hint)
{
    if (hint) {
        snprintf(s_channel_hint, sizeof(s_channel_hint), "%s", hint);
    } else {
        s_channel_hint[0] = '\0';
    }
}

void ai_set_channel(int channel_id)
{
    if (channel_id >= 0 && channel_id < AI_CHANNEL_MAX) {
        s_channel = channel_id;
    }
}

int ai_get_channel(void)
{
    return s_channel;
}

static const char *SYSTEM_PROMPT =
    "You are rt-claw, an AI assistant running on an embedded RTOS device. "
    "You can control hardware peripherals (GPIO, sensors, etc.) through tools. "
    "When the user asks to control hardware, use the appropriate tool. "
    "Be concise — this is an embedded device with limited display. "
    "Respond in the same language the user uses.";

static int is_retryable_status(int status)
{
    return status == 429 || status == 500 || status == 502 ||
           status == 503 || status == 529;
}

static cJSON *do_api_call(cJSON *req_body)
{
    char *body_str = cJSON_PrintUnformatted(req_body);
    if (!body_str) {
        return NULL;
    }

    char *resp_buf = claw_malloc(RESP_BUF_SIZE);
    if (!resp_buf) {
        cJSON_free(body_str);
        return NULL;
    }

    /* OpenAI: "Authorization: Bearer sk-..." */
    static char s_auth_bearer[AI_KEY_MAX + 8];
    int hdr_count;
    claw_net_header_t headers_claude[] = {
        { "Content-Type",      "application/json" },
        { "x-api-key",         s_api_key },
        { "anthropic-version", "2023-06-01" },
    };
    claw_net_header_t headers_openai[2];

    claw_net_header_t *headers;
    if (s_openai_compat) {
        snprintf(s_auth_bearer, sizeof(s_auth_bearer),
                 "Bearer %s", s_api_key);
        headers_openai[0] = (claw_net_header_t)
            { "Content-Type",  "application/json" };
        headers_openai[1] = (claw_net_header_t)
            { "Authorization", s_auth_bearer };
        headers = headers_openai;
        hdr_count = 2;
    } else {
        headers = headers_claude;
        hdr_count = 3;
    }

    for (int attempt = 0; attempt <= API_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            int delay = API_RETRY_BASE_MS * attempt;
            CLAW_LOGW(TAG, "retry %d/%d in %dms ...",
                      attempt, API_MAX_RETRIES, delay);
            claw_lcd_status("Retrying ...");
            claw_thread_delay_ms(delay);
        }

        size_t resp_len = 0;
        int status = claw_net_post(s_api_url, headers, hdr_count,
                                    body_str, strlen(body_str),
                                    resp_buf, RESP_BUF_SIZE, &resp_len);

        if (status < 0) {
            CLAW_LOGE(TAG, "HTTP transport failed");
            continue;
        }

        if (is_retryable_status(status)) {
            CLAW_LOGW(TAG, "HTTP %d (transient)", status);
            continue;
        }

        if (status != 200) {
            CLAW_LOGE(TAG, "HTTP %d: %.200s", status, resp_buf);
            cJSON_free(body_str);
            claw_free(resp_buf);
            return NULL;
        }

        cJSON_free(body_str);
        cJSON *result = cJSON_Parse(resp_buf);
        claw_free(resp_buf);
        return result;
    }

    CLAW_LOGE(TAG, "API call failed after %d retries", API_MAX_RETRIES);
    cJSON_free(body_str);
    claw_free(resp_buf);
    return NULL;
}

/* ---- Shared AI engine logic ---- */

/*
 * Wrap Claude tool schema into OpenAI format:
 *   { name, description, input_schema }
 *   → { type: "function", function: { name, description, parameters } }
 */
static cJSON *wrap_tools_openai(cJSON *claude_tools)
{
    cJSON *arr = cJSON_CreateArray();
    int n = cJSON_GetArraySize(claude_tools);
    for (int i = 0; i < n; i++) {
        cJSON *ct = cJSON_GetArrayItem(claude_tools, i);
        cJSON *wrapper = cJSON_CreateObject();
        cJSON_AddStringToObject(wrapper, "type", "function");

        cJSON *fn = cJSON_CreateObject();
        cJSON *name = cJSON_GetObjectItem(ct, "name");
        cJSON *desc = cJSON_GetObjectItem(ct, "description");
        cJSON *schema = cJSON_GetObjectItem(ct, "input_schema");
        if (name) {
            cJSON_AddStringToObject(fn, "name", name->valuestring);
        }
        if (desc) {
            cJSON_AddStringToObject(fn, "description",
                                    desc->valuestring);
        }
        if (schema) {
            cJSON_AddItemToObject(fn, "parameters",
                                  cJSON_Duplicate(schema, 1));
        }
        cJSON_AddItemToObject(wrapper, "function", fn);
        cJSON_AddItemToArray(arr, wrapper);
    }
    return arr;
}

static cJSON *build_request(const char *system_prompt,
                            cJSON *messages, cJSON *tools)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", s_model);
    cJSON_AddNumberToObject(req, "max_tokens", AI_MAX_TOKENS);

    if (s_openai_compat) {
        /*
         * OpenAI format: system prompt as first message,
         * tools wrapped in { type: "function", function: {...} }.
         */
        cJSON *msgs = cJSON_CreateArray();
        cJSON *sys_m = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_m, "role", "system");
        cJSON_AddStringToObject(sys_m, "content", system_prompt);
        cJSON_AddItemToArray(msgs, sys_m);

        int n = cJSON_GetArraySize(messages);
        for (int i = 0; i < n; i++) {
            cJSON_AddItemToArray(msgs,
                cJSON_Duplicate(cJSON_GetArrayItem(messages, i), 1));
        }
        cJSON_AddItemToObject(req, "messages", msgs);

        if (tools && cJSON_GetArraySize(tools) > 0) {
            cJSON *oai_tools = wrap_tools_openai(tools);
            cJSON_AddItemToObject(req, "tools", oai_tools);
        }
    } else {
        /* Claude format: system as separate field */
        cJSON_AddStringToObject(req, "system", system_prompt);
        cJSON_AddItemReferenceToObject(req, "messages", messages);
        if (tools && cJSON_GetArraySize(tools) > 0) {
            cJSON_AddItemReferenceToObject(req, "tools", tools);
        }
    }

    return req;
}

/*
 * Build "[Device Context]" section with runtime device state.
 * Returns bytes written into buf.
 */
static int build_device_context(char *buf, size_t size)
{
    uint32_t uptime_s = claw_tick_ms() / 1000;
    uint32_t hrs = uptime_s / 3600;
    uint32_t mins = (uptime_s % 3600) / 60;

    int off = snprintf(buf, size,
                       "\n\n[Device Context]\n"
                       "- Platform: "
#if defined(CONFIG_IDF_TARGET_ESP32C3)
                       "ESP32-C3"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
                       "ESP32-S3"
#elif defined(CLAW_PLATFORM_RTTHREAD)
                       "vexpress-a9 (RT-Thread)"
#else
                       "unknown"
#endif
                       "\n- Uptime: %uh %um\n",
                       (unsigned)hrs, (unsigned)mins);

#ifdef CLAW_PLATFORM_ESP_IDF
    off += snprintf(buf + off, size - off,
                    "- Free heap: %u bytes\n",
                    (unsigned)esp_get_free_heap_size());
#endif

#ifdef CONFIG_RTCLAW_SCHED_ENABLE
    off += snprintf(buf + off, size - off,
                    "- Scheduled tasks: %d active\n",
                    sched_task_count());
#endif

#ifdef CONFIG_RTCLAW_SWARM_ENABLE
    off += snprintf(buf + off, size - off,
                    "- Swarm nodes: %d online\n",
                    swarm_node_count());
#endif

    off += snprintf(buf + off, size - off,
                    "- Conversation memory: %d messages, "
                    "long-term: %d facts\n",
                    ai_memory_count(), ai_ltm_count());

    return off;
}

static char *build_system_prompt(void)
{
    char dev_ctx[512];
    int dev_len = build_device_context(dev_ctx, sizeof(dev_ctx));

    size_t base_len = strlen(SYSTEM_PROMPT);
    size_t hint_len = strlen(s_channel_hint);
    char *ltm_ctx = ai_ltm_build_context();
    size_t ltm_len = ltm_ctx ? strlen(ltm_ctx) : 0;

#ifdef CONFIG_RTCLAW_SKILL_ENABLE
    char *skill_ctx = ai_skill_build_summary();
#else
    char *skill_ctx = NULL;
#endif
    size_t skill_len = skill_ctx ? strlen(skill_ctx) : 0;

    size_t total = base_len + hint_len + dev_len
                   + ltm_len + skill_len + 1;
    char *p = claw_malloc(total);
    if (p) {
        size_t off = 0;
        memcpy(p + off, SYSTEM_PROMPT, base_len);
        off += base_len;
        if (hint_len > 0) {
            memcpy(p + off, s_channel_hint, hint_len);
            off += hint_len;
        }
        memcpy(p + off, dev_ctx, dev_len);
        off += dev_len;
        if (ltm_len > 0) {
            memcpy(p + off, ltm_ctx, ltm_len);
            off += ltm_len;
        }
        if (skill_len > 0) {
            memcpy(p + off, skill_ctx, skill_len);
            off += skill_len;
        }
        p[off] = '\0';
    }
    claw_free(ltm_ctx);
    claw_free(skill_ctx);
    return p;
}

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
        notify_status(AI_STATUS_TOOL_CALL, tool_name);

        cJSON *result_obj = cJSON_CreateObject();
        const claw_tool_t *tool = claw_tool_find(tool_name);

        if (tool) {
            tool->execute(input, result_obj);
        }
#ifdef CONFIG_RTCLAW_SWARM_ENABLE
        else {
            /*
             * Tool not available locally — try remote execution
             * via swarm RPC.  The swarm picks a peer node that
             * advertises the matching capability bit.
             */
            char *params_str = cJSON_PrintUnformatted(input);
            char remote_buf[SWARM_RPC_PAYLOAD_MAX];
            if (swarm_rpc_call(tool_name,
                               params_str ? params_str : "{}",
                               remote_buf,
                               sizeof(remote_buf)) == CLAW_OK) {
                cJSON *parsed = cJSON_Parse(remote_buf);
                if (parsed) {
                    cJSON_Delete(result_obj);
                    result_obj = parsed;
                } else {
                    cJSON_AddStringToObject(result_obj, "result",
                                            remote_buf);
                }
                CLAW_LOGI(TAG, "remote tool ok: %s", tool_name);
            } else {
                cJSON_AddStringToObject(result_obj, "error",
                                        "tool not available "
                                        "(local or swarm)");
                CLAW_LOGE(TAG, "tool not found: %s", tool_name);
            }
            if (params_str) {
                cJSON_free(params_str);
            }
        }
#else
        else {
            cJSON_AddStringToObject(result_obj, "error",
                                    "tool not found");
            CLAW_LOGE(TAG, "unknown tool: %s", tool_name);
        }
#endif

        char *result_str = cJSON_PrintUnformatted(result_obj);
        cJSON_Delete(result_obj);

        /*
         * Truncate tool results to save memory.  HTTP responses
         * can be tens of KB; the LLM only needs a summary.
         * Keep first 1500 bytes which is enough context.
         */
#define TOOL_RESULT_MAX 1500
        if (result_str && strlen(result_str) > TOOL_RESULT_MAX) {
            result_str[TOOL_RESULT_MAX] = '\0';
            CLAW_LOGD(TAG, "tool result truncated to %d bytes",
                      TOOL_RESULT_MAX);
        }

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

static int ai_chat_with_messages(const char *system_prompt,
                                 cJSON *messages, cJSON *tools,
                                 char *reply, size_t reply_size)
{
    int ret = CLAW_ERROR;
    reply[0] = '\0';

    claw_lcd_status("Thinking ...");
    claw_lcd_progress(0);
    notify_status(AI_STATUS_THINKING, NULL);

    for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {
        cJSON *req = build_request(system_prompt, messages, tools);
        cJSON *resp = do_api_call(req);
        cJSON_Delete(req);

        if (!resp) {
            snprintf(reply, reply_size, "[API request failed]");
            claw_lcd_status("API request failed");
            claw_lcd_progress(0);
            break;
        }

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

        int has_tool_calls = 0;

        if (s_openai_compat) {
            /* OpenAI: choices[0].message */
            cJSON *choices = cJSON_GetObjectItem(resp, "choices");
            cJSON *choice0 = choices ? cJSON_GetArrayItem(choices, 0)
                                     : NULL;
            cJSON *msg = choice0 ? cJSON_GetObjectItem(choice0,
                                                        "message")
                                 : NULL;
            cJSON *finish = choice0 ? cJSON_GetObjectItem(choice0,
                                                           "finish_reason")
                                    : NULL;
            const char *fr = (finish && cJSON_IsString(finish))
                             ? finish->valuestring : "";

            if (msg) {
                cJSON *c = cJSON_GetObjectItem(msg, "content");
                if (c && cJSON_IsString(c)) {
                    snprintf(reply, reply_size, "%s", c->valuestring);
                }
            }

            cJSON *tc = msg ? cJSON_GetObjectItem(msg, "tool_calls")
                            : NULL;
            if (tc && cJSON_GetArraySize(tc) > 0 &&
                (strcmp(fr, "tool_calls") == 0 ||
                 strcmp(fr, "stop") != 0)) {
                has_tool_calls = 1;
                CLAW_LOGI(TAG, "tool_use round %d", round + 1);
                claw_lcd_status("Tool call ...");
                claw_lcd_progress(
                    (round + 1) * 100 / MAX_TOOL_ROUNDS);

                /* Add assistant message with tool_calls */
                cJSON_AddItemToArray(messages,
                    cJSON_Duplicate(msg, 1));

                /* Execute each tool call */
                for (int t = 0; t < cJSON_GetArraySize(tc); t++) {
                    cJSON *call = cJSON_GetArrayItem(tc, t);
                    cJSON *fn = cJSON_GetObjectItem(call, "function");
                    cJSON *call_id = cJSON_GetObjectItem(call, "id");
                    const char *tname = "";
                    cJSON *args = NULL;
                    if (fn) {
                        cJSON *n = cJSON_GetObjectItem(fn, "name");
                        tname = n ? n->valuestring : "";
                        cJSON *a = cJSON_GetObjectItem(fn,
                                                        "arguments");
                        if (a && cJSON_IsString(a)) {
                            args = cJSON_Parse(a->valuestring);
                        }
                    }

                    CLAW_LOGI(TAG, "tool_use: %s", tname);
                    notify_status(AI_STATUS_TOOL_CALL, tname);

                    cJSON *res_obj = cJSON_CreateObject();
                    const claw_tool_t *tool = claw_tool_find(tname);
                    if (tool) {
                        tool->execute(args ? args : cJSON_CreateObject(),
                                      res_obj);
                    } else {
                        cJSON_AddStringToObject(res_obj, "error",
                                                "tool not found");
                    }
                    if (args) {
                        cJSON_Delete(args);
                    }

                    char *res_str = cJSON_PrintUnformatted(res_obj);
                    cJSON_Delete(res_obj);

                    /* OpenAI tool result: role=tool */
                    cJSON *tr_msg = cJSON_CreateObject();
                    cJSON_AddStringToObject(tr_msg, "role", "tool");
                    cJSON_AddStringToObject(tr_msg, "tool_call_id",
                        call_id ? call_id->valuestring : "");
                    cJSON_AddStringToObject(tr_msg, "content",
                        res_str ? res_str : "{}");
                    cJSON_AddItemToArray(messages, tr_msg);

                    if (res_str) {
                        cJSON_free(res_str);
                    }
                }

                cJSON_Delete(resp);
                notify_status(AI_STATUS_THINKING, NULL);
                continue;
            }
        } else {
            /* Claude format */
            cJSON *content = cJSON_GetObjectItem(resp, "content");
            cJSON *stop = cJSON_GetObjectItem(resp, "stop_reason");
            const char *stop_reason = (stop && cJSON_IsString(stop))
                                      ? stop->valuestring : "";

            extract_text(content, reply, reply_size);

            if (strcmp(stop_reason, "tool_use") == 0) {
                has_tool_calls = 1;
                CLAW_LOGI(TAG, "tool_use round %d", round + 1);
                claw_lcd_status("Tool call ...");
                claw_lcd_progress(
                    (round + 1) * 100 / MAX_TOOL_ROUNDS);

                cJSON *asst_msg = cJSON_CreateObject();
                cJSON_AddStringToObject(asst_msg, "role",
                                        "assistant");
                cJSON_AddItemToObject(asst_msg, "content",
                    cJSON_Duplicate(content, 1));
                cJSON_AddItemToArray(messages, asst_msg);

                cJSON *tool_results = execute_tool_calls(content);
                cJSON *result_msg = cJSON_CreateObject();
                cJSON_AddStringToObject(result_msg, "role", "user");
                cJSON_AddItemToObject(result_msg, "content",
                                      tool_results);
                cJSON_AddItemToArray(messages, result_msg);

                cJSON_Delete(resp);
                notify_status(AI_STATUS_THINKING, NULL);
                continue;
            }
        }

        if (has_tool_calls) {
            continue;
        }

        ret = CLAW_OK;
        claw_lcd_status("Done");
        claw_lcd_progress(100);
        cJSON_Delete(resp);
        break;
    }

    notify_status(AI_STATUS_DONE, NULL);
    return ret;
}

/* ---- Context compression ---- */

#define COMPRESS_THRESHOLD  6
#define COMPRESS_SUMMARY_MAX 512

static const char *COMPRESS_PROMPT =
    "Summarize the following conversation in 2-3 concise sentences. "
    "Preserve all key facts, decisions, and context. "
    "Write in the same language as the conversation.\n\n";

/*
 * Compress older messages for a channel by generating an AI summary.
 * Called on the worker thread so do_api_call() is safe.
 * On failure, does nothing (drop_oldest_pair_for handles overflow).
 */
static void try_compress_memory(int channel)
{
    int ch_count = ai_memory_count_channel(channel);
    if (ch_count < COMPRESS_THRESHOLD) {
        return;
    }

    cJSON *messages = ai_memory_build(channel);
    if (!messages) {
        return;
    }

    int total = cJSON_GetArraySize(messages);
    int keep = total / 2;
    int compress_n = total - keep;

    /* Build text from oldest messages */
    size_t text_sz = 1024;
    char *text = claw_malloc(text_sz);
    if (!text) {
        cJSON_Delete(messages);
        return;
    }

    size_t off = 0;
    for (int i = 0; i < compress_n && off < text_sz - 64; i++) {
        cJSON *m = cJSON_GetArrayItem(messages, i);
        const char *role = cJSON_GetObjectItem(m, "role")->valuestring;
        cJSON *c = cJSON_GetObjectItem(m, "content");
        const char *ct = cJSON_IsString(c) ? c->valuestring : "[...]";
        off += snprintf(text + off, text_sz - off,
                        "%s: %.*s\n", role, 200, ct);
    }

    /* Build summary request */
    size_t prompt_sz = strlen(COMPRESS_PROMPT) + off + 1;
    char *prompt = claw_malloc(prompt_sz);
    if (!prompt) {
        claw_free(text);
        cJSON_Delete(messages);
        return;
    }
    snprintf(prompt, prompt_sz, "%s%s", COMPRESS_PROMPT, text);
    claw_free(text);

    cJSON *req_msgs = cJSON_CreateArray();
    cJSON *user_m = cJSON_CreateObject();
    cJSON_AddStringToObject(user_m, "role", "user");
    cJSON_AddStringToObject(user_m, "content", prompt);
    cJSON_AddItemToArray(req_msgs, user_m);
    claw_free(prompt);

    cJSON *req_body = cJSON_CreateObject();
    cJSON_AddStringToObject(req_body, "model", s_model);
    cJSON_AddNumberToObject(req_body, "max_tokens", 256);
    cJSON_AddItemToObject(req_body, "messages", req_msgs);
    if (!s_openai_compat) {
        cJSON_AddStringToObject(req_body, "system",
                                "You are a conversation summarizer.");
    }

    cJSON *resp = do_api_call(req_body);
    cJSON_Delete(req_body);

    if (!resp) {
        cJSON_Delete(messages);
        CLAW_LOGW(TAG, "compression API call failed, skipping");
        return;
    }

    /* Extract summary text */
    char summary[COMPRESS_SUMMARY_MAX];
    summary[0] = '\0';
    if (s_openai_compat) {
        cJSON *choices = cJSON_GetObjectItem(resp, "choices");
        cJSON *c0 = choices ? cJSON_GetArrayItem(choices, 0) : NULL;
        cJSON *msg = c0 ? cJSON_GetObjectItem(c0, "message") : NULL;
        cJSON *ct = msg ? cJSON_GetObjectItem(msg, "content") : NULL;
        if (ct && cJSON_IsString(ct)) {
            snprintf(summary, sizeof(summary), "%s", ct->valuestring);
        }
    } else {
        cJSON *content = cJSON_GetObjectItem(resp, "content");
        extract_text(content, summary, sizeof(summary));
    }
    cJSON_Delete(resp);

    if (summary[0] == '\0') {
        cJSON_Delete(messages);
        return;
    }

    /*
     * Rebuild channel memory: clear old messages, add summary
     * as first user message, then re-add the newer half.
     */
    ai_memory_clear_channel(channel);

    char summary_tagged[COMPRESS_SUMMARY_MAX + 32];
    snprintf(summary_tagged, sizeof(summary_tagged),
             "[Earlier conversation summary] %s", summary);
    ai_memory_add("user", summary_tagged, channel);

    for (int i = compress_n; i < total; i++) {
        cJSON *m = cJSON_GetArrayItem(messages, i);
        const char *role = cJSON_GetObjectItem(m, "role")->valuestring;
        cJSON *c = cJSON_GetObjectItem(m, "content");
        if (cJSON_IsString(c)) {
            ai_memory_add(role, c->valuestring, channel);
        } else {
            char *s = cJSON_PrintUnformatted(c);
            if (s) {
                ai_memory_add(role, s, channel);
                cJSON_free(s);
            }
        }
    }

    cJSON_Delete(messages);
    CLAW_LOGI(TAG, "compressed %d msgs -> summary + %d msgs (ch=%d)",
              compress_n, keep, channel);
}

/* ---- Worker: process a single ai_chat request ---- */

static void do_chat(struct ai_request *req)
{
#ifdef CLAW_PLATFORM_ESP_IDF
    {
        uint32_t free_heap = esp_get_free_heap_size();
        if (free_heap < 60000 && ai_memory_count() > 2) {
            int cleared = ai_memory_count();
            ai_memory_clear();
            CLAW_LOGW(TAG, "low memory (%u bytes), cleared %d msgs",
                      (unsigned)free_heap, cleared);
            claw_lcd_status("Low memory - cleared");
        }
    }
#endif

    /* Compress before adding new message if channel is getting full */
    if (ai_memory_count_channel(req->channel) >=
        CONFIG_RTCLAW_AI_MEMORY_MAX_MSGS - 4) {
        try_compress_memory(req->channel);
    }

    ai_memory_add("user", req->input, req->channel);

    char *sys_prompt = build_system_prompt();
    if (!sys_prompt) {
        req->result = CLAW_ERROR;
        return;
    }

    cJSON *messages = ai_memory_build(req->channel);
    cJSON *tools = claw_tools_to_json();

    req->result = ai_chat_with_messages(sys_prompt, messages, tools,
                                         req->reply, req->reply_size);

    if (req->result == CLAW_OK && req->reply[0] != '\0') {
        ai_memory_add("assistant", req->reply, req->channel);
    }

    claw_free(sys_prompt);
    cJSON_Delete(messages);
    if (tools) {
        cJSON_Delete(tools);
    }
}

/* ---- Worker: process a single ai_chat_raw request ---- */

static void do_chat_raw(struct ai_request *req)
{
    char *sys_prompt = build_system_prompt();
    if (!sys_prompt) {
        req->result = CLAW_ERROR;
        return;
    }

    cJSON *messages = cJSON_CreateArray();
    cJSON *user_m = cJSON_CreateObject();
    cJSON_AddStringToObject(user_m, "role", "user");
    cJSON_AddStringToObject(user_m, "content", req->input);
    cJSON_AddItemToArray(messages, user_m);

    cJSON *tools = claw_tools_to_json_exclude("lcd_");

    req->result = ai_chat_with_messages(sys_prompt, messages, tools,
                                         req->reply, req->reply_size);

    claw_free(sys_prompt);
    cJSON_Delete(messages);
    if (tools) {
        cJSON_Delete(tools);
    }
}

/* ---- AI worker thread ---- */

static void ai_worker_thread(void *arg)
{
    (void)arg;
    struct ai_request *req;

    CLAW_LOGI(TAG, "worker started");

    while (!claw_thread_should_exit()) {
        if (claw_mq_recv(s_ai_queue, &req, sizeof(req),
                          1000) != CLAW_OK) {
            continue;
        }

        /* Apply per-request context */
        snprintf(s_channel_hint, sizeof(s_channel_hint),
                 "%s", req->channel_hint);
        s_active_cb = req->status_cb;

        CLAW_LOGD(TAG, "processing request (ch=%d, raw=%d)",
                  req->channel, req->raw);

        if (req->raw) {
            do_chat_raw(req);
        } else {
            do_chat(req);
        }

        s_active_cb = NULL;
        claw_sem_give(req->done);
    }
}

/* ---- Submit request and wait ---- */

static int submit_and_wait(struct ai_request *req)
{
    req->done = claw_sem_create("ai_done", 0);
    if (!req->done) {
        snprintf(req->reply, req->reply_size,
                 "[internal error: sem create failed]");
        return CLAW_ERROR;
    }

    struct ai_request *ptr = req;
    if (claw_mq_send(s_ai_queue, &ptr, sizeof(ptr),
                      CLAW_NO_WAIT) != CLAW_OK) {
        claw_sem_delete(req->done);
        snprintf(req->reply, req->reply_size,
                 "[AI is busy, please retry]");
        return CLAW_ERROR;
    }

    /* Wait with periodic exit check so shutdown doesn't hang */
    while (claw_sem_take(req->done, 500) != CLAW_OK) {
        if (claw_thread_should_exit()) {
            claw_sem_delete(req->done);
            snprintf(req->reply, req->reply_size,
                     "[shutdown in progress]");
            return CLAW_ERROR;
        }
    }
    claw_sem_delete(req->done);
    return req->result;
}

int ai_chat(const char *user_msg, char *reply, size_t reply_size)
{
    if (!user_msg || !reply || reply_size == 0) {
        return CLAW_ERROR;
    }

    if (strlen(s_api_key) == 0) {
        snprintf(reply, reply_size, "[no API key configured]");
        return CLAW_ERROR;
    }

    struct ai_request req = {
        .input = user_msg,
        .reply = reply,
        .reply_size = reply_size,
        .channel = s_channel,
        .status_cb = s_status_cb,
        .raw = 0,
        .result = CLAW_ERROR,
    };
    snprintf(req.channel_hint, sizeof(req.channel_hint),
             "%s", s_channel_hint);

    return submit_and_wait(&req);
}

int ai_chat_raw(const char *prompt, char *reply, size_t reply_size)
{
    if (!prompt || !reply || reply_size == 0) {
        return CLAW_ERROR;
    }

    if (strlen(s_api_key) == 0) {
        snprintf(reply, reply_size, "[no API key configured]");
        return CLAW_ERROR;
    }

    struct ai_request req = {
        .input = prompt,
        .reply = reply,
        .reply_size = reply_size,
        .channel = s_channel,
        .status_cb = s_status_cb,
        .raw = 1,
        .result = CLAW_ERROR,
    };
    snprintf(req.channel_hint, sizeof(req.channel_hint),
             "%s", s_channel_hint);

    return submit_and_wait(&req);
}

/* ---- Runtime config setters/getters ---- */

void ai_set_api_key(const char *key)
{
    if (key) {
        snprintf(s_api_key, sizeof(s_api_key), "%s", key);
    }
}

void ai_set_api_url(const char *url)
{
    if (url) {
        snprintf(s_api_url, sizeof(s_api_url), "%s", url);
    }
}

void ai_set_model(const char *model)
{
    if (model) {
        snprintf(s_model, sizeof(s_model), "%s", model);
    }
}

const char *ai_get_api_key(void)  { return s_api_key; }
const char *ai_get_api_url(void)  { return s_api_url; }
const char *ai_get_model(void)    { return s_model; }

int ai_engine_init(void)
{
    /*
     * Initialize from compile-time defaults (may be overridden
     * by platform-specific NVS load before this call).
     */
    if (s_api_key[0] == '\0') {
        snprintf(s_api_key, sizeof(s_api_key),
                 "%s", CONFIG_RTCLAW_AI_API_KEY);
    }
    if (s_api_url[0] == '\0') {
        snprintf(s_api_url, sizeof(s_api_url),
                 "%s", CONFIG_RTCLAW_AI_API_URL);
    }
    if (s_model[0] == '\0') {
        snprintf(s_model, sizeof(s_model),
                 "%s", CONFIG_RTCLAW_AI_MODEL);
    }

    s_ai_queue = claw_mq_create("ai_q",
                                sizeof(struct ai_request *),
                                CLAW_AI_QUEUE_DEPTH);
    if (!s_ai_queue) {
        CLAW_LOGE(TAG, "request queue create failed");
        return CLAW_ERROR;
    }

    if (ai_memory_init() != CLAW_OK) {
        CLAW_LOGW(TAG, "memory init failed");
    }
    if (ai_ltm_init() != CLAW_OK) {
        CLAW_LOGW(TAG, "ltm init failed");
    }

    /* Auto-detect API format from model name */
    s_openai_compat = (strncmp(s_model, "claude", 6) != 0);
    CLAW_LOGI(TAG, "api format: %s",
              s_openai_compat ? "openai" : "claude");

    s_worker_thread = claw_thread_create("ai_worker", ai_worker_thread,
                                          NULL, CLAW_AI_WORKER_STACK,
                                          CLAW_AI_WORKER_PRIO);
    if (!s_worker_thread) {
        CLAW_LOGE(TAG, "worker thread create failed");
        return CLAW_ERROR;
    }

    if (strlen(s_api_key) == 0) {
        CLAW_LOGW(TAG, "no API key configured");
        claw_lcd_status("No API key configured");
    } else {
        CLAW_LOGI(TAG, "engine ready (model: %s, tools: %d)",
                  s_model, claw_tools_count());
        claw_lcd_status("AI ready - waiting for input");
    }
    return CLAW_OK;
}

void ai_engine_stop(void)
{
    claw_thread_delete(s_worker_thread);
    s_worker_thread = NULL;

    if (s_ai_queue) {
        claw_mq_delete(s_ai_queue);
        s_ai_queue = NULL;
    }

    CLAW_LOGI(TAG, "stopped");
}

int ai_ping(void)
{
    if (s_api_key[0] == '\0' || s_api_url[0] == '\0') {
        return CLAW_ERROR;
    }

    /*
     * Build a minimal request — max_tokens=1, single "ping" message.
     * Does NOT acquire s_api_lock so it never blocks ai_chat().
     */
    char req[256];
    if (s_openai_compat) {
        snprintf(req, sizeof(req),
            "{\"model\":\"%s\","
            "\"messages\":[{\"role\":\"user\",\"content\":\"ping\"}],"
            "\"max_tokens\":1}", s_model);
    } else {
        snprintf(req, sizeof(req),
            "{\"model\":\"%s\","
            "\"messages\":[{\"role\":\"user\",\"content\":\"ping\"}],"
            "\"max_tokens\":1}", s_model);
    }

    /* Reuse the same header setup as do_api_call */
    char auth_buf[AI_KEY_MAX + 8];
    claw_net_header_t headers[3];
    int hdr_count;

    if (s_openai_compat) {
        snprintf(auth_buf, sizeof(auth_buf), "Bearer %s", s_api_key);
        headers[0] = (claw_net_header_t)
            { "Content-Type",  "application/json" };
        headers[1] = (claw_net_header_t)
            { "Authorization", auth_buf };
        hdr_count = 2;
    } else {
        headers[0] = (claw_net_header_t)
            { "Content-Type",      "application/json" };
        headers[1] = (claw_net_header_t)
            { "x-api-key",         s_api_key };
        headers[2] = (claw_net_header_t)
            { "anthropic-version", "2023-06-01" };
        hdr_count = 3;
    }

    char resp[256];
    size_t resp_len = 0;
    int status = claw_net_post(s_api_url, headers, hdr_count,
                                req, strlen(req),
                                resp, sizeof(resp), &resp_len);

    /* Any HTTP response means the API is reachable */
    return (status > 0) ? CLAW_OK : CLAW_ERROR;
}

/* OOP service registration */
#include "claw/core/claw_service.h"
static const char *ai_deps[] = { "gateway", "net", NULL };
CLAW_DEFINE_SIMPLE_SERVICE(ai_engine, "ai_engine",
    ai_engine_init, NULL, ai_engine_stop, ai_deps);
