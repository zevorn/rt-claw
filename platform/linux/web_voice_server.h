/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef PLATFORM_LINUX_WEB_VOICE_SERVER_H
#define PLATFORM_LINUX_WEB_VOICE_SERVER_H

#include "osal/claw_os.h"
#include "claw/core/errno.h"

#ifdef __cplusplus
extern "C" {
#endif

int web_voice_server_init(void);
int web_voice_server_start(void);
void web_voice_server_stop(void);
int web_voice_server_running(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_LINUX_WEB_VOICE_SERVER_H */
