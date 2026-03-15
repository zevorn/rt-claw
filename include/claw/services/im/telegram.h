/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Telegram Bot IM integration — receive messages via HTTP long polling
 * (getUpdates), forward to AI engine, reply via sendMessage API.
 */

#ifndef CLAW_SERVICES_IM_TELEGRAM_H
#define CLAW_SERVICES_IM_TELEGRAM_H

int telegram_init(void);
int telegram_start(void);

/**
 * Runtime configuration — set Bot token before telegram_init().
 * Takes effect on next telegram_start() (requires reboot).
 */
void telegram_set_bot_token(const char *token);
const char *telegram_get_bot_token(void);

#endif /* CLAW_SERVICES_IM_TELEGRAM_H */
