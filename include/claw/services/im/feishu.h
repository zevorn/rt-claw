/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Feishu (Lark) IM integration — receive messages via WebSocket
 * long connection, forward to AI engine, reply via HTTP API.
 */

#ifndef CLAW_SERVICES_IM_FEISHU_H
#define CLAW_SERVICES_IM_FEISHU_H

int feishu_init(void);
int feishu_start(void);

/**
 * Runtime configuration — set Feishu credentials before feishu_init().
 * Takes effect on next feishu_start() (requires reboot).
 */
void feishu_set_app_id(const char *app_id);
void feishu_set_app_secret(const char *app_secret);

const char *feishu_get_app_id(void);
const char *feishu_get_app_secret(void);

#endif /* CLAW_SERVICES_IM_FEISHU_H */
