#!/bin/bash
# SPDX-License-Identifier: MIT
#
# Unified QEMU launcher for rt-claw.
#
# Usage:
#   ./tools/qemu-run.sh -M <machine> [options]
#
# Machines:
#   qemu-a9   QEMU vexpress-a9 (RT-Thread, ARM Cortex-A9)
#   esp32c3   QEMU ESP32-C3 (ESP-IDF, Espressif QEMU fork)
#   esp32s3   QEMU ESP32-S3 (ESP-IDF, Espressif QEMU fork)
#
# Options:
#   -M MACHINE   Target machine (required)
#   -g           Enable GDB server (debug mode, port 1234)
#   --graphics   Enable LCD display window (esp32c3 only)
#   -h           Show this help
#
# Shell completion:
#   eval "$(tools/qemu-run.sh --setup-completion)"

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

MACHINES="qemu-a9 esp32c3 esp32s3"
MACHINE=""
GDB_MODE=0
GRAPHICS=0

usage() {
    sed -n '3,/^$/s/^# \?//p' "$0"
    exit "${1:-0}"
}

# ---- shell completion ----

setup_completion() {
    cat <<'COMP_EOF'
# bash completion for qemu-run.sh
_qemu_run() {
    local cur prev opts machines
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    machines="qemu-a9 esp32c3 esp32s3"
    opts="-M -g --graphics -h --help"

    case "$prev" in
        -M) COMPREPLY=( $(compgen -W "$machines" -- "$cur") ); return ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
    else
        COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
    fi
}
complete -F _qemu_run qemu-run.sh
complete -F _qemu_run tools/qemu-run.sh
complete -F _qemu_run ./tools/qemu-run.sh

# zsh compatibility
if [ -n "$ZSH_VERSION" ]; then
    autoload -U +X bashcompinit 2>/dev/null && bashcompinit
    complete -F _qemu_run qemu-run.sh
    complete -F _qemu_run tools/qemu-run.sh
    complete -F _qemu_run ./tools/qemu-run.sh
fi
COMP_EOF
    exit 0
}

# ---- argument parsing ----

while [ $# -gt 0 ]; do
    case "$1" in
        -M)       MACHINE="$2"; shift 2 ;;
        -g)       GDB_MODE=1; shift ;;
        --graphics) GRAPHICS=1; shift ;;
        --setup-completion) setup_completion ;;
        -h|--help) usage 0 ;;
        *)  echo "Unknown option: $1"; usage 1 ;;
    esac
done

if [ -z "$MACHINE" ]; then
    echo "Error: -M <machine> is required."
    echo ""
    usage 1
fi

# ---- qemu-a9: QEMU vexpress-a9 (RT-Thread) ----

run_qemu_a9() {
    local build_dir="$PROJECT_ROOT/build/qemu-a9"
    local platform_dir="$PROJECT_ROOT/platform/qemu-a9-rtthread"

    cd "$platform_dir" || exit 1

    if [ ! -f "sd.bin" ]; then
        echo "Creating SD card image..."
        dd if=/dev/zero of=sd.bin bs=1024 count=65536
    fi

    if [ ! -f "$build_dir/rtthread.bin" ]; then
        echo "Error: $build_dir/rtthread.bin not found. Build first:"
        echo "  make qemu-a9"
        exit 1
    fi

    local gdb_flags=""
    if [ "$GDB_MODE" -eq 1 ]; then
        gdb_flags="-S -s"
        echo "Starting QEMU in debug mode (GDB port 1234)..."
        echo "Connect: arm-none-eabi-gdb $build_dir/rtthread.elf -ex 'target remote :1234'"
    fi

    qemu-system-arm --version
    exec qemu-system-arm \
        -M vexpress-a9 \
        -smp cpus=1 \
        -kernel "$build_dir/rtthread.bin" \
        -nographic \
        -sd sd.bin \
        -nic user,model=lan9118 \
        $gdb_flags
}

# ---- esp32c3: QEMU ESP32-C3 (Espressif fork) ----

run_esp32c3() {
    local platform_dir="$PROJECT_ROOT/platform/esp32c3"

    cd "$platform_dir" || exit 1

    if [ -z "$IDF_PATH" ]; then
        echo "Error: IDF_PATH not set. Source ESP-IDF first:"
        echo "  source \$HOME/esp/esp-idf/export.sh"
        exit 1
    fi

    if [ ! -d "build" ]; then
        echo "Error: build/ not found. Build first:"
        echo "  make esp32c3"
        exit 1
    fi

    local flash_image="build/flash_image.bin"

    echo ">>> Generating merged flash image ..."
    (cd build && esptool.py --chip esp32c3 merge_bin \
        --fill-flash-size 4MB \
        -o flash_image.bin \
        @flash_args)

    local display_flag="-nographic"
    if [ "$GRAPHICS" -eq 1 ]; then
        display_flag=""
    fi

    local gdb_flags=""
    if [ "$GDB_MODE" -eq 1 ]; then
        gdb_flags="-S -s"
        echo "Starting QEMU in debug mode (GDB port 1234)..."
        echo "Connect: riscv32-esp-elf-gdb build/rt-claw.elf -ex 'target remote :1234'"
    fi

    echo ">>> Starting QEMU (ESP32-C3, icount=1) ..."
    exec qemu-system-riscv32 $display_flag \
        -icount 1 \
        -machine esp32c3 \
        -drive "file=$flash_image,if=mtd,format=raw" \
        -global driver=timer.esp32c3.timg,property=wdt_disable,value=true \
        -nic user,model=open_eth \
        $gdb_flags
}

# ---- esp32s3: QEMU ESP32-S3 (Espressif fork) ----

run_esp32s3() {
    local platform_dir="$PROJECT_ROOT/platform/esp32s3"

    cd "$platform_dir" || exit 1

    if [ -z "$IDF_PATH" ]; then
        echo "Error: IDF_PATH not set. Source ESP-IDF first:"
        echo "  source \$HOME/esp/esp-idf/export.sh"
        exit 1
    fi

    if [ ! -d "build" ]; then
        echo "Error: build/ not found. Build first:"
        echo "  make esp32s3"
        exit 1
    fi

    local flash_image="build/flash_image.bin"

    echo ">>> Generating merged flash image ..."
    (cd build && esptool.py --chip esp32s3 merge_bin \
        --fill-flash-size 4MB \
        -o flash_image.bin \
        @flash_args)

    local display_flag="-nographic"
    if [ "$GRAPHICS" -eq 1 ]; then
        display_flag=""
    fi

    local gdb_flags=""
    if [ "$GDB_MODE" -eq 1 ]; then
        gdb_flags="-S -s"
        echo "Starting QEMU in debug mode (GDB port 1234)..."
        echo "Connect: xtensa-esp32s3-elf-gdb build/rt-claw.elf -ex 'target remote :1234'"
    fi

    echo ">>> Starting QEMU (ESP32-S3, icount=3) ..."
    exec qemu-system-xtensa $display_flag \
        -icount 3 \
        -machine esp32s3 \
        -drive "file=$flash_image,if=mtd,format=raw" \
        -global driver=timer.esp32s3.timg,property=wdt_disable,value=true \
        -nic user,model=open_eth \
        $gdb_flags
}

# ---- dispatch ----

case "$MACHINE" in
    qemu-a9)  run_qemu_a9 ;;
    esp32c3)  run_esp32c3 ;;
    esp32s3)  run_esp32s3 ;;
    *)
        echo "Error: unknown machine '$MACHINE'"
        echo "Available: $MACHINES"
        exit 1
        ;;
esac
