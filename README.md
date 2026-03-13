<p align="center">
  <img src="images/logo.png" alt="RT-Claw" width="500">
</p>

<p align="center">
  <strong>Making AI Assistants Cheap Again</strong>
</p>

<p align="center">
  <a href="https://discord.gg/BZ9nFVzX"><img src="https://img.shields.io/badge/Discord-RT--Claw-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Discord"></a>
  <a href="https://qm.qq.com/q/heSPPC9De8"><img src="https://img.shields.io/badge/Join%20QQ-GTOC-brightgreen?style=for-the-badge&logo=QQ&logoColor=76bad9&color=76bad9" alt="QQ Group"></a>
  <a href="https://t.me/gevico_channel"><img src="https://img.shields.io/badge/Telegram-GTOC-blue?style=for-the-badge&logo=telegram" alt="Telegram"></a>
  <a href="https://space.bilibili.com/483048140"><img src="https://img.shields.io/badge/Bilibili-%E7%BB%9D%E5%AF%B9%E6%98%AF%E6%B3%BD%E6%96%87%E5%95%A6-FB7299?style=for-the-badge&logo=bilibili" alt="Bilibili"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT License"></a>
</p>

<p align="center"><a href="README_zh.md">中文</a> | <strong>English</strong></p>

**RT-Claw** is an [OpenClaw](https://github.com/openclaw/openclaw)-inspired intelligent assistant for embedded devices.
Multi-RTOS support via OSAL. Build swarm intelligence with networked nodes.
ESP32-S3 WiFi support adapted from [MimiClaw](https://github.com/memovai/mimiclaw).

> Deploy your own AI assistant on hardware that costs just one dollar — seamlessly integrated into your daily workflow, efficiently bridging the digital and physical worlds.

<p align="center">
  <img src="images/demo.png" alt="RT-Claw Demo — AI drawing on LCD via Tool Use" width="700">
</p>

[Architecture](docs/en/architecture.md) · [ESP32-C3 QEMU Guide](docs/en/esp32c3-qemu.md) · [Contributing](docs/en/contributing.md) · [Coding Style](docs/en/coding-style.md)

## Core Idea

rt-claw brings intelligence from the cloud to the edge through low-cost
embedded nodes and swarm networking.
Each node can sense the world, collaborate with others, and execute control
tasks in real time.

RT-Claw exposes atomized hardware capabilities — GPIO, sensors, LCD,
networking — as tools that an LLM can dynamically orchestrate. Adapt to any
scenario without writing, compiling, or flashing embedded code again.

## Features

| Feature | Description | Status |
|---------|-------------|--------|
| LLM Chat Engine | Interactive conversation with Claude API over HTTP | Done |
| Tool Use | LLM-driven hardware control (GPIO, system info, LCD) via function calling | Done |
| LCD Graphics | 320x240 RGB565 framebuffer with text, shapes, and drawing primitives; AI agent can draw on screen via tool calls | Done |
| Chat-first Shell | UART REPL where direct input goes to AI, /commands for system; UTF-8 support | Done |
| OSAL | Write once, run on FreeRTOS and RT-Thread with zero code changes | Done |
| Gateway | Thread-safe message routing between services | Done |
| Networking | Ethernet + HTTP client on ESP32-C3 QEMU; WiFi on real hardware | Done |
| Multi-Model API | Support mainstream LLM APIs: Claude, GPT, Gemini, DeepSeek, GLM, MiniMax, Grok, Moonshot, Baichuan, Qwen, Doubao, Llama (Ollama) | Planned |
| Web Config Portal | Lightweight built-in web page for configuring API keys, selecting models, and tuning parameters at runtime | Planned |
| Swarm Intelligence | Node discovery, heartbeat, distributed task scheduling | In Progress |
| Conversation Memory | Short-term RAM ring buffer + long-term NVS Flash persistent storage | Done |
| Skill Memory | Nodes learn and recall frequently used operation patterns | In Progress |
| Scheduled Tasks | Timer-driven task execution and periodic automation | Done |
| IM Integrations | Connect to Feishu, DingTalk, QQ, and Telegram as message channels | In Progress |
| Claw Skill Provider | Serve as a skill for other Claws, giving them the ability to sense and control the physical world | Planned |

## Architecture

```
+---------------------------------------------------+
|                rt-claw Application                |
|  gateway | swarm | net | ai_engine | tools | lcd  |
+---------------------------------------------------+
|            osal/claw_os.h  (OSAL API)             |
+-----------------+---------------------------------+
| FreeRTOS (IDF)  |          RT-Thread              |
+-----------------+---------------------------------+
| ESP32-C3 / S3   |  QEMU vexpress-a9               |
| WiFi / BLE      |  Ethernet / UART                |
+-----------------+---------------------------------+
```

## Supported Platforms

| Platform | Target | RTOS | Build | Status |
|----------|--------|------|-------|--------|
| ESP32-C3 | QEMU (Espressif fork) | ESP-IDF + FreeRTOS | Meson + CMake | AI verified |
| ESP32-S3 | QEMU (Espressif fork) | ESP-IDF + FreeRTOS | Meson + CMake | AI verified |
| ESP32-C3 | Real hardware | ESP-IDF + FreeRTOS | Meson + CMake | Untested |
| ESP32-S3 | Real hardware | ESP-IDF + FreeRTOS | Meson + CMake | Untested |
| QEMU vexpress-a9 | QEMU | RT-Thread | Meson + SCons | AI verified |

## Quick Start

### ESP32-S3 Real Hardware (WiFi + PSRAM)

> Requires an ESP32-S3 board with **16 MB flash** and **8 MB PSRAM** (e.g. ESP32-S3-DevKitC-1).

**1. Install system dependencies + ESP-IDF**

```bash
# Ubuntu / Debian
sudo apt install git wget flex bison gperf python3 python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 meson

# One-line ESP-IDF setup (clones ESP-IDF v5.4, installs toolchain)
./scripts/setup-esp-env.sh
```

**2. Configure API key**

```bash
source $HOME/esp/esp-idf/export.sh

idf.py -C platform/esp32s3 menuconfig
# Navigate: Component config → rt-claw Configuration → AI Engine
#   → API Key / API URL / Model
```

**3. Configure WiFi**

Option A — build-time defaults (menuconfig):

```bash
idf.py -C platform/esp32s3 menuconfig
# Navigate: Component config → rt-claw Configuration → WiFi
#   → Default WiFi SSID
#   → Default WiFi password
```

Option B — runtime via shell (saved to NVS, survives reboot):

```
/wifi_set <SSID> <PASSWORD>
```

NVS credentials (Option B) take priority over build-time defaults.
If neither is configured, the device boots offline and waits for `/wifi_set`.

**4. Build**

```bash
make esp32s3
```

**5. Flash and monitor**

```bash
# Flash (auto-detects serial port)
make flash-esp32s3

# Serial monitor (Ctrl+] to exit)
make monitor-esp32s3

# Or specify a port explicitly
idf.py -C platform/esp32s3 -p /dev/ttyUSB0 flash monitor
```

**6. Shell commands**

| Command | Description |
|---------|-------------|
| *(direct input)* | Send message to AI |
| `/ai_set key\|url\|model <v>` | Set AI API config (persisted to NVS) |
| `/ai_status` | Show current AI config |
| `/feishu_set <id> <secret>` | Set Feishu credentials (reboot to apply) |
| `/feishu_status` | Show Feishu config |
| `/wifi_set <SSID> <PASS>` | Save WiFi credentials to NVS |
| `/wifi_status` | Show connection state and IP |
| `/wifi_scan` | Scan nearby access points |
| `/help` | List all commands |

### ESP32-C3 (ESP-IDF + QEMU)

> **No hardware? No problem.** Open
> [cnb.cool/gevico.online/rtclaw/rt-claw](https://cnb.cool/gevico.online/rtclaw/rt-claw)
> to launch a CNB Cloud-Native IDE with all toolchains pre-installed.
> Build and run RT-Claw on QEMU directly in your browser.

**1. Install system dependencies**

```bash
# Ubuntu / Debian
sudo apt install git wget flex bison gperf python3 python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 libgcrypt20-dev libglib2.0-dev libpixman-1-dev \
    libsdl2-dev libslirp-dev meson

# Arch Linux
sudo pacman -S --needed libgcrypt glib2 pixman sdl2 libslirp \
    python cmake ninja gcc git wget flex bison meson
```

**2. Install ESP-IDF + QEMU**

```bash
# One-line setup (clones ESP-IDF v5.4, installs toolchain + QEMU)
./scripts/setup-esp-env.sh
```

**3. Choose a configuration preset**

| Preset | File | Shell | Feishu | Description |
|--------|------|-------|--------|-------------|
| **Quick Demo** | `sdkconfig.defaults.demo` | On | Off | Interactive terminal with full AI agent |
| **Feishu Bot** | `sdkconfig.defaults.feishu` | Off | On | Headless IM bot, saves RAM |
| **Default** | `sdkconfig.defaults` | Off | Off | Minimal base for custom builds |

```bash
source $HOME/esp/esp-idf/export.sh

# Pick one:
cp platform/esp32c3-qemu/sdkconfig.defaults.demo \
   platform/esp32c3-qemu/sdkconfig.defaults        # Quick Demo
# cp platform/esp32c3-qemu/sdkconfig.defaults.feishu \
#    platform/esp32c3-qemu/sdkconfig.defaults       # Feishu Bot

idf.py -C platform/esp32c3-qemu set-target esp32c3
```

All presets include: AI engine, Tool Use, swarm heartbeat, scheduler,
LCD, skills, and boot-time AI connectivity test.

**4. Configure API key**

Option A — environment variables (all platforms):

```bash
export RTCLAW_AI_API_KEY='<your-api-key>'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'
```

Option B — ESP-IDF menuconfig:

```bash
idf.py -C platform/esp32c3-qemu menuconfig
# Navigate: Component config → rt-claw Configuration → AI Engine
```

Option C — Meson option:

```bash
meson configure build/esp32c3-qemu -Dai_api_key='<your-api-key>'
```

**5. (Optional) Configure Feishu bot**

Option A — environment variables:

```bash
export RTCLAW_FEISHU_APP_ID='<your-app-id>'
export RTCLAW_FEISHU_APP_SECRET='<your-app-secret>'
```

Option B — ESP-IDF menuconfig:

```bash
idf.py -C platform/esp32c3-qemu menuconfig
# Navigate: Component config → rt-claw Configuration → Feishu (Lark) Integration
```

Create an app on [Feishu Open Platform](https://open.feishu.cn), enable
**Event Subscription → Long Connection** mode, and subscribe to
`im.message.receive_v1`. The device establishes a WebSocket long connection
on boot — no public IP required.

**6. Build and run**

```bash
# Unified build (recommended)
make esp32c3-qemu

# Run on QEMU
make run-esp32c3-qemu

# Or flash to real hardware (untested)
idf.py -C platform/esp32c3-qemu -p /dev/ttyUSB0 flash monitor
```

### QEMU vexpress-a9 (RT-Thread)

```bash
# Prerequisites: arm-none-eabi-gcc, qemu-system-arm, scons, meson, ninja

# Configure via env vars (picked up at build time)
export RTCLAW_AI_API_KEY='<your-key>'

# Unified build
make vexpress-a9-qemu

# Start API proxy (RT-Thread has no TLS, proxy forwards HTTP->HTTPS)
python3 scripts/api-proxy.py https://api.anthropic.com &

# Run
make run-vexpress-a9-qemu
```

## Project Structure

```
rt-claw/
├── meson.build                  # Meson build definition (cross-compiles claw + osal)
├── meson_options.txt            # Meson build options (osal backend, features, AI config)
├── Makefile                     # Unified build entry (make esp32c3-qemu / make vexpress-a9-qemu)
├── include/                     # Unified public headers (aligned with claw/ and osal/)
│   ├── claw/                   #   Public headers for claw/
│   │   ├── claw_config.h       #     Project configuration
│   │   ├── claw_init.h         #     Boot entry API
│   │   ├── core/               #     Gateway, scheduler, service interface
│   │   ├── services/           #     AI, net, swarm, IM service headers
│   │   ├── shell/              #     Shared shell command headers
│   │   └── tools/              #     Tool Use framework headers
│   └── osal/                   #   Public headers for osal/
│       ├── claw_os.h           #     OSAL API
│       └── claw_net.h          #     Network abstraction
├── osal/                        # OS Abstraction Layer
│   ├── freertos/                #   FreeRTOS implementation
│   └── rtthread/                #   RT-Thread implementation
├── claw/                        # Platform-independent core
│   ├── claw_init.c             #   Boot entry point
│   ├── core/                   #   Gateway, scheduler
│   ├── services/ai/            #   LLM chat engine (Claude API)
│   ├── services/net/           #   Network service
│   ├── services/swarm/         #   Swarm intelligence
│   └── tools/                  #   Tool Use framework (GPIO, system, LCD)
├── platform/
│   ├── esp32c3-qemu/           # ESP32-C3 QEMU (ESP-IDF, Meson + CMake)
│   ├── esp32s3-qemu/           # ESP32-S3 QEMU (ESP-IDF, Meson + CMake)
│   └── vexpress-a9-qemu/      # RT-Thread BSP (Meson + SCons)
├── vendor/
│   ├── lib/cjson/              # cJSON library
│   └── os/
│       ├── freertos/           # FreeRTOS-Kernel (submodule)
│       └── rt-thread/          # RT-Thread (submodule)
├── docs/
│   ├── en/                     # English documentation
│   └── zh/                     # Chinese documentation
└── scripts/
    ├── api-proxy.py            # HTTP→HTTPS proxy for QEMU (no TLS on RT-Thread)
    ├── setup-esp-env.sh        # Install ESP-IDF + QEMU
    ├── gen-esp32c3-cross.py    # Auto-generate Meson cross-file from ESP-IDF
    └── ...
```

## Community

Join the GTOC (Gevico Open-Source Community) channels:

- **Discord**: [RT-Claw](https://discord.gg/BZ9nFVzX)
- **QQ Group**: [Join](https://qm.qq.com/q/heSPPC9De8)
- **Telegram**: [GTOC Channel](https://t.me/gevico_channel)
- **Bilibili**: [Zevorn](https://space.bilibili.com/483048140)
- **WeChat**: GTOC

## Documentation

- [Coding Style](docs/en/coding-style.md)
- [Contributing](docs/en/contributing.md)
- [Architecture](docs/en/architecture.md)
- [ESP32-C3 QEMU Guide](docs/en/esp32c3-qemu.md)

## License

MIT
