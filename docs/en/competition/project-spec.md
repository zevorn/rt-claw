# RT-Claw Project Specification

> Zhongguancun Lobster Competition · Productivity Track

---

## 1. Overview

**RT-Claw** is an open-source embedded AI assistant framework that brings Large Language Model (LLM) capabilities to low-cost microcontrollers. The name stands for "Real-Time Claw" — an AI claw running on RTOS that can grab and control any hardware resource.

Inspired by the OpenClaw community, implemented in pure C99 with an OS Abstraction Layer (OSAL) for cross-RTOS portability. Currently supports FreeRTOS and RT-Thread across ESP32-C3, ESP32-S3, Zynq-A9, and vexpress-A9 platforms.

---

## 2. Architecture

```
┌─────────────────────────────────────────────────┐
│                  Application                     │
│    Shell (UART REPL)  ·  IM Bot (Feishu/TG)     │
├─────────────────────────────────────────────────┤
│                  AI Services                     │
│   AI Engine  ·  Skill System  ·  AI Memory      │
├─────────────────────────────────────────────────┤
│                   Tool Use                       │
│  GPIO · LCD · Audio · Net · Sched · OTA · ...   │
├─────────────────────────────────────────────────┤
│                Core Services                     │
│   Gateway  ·  Scheduler  ·  Heartbeat  ·  Swarm │
├─────────────────────────────────────────────────┤
│                    OSAL                          │
│             claw_os.h · claw_net.h               │
├──────────────────────┬──────────────────────────┤
│     FreeRTOS         │       RT-Thread           │
├──────────────────────┴──────────────────────────┤
│                  Hardware                        │
│   ESP32-C3 · ESP32-S3 · Zynq-A9 · vexpress-A9  │
└─────────────────────────────────────────────────┘
```

### Key Components

- **OSAL**: Unified `claw_os.h` interface — threads, mutexes, semaphores, queues, timers. Link-time binding, zero `#ifdef`.
- **Gateway**: Message bus with Pipeline processing + service registry. Message types: DATA, CMD, EVENT, SWARM, AI_REQ.
- **AI Engine**: HTTP API calls to any LLM (Claude, GPT, Gemini, DeepSeek). Auto-builds Tool Use JSON requests.
- **Skill System**: AI composes multi-tool workflows, persists to NVS Flash.
- **AI Memory**: Short-term RAM ring buffer + long-term NVS Flash persistence.
- **Tool Use**: 30+ built-in tools (GPIO, LCD, Audio, Network, Scheduler, OTA, Memory).
- **Swarm**: UDP broadcast discovery, capability bitmap, cross-node remote invocation.

---

## 3. Technical Highlights

1. **Pure C99, minimal dependencies** — ~8000 lines core, only cJSON as external dependency
2. **Multi-RTOS portability** — Same code on FreeRTOS + RT-Thread, zero platform-specific calls
3. **AI-driven hardware control** — Natural language → tool selection → hardware action
4. **Browser flashing** — Web Serial API + esptool-js, zero toolchain install
5. **Full simulator support** — QEMU for all platforms, zero-hardware development
6. **Swarm networking** — Multi-node auto-discovery and collaborative execution
7. **$1 hardware cost** — Complete AI assistant on ESP32-C3 module

---

## 4. Project Info

| Field | Value |
|-------|-------|
| Name | RT-Claw (Real-Time Claw) |
| License | MIT |
| Repository | [github.com/zevorn/rt-claw](https://github.com/zevorn/rt-claw) |
| Website | [zevorn.github.io/rt-claw](https://zevorn.github.io/rt-claw) |
| Language | C (gnu99) |
| Author | Chao Liu |
| Track | Productivity |
