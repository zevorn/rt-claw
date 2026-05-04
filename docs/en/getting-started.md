# Getting Started

**English** | [中文](../zh/getting-started.md)

This guide covers building, flashing, and running rt-claw on every supported
platform: ESP32-C3, ESP32-S3, vexpress-a9 (RT-Thread), Zynq-A9 (FreeRTOS),
Zephyr (Cortex-A9 / Cortex-M3), and Linux native.

## Prerequisites

### System Dependencies

**Ubuntu / Debian:**

```bash
sudo apt-get install -y git wget flex bison gperf python3 python3-pip \
    python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 gcc-arm-none-eabi qemu-system-arm scons meson \
    libcurl4-openssl-dev ca-certificates \
    libgcrypt20-dev libglib2.0-dev libpixman-1-dev libsdl2-dev libslirp-dev
```

**Arch Linux:**

```bash
sudo pacman -S --needed git wget flex bison python python-pip cmake ninja \
    ccache dfu-util libusb arm-none-eabi-gcc arm-none-eabi-newlib \
    qemu-system-arm scons meson curl ca-certificates \
    libgcrypt glib2 pixman sdl2 libslirp
```

### ESP-IDF + QEMU

Required for all ESP32-C3 and ESP32-S3 targets. The setup script installs
ESP-IDF v5.4, the toolchain, and Espressif QEMU (riscv32 + xtensa):

```bash
./scripts/setup-esp-env.sh
source $HOME/esp/esp-idf/export.sh    # required before every build session
```

### vexpress-a9 Only

The ARM QEMU and SCons packages from the system dependencies above are
sufficient. No ESP-IDF needed.

## Configuration

### AI Credentials

Set via environment variables (recommended) or Meson options.

| Variable | Purpose | Example |
|----------|---------|---------|
| `RTCLAW_AI_API_KEY` | API key | `sk-ant-...` |
| `RTCLAW_AI_API_URL` | API endpoint | `https://api.anthropic.com/v1/messages` |
| `RTCLAW_AI_MODEL` | Model name | `claude-sonnet-4-6` |

```bash
# Environment variables (shell profile or before build)
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

# Or pass directly to Meson (highest priority)
meson setup build/... --cross-file ... -Dai_api_key='sk-...' -Dai_api_url='...'
```

**Priority:** Meson option > environment variable > `claw_config.h` default.

### Feishu (Lark) IM

To use the Feishu bot gateway:

1. Create an app at [open.feishu.cn](https://open.feishu.cn)
2. Enable **Event Subscription** with long connection mode
3. Subscribe to `im.message.receive_v1`
4. Export credentials:

```bash
export RTCLAW_FEISHU_APP_ID='cli_...'
export RTCLAW_FEISHU_APP_SECRET='...'
```

Or use Meson options: `-Dfeishu=true -Dfeishu_app_id=... -Dfeishu_app_secret=...`

### Telegram Bot

To use the Telegram bot integration:

1. Create a bot via [@BotFather](https://t.me/BotFather) on Telegram
2. Copy the bot token (e.g. `123456:ABC-DEF...`)
3. Export credentials:

```bash
export RTCLAW_TELEGRAM_BOT_TOKEN='123456:ABC-DEF...'
```

Or use Meson options: `-Dtelegram=true -Dtelegram_bot_token='...'`

For QEMU testing (no TLS), start the Telegram API proxy on a separate port:

```bash
python3 scripts/api-proxy.py https://api.telegram.org 8889
```

And set the API URL to the proxy:

```bash
meson configure build/<platform>/meson \
    -Dtelegram=true \
    -Dtelegram_bot_token='...' \
    -Dtelegram_api_url='http://10.0.2.2:8889'
```

**Real hardware** (ESP32-C3/S3 with WiFi) has native TLS, no proxy needed:

```bash
export RTCLAW_TELEGRAM_BOT_TOKEN='123456:ABC-DEF...'

# ESP32-C3 xiaozhi-xmini example
make build-esp32c3-xiaozhi-xmini
make flash-esp32c3-xiaozhi-xmini
make run-esp32c3-xiaozhi-xmini    # serial monitor — watch [telegram] logs

# ESP32-S3 example
make esp32s3
make flash-esp32s3
make monitor-esp32s3
```

After flashing, configure WiFi via `/wifi_set <SSID> <PASS>`, then reboot.
The device connects to WiFi, starts polling Telegram, and begins receiving
messages from your bot.

### WiFi (Hardware Boards Only)

Configure via `idf.py menuconfig` before build, or at runtime:

```
/wifi_set <SSID> <PASSWORD>
```

## ESP32-C3

### QEMU (No Hardware Needed)

The QEMU board ships with three configuration presets:

| Preset | File | Shell | Feishu | Use Case |
|--------|------|-------|--------|----------|
| Quick Demo | `sdkconfig.defaults.demo` | On | Off | Interactive shell + full AI agent |
| Feishu Bot | `sdkconfig.defaults.feishu` | Off | On | Headless IM bot |
| Default | `sdkconfig.defaults` | — | — | Minimal / custom |

To switch presets, copy the desired file before building:

```bash
source $HOME/esp/esp-idf/export.sh

# Select a preset (optional — skip to use the current default)
cp platform/esp32c3/boards/qemu/sdkconfig.defaults.demo \
   platform/esp32c3/boards/qemu/sdkconfig.defaults

# Build and run
make build-esp32c3-qemu
make run-esp32c3-qemu               # serial only
make run-esp32c3-qemu GRAPHICS=1    # with LCD window
make run-esp32c3-qemu GDB=1         # debug (GDB port 1234)
```

### devkit (4 MB, Real Hardware)

Generic ESP32-C3 development board with WiFi.

```bash
source $HOME/esp/esp-idf/export.sh
make build-esp32c3-devkit
make flash-esp32c3-devkit
make run-esp32c3-devkit              # serial monitor
```

### xiaozhi-xmini (16 MB, Real Hardware)

XiaoZhi Mini board with 16 MB flash, WiFi, and ES8311 audio codec.

```bash
source $HOME/esp/esp-idf/export.sh
make build-esp32c3-xiaozhi-xmini
make flash-esp32c3-xiaozhi-xmini
make run-esp32c3-xiaozhi-xmini      # serial monitor
```

## ESP32-S3

### QEMU

```bash
source $HOME/esp/esp-idf/export.sh
make build-esp32s3-qemu
make run-esp32s3-qemu
make run-esp32s3-qemu GRAPHICS=1    # with LCD window
make run-esp32s3-qemu GDB=1         # debug (GDB port 1234)
```

### Real Hardware (16 MB Flash + 8 MB PSRAM)

```bash
source $HOME/esp/esp-idf/export.sh
make esp32s3                         # alias for build-esp32s3-default
make flash-esp32s3
make monitor-esp32s3                 # serial monitor
```

WiFi config: `idf.py menuconfig` before build, or `/wifi_set` at runtime.

## QEMU vexpress-a9 (RT-Thread)

No ESP-IDF required. Uses Meson + SCons.

```bash
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

make vexpress-a9-qemu

# RT-Thread has no TLS — start the HTTP-to-HTTPS proxy
python3 scripts/api-proxy.py https://api.anthropic.com &

make run-vexpress-a9-qemu
make run-vexpress-a9-qemu GDB=1     # debug (GDB port 1234)
```

> **Note:** The vexpress-a9 RT-Thread port does not support TLS. The bundled
> `scripts/api-proxy.py` bridges HTTP (QEMU) to HTTPS (upstream API). It must
> be running before the firmware makes AI requests.

## QEMU Zynq-A9 (FreeRTOS + FreeRTOS+TCP)

No ESP-IDF required. Uses Meson only (full firmware build).

```bash
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

make build-zynq-a9-qemu
make run-zynq-a9-qemu
make run-zynq-a9-qemu GDB=1        # debug (GDB port 1234)
```

> **Note:** Zynq-A9 uses FreeRTOS+TCP with Cadence GEM Ethernet.
> Like vexpress-a9, it does not support TLS — use `scripts/api-proxy.py`
> to bridge HTTP to HTTPS for AI API requests.

## Linux Native

No cross-compiler or QEMU needed. Builds and runs directly on the host.

```bash
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

make build-linux
make run-linux
```

The Linux OSAL uses pthreads, libcurl (with TLS), and file-based KV storage.
See [HTTPS on Embedded Linux](#https-on-embedded-linux) for deployment
considerations.

## Zephyr (Cortex-A9 / Cortex-M3)

### Prerequisites

- **Zephyr SDK 1.0.1** -- install via the official
  [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/)
- Git submodules initialized (`git submodule update --init --recursive`)

No ESP-IDF required. The Zephyr build uses CMake (west) and compiles
OSAL + claw sources directly into the Zephyr application (no Meson
prebuilt `.a` files).

### Cortex-A9 QEMU (Xilinx QEMU)

```bash
export RTCLAW_AI_API_KEY='sk-...'
# TLS not yet enabled by default — use api-proxy for HTTPS
export RTCLAW_AI_API_URL='http://10.0.2.2:8888/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

# Start proxy in another terminal: python3 scripts/api-proxy.py
make build-zephyr-cortex-a9-qemu
make run-zephyr-cortex-a9-qemu
```

> **Note:** The Cortex-A9 target requires Xilinx QEMU (`qemu-system-aarch64`
> with Xilinx machine support), not standard upstream QEMU.

### Cortex-M3 QEMU (Standard QEMU)

```bash
make build-zephyr-cortex-m3-qemu
make run-zephyr-cortex-m3-qemu
```

The Cortex-M3 target uses the standard `qemu-system-arm` with the
`lm3s6965evb` machine, which is included in the system QEMU package.

> **Note:** Zephyr includes mbedTLS for native HTTPS, but TLS
> Kconfig is not yet enabled by default. Until then, use
> `scripts/api-proxy.py` for AI API HTTPS requests, same as
> vexpress-a9 and Zynq-A9.

## HTTPS on Embedded Linux

When deploying the Linux native build on embedded Linux systems (e.g.,
Zynq running PetaLinux, Raspberry Pi, Buildroot/Yocto targets), HTTPS
connections to AI APIs and IM services require proper TLS infrastructure.

### System Time

TLS certificate validation depends on accurate system time. Embedded
systems often boot with epoch time (1970-01-01), causing all HTTPS
connections to fail with certificate expiry errors.

**Fix:** Sync time via NTP on boot.

```bash
# One-shot NTP sync (busybox / embedded)
ntpd -q -p pool.ntp.org

# Or with chrony / systemd-timesyncd
timedatectl set-ntp true
```

If no network NTP is available, set time manually or from an RTC:

```bash
date -s "2026-03-23 12:00:00"
hwclock -s                          # read from hardware RTC
```

> **Symptom:** libcurl returns `CURLE_SSL_CACERT` (60) or
> `CURLE_PEER_FAILED_VERIFICATION` (51) immediately after boot.
> Check `date` — if it shows 1970, time sync is the issue.

### CA Certificates

libcurl needs root CA certificates to verify HTTPS server identity.
Desktop Linux ships `ca-certificates`, but minimal embedded rootfs
images often omit them.

**Debian / Ubuntu / PetaLinux:**

```bash
apt-get install -y ca-certificates
update-ca-certificates
```

**Buildroot:**

Enable `BR2_PACKAGE_CA_CERTIFICATES` in `make menuconfig`.

**Yocto:**

Add to your image recipe:

```
IMAGE_INSTALL:append = " ca-certificates"
```

**Manual (any system):**

```bash
# Download Mozilla CA bundle
curl -o /etc/ssl/certs/ca-certificates.crt \
    https://curl.se/ca/cacert.pem

# Tell libcurl where to find it
export CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt
```

> **Symptom:** libcurl returns `CURLE_SSL_CACERT` (60) even with
> correct time. Check if `/etc/ssl/certs/ca-certificates.crt` exists.

### libcurl with TLS

The Linux OSAL (`osal/linux/`) uses libcurl for all HTTP/HTTPS requests.
libcurl must be built with TLS backend support.

**Check TLS support:**

```bash
curl --version | grep -i ssl
# Should show: OpenSSL/x.x.x or mbedTLS/x.x.x or similar
```

**Common embedded Linux packages:**

| Distribution | Package | TLS Backend |
|-------------|---------|-------------|
| Debian / Ubuntu | `libcurl4-openssl-dev` | OpenSSL |
| Buildroot | `BR2_PACKAGE_LIBCURL` + `BR2_PACKAGE_OPENSSL` | OpenSSL |
| Yocto | `libcurl` (with `PACKAGECONFIG:append = "ssl"`) | OpenSSL |
| Alpine | `curl-dev` | OpenSSL or mbedTLS |

> **Symptom:** Meson setup fails with `Dependency "libcurl" not found`.
> Install `libcurl4-openssl-dev` (or equivalent).

### DNS Resolution

Ensure `/etc/resolv.conf` has valid nameservers:

```bash
echo "nameserver 8.8.8.8" > /etc/resolv.conf
```

Without DNS, libcurl cannot resolve API hostnames like
`api.anthropic.com` or `open.feishu.cn`.

### Quick Checklist

| Item | Check | Fix |
|------|-------|-----|
| System time | `date` shows current year | NTP sync or manual `date -s` |
| CA certificates | `/etc/ssl/certs/ca-certificates.crt` exists | Install `ca-certificates` package |
| libcurl TLS | `curl --version` shows SSL | Install libcurl with OpenSSL support |
| DNS | `nslookup api.anthropic.com` resolves | Configure `/etc/resolv.conf` |
| Connectivity | `curl -I https://api.anthropic.com` returns 200 | Check firewall / proxy settings |

## Debug with GDB

All QEMU targets support `GDB=1` which pauses the CPU at startup and opens
a GDB server on port 1234.

**ESP32-C3 example:**

```bash
# Terminal 1 — start QEMU in debug mode
make run-esp32c3-qemu GDB=1

# Terminal 2 — attach GDB
riscv32-esp-elf-gdb build/esp32c3-qemu/idf/rt-claw.elf \
    -ex 'target remote :1234'
```

**ESP32-S3 example:**

```bash
# Terminal 1
make run-esp32s3-qemu GDB=1

# Terminal 2
xtensa-esp32s3-elf-gdb build/esp32s3-qemu/idf/rt-claw.elf \
    -ex 'target remote :1234'
```

**vexpress-a9 example:**

```bash
# Terminal 1
make run-vexpress-a9-qemu GDB=1

# Terminal 2
arm-none-eabi-gdb build/vexpress-a9-qemu/rtthread.elf \
    -ex 'target remote :1234'
```

## CNB Cloud IDE

For browser-based development without local setup, open the project on CNB:

<https://cnb.cool/gevico.online/rtclaw/rt-claw>

The cloud environment comes pre-configured with ESP-IDF and QEMU.
