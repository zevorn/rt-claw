#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# PlatformIO Meson bridge helper.
#
# Called by the CMake RTCLAW_PLATFORMIO branch to:
#   1. Generate Meson cross-file from PlatformIO compile_commands.json
#   2. Validate four-way IDF/compiler consistency
#   3. Detect and handle stale Meson compiler state
#   4. Run meson compile (incremental)

import configparser
import json
import os
import shutil
import subprocess
import sys


def fail(msg):
    sys.stderr.write(f"[pio_meson_bridge] Error: {msg}\n")
    sys.exit(1)


def info(msg):
    print(f"[pio_meson_bridge] {msg}")


def extract_cc_from_compile_commands(cc_json_path):
    """Extract compiler path from compile_commands.json."""
    if not os.path.exists(cc_json_path):
        return None
    with open(cc_json_path) as f:
        entries = json.load(f)
    for e in entries:
        if "rt_claw_stub.c" in e.get("file", ""):
            return e["command"].split()[0]
    for e in entries:
        cmd = e.get("command", "")
        if cmd:
            return cmd.split()[0]
    return None


def extract_cc_from_cross_ini(cross_ini_path):
    """Extract compiler path from generated Meson cross-file."""
    if not os.path.exists(cross_ini_path):
        return None
    cp = configparser.ConfigParser()
    cp.read(cross_ini_path)
    if cp.has_option("binaries", "c"):
        return cp.get("binaries", "c").strip("'\"")
    return None


def extract_cc_from_meson_introspection(meson_build_dir):
    """Extract the actual compiler Meson is configured with."""
    intro = os.path.join(meson_build_dir, "meson-info",
                         "intro-compilers.json")
    if not os.path.exists(intro):
        return None
    with open(intro) as f:
        data = json.load(f)
    host = data.get("host", {})
    c_info = host.get("c", {})
    exelist = c_info.get("exelist", [])
    return exelist[0] if exelist else None


def normalize(path):
    """Normalize a compiler path for comparison."""
    if not path:
        return None
    return os.path.realpath(os.path.expanduser(path))


def validate_consistency(idf_cc_json, cross_ini, meson_build_dir,
                         framework_path):
    """Four-way IDF consistency validation.

    Compares compiler paths from:
      (a) PlatformIO compile_commands.json
      (b) Generated cross.ini
      (c) Meson introspection (if exists)
      (d) Framework path logged for reference
    Fails on any mismatch.
    """
    info("Validating IDF/compiler consistency...")

    cc_from_db = extract_cc_from_compile_commands(idf_cc_json)
    cc_from_cross = extract_cc_from_cross_ini(cross_ini)
    cc_from_meson = extract_cc_from_meson_introspection(meson_build_dir)

    info(f"  Framework:         {framework_path}")
    info(f"  compile_commands:  {cc_from_db}")
    info(f"  cross.ini:         {cc_from_cross}")
    info(f"  Meson introspect:  {cc_from_meson}")

    norm_db = normalize(cc_from_db)
    norm_cross = normalize(cc_from_cross)
    norm_meson = normalize(cc_from_meson)

    if norm_db and norm_cross and norm_db != norm_cross:
        fail(f"Compiler mismatch!\n"
             f"  compile_commands.json: {cc_from_db} -> {norm_db}\n"
             f"  cross.ini:            {cc_from_cross} -> {norm_cross}")

    stale = False
    if norm_meson and norm_cross and norm_meson != norm_cross:
        info(f"  Meson configured with different compiler:")
        info(f"    Meson:  {norm_meson}")
        info(f"    Cross:  {norm_cross}")
        stale = True

    info("  Consistency check passed")
    return stale


def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="PlatformIO Meson bridge helper")
    parser.add_argument("--chip", required=True,
                        choices=["esp32c3", "esp32s3"])
    parser.add_argument("--board", required=True)
    parser.add_argument("--idf-build-dir", required=True)
    parser.add_argument("--board-build-dir", required=True)
    parser.add_argument("--meson-build-dir", required=True)
    parser.add_argument("--framework-path", default="")
    args = parser.parse_args()

    gen_cross = os.path.join(os.path.dirname(__file__),
                             f"gen-{args.chip}-cross.py")
    if not os.path.exists(gen_cross):
        fail(f"gen-cross script not found: {gen_cross}")

    cc_json = os.path.join(args.idf_build_dir, "compile_commands.json")
    if not os.path.exists(cc_json):
        fail(f"compile_commands.json not found at {cc_json}.\n"
             f"  PlatformIO cmake configure may not have completed.")

    info(f"Generating cross-file for {args.chip}-{args.board}...")
    subprocess.check_call(
        [sys.executable, gen_cross,
         "--idf-build-dir", args.idf_build_dir,
         "--board-build-dir", args.board_build_dir,
         args.board])

    cross_ini = os.path.join(args.board_build_dir, "cross.ini")

    stale = validate_consistency(cc_json, cross_ini,
                                 args.meson_build_dir,
                                 args.framework_path)

    if stale and os.path.isdir(args.meson_build_dir):
        info("Removing stale Meson build directory...")
        shutil.rmtree(args.meson_build_dir)

    if not os.path.exists(os.path.join(args.meson_build_dir,
                                       "build.ninja")):
        info("Running meson setup...")
        subprocess.check_call(
            ["meson", "setup", args.meson_build_dir,
             f"--cross-file={cross_ini}"],
            cwd=os.path.dirname(os.path.dirname(__file__)))
    else:
        subprocess.check_call(
            ["meson", "setup", "--reconfigure", args.meson_build_dir,
             f"--cross-file={cross_ini}"],
            cwd=os.path.dirname(os.path.dirname(__file__)))

    info("Running meson compile...")
    subprocess.check_call(
        ["meson", "compile", "-C", args.meson_build_dir],
        cwd=os.path.dirname(os.path.dirname(__file__)))

    for lib in ["claw/librtclaw.a", "osal/libosal.a",
                "vendor/lib/cjson/libcjson.a"]:
        path = os.path.join(args.meson_build_dir, lib)
        if not os.path.exists(path):
            fail(f"Expected Meson archive missing: {path}")

    info(f"Meson bridge complete for {args.chip}-{args.board}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
