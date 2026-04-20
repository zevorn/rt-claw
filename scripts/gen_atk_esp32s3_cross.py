#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

import io
import json
import os
import re
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BOARD = sys.argv[1] if len(sys.argv) > 1 else 'default'

BOARD_BUILD_DIR = os.path.join(PROJECT_ROOT, 'build', f'atk-esp32s3-{BOARD}')
IDF_BUILD_DIR   = os.path.join(BOARD_BUILD_DIR, 'idf')
CC_JSON         = os.path.join(IDF_BUILD_DIR, 'compile_commands.json')
CROSS_INI       = os.path.join(BOARD_BUILD_DIR, 'cross.ini')
SDKCONFIG_H     = os.path.join(IDF_BUILD_DIR, 'config', 'sdkconfig.h')



def main():
    if not os.path.exists(CC_JSON):
        print(f'Error: {CC_JSON} not found.', file=sys.stderr)
        return 1

    with open(CC_JSON) as f:
        cc = json.load(f)

    entry = None
    search_order = ['rt_claw_stub.c', 'claw/init.c', 'main/main.c']
    for pattern in search_order:
        for e in cc:
            if pattern in e.get('file', ''):
                entry = e
                break
        if entry:
            break

    if not entry:
        print('Error: no suitable source found in compile_commands.json', file=sys.stderr)
        return 1

    cmd = entry['command']
    compiler = cmd.split()[0]
    ar = compiler.replace('-gcc', '-ar')
    strip_bin = compiler.replace('-gcc', '-strip')

    all_includes = re.findall(r'-I(\S+)', cmd)
    all_includes += re.findall(r'-isystem\s+(\S+)', cmd)
    project_src_dirs = {'rt-claw/src', 'rt-claw/osal', 'rt-claw/vendor'}
    idf_includes = []
    for p in all_includes:
        if any(d in p for d in project_src_dirs):
            continue
        idf_includes.append(p)

    c_args = [
        '-mlongcalls',
        '-ffunction-sections',
        '-fdata-sections',
        '-fstrict-volatile-bitfields',
        '-fdiagnostics-color=always',
    ]

    if os.path.exists(SDKCONFIG_H):
        c_args.append(f'-include')
        c_args.append(SDKCONFIG_H)

    c_args.extend([
        '-DCLAW_PLATFORM_ESP_IDF',
        '-DESP_PLATFORM',
        '-D_GNU_SOURCE',
        '-D_POSIX_READER_WRITER_LOCKS',
    ])

    for inc in idf_includes:
        c_args.append(f'-isystem')
        c_args.append(inc)

    os.makedirs(os.path.dirname(CROSS_INI), exist_ok=True)
    buf = io.StringIO()
    f = buf

    f.write('# Auto-generated for ATK-ESP32-S3\n')
    f.write(f'# Board: {BOARD}\n\n')

    f.write('[binaries]\n')
    f.write(f"c = '{compiler}'\n")
    f.write(f"ar = '{ar}'\n")
    f.write(f"strip = '{strip_bin}'\n\n")

    f.write('[host_machine]\n')
    f.write("system = 'none'\n")
    f.write("cpu_family = 'xtensa'\n")
    f.write("cpu = 'esp32s3'\n")
    f.write("endian = 'little'\n\n")

    f.write('[built-in options]\n')
    args_str = ', '.join(f"'{a}'" for a in c_args)
    f.write(f'c_args = [{args_str}]\n')
    f.write("b_staticpic = false\n")
    f.write("b_pie = false\n\n")

    f.write('[project options]\n')
    f.write("osal = 'freertos'\n")

    kconfig_to_meson = {
        'CONFIG_RTCLAW_SWARM_ENABLE':     'swarm',
        'CONFIG_RTCLAW_SCHED_ENABLE':     'sched',
        'CONFIG_RTCLAW_SKILL_ENABLE':     'skill',
        'CONFIG_RTCLAW_TOOL_GPIO':        'tool_gpio',
        'CONFIG_RTCLAW_TOOL_SYSTEM':      'tool_system',
        'CONFIG_RTCLAW_TOOL_SCHED':       'tool_sched',
        'CONFIG_RTCLAW_TOOL_NET':         'tool_net',
        'CONFIG_RTCLAW_TOOL_MOUSE':       'tool_mouse',
        'CONFIG_RTCLAW_HEARTBEAT_ENABLE': 'heartbeat',
        'CONFIG_RTCLAW_FEISHU_ENABLE':    'feishu',
        'CONFIG_RTCLAW_TELEGRAM_ENABLE':  'telegram',
        'CONFIG_RTCLAW_OTA_ENABLE':       'ota',
    }
    enabled = set()
    if os.path.exists(SDKCONFIG_H):
        with open(SDKCONFIG_H) as sh:
            for line in sh:
                for kconf in kconfig_to_meson:
                    if f'#define {kconf} ' in line or f'#define {kconf}\n' in line:
                        enabled.add(kconf)
    for kconf, mopt in kconfig_to_meson.items():
        val = 'true' if kconf in enabled else 'false'
        f.write(f'{mopt} = {val}\n')

    new_content = buf.getvalue()
    old_content = ''
    if os.path.exists(CROSS_INI):
        with open(CROSS_INI) as existing:
            old_content = existing.read()
    if new_content != old_content:
        with open(CROSS_INI, 'w') as out:
            out.write(new_content)

    print(f'Generated: {CROSS_INI}')
    return 0


if __name__ == '__main__':
    sys.exit(main())