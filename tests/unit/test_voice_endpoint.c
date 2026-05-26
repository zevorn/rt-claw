/* SPDX-License-Identifier: MIT */

#include "framework/test.h"

#ifdef CONFIG_RTCLAW_VOICE_ENABLE

#include "claw/services/voice/voice_endpoint.h"

#include <string.h>

#define TEST_VOICE_SESSION_ID 42

static int s_state_calls;
static int s_transcript_calls;
static int s_assistant_calls;
static int s_tts_calls;
static int s_tts_done_calls;
static int s_error_calls;
static int s_last_session_id;
static int s_last_state;
static char s_last_text[64];
static size_t s_last_audio_len;

static void reset_backend_state(void)
{
    s_state_calls = 0;
    s_transcript_calls = 0;
    s_assistant_calls = 0;
    s_tts_calls = 0;
    s_tts_done_calls = 0;
    s_error_calls = 0;
    s_last_session_id = -1;
    s_last_state = -1;
    s_last_text[0] = '\0';
    s_last_audio_len = 0;
}

static claw_err_t test_send_state(int session_id, int state,
                                  const char *detail)
{
    s_state_calls++;
    s_last_session_id = session_id;
    s_last_state = state;
    snprintf(s_last_text, sizeof(s_last_text), "%s", detail ? detail : "");
    return CLAW_OK;
}

static claw_err_t test_send_transcript(int session_id, const char *text)
{
    s_transcript_calls++;
    s_last_session_id = session_id;
    snprintf(s_last_text, sizeof(s_last_text), "%s", text ? text : "");
    return CLAW_OK;
}

static claw_err_t test_send_assistant_text(int session_id, const char *text)
{
    s_assistant_calls++;
    s_last_session_id = session_id;
    snprintf(s_last_text, sizeof(s_last_text), "%s", text ? text : "");
    return CLAW_OK;
}

static claw_err_t test_send_tts_audio(int session_id,
                                      const void *data,
                                      size_t data_len,
                                      const char *mime_type)
{
    (void)data;
    s_tts_calls++;
    s_last_session_id = session_id;
    s_last_audio_len = data_len;
    snprintf(s_last_text, sizeof(s_last_text), "%s",
             mime_type ? mime_type : "");
    return CLAW_OK;
}

static claw_err_t test_send_tts_done(int session_id)
{
    s_tts_done_calls++;
    s_last_session_id = session_id;
    return CLAW_OK;
}

static claw_err_t test_send_error(int session_id, const char *message)
{
    s_error_calls++;
    s_last_session_id = session_id;
    snprintf(s_last_text, sizeof(s_last_text), "%s",
             message ? message : "");
    return CLAW_OK;
}

static const struct voice_endpoint_backend s_backend = {
    .send_state = test_send_state,
    .send_transcript = test_send_transcript,
    .send_assistant_text = test_send_assistant_text,
    .send_tts_audio = test_send_tts_audio,
    .send_tts_done = test_send_tts_done,
    .send_error = test_send_error,
};

static void test_endpoint_attach_and_detach(void)
{
    reset_backend_state();
    voice_endpoint_detach(TEST_VOICE_SESSION_ID);

    TEST_ASSERT_EQ(voice_endpoint_attach(TEST_VOICE_SESSION_ID,
                                         &s_backend), CLAW_OK);
    TEST_ASSERT_EQ(voice_endpoint_attached(), 1);
    TEST_ASSERT_EQ(voice_endpoint_session_id(), TEST_VOICE_SESSION_ID);

    voice_endpoint_detach(TEST_VOICE_SESSION_ID);
    TEST_ASSERT_EQ(voice_endpoint_attached(), 0);
}

static void test_endpoint_routes_callbacks(void)
{
    static const uint8_t audio[] = { 1, 2, 3, 4 };

    reset_backend_state();
    TEST_ASSERT_EQ(voice_endpoint_attach(TEST_VOICE_SESSION_ID,
                                         &s_backend), CLAW_OK);

    TEST_ASSERT_EQ(voice_endpoint_send_state(VOICE_ENDPOINT_CAPTURING,
                                             "capture"), CLAW_OK);
    TEST_ASSERT_EQ(s_state_calls, 1);
    TEST_ASSERT_EQ(s_last_session_id, TEST_VOICE_SESSION_ID);
    TEST_ASSERT_EQ(s_last_state, VOICE_ENDPOINT_CAPTURING);
    TEST_ASSERT_STR_EQ(s_last_text, "capture");

    TEST_ASSERT_EQ(voice_endpoint_send_transcript("hello"), CLAW_OK);
    TEST_ASSERT_EQ(s_transcript_calls, 1);
    TEST_ASSERT_STR_EQ(s_last_text, "hello");

    TEST_ASSERT_EQ(voice_endpoint_send_assistant_text("world"), CLAW_OK);
    TEST_ASSERT_EQ(s_assistant_calls, 1);
    TEST_ASSERT_STR_EQ(s_last_text, "world");

    TEST_ASSERT_EQ(voice_endpoint_send_tts_audio(audio, sizeof(audio),
                                                 "audio/wav"), CLAW_OK);
    TEST_ASSERT_EQ(s_tts_calls, 1);
    TEST_ASSERT_EQ(s_last_audio_len, sizeof(audio));
    TEST_ASSERT_STR_EQ(s_last_text, "audio/wav");

    TEST_ASSERT_EQ(voice_endpoint_send_tts_done(), CLAW_OK);
    TEST_ASSERT_EQ(s_tts_done_calls, 1);
    TEST_ASSERT_EQ(s_last_session_id, TEST_VOICE_SESSION_ID);

    TEST_ASSERT_EQ(voice_endpoint_send_error("boom"), CLAW_OK);
    TEST_ASSERT_EQ(s_error_calls, 1);
    TEST_ASSERT_STR_EQ(s_last_text, "boom");

    voice_endpoint_detach(TEST_VOICE_SESSION_ID);
}

static void test_endpoint_tts_done_optional(void)
{
    struct voice_endpoint_backend backend = s_backend;

    reset_backend_state();
    backend.send_tts_done = NULL;
    TEST_ASSERT_EQ(voice_endpoint_attach(TEST_VOICE_SESSION_ID,
                                         &backend), CLAW_OK);
    TEST_ASSERT_EQ(voice_endpoint_send_tts_done(), CLAW_OK);
    TEST_ASSERT_EQ(s_tts_done_calls, 0);
    voice_endpoint_detach(TEST_VOICE_SESSION_ID);
}

static void test_endpoint_rejects_missing_backend(void)
{
    reset_backend_state();
    voice_endpoint_detach(TEST_VOICE_SESSION_ID);

    TEST_ASSERT_EQ(voice_endpoint_send_state(VOICE_ENDPOINT_IDLE, NULL),
                   CLAW_ERR_NOENT);
    TEST_ASSERT_EQ(voice_endpoint_send_transcript("unused"), CLAW_ERR_NOENT);
    TEST_ASSERT_EQ(voice_endpoint_send_assistant_text("unused"),
                   CLAW_ERR_NOENT);
    TEST_ASSERT_EQ(voice_endpoint_send_tts_audio("x", 1, "audio/wav"),
                   CLAW_ERR_NOENT);
    TEST_ASSERT_EQ(voice_endpoint_send_tts_done(), CLAW_ERR_NOENT);
    TEST_ASSERT_EQ(voice_endpoint_send_error("unused"), CLAW_ERR_NOENT);
}

int test_voice_endpoint_suite(void)
{
    printf("=== test_voice_endpoint ===\n");
    TEST_BEGIN();

    RUN_TEST(test_endpoint_attach_and_detach);
    RUN_TEST(test_endpoint_routes_callbacks);
    RUN_TEST(test_endpoint_tts_done_optional);
    RUN_TEST(test_endpoint_rejects_missing_backend);

    TEST_END();
}

#else

int test_voice_endpoint_suite(void)
{
    return 0;
}

#endif
