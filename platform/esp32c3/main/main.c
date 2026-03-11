/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * Platform entry for ESP32-C3 / ESP-IDF + FreeRTOS.
 */

#include "claw_os.h"
#include "claw_init.h"
#include "ai_engine.h"

#include <stdio.h>
#include <string.h>

#define TAG         "main"
#define INPUT_SIZE  256
#define REPLY_SIZE  4096

static void chat_loop(void)
{
    char input[INPUT_SIZE];
    char *reply = claw_malloc(REPLY_SIZE);

    if (!reply) {
        CLAW_LOGE(TAG, "no memory for reply buffer");
        return;
    }

    claw_log_raw("\n");
    claw_log_raw("=================================\n");
    claw_log_raw("  rt-claw AI Chat  (type 'exit' to quit)\n");
    claw_log_raw("=================================\n");

    while (1) {
        claw_log_raw("\nYou> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
            break;

        /* Strip trailing newline/CR */
        input[strcspn(input, "\r\n")] = '\0';

        if (input[0] == '\0')
            continue;
        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0)
            break;

        if (ai_chat(input, reply, REPLY_SIZE) == CLAW_OK)
            claw_log_raw("\nAssistant> %s\n", reply);
        else
            claw_log_raw("\n[error] %s\n", reply);
    }

    claw_free(reply);
    claw_log_raw("Chat ended.\n");
}

void app_main(void)
{
    claw_init();
    chat_loop();

    /* Keep main task alive */
    while (1) {
        claw_thread_delay_ms(1000);
    }
}
