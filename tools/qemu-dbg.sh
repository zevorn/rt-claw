#!/bin/bash
# Launch rt-claw on QEMU with GDB server (port 1234)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR" || exit 1

if [ ! -f "sd.bin" ]; then
    echo "Creating SD card image..."
    dd if=/dev/zero of=sd.bin bs=1024 count=65536
fi

if [ ! -f "rtthread.bin" ]; then
    echo "Error: rtthread.bin not found. Build first with 'scons'."
    exit 1
fi

echo "Starting QEMU in debug mode (GDB port 1234)..."
echo "Connect with: arm-none-eabi-gdb -ex 'target remote :1234' rtthread.elf"

exec qemu-system-arm \
    -M vexpress-a9 \
    -smp cpus=2 \
    -kernel rtthread.bin \
    -nographic \
    -sd sd.bin \
    -S -s
