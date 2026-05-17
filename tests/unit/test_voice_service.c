/* SPDX-License-Identifier: MIT */

#include "framework/test.h"

#ifdef CONFIG_RTCLAW_VOICE_ENABLE

#include "claw/services/voice/voice_service.h"

#define TEST_VOICE_SESSION_ID 77

static claw_err_t test_send_state(int session_id, int state,
                                  const char *detail)
{
    (void)session_id;
    (void)state;
    (void)detail;
    return CLAW_OK;
}

static const struct voice_endpoint_backend s_backend = {
    .send_state = test_send_state,
};

static void test_disabled_attach_is_retained_on_enable(void)
{
    struct voice_endpoint_event event = {
        .session_id = TEST_VOICE_SESSION_ID,
        .type = VOICE_ENDPOINT_EVENT_ATTACH,
    };

    voice_config_set_enabled(0);
    TEST_ASSERT_EQ(voice_service_init(), CLAW_OK);
    TEST_ASSERT_EQ(voice_service_start(), CLAW_OK);
    TEST_ASSERT_EQ(voice_endpoint_attach(TEST_VOICE_SESSION_ID,
                                         &s_backend), CLAW_OK);
    TEST_ASSERT_EQ(voice_submit_event(&event), CLAW_OK);
    claw_thread_delay_ms(100);

    voice_config_set_enabled(1);
    claw_thread_delay_ms(100);

    TEST_ASSERT_EQ(voice_state_get(), VOICE_ENDPOINT_SESSION_READY);

    voice_endpoint_detach(TEST_VOICE_SESSION_ID);
    voice_service_stop();
}

static void test_voice_config_snapshot_copies_runtime_config(void)
{
    voice_runtime_config_t cfg;

    TEST_ASSERT_EQ(voice_config_set_string("endpoint_backend", "local"),
                   CLAW_OK);
    TEST_ASSERT_EQ(voice_config_set_string("input_sample_rate", "22050"),
                   CLAW_OK);
    TEST_ASSERT_EQ(voice_config_set_string("input_channels", "2"), CLAW_OK);
    TEST_ASSERT_EQ(voice_config_snapshot(&cfg), CLAW_OK);

    TEST_ASSERT_STR_EQ(cfg.endpoint_backend, "local");
    TEST_ASSERT_EQ(cfg.input_sample_rate, 22050);
    TEST_ASSERT_EQ(cfg.input_channels, 2);
}

int test_voice_service_suite(void)
{
    printf("=== test_voice_service ===\n");
    TEST_BEGIN();

    RUN_TEST(test_disabled_attach_is_retained_on_enable);
    RUN_TEST(test_voice_config_snapshot_copies_runtime_config);

    TEST_END();
}

#else

int test_voice_service_suite(void)
{
    return 0;
}

#endif
