#!/bin/bash
# SPDX-License-Identifier: MIT
#
# Install Zephyr SDK and Python dependencies for CI.

set -euo pipefail

ZEPHYR_SDK_VERSION="1.0.1"
ZEPHYR_SDK_DIR="$HOME/zephyr-sdk-${ZEPHYR_SDK_VERSION}"

if [ -d "$ZEPHYR_SDK_DIR" ]; then
    echo "Zephyr SDK ${ZEPHYR_SDK_VERSION} already installed"
else
    echo "Installing Zephyr SDK ${ZEPHYR_SDK_VERSION}..."
    wget -q "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz" \
        -O /tmp/zephyr-sdk.tar.xz
    tar xf /tmp/zephyr-sdk.tar.xz -C "$HOME"
    rm /tmp/zephyr-sdk.tar.xz

    cd "$ZEPHYR_SDK_DIR"
    ./setup.sh -t arm-zephyr-eabi -h -c
    echo "Zephyr SDK installed at $ZEPHYR_SDK_DIR"
fi

pip3 install --quiet pykwalify

# --- Xilinx QEMU (for qemu_cortex_a9 runtime testing) ---
XILINX_QEMU_DIR="$HOME/xilinx-qemu"
XILINX_QEMU_BIN="$XILINX_QEMU_DIR/build/qemu-system-aarch64"

if [ -x "$XILINX_QEMU_BIN" ]; then
    echo "Xilinx QEMU already built"
else
    echo "Building Xilinx QEMU (for arm-generic-fdt-7series)..."
    sudo apt-get update -qq
    sudo apt-get install -y -qq \
        libglib2.0-dev libpixman-1-dev libslirp-dev \
        pkg-config python3-venv > /dev/null 2>&1

    git clone --depth 1 --branch xlnx_rel_v2024.1 \
        https://github.com/Xilinx/qemu.git "$XILINX_QEMU_DIR"

    cd "$XILINX_QEMU_DIR"
    ./configure \
        --target-list=aarch64-softmmu \
        --prefix="$XILINX_QEMU_DIR/install" \
        --enable-slirp \
        --disable-docs \
        --disable-werror
    make -j"$(nproc)"
    echo "Xilinx QEMU built at $XILINX_QEMU_BIN"
fi

echo "$XILINX_QEMU_DIR/build" >> "$GITHUB_PATH" 2>/dev/null || true

echo "Zephyr SDK setup complete"
