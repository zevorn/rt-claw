# SPDX-License-Identifier: MIT
# Platform configuration and QEMU path resolution for rt-claw functional tests.

import os
import shutil
from dataclasses import dataclass, field
from typing import List, Optional

PROJECT_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")


@dataclass
class PlatformConfig:
    name: str
    qemu_bin: str
    boot_marker: str
    shell_prompt: str
    boot_timeout: int
    shell_timeout: int
    has_shell: bool
    flash_path: str = ""
    extra_files: dict = field(default_factory=dict)
    qemu_args: List[str] = field(default_factory=list)


def _resolve_esp32c3() -> PlatformConfig:
    flash = os.path.join(BUILD_DIR, "esp32c3-qemu", "idf", "flash_image.bin")
    return PlatformConfig(
        name="esp32c3-qemu",
        qemu_bin=resolve_qemu_binary("esp32c3"),
        flash_path=flash,
        boot_marker="rt-claw v",
        shell_prompt="rt-claw chat>",
        boot_timeout=90,
        shell_timeout=30,
        has_shell=True,
        qemu_args=[
            "-nographic",
            "-icount", "1",
            "-machine", "esp32c3",
            "-global",
            "driver=timer.esp32c3.timg,property=wdt_disable,value=true",
            "-nic", "user,model=open_eth",
        ],
    )


def _resolve_esp32s3() -> PlatformConfig:
    flash = os.path.join(BUILD_DIR, "esp32s3-qemu", "idf", "flash_image.bin")
    return PlatformConfig(
        name="esp32s3-qemu",
        qemu_bin=resolve_qemu_binary("esp32s3"),
        flash_path=flash,
        boot_marker="rt-claw v",
        shell_prompt="rt-claw chat>",
        boot_timeout=180,
        shell_timeout=60,
        has_shell=True,
        qemu_args=[
            "-nographic",
            "-icount", "1",
            "-machine", "esp32s3",
            "-nic", "user,model=open_eth",
        ],
    )


def _resolve_vexpress_a9() -> PlatformConfig:
    kernel = os.path.join(BUILD_DIR, "vexpress-a9-qemu", "rtthread.bin")
    sd_bin = os.path.join(PROJECT_ROOT, "platform", "vexpress-a9", "sd.bin")
    return PlatformConfig(
        name="vexpress-a9-qemu",
        qemu_bin=resolve_qemu_binary("vexpress-a9"),
        flash_path=kernel,
        boot_marker="rt-claw v",
        shell_prompt="",
        boot_timeout=60,
        shell_timeout=15,
        has_shell=False,
        extra_files={"sd_bin": sd_bin},
        qemu_args=[
            "-M", "vexpress-a9",
            "-smp", "cpus=1",
            "-nographic",
            "-nic", "user,model=lan9118",
        ],
    )



def _resolve_zynq_a9() -> PlatformConfig:
    kernel = os.path.join(
        BUILD_DIR, "zynq-a9-qemu", "platform", "zynq-a9", "rtclaw.elf"
    )
    return PlatformConfig(
        name="zynq-a9-qemu",
        qemu_bin=resolve_qemu_binary("zynq-a9"),
        flash_path=kernel,
        boot_marker="Zynq-A9 QEMU",
        shell_prompt="",
        boot_timeout=30,
        shell_timeout=15,
        has_shell=False,
        qemu_args=[
            "-M", "xilinx-zynq-a9",
            "-smp", "1",
            "-nographic",
            "-nic", "user,model=cadence_gem",
        ],
    )


_PLATFORM_MAP = {
    "esp32c3-qemu": _resolve_esp32c3,
    "esp32s3-qemu": _resolve_esp32s3,
    "vexpress-a9-qemu": _resolve_vexpress_a9,
    "zynq-a9-qemu": _resolve_zynq_a9,
}


def get_platform(name: Optional[str] = None) -> PlatformConfig:
    """Resolve platform config by name or RTCLAW_TEST_PLATFORM env var."""
    name = name or os.environ.get("RTCLAW_TEST_PLATFORM", "esp32c3-qemu")
    factory = _PLATFORM_MAP.get(name)
    if factory is None:
        raise ValueError(
            f"Unknown platform: {name} "
            f"(available: {', '.join(_PLATFORM_MAP)})"
        )
    return factory()


def resolve_qemu_binary(arch: str) -> str:
    """
    Find QEMU binary: env var > Espressif install > PATH fallback.

    For ESP32 targets, Espressif's fork is checked first because
    the upstream QEMU in PATH does not support esp32c3/esp32s3 machines.
    """
    env_key = f"RTCLAW_QEMU_{arch.upper().replace('-', '_')}"
    env_val = os.environ.get(env_key)
    if env_val and os.path.isfile(env_val):
        return env_val

    bin_map = {
        "esp32c3": "qemu-system-riscv32",
        "esp32s3": "qemu-system-xtensa",
        "vexpress-a9": "qemu-system-arm",
        "zynq-a9": "qemu-system-arm",
    }
    binary = bin_map.get(arch)
    if binary is None:
        raise ValueError(f"Unknown arch: {arch}")

    # Espressif QEMU first (upstream doesn't support esp32 machines)
    espressif_dirs = {
        "esp32c3": os.path.expanduser(
            "~/.espressif/tools/qemu-riscv32"
        ),
        "esp32s3": os.path.expanduser(
            "~/.espressif/tools/qemu-xtensa"
        ),
    }

    esp_dir = espressif_dirs.get(arch)
    if esp_dir and os.path.isdir(esp_dir):
        for root, _, files in os.walk(esp_dir):
            if binary in files:
                return os.path.join(root, binary)

    # PATH fallback (works for vexpress-a9, may work for ESP if
    # the user has Espressif QEMU in PATH)
    found = shutil.which(binary)
    if found:
        return found

    raise FileNotFoundError(
        f"Cannot find {binary}. Set {env_key} or install QEMU."
    )


def build_qemu_command(config: PlatformConfig,
                       flash_path: str,
                       sd_path: Optional[str] = None) -> List[str]:
    """Build the full QEMU command line."""
    cmd = [config.qemu_bin] + list(config.qemu_args)

    if config.name.startswith("esp32c3") or config.name.startswith("esp32s3"):
        cmd += [
            "-drive",
            f"file={flash_path},if=mtd,format=raw",
        ]
    elif config.name.startswith("vexpress"):
        cmd += ["-kernel", flash_path]
        if sd_path:
            cmd += ["-sd", sd_path]

    elif config.name.startswith("zynq"):
        cmd += ["-kernel", flash_path]

    return cmd
