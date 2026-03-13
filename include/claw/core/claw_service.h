/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Service lifecycle interface — unified init/start/stop pattern.
 */

#ifndef CLAW_SERVICE_H
#define CLAW_SERVICE_H

typedef struct {
    const char *name;
    int  (*init)(void);
    int  (*start)(void);     /* NULL if no start phase needed */
    void (*stop)(void);      /* NULL if no stop support */
} claw_service_t;

#endif /* CLAW_SERVICE_H */
