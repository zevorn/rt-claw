#!/bin/bash
# Setup ESP-IDF development environment + Espressif QEMU for ESP32-C3
#
# Prerequisites (Arch Linux):
#   sudo pacman -S --needed libgcrypt glib2 pixman sdl2 libslirp \
#       python cmake ninja gcc git wget flex bison
#
# This script:
#   1. Clones ESP-IDF (if not present)
#   2. Installs ESP-IDF tools (toolchain + QEMU)
#   3. Exports environment

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

ESP_IDF_DIR="${ESP_IDF_DIR:-$HOME/esp/esp-idf}"
ESP_IDF_TAG="${ESP_IDF_TAG:-v5.4}"

echo "=== rt-claw ESP32-C3 Environment Setup ==="
echo "ESP-IDF directory: $ESP_IDF_DIR"
echo "ESP-IDF version:   $ESP_IDF_TAG"
echo ""

# Step 1: Clone ESP-IDF
if [ ! -d "$ESP_IDF_DIR" ]; then
    echo ">>> Cloning ESP-IDF $ESP_IDF_TAG ..."
    mkdir -p "$(dirname "$ESP_IDF_DIR")"
    git clone --depth 1 --branch "$ESP_IDF_TAG" \
        --recursive --shallow-submodules \
        https://github.com/espressif/esp-idf.git "$ESP_IDF_DIR"
else
    echo ">>> ESP-IDF already exists at $ESP_IDF_DIR"
fi

# Step 2: Install tools (toolchain + QEMU)
echo ""
echo ">>> Installing ESP-IDF tools (including QEMU) ..."
cd "$ESP_IDF_DIR"
./install.sh esp32c3

echo ""
echo ">>> Installing Espressif QEMU (riscv32) ..."
python3 tools/idf_tools.py install qemu-riscv32

# Step 3: Print usage
echo ""
echo "=== Setup Complete ==="
echo ""
echo "To activate the environment, run:"
echo "  source $ESP_IDF_DIR/export.sh"
echo ""
echo "To build rt-claw for ESP32-C3:"
echo "  cd $PROJECT_DIR/platform/esp32c3"
echo "  idf.py set-target esp32c3"
echo "  idf.py build"
echo ""
echo "To run on QEMU:"
echo "  idf.py qemu monitor"
echo ""
echo "Or use the helper script:"
echo "  $PROJECT_DIR/tools/qemu-run.sh -m esp32c3"
