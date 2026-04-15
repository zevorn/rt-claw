/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MicroPython bridge for vexpress-a9 / RT-Thread.
 */

#include "platform/scripting.h"

#include <rtdevice.h>
#include <rtthread.h>
#include <string.h>

#include "py/compile.h"
#include "py/gc.h"
#include "py/mpthread.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "port/mpconfigport.h"
#include "port/mpputsnport.h"

#define CAPTURE_NAME_LEN 16
#define MICROPY_STACK_MIN 4096
#define MICROPYTHON_LANG "micropython"

struct script_capture_device {
    struct rt_device parent;
    char            *buffer;
    size_t           size;
    size_t           len;
};

static rt_mutex_t s_script_lock = RT_NULL;
static rt_uint32_t s_capture_seq;

static rt_err_t capture_open(rt_device_t dev, rt_uint16_t oflag)
{
    (void)dev;
    (void)oflag;
    return RT_EOK;
}

static rt_err_t capture_close(rt_device_t dev)
{
    (void)dev;
    return RT_EOK;
}

static rt_ssize_t capture_read(rt_device_t dev, rt_off_t pos,
                               void *buffer, rt_size_t size)
{
    (void)dev;
    (void)pos;
    (void)buffer;
    (void)size;
    return 0;
}

static rt_ssize_t capture_write(rt_device_t dev, rt_off_t pos,
                                const void *buffer, rt_size_t size)
{
    struct script_capture_device *capture;
    size_t avail;
    size_t copy;

    (void)pos;
    capture = (struct script_capture_device *)dev;
    if (!capture->buffer || capture->size == 0) {
        return size;
    }

    if (capture->len >= capture->size - 1) {
        return size;
    }

    avail = capture->size - capture->len - 1;
    copy = size < avail ? size : avail;
    if (copy > 0) {
        rt_memcpy(capture->buffer + capture->len, buffer, copy);
        capture->len += copy;
        capture->buffer[capture->len] = '\0';
    }

    return size;
}

static rt_err_t capture_control(rt_device_t dev, int cmd, void *args)
{
    (void)dev;
    (void)cmd;
    (void)args;
    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops s_capture_ops = {
    .init = RT_NULL,
    .open = capture_open,
    .close = capture_close,
    .read = capture_read,
    .write = capture_write,
    .control = capture_control,
};
#endif

static rt_mutex_t get_script_lock(void)
{
    if (s_script_lock == RT_NULL) {
        s_script_lock = rt_mutex_create("mpy_lock", RT_IPC_FLAG_PRIO);
    }

    return s_script_lock;
}

static int micropython_exec_string(const char *code)
{
    nlr_buf_t nlr;

    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lexer = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_,
            code,
            strlen(code),
            0
        );
        qstr source_name = lexer->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lexer, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);

        mp_call_function_0(module_fun);
        nlr_pop();
        return 0;
    }

    mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    return -1;
}

static int micropython_run(const char *code)
{
    int stack_dummy;
    void *stack_top = (void *)&stack_dummy;
    char *heap = RT_NULL;
    int rc;

    if (rt_thread_self()->stack_size < MICROPY_STACK_MIN) {
        static const char msg[] =
            "MicroPython requires at least 4096 bytes of stack.\n";

        mp_putsn(msg, sizeof(msg) - 1);
        return -1;
    }

#if MICROPY_PY_THREAD
    mp_thread_init(
        rt_thread_self()->stack_addr,
        ((rt_uint32_t)stack_top
         - (rt_uint32_t)rt_thread_self()->stack_addr) / 4
    );
#endif

    mp_stack_set_top(stack_top);
    mp_stack_set_limit(rt_thread_self()->stack_size - 1024);

#if MICROPY_ENABLE_GC
    heap = rt_malloc(MICROPY_HEAP_SIZE);
    if (!heap) {
        static const char msg[] =
            "No memory for MicroPython heap.\n";

        mp_putsn(msg, sizeof(msg) - 1);
        return -1;
    }
    gc_init(heap, heap + MICROPY_HEAP_SIZE);
#endif

    mp_init();
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
    mp_obj_list_append(
        mp_sys_path,
        mp_obj_new_str(MICROPY_PY_PATH_FIRST,
                       strlen(MICROPY_PY_PATH_FIRST))
    );
    mp_obj_list_append(
        mp_sys_path,
        mp_obj_new_str(MICROPY_PY_PATH_SECOND,
                       strlen(MICROPY_PY_PATH_SECOND))
    );
    mp_obj_list_init(mp_sys_argv, 0);

    rc = micropython_exec_string(code);

    gc_sweep_all();
    mp_deinit();

#if MICROPY_PY_THREAD
    mp_thread_deinit();
#endif

#if MICROPY_ENABLE_GC
    rt_free(heap);
#endif

    return rc;
}

int claw_platform_script_supported(const char *language)
{
    if (!language) {
        return 0;
    }

    return strcmp(language, MICROPYTHON_LANG) == 0;
}

int claw_platform_run_script(const char *language, const char *code,
                             char *output, size_t output_size)
{
    struct script_capture_device capture;
    char capture_name[CAPTURE_NAME_LEN];
    rt_device_t old_console;
    rt_mutex_t lock;
    int rc = -1;

    if (!claw_platform_script_supported(language) || !code ||
        !output || output_size == 0) {
        return -1;
    }

    lock = get_script_lock();
    if (lock == RT_NULL) {
        rt_snprintf(output, output_size,
                    "failed to create script lock");
        return -1;
    }

    if (rt_mutex_take(lock, RT_WAITING_FOREVER) != RT_EOK) {
        rt_snprintf(output, output_size,
                    "failed to acquire script lock");
        return -1;
    }

    output[0] = '\0';
    old_console = rt_console_get_device();
    if (old_console == RT_NULL) {
        rt_snprintf(output, output_size,
                    "console device is not available");
        goto out_unlock;
    }

    rt_memset(&capture, 0, sizeof(capture));
    capture.parent.type = RT_Device_Class_Char;
#ifdef RT_USING_DEVICE_OPS
    capture.parent.ops = &s_capture_ops;
#else
    capture.parent.open = capture_open;
    capture.parent.close = capture_close;
    capture.parent.read = capture_read;
    capture.parent.write = capture_write;
    capture.parent.control = capture_control;
#endif
    capture.buffer = output;
    capture.size = output_size;

    s_capture_seq++;
    rt_snprintf(capture_name, sizeof(capture_name),
                "mpcap%lu", (unsigned long)s_capture_seq);

    if (rt_device_register(&capture.parent, capture_name,
                           RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_snprintf(output, output_size,
                    "failed to register capture console");
        goto out_unlock;
    }

    if (rt_console_set_device(capture_name) == RT_NULL) {
        rt_snprintf(output, output_size,
                    "failed to switch console");
        rt_device_unregister(&capture.parent);
        goto out_unlock;
    }

    mp_putsn_init();
    rc = micropython_run(code);
    mp_putsn_deinit();

    rt_console_set_device(old_console->parent.name);
    rt_device_unregister(&capture.parent);

out_unlock:
    rt_mutex_release(lock);
    return rc;
}
