<p align="center">
  <img src="images/logo.png" alt="RT-Claw" width="500">
</p>

<p align="center">
  <strong>Making AI Assistants Cheap Again</strong>
</p>

<p align="center">
  <a href="https://discord.gg/gcxwYXQr"><img src="https://img.shields.io/badge/Discord-RT--Claw-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Discord"></a>
  <a href="https://qm.qq.com/q/heSPPC9De8"><img src="https://img.shields.io/badge/Join%20QQ-GTOC-brightgreen?style=for-the-badge&logo=QQ&logoColor=76bad9&color=76bad9" alt="QQ Group"></a>
  <a href="https://t.me/gevico_channel"><img src="https://img.shields.io/badge/Telegram-GTOC-blue?style=for-the-badge&logo=telegram" alt="Telegram"></a>
  <a href="https://space.bilibili.com/483048140"><img src="https://img.shields.io/badge/Bilibili-%E7%BB%9D%E5%AF%B9%E6%98%AF%E6%B3%BD%E6%96%87%E5%95%A6-FB7299?style=for-the-badge&logo=bilibili" alt="Bilibili"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT License"></a>
</p>

<p align="center"><a href="README_zh.md">中文</a> | <strong>English</strong></p>

**RT-Claw** is an [OpenClaw](https://github.com/openclaw/openclaw)-inspired intelligent assistant for embedded devices.
Multi-OS support via OSAL (FreeRTOS, RT-Thread, Linux). Build swarm intelligence with networked nodes.
ESP32-S3 WiFi support adapted from [MimiClaw](https://github.com/memovai/mimiclaw).

> Deploy your own AI assistant on hardware that costs just one dollar — seamlessly integrated into your daily workflow, efficiently bridging the digital and physical worlds.

<p align="center">
  <img src="images/demo.png" alt="RT-Claw Demo — AI drawing on LCD via Tool Use" width="700">
</p>

[Getting Started](docs/en/getting-started.md) · [Usage](docs/en/usage.md) · [Architecture](docs/en/architecture.md) · [Porting](docs/en/porting.md) · [Tuning](docs/en/tuning.md) · [Contributing](docs/en/contributing.md)

## Features

| Feature | Description | Status |
|---------|-------------|--------|
| LLM Chat Engine | Interactive conversation with LLM API over HTTP | Done |
| Tool Use | LLM-driven function calling to interact with hardware and services; 30+ built-in tools | Done |
| Skills | Composable multi-tool workflows; AI can create, persist, and execute skills that orchestrate multiple tools | Done |
| Conversation Memory | Short-term RAM ring buffer + long-term NVS Flash persistent storage; AI can save/delete/list memories | Done |
| Swarm Intelligence | Node discovery, heartbeat, capability bitmap, remote tool invocation across nodes | Done |
| Scheduled Tasks | Timer-driven task execution and periodic automation; AI can create/list/remove tasks | Done |
| Chat-first Shell | UART REPL with insert-mode editing, tab completion, UTF-8; direct input goes to AI, /commands for system | Done |
| OSAL | Write once, run on FreeRTOS, RT-Thread and Linux with zero code changes | Done |
| Gateway | Thread-safe message routing between services | Done |
| Networking | Ethernet (QEMU) and WiFi (real hardware); HTTP client for API calls | Done |
| IM Integrations | Feishu (Lark) via WebSocket long connection; planned: DingTalk, QQ, Telegram | In Progress |
| Web Flash & Serial | Browser-based firmware flash (esptool-js) and serial terminal with ANSI color rendering | Done |
| Multi-Model API | Support mainstream LLM APIs: Claude, GPT, Gemini, DeepSeek, GLM, MiniMax, Grok, Moonshot, Baichuan, Qwen, Doubao, Llama (Ollama) | Planned |
| Web Config Portal | Lightweight built-in web page for configuring API keys, selecting models, and tuning parameters at runtime | Planned |
| Claw Skill Provider | Serve as a skill for other Claws, giving them the ability to sense and control the physical world | Planned |

## Architecture

```
+----------------------------------------------------------------+
|                      rt-claw Application                       |
|     gateway | net | swarm | ai_engine | shell | sched | im     |
+----------------------------------------------------------------+
|                       skills (AI Skills)                       |
|              (one skill composes multiple tools)               |
+----------------------------------------------------------------+
|                        tools (Tool Use)                        |
|    gpio | system | lcd | audio | http | scheduler | memory     |
+----------------------------------------------------------------+
|                     drivers (Hardware BSP)                     |
|       WiFi | ES8311 | SSD1306 | serial | LCD framebuffer       |
+----------------------------------------------------------------+
|                   osal/claw_os.h (OSAL API)                    |
+----------------+----------------------+--------------+---------+
| FreeRTOS (IDF) | FreeRTOS(standalone) |  RT-Thread   |  Linux  |
+----------------+----------------------+--------------+---------+
| ESP32-C3 / S3  |  QEMU Zynq-A9 (GEM)  | vexpress-a9  |  Native |
+----------------+----------------------+--------------+---------+
```

## Supported Platforms

| Platform | Target | OS | Build | Status |
|----------|--------|----|-------|--------|
| ESP32-C3 | QEMU, xiaozhi-xmini, generic devkit | FreeRTOS (ESP-IDF) | Meson + CMake | Verified |
| ESP32-S3 | QEMU, real hardware | FreeRTOS (ESP-IDF) | Meson + CMake | Verified |
| Zynq-A9 | QEMU | FreeRTOS (standalone) | Meson (full firmware) | Verified |
| vexpress-a9 | QEMU | RT-Thread | Meson + SCons | Verified |
| Linux | Native (x86_64, aarch64) | Linux (pthreads) | Meson | Verified |

## Quick Start

```bash
# 1. Install ESP-IDF + QEMU (one-line setup)
./scripts/setup-esp-env.sh

# 2. Set your API key
export RTCLAW_AI_API_KEY='<your-api-key>'

# 3. Build and run on QEMU
make build-esp32c3-qemu
make run-esp32c3-qemu
```

> **No hardware? No problem.** Try the [CNB Cloud IDE](https://cnb.cool/gevico.online/rtclaw/rt-claw) — all toolchains pre-installed, build and run in your browser.

For real hardware (ESP32-S3/C3), WiFi setup, Feishu bot, and more — see the **[Getting Started Guide](docs/en/getting-started.md)**.

## Community

Join the GTOC (Gevico Open-Source Community) channels:

- **Discord**: [RT-Claw](https://discord.gg/gcxwYXQr)
- **QQ Group**: [Join](https://qm.qq.com/q/heSPPC9De8)
- **Telegram**: [GTOC Channel](https://t.me/gevico_channel)
- **Bilibili**: [Zevorn](https://space.bilibili.com/483048140)
- **WeChat**: [GTOC](https://mp.weixin.qq.com/s/PhTZKjk4FO0iVveBB9OvSQ)

## Acknowledgments

Inspired by [OpenClaw](https://github.com/openclaw/openclaw), [Nanobot](https://github.com/HKUDS/nanobot), and [MimiClaw](https://github.com/memovai/mimiclaw).

## Star History

<a href="https://www.star-history.com/?repos=zevorn%2Frt-claw&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/image?repos=zevorn/rt-claw&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/image?repos=zevorn/rt-claw&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/image?repos=zevorn/rt-claw&type=date&legend=top-left" />
 </picture>
</a>

## License

MIT
