#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# PlatformIO pre-build script for rt-claw.
#
# Sets CMake variables so the ESP-IDF component can orchestrate
# the Meson build chain during cmake build phase (where
# compile_commands.json is available).

import os
import shutil
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


def pre_build():
    build_targets = COMMAND_LINE_TARGETS or ["buildprog"]  # noqa: F821
    skip_targets = {"clean", "cleanall", "idedata", "menuconfig"}
    if any(t in skip_targets for t in build_targets):
        _info(f"Skipping pre-build for {build_targets}")
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

    project_dir = env.subst("$PROJECT_DIR")
    project_root = os.path.dirname(os.path.dirname(project_dir))
    meson_build_dir = os.path.join(project_root, "build",
                                   f"{chip}-{board}", "meson")

    cmake_extra = env.GetProjectOption("board_build.cmake_extra_args", "")
    cmake_extra += (f" -DRTCLAW_MESON_BUILD_DIR={meson_build_dir}"
                    f" -DRTCLAW_PLATFORMIO=1")
    try:
        pio_board.update("build.cmake_extra_args", cmake_extra)
    except Exception:
        pass

    _info(f"Pre-build: {chip}-{board}")
    _info(f"  Meson build dir: {meson_build_dir}")


pre_build()
