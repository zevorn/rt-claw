#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Unit test runner: build vexpress-a9 with RTCLAW_UNIT_TEST=1, run in QEMU.

import os
import subprocess
import sys

PROJECT_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..")
)
BUILD_DIR = os.path.join(PROJECT_ROOT, "build", "vexpress-a9-qemu")
A9_PLATFORM = os.path.join(PROJECT_ROOT, "platform", "vexpress-a9")
QEMU_TIMEOUT = 60


def build():
    """Build vexpress-a9 firmware in unit test mode."""
    env = os.environ.copy()
    env["RTCLAW_UNIT_TEST"] = "1"

    print(">>> Building unit test firmware (vexpress-a9) ...")
    subprocess.check_call(
        ["make", "vexpress-a9-qemu"],
        cwd=PROJECT_ROOT,
        env=env,
    )


def run_qemu():
    """Run unit test firmware in QEMU with semihosting."""
    kernel = os.path.join(BUILD_DIR, "rtthread.bin")
    if not os.path.isfile(kernel):
        print(f"Kernel not found: {kernel}")
        return 1

    sd_bin = os.path.join(A9_PLATFORM, "sd.bin")
    if not os.path.isfile(sd_bin):
        print("Creating SD card image ...")
        subprocess.check_call([
            "dd", "if=/dev/zero", f"of={sd_bin}",
            "bs=1024", "count=65536",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    cmd = [
        "qemu-system-arm",
        "-M", "vexpress-a9",
        "-smp", "cpus=1",
        "-kernel", kernel,
        "-nographic",
        "-sd", sd_bin,
        "-nic", "user,model=lan9118",
        "-semihosting-config", "enable=on,target=native",
    ]

    print(f">>> Running QEMU (timeout={QEMU_TIMEOUT}s) ...")
    try:
        result = subprocess.run(
            cmd,
            timeout=QEMU_TIMEOUT,
            capture_output=True,
            text=True,
        )
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)
        return result.returncode
    except subprocess.TimeoutExpired:
        print("TIMEOUT: unit tests did not complete")
        return 124


def main():
    try:
        build()
    except subprocess.CalledProcessError:
        print("Build failed")
        return 1

    rc = run_qemu()
    if rc == 0:
        print("\n>>> Unit tests PASSED")
    else:
        print(f"\n>>> Unit tests FAILED (rc={rc})")
    return rc


if __name__ == "__main__":
    sys.exit(main())
