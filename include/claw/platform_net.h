/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Platform hooks for RT-Thread network bring-up.
 */

#ifndef CLAW_PLATFORM_NET_H
#define CLAW_PLATFORM_NET_H

const char *claw_platform_net_device_name(void);
void claw_platform_net_prepare(void);

#endif /* CLAW_PLATFORM_NET_H */
