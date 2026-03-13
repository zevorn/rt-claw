# rt-claw unified build entry
# Usage: make <platform>

PROJECT_ROOT := $(shell pwd)
BUILD_DIR    := $(PROJECT_ROOT)/build

.PHONY: help
help:
	@echo "rt-claw build system"
	@echo ""
	@echo "Targets:"
	@echo "  make qemu-a9      Build for QEMU vexpress-a9 (RT-Thread)"
	@echo "  make esp32c3      Build for ESP32-C3 (ESP-IDF + FreeRTOS)"
	@echo "  make esp32s3      Build for ESP32-S3 (ESP-IDF + FreeRTOS)"
	@echo "  make run-qemu-a9  Run RT-Thread on QEMU"
	@echo "  make run-esp32c3  Run ESP32-C3 on QEMU"
	@echo "  make run-esp32s3  Run ESP32-S3 on QEMU"
	@echo "  make clean        Clean all build artifacts"
	@echo "  make check        Run code style checks"
	@echo ""
	@echo "Build output: $(BUILD_DIR)/<platform>/"

# --- QEMU vexpress-a9 (RT-Thread) ---
MESON_BUILDDIR_A9 := $(BUILD_DIR)/qemu-a9
CROSS_FILE_A9     := platform/qemu-a9-rtthread/cross.ini

.PHONY: qemu-a9
qemu-a9:
	@if [ ! -f $(MESON_BUILDDIR_A9)/build.ninja ]; then \
		meson setup $(MESON_BUILDDIR_A9) --cross-file $(CROSS_FILE_A9); \
	fi
	meson compile -C $(MESON_BUILDDIR_A9)
	cd platform/qemu-a9-rtthread && scons -j$$(nproc)
	@cp -f platform/qemu-a9-rtthread/rtthread.elf $(MESON_BUILDDIR_A9)/
	@cp -f platform/qemu-a9-rtthread/rtthread.bin $(MESON_BUILDDIR_A9)/
	@cp -f platform/qemu-a9-rtthread/rtthread.map $(MESON_BUILDDIR_A9)/
	@echo "Output: $(MESON_BUILDDIR_A9)/"

.PHONY: run-qemu-a9
run-qemu-a9: qemu-a9
	tools/qemu-run.sh -M qemu-a9

# --- ESP32-C3 (ESP-IDF) ---
# Prerequisite: source $$HOME/esp/esp-idf/export.sh
MESON_BUILDDIR_C3 := $(BUILD_DIR)/esp32c3
CROSS_FILE_C3     := platform/esp32c3/cross.ini
ESP_PLATFORM_DIR  := platform/esp32c3

.PHONY: esp32c3
esp32c3:
	@if [ ! -f $(ESP_PLATFORM_DIR)/sdkconfig ]; then \
		cd $(ESP_PLATFORM_DIR) && idf.py set-target esp32c3; \
	fi
	@if [ ! -f $(ESP_PLATFORM_DIR)/build/compile_commands.json ]; then \
		cd $(ESP_PLATFORM_DIR) && idf.py reconfigure; \
	fi
	python3 scripts/gen-esp32c3-cross.py
	@if [ ! -f $(MESON_BUILDDIR_C3)/build.ninja ]; then \
		meson setup $(MESON_BUILDDIR_C3) --cross-file $(CROSS_FILE_C3); \
	fi
	meson compile -C $(MESON_BUILDDIR_C3)
	cd $(ESP_PLATFORM_DIR) && idf.py reconfigure && idf.py build
	@echo "Output: $(ESP_PLATFORM_DIR)/build/rt-claw.bin"

.PHONY: run-esp32c3
run-esp32c3: esp32c3
	tools/qemu-run.sh -M esp32c3

# --- ESP32-S3 (ESP-IDF) ---
# Prerequisite: source $$HOME/esp/esp-idf/export.sh
MESON_BUILDDIR_S3 := $(BUILD_DIR)/esp32s3
CROSS_FILE_S3     := platform/esp32s3/cross.ini
ESP_S3_PLATFORM   := platform/esp32s3

.PHONY: esp32s3
esp32s3:
	@if [ ! -f $(ESP_S3_PLATFORM)/sdkconfig ]; then \
		cd $(ESP_S3_PLATFORM) && idf.py set-target esp32s3; \
	fi
	@if [ ! -f $(ESP_S3_PLATFORM)/build/compile_commands.json ]; then \
		cd $(ESP_S3_PLATFORM) && idf.py reconfigure; \
	fi
	python3 scripts/gen-esp32s3-cross.py
	@if [ ! -f $(MESON_BUILDDIR_S3)/build.ninja ]; then \
		meson setup $(MESON_BUILDDIR_S3) --cross-file $(CROSS_FILE_S3); \
	fi
	meson compile -C $(MESON_BUILDDIR_S3)
	cd $(ESP_S3_PLATFORM) && idf.py reconfigure && idf.py build
	@echo "Output: $(ESP_S3_PLATFORM)/build/rt-claw.bin"

.PHONY: run-esp32s3
run-esp32s3: esp32s3
	tools/qemu-run.sh -M esp32s3

# --- Clean ---
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	cd platform/qemu-a9-rtthread && scons -c 2>/dev/null || true

.PHONY: clean-qemu-a9
clean-qemu-a9:
	rm -rf $(BUILD_DIR)/qemu-a9
	cd platform/qemu-a9-rtthread && scons -c 2>/dev/null || true

.PHONY: clean-esp32c3
clean-esp32c3:
	rm -rf $(BUILD_DIR)/esp32c3
	rm -f platform/esp32c3/cross.ini

.PHONY: clean-esp32s3
clean-esp32s3:
	rm -rf $(BUILD_DIR)/esp32s3
	rm -f platform/esp32s3/cross.ini

# --- Checks ---
.PHONY: check
check:
	scripts/check-patch.sh

.PHONY: check-staged
check-staged:
	scripts/check-patch.sh --staged
