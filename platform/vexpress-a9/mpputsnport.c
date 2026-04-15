/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Local MicroPython output bridge for RT-Thread.
 */

#include <rtdevice.h>
#include <rtthread.h>

#include "mpputsnport.h"

static rt_device_t s_console_dev = RT_NULL;
static rt_uint16_t s_console_open_flag;

void mp_putsn(const char *str, size_t len)
{
    if (s_console_dev) {
        rt_device_write(s_console_dev, 0, str, len);
    }
}

void mp_putsn_stream(const char *str, size_t len)
{
    if (s_console_dev) {
        rt_uint16_t old_flag = s_console_dev->open_flag;

        s_console_dev->open_flag |= RT_DEVICE_FLAG_STREAM;
        rt_device_write(s_console_dev, 0, str, len);
        s_console_dev->open_flag = old_flag;
    }
}

void mp_putsn_init(void)
{
    s_console_dev = rt_console_get_device();
    if (s_console_dev) {
        s_console_open_flag = s_console_dev->open_flag;
    }
}

void mp_putsn_deinit(void)
{
    if (s_console_dev) {
        s_console_dev->open_flag = s_console_open_flag;
        s_console_dev = RT_NULL;
    }
}
