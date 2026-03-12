#!/bin/bash
# Build rt-claw for ESP32-C3
#
# Prerequisites: ESP-IDF environment sourced (. $IDF_PATH/export.sh)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLATFORM_DIR="$(dirname "$SCRIPT_DIR")/platform/esp32c3"

if [ -z "$IDF_PATH" ]; then
    echo "Error: IDF_PATH not set. Source ESP-IDF first:"
    echo "  source \$HOME/esp/esp-idf/export.sh"
    exit 1
fi

cd "$PLATFORM_DIR"

# Set target if not already configured
if [ ! -f "sdkconfig" ]; then
    echo ">>> Setting target to esp32c3 ..."
    idf.py set-target esp32c3
fi

echo ">>> Building rt-claw for ESP32-C3 ..."
idf.py build

echo ""
echo "=== Build Complete ==="
echo "Run on QEMU:  ./tools/esp32c3-qemu-run.sh --raw"
echo "Flash device: idf.py -p /dev/ttyUSB0 flash monitor"
