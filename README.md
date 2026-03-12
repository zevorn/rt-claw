<p align="center">
  <img src="logo.png" alt="RT-Claw" width="500">
</p>

<p align="center">
  <strong>Making AI Assistants Cheap</strong>
</p>

<p align="center">
  <a href="https://qm.qq.com/q/heSPPC9De8"><img src="https://img.shields.io/badge/Join%20QQ-GTOC-brightgreen?style=for-the-badge&logo=QQ&logoColor=76bad9&color=76bad9" alt="QQ Group"></a>
  <a href="https://t.me/gevico_channel"><img src="https://img.shields.io/badge/Telegram-GTOC-blue?style=for-the-badge&logo=telegram" alt="Telegram"></a>
  <a href="https://space.bilibili.com/483048140"><img src="https://img.shields.io/badge/Bilibili-%E7%BB%9D%E5%AF%B9%E6%98%AF%E6%B3%BD%E6%96%87%E5%95%A6-FB7299?style=for-the-badge&logo=bilibili" alt="Bilibili"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT License"></a>
</p>

[中文](README_zh.md) | **English**

**RT-Claw** is an [OpenClaw](https://github.com/openclaw/openclaw)-inspired intelligent assistant for embedded devices.
Multi-RTOS support via OSAL. Build swarm intelligence with networked nodes.

[Architecture](docs/en/architecture.md) · [ESP32-C3 QEMU Guide](docs/en/esp32c3-qemu.md) · [Contributing](docs/en/contributing.md) · [Coding Style](docs/en/coding-style.md)

## Core Idea

rt-claw brings intelligence from the cloud to the edge through low-cost
embedded nodes and swarm networking.
Each node can sense the world, collaborate with others, and execute control
tasks in real time.

## Features

- **LLM Chat Engine** — interactive conversation with Claude API over HTTP
- **Tool Use** — LLM-driven hardware control (GPIO, system info, LCD) via
  function calling
- **LCD Graphics** — 320x240 RGB565 framebuffer with text, shapes, and
  drawing primitives; AI agent can draw on screen via tool calls
- **ESP-IDF Shell** — esp_console-based REPL with line editing, history,
  and UTF-8 input support
- **OSAL** — write once, run on FreeRTOS and RT-Thread with zero code changes
- **Gateway** — thread-safe message routing between services
- **Networking** — Ethernet + HTTP client on ESP32-C3 QEMU; WiFi on real
  hardware
- **Swarm** (planned) — node discovery, heartbeat, task distribution

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

| Platform | RTOS | Build System | Status |
|----------|------|-------------|--------|
| ESP32-C3 | ESP-IDF + FreeRTOS | CMake (idf.py) | Networking + AI working on QEMU |
| QEMU vexpress-a9 | RT-Thread | SCons | Boot verified |

## Quick Start

### ESP32-C3 (ESP-IDF + QEMU)

```bash
# Prerequisites: ESP-IDF v5.x, Espressif QEMU
source $HOME/esp/esp-idf/export.sh
cd platform/esp32c3
idf.py set-target esp32c3
idf.py build
idf.py qemu monitor                   # QEMU (serial only)
idf.py qemu --graphics monitor        # QEMU with LCD display
idf.py -p /dev/ttyUSB0 flash monitor  # real hardware
```

### QEMU vexpress-a9 (RT-Thread)

```bash
# Prerequisites: arm-none-eabi-gcc, qemu-system-arm, scons
cd platform/qemu-a9-rtthread
scons -j$(nproc)
../../tools/qemu-run.sh
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
├── docs/
│   ├── en/                      # English documentation
│   └── zh/                      # Chinese documentation
├── scripts/                     # Code style & dev tools
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
