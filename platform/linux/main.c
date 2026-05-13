/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Platform entry for Linux native.
 */

#include <stdio.h>
#include <signal.h>

#include "osal/claw_os.h"
#include "osal/claw_kv.h"
#include "platform/board.h"
#include "claw/shell/shell_commands.h"
#ifdef CONFIG_RTCLAW_VOICE_ENABLE
#include "claw/services/voice/voice_service.h"
#endif
#ifdef CONFIG_RTCLAW_LINUX_WEB_VOICE_ENABLE
#include "platform/linux/web_voice_server.h"
#endif

extern int claw_init(void);
extern void claw_deinit(void);
extern void linux_shell_loop(void);

volatile sig_atomic_t g_exit_flag;

static void signal_handler(int sig)
{
    (void)sig;
    g_exit_flag = 1;
}

int main(void)
{
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("rt-claw: Linux native - Real-Time Claw\n");

    claw_kv_init();
    board_early_init();
    shell_nvs_config_load();
    claw_init();
#ifdef CONFIG_RTCLAW_LINUX_WEB_VOICE_ENABLE
    if (web_voice_server_init() == CLAW_OK && voice_config_get_enabled()) {
        web_voice_server_start();
    }
#endif
    linux_shell_loop();

#ifdef CONFIG_RTCLAW_LINUX_WEB_VOICE_ENABLE
    web_voice_server_stop();
#endif
    claw_deinit();

    return 0;
}
