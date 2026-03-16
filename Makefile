# rt-claw unified build entry
# Usage: make <platform>
#
# Run targets accept optional variables:
#   GDB=1       Enable GDB server (debug mode, port 1234)
#   GRAPHICS=1  Enable LCD display window (ESP32 QEMU only)

PROJECT_ROOT := $(shell pwd)
BUILD_DIR    := $(PROJECT_ROOT)/build

GDB      ?= 0
GRAPHICS ?= 0

.PHONY: help
help:
	@echo "rt-claw build system"
	@echo ""
	@echo "ESP32-C3 (board = qemu | devkit | xiaozhi-xmini):"
	@echo "  make build-esp32c3-qemu         Build for QEMU (default)"
	@echo "  make build-esp32c3-devkit    Build for devkit (4MB)"
	@echo "  make build-esp32c3-xiaozhi-xmini      Build for xiaozhi-xmini (16MB)"
	@echo "  make run-esp32c3-qemu           Build + launch QEMU simulator"
	@echo "  make run-esp32c3-<board>         Serial monitor (hardware)"
	@echo "  make flash-esp32c3-<board>       Build + flash (hardware)"
	@echo ""
	@echo "ESP32-S3 (board = qemu | default):"
	@echo "  make build-esp32s3-qemu         Build ESP32-S3 QEMU"
	@echo "  make run-esp32s3-qemu           Build + launch ESP32-S3 QEMU"
	@echo "  make build-esp32s3              Build ESP32-S3 real hardware"
	@echo "  make flash-esp32s3              Flash ESP32-S3"
	@echo "  make monitor-esp32s3            Serial monitor ESP32-S3"
	@echo ""
	@echo "vexpress-a9 (RT-Thread):"
	@echo "  make vexpress-a9-qemu           Build QEMU vexpress-a9"
	@echo "  make run-vexpress-a9-qemu       Build + launch vexpress-a9"
	@echo ""
	@echo "Options:"
	@echo "  GDB=1       Debug mode (GDB port 1234)"
	@echo "  GRAPHICS=1  LCD display window (QEMU only)"
	@echo ""
	@echo "Tests (unit — cross-compiled, QEMU semihosting):"
	@echo "  make test-unit             Build + run unit tests (vexpress-a9)"
	@echo ""
	@echo "Tests (functional — requires pre-built firmware):"
	@echo "  make test-functional       Run all functional tests"
	@echo "  make test-boot             Boot/banner tests only"
	@echo "  make test-shell            Shell command tests only"
	@echo "  make test-persist          KV persistence tests only"
	@echo "  make test-online           AI connectivity tests only"
	@echo ""
	@echo "Tests (CI — profile switch + build + test):"
	@echo "  make test-smoke-esp32c3    Smoke tests (ESP32-C3)"
	@echo "  make test-smoke-esp32s3    Smoke tests (ESP32-S3)"
	@echo "  make test-smoke-vexpress   Smoke tests (vexpress-a9)"
	@echo "  make test-online-esp32c3   AI online tests (ESP32-C3)"
	@echo "  make test-online-esp32s3   AI online tests (ESP32-S3)"
	@echo ""
	@echo "Clean:"
	@echo "  make clean                 Clean all build artifacts"
	@echo "  make clean-<platform>      Clean specific platform"
	@echo ""
	@echo "Checks:"
	@echo "  make check                 Run code style checks"

# --- QEMU vexpress-a9 (RT-Thread) ---
MESON_BUILDDIR_A9 := $(BUILD_DIR)/vexpress-a9-qemu
CROSS_FILE_A9     := platform/vexpress-a9/cross.ini
A9_PLATFORM       := platform/vexpress-a9

.PHONY: vexpress-a9-qemu
vexpress-a9-qemu:
	@if [ ! -f $(MESON_BUILDDIR_A9)/build.ninja ]; then \
		meson setup $(MESON_BUILDDIR_A9) --cross-file $(CROSS_FILE_A9); \
	fi
	meson compile -C $(MESON_BUILDDIR_A9)
	cd $(A9_PLATFORM) && scons -j$$(nproc)
	@cp -f $(A9_PLATFORM)/rtthread.elf $(MESON_BUILDDIR_A9)/
	@cp -f $(A9_PLATFORM)/rtthread.bin $(MESON_BUILDDIR_A9)/
	@cp -f $(A9_PLATFORM)/rtthread.map $(MESON_BUILDDIR_A9)/
	@echo "Output: $(MESON_BUILDDIR_A9)/"

.PHONY: run-vexpress-a9-qemu
run-vexpress-a9-qemu: vexpress-a9-qemu
	@if [ ! -f $(A9_PLATFORM)/sd.bin ]; then \
		echo "Creating SD card image..."; \
		dd if=/dev/zero of=$(A9_PLATFORM)/sd.bin bs=1024 count=65536; \
	fi
	@if [ "$(GDB)" = "1" ]; then \
		echo "Starting QEMU in debug mode (GDB port 1234)..."; \
		echo "Connect: arm-none-eabi-gdb $(MESON_BUILDDIR_A9)/rtthread.elf -ex 'target remote :1234'"; \
	fi
	qemu-system-arm --version
	qemu-system-arm \
		-M vexpress-a9 \
		-smp cpus=1 \
		-kernel $(MESON_BUILDDIR_A9)/rtthread.bin \
		-nographic \
		-sd $(A9_PLATFORM)/sd.bin \
		-nic user,model=lan9118 \
		$(if $(filter 1,$(GDB)),-S -s)

# --- ESP32-C3 unified targets ---
# Prerequisite: source $$HOME/esp/esp-idf/export.sh
#
# All boards share platform/esp32c3/ as the single ESP-IDF project.
# Board-specific config lives in platform/esp32c3/boards/<board>/.
#
# Each board gets its own build directory:
#   build/esp32c3-<board>/
#     meson/       Meson output (libclaw.a, libosal.a)
#     idf/         ESP-IDF output (firmware, sdkconfig, etc.)
#     cross.ini    Auto-generated Meson cross-file
#
# Boards:
#   qemu       QEMU virtual devkit (default, 4MB, OpenCores Ethernet)
#   devkit     Generic 4MB devkit (WiFi)
#   xiaozhi-xmini    XiaoZhi 16MB board (WiFi, OTA)
#
ESP_C3_DIR := platform/esp32c3

# Default aliases
.PHONY: build-esp32c3 run-esp32c3
build-esp32c3: build-esp32c3-qemu
run-esp32c3: run-esp32c3-qemu

# ---- All boards: build ----

.PHONY: build-esp32c3-qemu build-esp32c3-devkit build-esp32c3-xiaozhi-xmini
build-esp32c3-qemu:    C3_BOARD = qemu
build-esp32c3-qemu:    _c3-build
build-esp32c3-devkit:  C3_BOARD = devkit
build-esp32c3-devkit:  _c3-build
build-esp32c3-xiaozhi-xmini: C3_BOARD = xiaozhi-xmini
build-esp32c3-xiaozhi-xmini: _c3-build

# ---- QEMU: run (build + launch simulator) ----

.PHONY: run-esp32c3-qemu
run-esp32c3-qemu: build-esp32c3-qemu
	@echo ">>> Generating merged flash image ..."
	cd $(BUILD_DIR)/esp32c3-qemu/idf && esptool.py --chip esp32c3 merge_bin \
		--fill-flash-size 4MB -o flash_image.bin @flash_args
	@if [ "$(GDB)" = "1" ]; then \
		echo "Starting QEMU in debug mode (GDB port 1234)..."; \
		echo "Connect: riscv32-esp-elf-gdb $(BUILD_DIR)/esp32c3-qemu/idf/rt-claw.elf -ex 'target remote :1234'"; \
	fi
	@echo ">>> Starting QEMU (ESP32-C3, icount=1) ..."
	qemu-system-riscv32 \
		$(if $(filter 1,$(GRAPHICS)),,-nographic) \
		-icount 1 \
		-machine esp32c3 \
		-drive file=$(BUILD_DIR)/esp32c3-qemu/idf/flash_image.bin,if=mtd,format=raw \
		-global driver=timer.esp32c3.timg,property=wdt_disable,value=true \
		-nic user,model=open_eth \
		$(if $(filter 1,$(GDB)),-S -s)

# ---- QEMU: flash (not supported) ----

.PHONY: flash-esp32c3-qemu
flash-esp32c3-qemu:
	@echo "Error: flash is not supported for QEMU. Use 'make run-esp32c3-qemu' instead."
	@exit 1

# ---- Hardware boards: flash + run ----

.PHONY: flash-esp32c3-devkit flash-esp32c3-xiaozhi-xmini
flash-esp32c3-devkit:  build-esp32c3-devkit
	cd $(ESP_C3_DIR) && idf.py -B $(BUILD_DIR)/esp32c3-devkit/idf flash
flash-esp32c3-xiaozhi-xmini: build-esp32c3-xiaozhi-xmini
	cd $(ESP_C3_DIR) && idf.py -B $(BUILD_DIR)/esp32c3-xiaozhi-xmini/idf flash

.PHONY: run-esp32c3-devkit run-esp32c3-xiaozhi-xmini
run-esp32c3-devkit:
	cd $(ESP_C3_DIR) && idf.py -B $(BUILD_DIR)/esp32c3-devkit/idf monitor
run-esp32c3-xiaozhi-xmini:
	cd $(ESP_C3_DIR) && idf.py -B $(BUILD_DIR)/esp32c3-xiaozhi-xmini/idf monitor

# ---- Internal: shared build logic for all ESP32-C3 boards ----

.PHONY: _c3-build
_c3-build:
	@if [ ! -f $(BUILD_DIR)/esp32c3-$(C3_BOARD)/idf/build.ninja ]; then \
		cd $(ESP_C3_DIR) && idf.py \
			-B $(BUILD_DIR)/esp32c3-$(C3_BOARD)/idf \
			-DRTCLAW_BOARD=$(C3_BOARD) set-target esp32c3; \
	fi
	@if [ ! -f $(BUILD_DIR)/esp32c3-$(C3_BOARD)/idf/compile_commands.json ]; then \
		cd $(ESP_C3_DIR) && idf.py \
			-B $(BUILD_DIR)/esp32c3-$(C3_BOARD)/idf \
			-DRTCLAW_BOARD=$(C3_BOARD) reconfigure; \
	fi
	python3 scripts/gen-esp32c3-cross.py $(C3_BOARD)
	@if [ ! -f $(BUILD_DIR)/esp32c3-$(C3_BOARD)/meson/build.ninja ]; then \
		meson setup $(BUILD_DIR)/esp32c3-$(C3_BOARD)/meson \
			--cross-file $(BUILD_DIR)/esp32c3-$(C3_BOARD)/cross.ini; \
	fi
	meson compile -C $(BUILD_DIR)/esp32c3-$(C3_BOARD)/meson
	cd $(ESP_C3_DIR) && idf.py \
		-B $(BUILD_DIR)/esp32c3-$(C3_BOARD)/idf \
		-DRTCLAW_BOARD=$(C3_BOARD) reconfigure && \
	idf.py \
		-B $(BUILD_DIR)/esp32c3-$(C3_BOARD)/idf \
		-DRTCLAW_BOARD=$(C3_BOARD) build
	@echo "Output: $(BUILD_DIR)/esp32c3-$(C3_BOARD)/"

# --- ESP32-S3 unified targets ---
# Prerequisite: source $$HOME/esp/esp-idf/export.sh
#
# All boards share platform/esp32s3/ as the single ESP-IDF project.
# Board-specific config lives in platform/esp32s3/boards/<board>/.
#
# Boards:
#   qemu       QEMU virtual devkit (4MB, OpenCores Ethernet)
#   default    Real hardware (16MB, WiFi + PSRAM)
#
ESP_S3_DIR := platform/esp32s3

# Espressif QEMU xtensa binary (not in PATH by default after export.sh).
# Dynamically find the installed version to avoid hardcoding version strings.
QEMU_XTENSA := $(shell find $(HOME)/.espressif/tools/qemu-xtensa -name qemu-system-xtensa -type f 2>/dev/null | head -1)

# Default aliases
.PHONY: build-esp32s3 esp32s3-qemu
build-esp32s3: build-esp32s3-default

# ---- All boards: build ----

.PHONY: build-esp32s3-qemu build-esp32s3-default
build-esp32s3-qemu:    S3_BOARD = qemu
build-esp32s3-qemu:    _s3-build
build-esp32s3-default: S3_BOARD = default
build-esp32s3-default: _s3-build

# Backwards-compatible alias
esp32s3-qemu: build-esp32s3-qemu
.PHONY: esp32s3
esp32s3: build-esp32s3-default

# ---- QEMU: run (build + launch simulator) ----

.PHONY: run-esp32s3-qemu
run-esp32s3-qemu: build-esp32s3-qemu
	@echo ">>> Generating merged flash image ..."
	cd $(BUILD_DIR)/esp32s3-qemu/idf && esptool.py --chip esp32s3 merge_bin \
		--fill-flash-size 4MB -o flash_image.bin @flash_args
	@if [ "$(GDB)" = "1" ]; then \
		echo "Starting QEMU in debug mode (GDB port 1234)..."; \
		echo "Connect: xtensa-esp32s3-elf-gdb $(BUILD_DIR)/esp32s3-qemu/idf/rt-claw.elf -ex 'target remote :1234'"; \
	fi
	@echo ">>> Starting QEMU (ESP32-S3, icount=1) ..."
	$(QEMU_XTENSA) \
		$(if $(filter 1,$(GRAPHICS)),,-nographic) \
		-icount 1 \
		-machine esp32s3 \
		-drive file=$(BUILD_DIR)/esp32s3-qemu/idf/flash_image.bin,if=mtd,format=raw \
		-nic user,model=open_eth \
		$(if $(filter 1,$(GDB)),-S -s)

# ---- Hardware: flash + monitor ----

.PHONY: flash-esp32s3
flash-esp32s3: build-esp32s3-default
	cd $(ESP_S3_DIR) && idf.py -B $(BUILD_DIR)/esp32s3-default/idf flash

.PHONY: monitor-esp32s3
monitor-esp32s3:
	cd $(ESP_S3_DIR) && idf.py -B $(BUILD_DIR)/esp32s3-default/idf monitor

# ---- Internal: shared build logic for all ESP32-S3 boards ----

.PHONY: _s3-build
_s3-build:
	@if [ ! -f $(BUILD_DIR)/esp32s3-$(S3_BOARD)/idf/build.ninja ]; then \
		cd $(ESP_S3_DIR) && idf.py \
			-B $(BUILD_DIR)/esp32s3-$(S3_BOARD)/idf \
			-DRTCLAW_BOARD=$(S3_BOARD) set-target esp32s3; \
	fi
	@if [ ! -f $(BUILD_DIR)/esp32s3-$(S3_BOARD)/idf/compile_commands.json ]; then \
		cd $(ESP_S3_DIR) && idf.py \
			-B $(BUILD_DIR)/esp32s3-$(S3_BOARD)/idf \
			-DRTCLAW_BOARD=$(S3_BOARD) reconfigure; \
	fi
	python3 scripts/gen-esp32s3-cross.py $(S3_BOARD)
	@if [ ! -f $(BUILD_DIR)/esp32s3-$(S3_BOARD)/meson/build.ninja ]; then \
		meson setup $(BUILD_DIR)/esp32s3-$(S3_BOARD)/meson \
			--cross-file $(BUILD_DIR)/esp32s3-$(S3_BOARD)/cross.ini; \
	fi
	meson compile -C $(BUILD_DIR)/esp32s3-$(S3_BOARD)/meson
	cd $(ESP_S3_DIR) && idf.py \
		-B $(BUILD_DIR)/esp32s3-$(S3_BOARD)/idf \
		-DRTCLAW_BOARD=$(S3_BOARD) reconfigure && \
	idf.py \
		-B $(BUILD_DIR)/esp32s3-$(S3_BOARD)/idf \
		-DRTCLAW_BOARD=$(S3_BOARD) build
	@echo "Output: $(BUILD_DIR)/esp32s3-$(S3_BOARD)/"

# --- Clean ---

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	cd platform/vexpress-a9 && scons -c 2>/dev/null || true

.PHONY: clean-esp32c3
clean-esp32c3:
	rm -rf $(BUILD_DIR)/esp32c3-*

.PHONY: clean-vexpress-a9
clean-vexpress-a9:
	rm -rf $(BUILD_DIR)/vexpress-a9-qemu
	cd platform/vexpress-a9 && scons -c 2>/dev/null || true

.PHONY: clean-esp32s3
clean-esp32s3:
	rm -rf $(BUILD_DIR)/esp32s3-*

# --- Flash image generation (no interactive QEMU) ---

.PHONY: run-esp32c3-qemu-flash
run-esp32c3-qemu-flash: build-esp32c3-qemu
	@echo ">>> Generating merged flash image (ESP32-C3) ..."
	cd $(BUILD_DIR)/esp32c3-qemu/idf && esptool.py --chip esp32c3 merge_bin \
		--fill-flash-size 4MB -o flash_image.bin @flash_args

.PHONY: run-esp32s3-qemu-flash
run-esp32s3-qemu-flash: build-esp32s3-qemu
	@echo ">>> Generating merged flash image (ESP32-S3) ..."
	cd $(BUILD_DIR)/esp32s3-qemu/idf && esptool.py --chip esp32s3 merge_bin \
		--fill-flash-size 4MB -o flash_image.bin @flash_args

# --- Functional tests (Python) ---
#
# Quick targets: run against pre-built firmware.
# CI targets (test-smoke-*, test-online-*): profile switch + build + test.
#
# Env vars:
#   RTCLAW_TEST_PLATFORM  Platform to test (default: esp32c3-qemu)
#   RTCLAW_AI_API_KEY     Required for test-online-* targets

FUNCTEST = python3 -m unittest discover -s tests/functional -v

.PHONY: test-functional
test-functional:
	$(FUNCTEST)

.PHONY: test-boot
test-boot:
	$(FUNCTEST) -k TestBoot

.PHONY: test-shell
test-shell:
	$(FUNCTEST) -k TestShell

.PHONY: test-persist
test-persist:
	$(FUNCTEST) -k TestKvPersistence

.PHONY: test-online
test-online:
	$(FUNCTEST) -k TestAiOnline

# --- Unit tests (RT-Thread / vexpress-a9 / semihosting) ---

.PHONY: test-unit
test-unit:
	python3 tests/unit/run.py

# --- CI pipeline targets (profile switch + build + flash + test) ---

C3_DEFAULTS := platform/esp32c3/boards/qemu/sdkconfig.defaults
S3_DEFAULTS := platform/esp32s3/boards/qemu/sdkconfig.defaults

# Functional test runner macro: discover from tests/functional/, filter by pattern.
# Usage: $(call _functest,<platform>,<pattern>)
_functest = RTCLAW_TEST_PLATFORM=$(1) python3 -m unittest discover -s tests/functional -p '$(2)' -v

# Helper: switch sdkconfig profile, rebuild, run tests, restore.
# Usage: $(call _run_profile_test,<defaults-path>,<profile>,<build-target>,<flash-target>,<platform>,<pattern>)
define _run_profile_test
	@cp $(1) $(1).bak
	@cp $(1).$(2) $(1)
	@rm -rf $(BUILD_DIR)/$(5)/idf
	@trap 'mv $(1).bak $(1)' EXIT; \
	$(MAKE) $(3) && $(MAKE) $(4) && \
	$(call _functest,$(5),$(6))
endef

.PHONY: test-smoke-esp32c3
test-smoke-esp32c3:
	$(call _run_profile_test,$(C3_DEFAULTS),demo,build-esp32c3-qemu,run-esp32c3-qemu-flash,esp32c3-qemu,test_[bsk]*.py)

.PHONY: test-smoke-esp32s3
test-smoke-esp32s3:
	$(call _run_profile_test,$(S3_DEFAULTS),demo,build-esp32s3-qemu,run-esp32s3-qemu-flash,esp32s3-qemu,test_[bsk]*.py)

.PHONY: test-smoke-vexpress
test-smoke-vexpress: vexpress-a9-qemu
	$(call _functest,vexpress-a9-qemu,test_boot.py)

.PHONY: test-online-esp32c3
test-online-esp32c3: run-esp32c3-qemu-flash
	$(call _functest,esp32c3-qemu,test_ai_online.py)

.PHONY: test-online-esp32s3
test-online-esp32s3: run-esp32s3-qemu-flash
	$(call _functest,esp32s3-qemu,test_ai_online.py)

# --- Checks ---
.PHONY: check
check:
	scripts/check-patch.sh

.PHONY: check-staged
check-staged:
	scripts/check-patch.sh --staged
