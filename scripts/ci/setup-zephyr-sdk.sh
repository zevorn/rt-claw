#!/bin/bash
# SPDX-License-Identifier: MIT
#
# Install Zephyr SDK and Python dependencies for CI.

set -euo pipefail

ZEPHYR_SDK_VERSION="0.17.0"
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

echo "Zephyr SDK setup complete"
