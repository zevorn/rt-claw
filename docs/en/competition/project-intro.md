# RT-Claw: Embedded AI Assistant on $1 Hardware

> Zhongguancun Lobster Competition · Productivity Track

## What Can Your Lobster Do?

**RT-Claw** is an AI assistant framework running on embedded real-time operating systems (RTOS). It brings LLM capabilities down to microcontrollers costing just **$1 with 400KB RAM** — every chip can understand natural language and control hardware.

### Core Capabilities

| Capability | Description |
|------------|-------------|
| **LLM Tool Use** | AI dynamically invokes 30+ hardware tools (GPIO, LCD, network, audio, scheduler) without reflashing |
| **Multi-tool Skills** | Skill system lets AI compose tool workflows and persist them across reboots |
| **Swarm Intelligence** | Nodes auto-discover, share capabilities, and collaborate across devices |
| **Chat-first Shell** | UART serial becomes an AI chat terminal with Tab completion |
| **IM Bot** | Feishu/Telegram integration for remote device control |
| **Browser Flashing** | Flash firmware from a web browser — zero toolchain install |
| **Conversation Memory** | Short-term RAM buffer + long-term NVS Flash persistence |
| **Multi-RTOS** | Same code runs on FreeRTOS and RT-Thread via OSAL abstraction |

### One-liner

> Control embedded hardware with natural language. Build AI nodes with $1 chips. Network them into a distributed AI system via swarm protocol.

---

## What Problem Does It Solve?

### Problem 1: AI Is Trapped in the Cloud

Current AI assistants run on cloud servers or powerful devices. But many real-world scenarios (factories, agriculture, smart homes, edge gateways) need AI running directly on low-cost, low-power embedded devices.

**RT-Claw's approach**: Deploy LLM Tool Use capabilities (not the model itself) to MCUs. Devices call cloud models via HTTP for decisions, then execute hardware operations locally. Cost drops from hundreds of dollars to **about $1**.

### Problem 2: Embedded Development Is Hard and Slow

Traditional embedded development: write C → cross-compile → flash → debug → repeat for every change.

**RT-Claw's approach**: AI Tool Use + Skill system. Describe your need in natural language, AI orchestrates tool calls automatically. No recompilation needed — conversation is programming.

### Problem 3: Devices Are Isolated Islands

Individual IoT devices work alone. Cross-device collaboration requires complex protocol development.

**RT-Claw's approach**: Built-in Swarm Intelligence protocol. Nodes auto-discover, broadcast capabilities, and distribute tasks. Plug-and-play multi-device collaboration.

---

## How Well Does It Work?

### Verified Platforms

| Platform | Chip | RTOS | Status |
|----------|------|------|--------|
| ESP32-C3 | RISC-V, 160MHz | FreeRTOS (ESP-IDF) | ✅ Real hardware + QEMU |
| ESP32-S3 | Xtensa, 240MHz | FreeRTOS (ESP-IDF) | ✅ Real hardware + QEMU |
| Zynq-A9 | ARM Cortex-A9 | FreeRTOS + FreeRTOS+TCP | ✅ QEMU |
| vexpress-A9 | ARM Cortex-A9 | RT-Thread v5.3.0 | ✅ QEMU |

### Metrics

- **Minimum hardware cost**: ESP32-C3 module ~$1, runs full AI assistant
- **Memory footprint**: Core framework < 400KB RAM
- **Tool response latency**: Local tool invocation < 10ms
- **Built-in tools**: 30+ (GPIO / LCD / Network / Audio / Scheduler / OTA)
- **RTOS coverage**: 2 RTOSes (FreeRTOS + RT-Thread), 4 hardware platforms
- **Codebase**: Pure C99, ~8000 lines of core code, auditable and configurable

---

## Project Info

| Field | Value |
|-------|-------|
| **Name** | RT-Claw |
| **License** | MIT |
| **Repository** | [github.com/zevorn/rt-claw](https://github.com/zevorn/rt-claw) |
| **Language** | C (gnu99) |
| **Track** | Productivity |
| **Author** | Chao Liu |
