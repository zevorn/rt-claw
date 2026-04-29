#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# PlatformIO pre-build script for rt-claw.
#
# Orchestrates the Meson build chain by deriving toolchain paths
# from PlatformIO's package manager, then running meson compile
# to produce claw/osal objects.  The CMake POST_BUILD step in
# components/rt_claw/ merges these objects into the firmware.

import configparser
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821  PlatformIO SCons global


def _fail(msg):
    sys.stderr.write(f"[pio_pre_build] Error: {msg}\n")
    env.Exit(1)


def _info(msg):
    print(f"[pio_pre_build] {msg}")


def _check_tool(name):
    if shutil.which(name) is None:
        if name == "meson":
            hint = "  pip install meson"
        else:
            hint = "  apt install ninja-build  (or pip install ninja)"
        _fail(f"'{name}' not found. Install it:\n{hint}")


def _hash_files(paths):
    h = hashlib.sha256()
    for p in sorted(paths):
        if os.path.exists(p):
            with open(p, "rb") as f:
                h.update(f.read())
    return h.hexdigest()


def _stamp_changed(stamp_path, current_hash):
    if not os.path.exists(stamp_path):
        return True
    with open(stamp_path) as f:
        return f.read().strip() != current_hash


def _write_stamp(stamp_path, current_hash):
    os.makedirs(os.path.dirname(stamp_path), exist_ok=True)
    with open(stamp_path, "w") as f:
        f.write(current_hash)


def _resolve_compiler(platform_obj, chip):
    """Get compiler path from PlatformIO's installed toolchain."""
    if chip == "esp32c3":
        pkg = "toolchain-riscv32-esp"
        prefix = "riscv32-esp-elf"
    else:
        pkg = "toolchain-xtensa-esp-elf"
        prefix = "xtensa-esp32s3-elf"
    pkg_dir = platform_obj.get_package_dir(pkg)
    if not pkg_dir:
        _fail(f"Toolchain package '{pkg}' not found")
    compiler = os.path.join(pkg_dir, "bin", f"{prefix}-gcc")
    if not os.path.exists(compiler):
        _fail(f"Compiler not found: {compiler}")
    return compiler


def _resolve_idf_path(platform_obj):
    framework_dir = platform_obj.get_package_dir("framework-espidf")
    if not framework_dir or not os.path.isdir(framework_dir):
        _fail("framework-espidf package not found in PlatformIO")
    return framework_dir


def _generate_cross_file(cross_ini, compiler, chip, board,
                         idf_path, sdkconfig_h):
    """Generate Meson cross-file from PlatformIO toolchain info."""
    ar = compiler.replace("-gcc", "-ar")
    strip_bin = compiler.replace("-gcc", "-strip")

    if chip == "esp32c3":
        cpu_family = "riscv32"
        cpu = "esp32c3"
        arch_flags = [
            "-march=rv32imc_zicsr_zifencei",
            "-ffunction-sections",
            "-fdata-sections",
            "-fno-jump-tables",
            "-fno-tree-switch-conversion",
            "-fdiagnostics-color=always",
        ]
    else:
        cpu_family = "xtensa"
        cpu = "esp32s3"
        arch_flags = [
            "-mlongcalls",
            "-ffunction-sections",
            "-fdata-sections",
            "-fstrict-volatile-bitfields",
            "-fdiagnostics-color=always",
        ]

    c_args = list(arch_flags)

    if sdkconfig_h and os.path.exists(sdkconfig_h):
        c_args.extend(["-include", sdkconfig_h])

    c_args.extend([
        "-DCLAW_PLATFORM_ESP_IDF",
        "-DESP_PLATFORM",
        "-D_GNU_SOURCE",
        "-D_POSIX_READER_WRITER_LOCKS",
    ])

    idf_inc_dirs = [
        "components/freertos/FreeRTOS-Kernel/include",
        "components/freertos/FreeRTOS-Kernel/portable/riscv/include"
        if chip == "esp32c3" else
        "components/freertos/FreeRTOS-Kernel/portable/xtensa/include",
        "components/freertos/config/include",
        "components/freertos/config/include/freertos",
        "components/freertos/config/riscv/include"
        if chip == "esp32c3" else
        "components/freertos/config/xtensa/include",
        "components/esp_common/include",
        "components/esp_hw_support/include",
        "components/esp_system/include",
        "components/esp_rom/include",
        f"components/esp_rom/{chip}/include",
        "components/hal/include",
        f"components/hal/{chip}/include",
        "components/soc/include",
        f"components/soc/{chip}/include",
        f"components/soc/{chip}/register",
        "components/heap/include",
        "components/log/include",
        "components/newlib/platform_include",
        "components/esp_timer/include",
        "components/esp_event/include",
        "components/esp_netif/include",
        "components/esp_wifi/include",
        "components/esp_eth/include",
        "components/lwip/include",
        "components/lwip/lwip/src/include",
        "components/lwip/port/include",
        "components/esp_http_client/include",
        "components/esp_websocket_client/include",
        "components/json/cJSON",
        "components/nvs_flash/include",
        "components/esp-tls/include",
        "components/esp_driver_gpio/include",
        "components/driver/deprecated",
        "components/esp_lcd/include",
        "components/esp_driver_i2c/include",
        "components/esp_driver_i2s/include",
        "components/app_update/include",
        "components/esp_http_server/include",
        "components/riscv/include" if chip == "esp32c3"
        else "components/xtensa/include",
        "components/esp_partition/include",
        "components/spi_flash/include",
        "components/bootloader_support/include",
    ]

    for d in idf_inc_dirs:
        full = os.path.join(idf_path, d)
        if os.path.isdir(full):
            c_args.extend(["-isystem", full])

    kconfig_to_meson = {
        "CONFIG_RTCLAW_SWARM_ENABLE": "swarm",
        "CONFIG_RTCLAW_SCHED_ENABLE": "sched",
        "CONFIG_RTCLAW_SKILL_ENABLE": "skill",
        "CONFIG_RTCLAW_TOOL_GPIO": "tool_gpio",
        "CONFIG_RTCLAW_TOOL_SYSTEM": "tool_system",
        "CONFIG_RTCLAW_TOOL_SCHED": "tool_sched",
        "CONFIG_RTCLAW_TOOL_NET": "tool_net",
        "CONFIG_RTCLAW_TOOL_MOUSE": "tool_mouse",
        "CONFIG_RTCLAW_HEARTBEAT_ENABLE": "heartbeat",
        "CONFIG_RTCLAW_FEISHU_ENABLE": "feishu",
        "CONFIG_RTCLAW_TELEGRAM_ENABLE": "telegram",
        "CONFIG_RTCLAW_OTA_ENABLE": "ota",
    }
    enabled = set()
    if sdkconfig_h and os.path.exists(sdkconfig_h):
        with open(sdkconfig_h) as sh:
            for line in sh:
                for kconf in kconfig_to_meson:
                    if f"#define {kconf} " in line or \
                       f"#define {kconf}\n" in line:
                        enabled.add(kconf)

    buf = io.StringIO()
    buf.write(f"# Auto-generated Meson cross-file for {chip.upper()} "
              f"(PlatformIO)\n")
    buf.write(f"# Board: {board}\n")
    buf.write("# DO NOT edit manually or commit to git.\n\n")
    buf.write("[binaries]\n")
    buf.write(f"c = '{compiler}'\n")
    buf.write(f"ar = '{ar}'\n")
    buf.write(f"strip = '{strip_bin}'\n\n")
    buf.write("[host_machine]\n")
    buf.write("system = 'none'\n")
    buf.write(f"cpu_family = '{cpu_family}'\n")
    buf.write(f"cpu = '{cpu}'\n")
    buf.write("endian = 'little'\n\n")
    buf.write("[built-in options]\n")
    args_str = ", ".join(f"'{a}'" for a in c_args)
    buf.write(f"c_args = [{args_str}]\n")
    buf.write("b_staticpic = false\n")
    buf.write("b_pie = false\n\n")
    buf.write("[project options]\n")
    buf.write("osal = 'freertos'\n")
    for kconf, mopt in kconfig_to_meson.items():
        val = "true" if kconf in enabled else "false"
        buf.write(f"{mopt} = {val}\n")

    new_content = buf.getvalue()
    old_content = ""
    if os.path.exists(cross_ini):
        with open(cross_ini) as existing:
            old_content = existing.read()

    os.makedirs(os.path.dirname(cross_ini), exist_ok=True)
    if new_content != old_content:
        with open(cross_ini, "w") as out:
            out.write(new_content)
        return True
    return False


def _validate_idf_consistency(idf_path, compiler, cross_ini):
    """Verify compiler consistency between PlatformIO toolchain and
    generated cross-file (AC-4)."""
    _info("Validating IDF path consistency...")

    cp = configparser.ConfigParser()
    cp.read(cross_ini)
    cross_compiler = None
    if cp.has_option("binaries", "c"):
        cross_compiler = cp.get("binaries", "c").strip("'\"")

    _info(f"  Framework path:   {idf_path}")
    _info(f"  PIO compiler:     {compiler}")
    _info(f"  Cross compiler:   {cross_compiler}")

    if cross_compiler:
        pio_base = os.path.basename(compiler)
        cross_base = os.path.basename(cross_compiler)
        if pio_base != cross_base:
            _fail(
                f"IDF consistency check failed!\n"
                f"  PlatformIO compiler: {compiler}\n"
                f"  Cross-file compiler: {cross_compiler}\n"
                f"  These must use the same toolchain.")

    _info("  IDF consistency check passed")


def pre_build():
    build_targets = COMMAND_LINE_TARGETS or ["buildprog"]  # noqa: F821
    skip_targets = {"clean", "cleanall", "idedata", "menuconfig"}
    if any(t in skip_targets for t in build_targets):
        _info(f"Skipping Meson chain for {build_targets}")
        return

    _check_tool("meson")
    _check_tool("ninja")

    board = env.GetProjectOption("custom_rtclaw_board", None)
    if not board:
        _fail("custom_rtclaw_board not set in platformio.ini environment")

    pio_board = env.BoardConfig()
    mcu = pio_board.get("build.mcu", "esp32c3")
    if "esp32s3" in mcu or "esp32-s3" in mcu:
        chip = "esp32s3"
    else:
        chip = "esp32c3"

    platform_obj = env.PioPlatform()
    idf_path = _resolve_idf_path(platform_obj)
    compiler = _resolve_compiler(platform_obj, chip)

    project_dir = env.subst("$PROJECT_DIR")
    project_root = os.path.dirname(os.path.dirname(project_dir))
    board_build_dir = os.path.join(project_root, "build",
                                   f"{chip}-{board}")
    meson_build_dir = os.path.join(board_build_dir, "meson")

    pio_build_dir = os.path.join(project_dir, ".pio", "build",
                                 env.subst("$PIOENV"))
    sdkconfig_h = os.path.join(pio_build_dir, "config", "sdkconfig.h")
    if not os.path.exists(sdkconfig_h):
        sdkconfig_h = None

    stamp_path = os.path.join(board_build_dir, ".pio_stamp")
    sdkconfig_defaults = os.path.join(project_dir, "boards", board,
                                      "sdkconfig.defaults")
    hash_inputs = [
        sdkconfig_defaults,
        os.path.join(project_root, "meson.build"),
        os.path.join(project_root, "meson_options.txt"),
        os.path.join(project_root, "claw_config.h"),
    ]
    current_hash = _hash_files(hash_inputs)
    needs_reconfigure = _stamp_changed(stamp_path, current_hash)

    cross_ini = os.path.join(board_build_dir, "cross.ini")
    _info(f"Generating Meson cross-file for {chip}-{board}...")
    changed = _generate_cross_file(cross_ini, compiler, chip, board,
                                   idf_path, sdkconfig_h)

    _validate_idf_consistency(idf_path, compiler, cross_ini)

    build_ninja = os.path.join(meson_build_dir, "build.ninja")
    if not os.path.exists(build_ninja):
        _info("Running meson setup...")
        subprocess.check_call(
            ["meson", "setup", meson_build_dir,
             f"--cross-file={cross_ini}"],
            cwd=project_root)
    elif needs_reconfigure or changed:
        _info("Running meson setup --reconfigure...")
        subprocess.check_call(
            ["meson", "setup", "--reconfigure", meson_build_dir,
             f"--cross-file={cross_ini}"],
            cwd=project_root)

    _info("Running meson compile...")
    subprocess.check_call(
        ["meson", "compile", "-C", meson_build_dir],
        cwd=project_root)

    _write_stamp(stamp_path, current_hash)

    import glob
    meson_objs = (
        glob.glob(os.path.join(meson_build_dir,
                               "claw", "librtclaw.a.p", "*.o")) +
        glob.glob(os.path.join(meson_build_dir,
                               "osal", "libosal.a.p", "*.o")) +
        glob.glob(os.path.join(meson_build_dir,
                               "vendor", "lib", "cjson",
                               "libcjson.a.p", "*.o"))
    )
    if not meson_objs:
        _fail(f"No Meson objects found in {meson_build_dir}")

    env.Append(
        LINKFLAGS=["-Wl,--whole-archive"] +
                  meson_objs +
                  ["-Wl,--no-whole-archive"]
    )
    _info(f"Meson build complete for {chip}-{board} "
          f"({len(meson_objs)} objects injected)")


pre_build()
