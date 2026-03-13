<p align="center">
  <img src="images/logo.png" alt="RT-Claw" width="500">
</p>

<p align="center">
  <strong>Making AI Assistants Cheap Again</strong>
</p>

<p align="center">
  <a href="https://qm.qq.com/q/heSPPC9De8"><img src="https://img.shields.io/badge/Join%20QQ-GTOC-brightgreen?style=for-the-badge&logo=QQ&logoColor=76bad9&color=76bad9" alt="QQ Group"></a>
  <a href="https://t.me/gevico_channel"><img src="https://img.shields.io/badge/Telegram-GTOC-blue?style=for-the-badge&logo=telegram" alt="Telegram"></a>
  <a href="https://space.bilibili.com/483048140"><img src="https://img.shields.io/badge/Bilibili-%E7%BB%9D%E5%AF%B9%E6%98%AF%E6%B3%BD%E6%96%87%E5%95%A6-FB7299?style=for-the-badge&logo=bilibili" alt="Bilibili"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT License"></a>
</p>

<p align="center"><a href="README_zh.md">中文</a> | <strong>English</strong></p>

**RT-Claw** is an [OpenClaw](https://github.com/openclaw/openclaw)-inspired intelligent assistant for embedded devices.
Multi-RTOS support via OSAL. Build swarm intelligence with networked nodes.

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
|               claw_os.h  (OSAL API)               |
+-----------------+---------------------------------+
| FreeRTOS (IDF)  |          RT-Thread              |
+-----------------+---------------------------------+
| ESP32-C3        |  QEMU vexpress-a9               |
| WiFi / BLE      |  Ethernet / UART                |
+-----------------+---------------------------------+
```

## Supported Platforms

| Platform | Target | RTOS | Build | Status |
|----------|--------|------|-------|--------|
| ESP32-C3 | QEMU (Espressif fork) | ESP-IDF + FreeRTOS | Meson + CMake | AI verified |
| ESP32-C3 | Real hardware | ESP-IDF + FreeRTOS | Meson + CMake | Untested |
| QEMU vexpress-a9 | QEMU | RT-Thread | Meson + SCons | AI verified |

## Quick Start

### ESP32-C3 (ESP-IDF + QEMU)

**1. Install system dependencies**

```bash
# Ubuntu / Debian
sudo apt install git wget flex bison gperf python3 python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 libgcrypt20-dev libglib2.0-dev libpixman-1-dev \
    libsdl2-dev libslirp-dev

# Arch Linux
sudo pacman -S --needed libgcrypt glib2 pixman sdl2 libslirp \
    python cmake ninja gcc git wget flex bison
```

**2. Install ESP-IDF + QEMU**

```bash
# One-line setup (clones ESP-IDF v5.4, installs toolchain + QEMU)
./tools/setup-esp-env.sh
```

**3. Choose a configuration preset**

| Preset | File | Shell | Feishu | Description |
|--------|------|-------|--------|-------------|
| **Quick Demo** | `sdkconfig.defaults.demo` | On | Off | Interactive terminal with full AI agent |
| **Feishu Bot** | `sdkconfig.defaults.feishu` | Off | On | Headless IM bot, saves RAM |
| **Default** | `sdkconfig.defaults` | Off | Off | Minimal base for custom builds |

```bash
source $HOME/esp/esp-idf/export.sh
cd platform/esp32c3

# Pick one:
cp sdkconfig.defaults.demo sdkconfig.defaults    # Quick Demo
# cp sdkconfig.defaults.feishu sdkconfig.defaults # Feishu Bot

idf.py set-target esp32c3
```

All presets include: AI engine, Tool Use, swarm heartbeat, scheduler,
LCD, skills, and boot-time AI connectivity test.

**4. Configure API key**

```bash
idf.py menuconfig
# Navigate: rt-claw Configuration → AI Engine
#   - LLM API Key:          <your-api-key>
#   - LLM API endpoint URL: https://api.anthropic.com/v1/messages
#   - LLM model name:       claude-sonnet-4-6
```

**5. (Optional) Configure Feishu bot**

```bash
idf.py menuconfig
# Navigate: rt-claw Configuration → Feishu (Lark) Integration
#   - Enable Feishu IM integration: [*]
#   - Feishu App ID:     <your-app-id>
#   - Feishu App Secret: <your-app-secret>
```

Create an app on [Feishu Open Platform](https://open.feishu.cn), enable
**Event Subscription → Long Connection** mode, and subscribe to
`im.message.receive_v1`. The device establishes a WebSocket long connection
on boot — no public IP required.

**6. Build and run**

```bash
# Unified build (recommended)
make esp32c3

# Run on QEMU
./tools/qemu-run.sh -m esp32c3

# Or flash to real hardware (untested)
idf.py -p /dev/ttyUSB0 flash monitor
```

### QEMU vexpress-a9 (RT-Thread)

```bash
# Prerequisites: arm-none-eabi-gcc, qemu-system-arm, scons, meson, ninja

# Unified build
make qemu-a9

# Configure API key (optional)
meson configure build/qemu-a9 -Dai_api_key='<your-key>'
meson compile -C build/qemu-a9
cd platform/qemu-a9-rtthread && scons -j$(nproc)

# Start API proxy (RT-Thread has no TLS, proxy forwards HTTP->HTTPS)
python3 tools/api-proxy.py https://api.anthropic.com &

# Run
./tools/qemu-run.sh -m qemu-a9
```

## Project Structure

```
rt-claw/
├── osal/                        # OS Abstraction Layer
│   ├── include/claw_os.h       #   Unified RTOS API
│   ├── freertos/                #   FreeRTOS implementation
│   └── rtthread/                #   RT-Thread implementation
├── src/                         # Platform-independent core
│   ├── claw_init.*              #   Boot entry point
│   ├── claw_config.h            #   Project configuration
│   ├── core/gateway.*           #   Message routing
│   ├── services/ai/             #   LLM chat engine (Claude API)
│   ├── services/net/            #   Network service
│   ├── services/swarm/          #   Swarm intelligence
│   └── tools/                   #   Tool Use framework (GPIO, system, LCD)
├── platform/
│   ├── esp32c3/                 # ESP-IDF project (CMake)
│   └── qemu-a9-rtthread/       # RT-Thread BSP (SCons)
├── vendor/
│   ├── freertos/                # FreeRTOS-Kernel (submodule)
│   └── rt-thread/               # RT-Thread (submodule)
├── meson.build                  # Meson build definition (cross-compiles src + osal)
├── meson.options                # Meson build options (osal backend, features, AI config)
├── Makefile                     # Unified build entry (make esp32c3 / make qemu-a9)
├── docs/
│   ├── en/                      # English documentation
│   └── zh/                      # Chinese documentation
├── scripts/
│   ├── gen-esp32c3-cross.py     # Auto-generate Meson cross-file from ESP-IDF
│   └── ...
└── tools/                       # Build, launch & dev scripts
```

## Community

Join the GTOC (Gevico Open-Source Community) channels:

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
