#!/bin/bash
# Launch rt-claw on QEMU vexpress-a9 (RT-Thread)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build/qemu-a9"
PLATFORM_DIR="$PROJECT_ROOT/platform/qemu-a9-rtthread"

cd "$PLATFORM_DIR" || exit 1

if [ ! -f "sd.bin" ]; then
    echo "Creating SD card image..."
    dd if=/dev/zero of=sd.bin bs=1024 count=65536
fi

if [ ! -f "$BUILD_DIR/rtthread.bin" ]; then
    echo "Error: $BUILD_DIR/rtthread.bin not found. Build first:"
    echo "  make qemu-a9"
    exit 1
fi

qemu-system-arm --version
exec qemu-system-arm \
    -M vexpress-a9 \
    -smp cpus=1 \
    -kernel "$BUILD_DIR/rtthread.bin" \
    -nographic \
    -sd sd.bin \
    -nic user,model=lan9118
