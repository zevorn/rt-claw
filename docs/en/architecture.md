# Architecture

**English** | [中文](../zh/architecture.md)

## Overview

rt-claw is an OpenClaw-inspired AI assistant targeting embedded RTOS platforms.
It achieves multi-RTOS portability through an OS Abstraction Layer (OSAL) --
the same core code runs on FreeRTOS (ESP-IDF) and RT-Thread unmodified.

All core logic lives in `claw/`. Meson cross-compiles it into `librtclaw.a`
and `libosal.a`, which each platform's native build system (CMake or SCons)
links into the final firmware binary.

## Layer Diagram

```
+----------------------------------------------------------------------+
|  Application                                                         |
|  gateway | swarm | net | ai_engine | tools | shell | sched | im      |
|  heartbeat                                                           |
+----------------------------------------------------------------------+
|  OSAL                                                                |
|  claw_os.h  |  claw_net.h                                            |
+----------------------------------------------------------------------+
|  RTOS                                                                |
|  FreeRTOS (ESP-IDF)              |  RT-Thread                        |
+----------------------------------------------------------------------+
|  Hardware                                                            |
|  ESP32-C3/S3 (WiFi/BLE/OLED/Audio)  |  vexpress-a9 (Ethernet/UART)   |
+----------------------------------------------------------------------+
```

## OSAL (OS Abstraction Layer)

Interface header: `include/osal/claw_os.h`

Abstracted primitives: Thread, Mutex, Semaphore, Message Queue, Timer,
Memory (malloc/free), Logging (CLAW_LOGI/LOGW/LOGE/LOGD), Tick/Time.

Implementations:

- `osal/freertos/claw_os_freertos.c` -- linked on ESP-IDF platforms
- `osal/rtthread/claw_os_rtthread.c` -- linked on RT-Thread platforms

Network abstraction: `include/osal/claw_net.h` -- HTTP POST interface.

Design: link-time binding. Zero overhead. No function pointers in core code.
No conditional compilation (`#ifdef`) in `claw/` source files.

```
claw/*.c  --->  #include "osal/claw_os.h"
                        |
          +-------------+-------------+
          |                           |
  claw_os_freertos.c          claw_os_rtthread.c
  (linked on ESP-IDF)         (linked on RT-Thread)
```

## Core Services

### Gateway (`claw/core/gateway.c`)

Message routing hub with service registry and type-based dispatch. Services
register with a type_mask bitmap and their own message queue; the gateway
delivers incoming messages to all matching consumers. Message types: DATA,
CMD, EVENT, SWARM, AI_REQ. Queue: 16 messages x 256 bytes. Dedicated thread
at priority 15.

### Scheduler (`claw/core/scheduler.c`)

Timer-driven task execution with 1-second tick resolution. Supports up to
8 concurrent tasks (one-shot and repeating). AI can create, list, and
remove tasks via tool calls. Persistent across reboots via NVS storage.
A dedicated AI worker thread processes scheduled AI tasks with round-robin
pending queue -- tasks that arrive while the worker is busy are queued and
executed in turn, preventing task starvation.

### Heartbeat (`claw/core/heartbeat.c`)

Optional periodic AI check-in every 5 minutes. Sends a heartbeat prompt
to the AI engine so the assistant can perform background monitoring.
Depends on the scheduler service for timing.

### AI Engine (`claw/services/ai/`)

Claude/OpenAI-compatible API HTTP client with Tool Use support. 24 built-in
tools covering GPIO, system info, LCD, audio, scheduler, HTTP requests, and
long-term memory. Each tool declares required capabilities (`SWARM_CAP_*`
bitmap) and flags (`CLAW_TOOL_LOCAL_ONLY`) for swarm routing decisions.
Conversation memory: RAM ring buffer (short-term) + NVS storage (long-term).
Skill system for reusable prompt templates.

### Swarm (`claw/services/swarm/`)

Distributed node coordination. UDP broadcast discovery on port 5300.
20-byte heartbeat packets carry capability bitmap, load percentage, node
role (WORKER / THINKER / COORDINATOR / OBSERVER), and active task count.
Load-aware node selection picks the least-loaded capable node for RPC.
Exponential-backoff RPC retry (3 attempts, 500ms / 1s / 2s). Tools marked
`CLAW_TOOL_LOCAL_ONLY` are never delegated remotely. Tool capability
matching uses `claw_tool_t.required_caps` with prefix-based fallback.

### Network (`claw/services/net/`)

Platform-aware HTTP client. ESP-IDF: `esp_http_client` with mbedTLS for
HTTPS. RT-Thread: BSD sockets routed through `scripts/api-proxy.py`
(HTTP-to-HTTPS proxy for environments without native TLS).

### Feishu IM (`claw/services/im/feishu.c`)

WebSocket long connection to Feishu/Lark messaging platform. No public IP
or webhook endpoint required. Event subscription: `im.message.receive_v1`.
Bidirectional message relay between Feishu users and the AI engine.

### Telegram IM (`claw/services/im/telegram.c`)

HTTP long polling integration with Telegram Bot API. Three-thread architecture:
poll thread (getUpdates with 30s timeout), AI worker thread (ai_chat + channel
hint), and outbound thread (sendMessage with auto-chunking for messages >4096
chars). Bot Token authentication, no webhook or public IP needed. Supports
typing indicators via sendChatAction.

### Shell (`claw/shell/`)

UART REPL with chat-first design. Direct text input goes to the AI engine.
`/commands` trigger system operations. Insert-mode line editing with tab
completion. UTF-8 aware.

## Driver Architecture

Linux-kernel style organization:

```
drivers/<subsystem>/<vendor>/<driver>.c      -- implementation
include/drivers/<subsystem>/<vendor>/<hdr>.h -- public header
```

| Driver | Path | Description |
|--------|------|-------------|
| WiFi Manager | `drivers/net/espressif/` | ESP32 WiFi STA management (shared C3/S3) |
| ES8311 Audio | `drivers/audio/espressif/` | I2C audio codec with preset sound effects |
| SSD1306 OLED | `drivers/display/espressif/` | I2C OLED display (128x64) |
| Console | `drivers/serial/espressif/` | Serial console driver |

## Platforms

| Platform | CPU | RTOS | Build | Network | Boards |
|----------|-----|------|-------|---------|--------|
| ESP32-C3 | RISC-V 160MHz | FreeRTOS (ESP-IDF) | Meson + CMake | WiFi / Ethernet (QEMU) | qemu, devkit, xiaozhi-xmini |
| ESP32-S3 | Xtensa LX7 240MHz dual-core | FreeRTOS (ESP-IDF) | Meson + CMake | WiFi + PSRAM / Ethernet (QEMU) | qemu, default |
| vexpress-a9 | ARM Cortex-A9 | RT-Thread | Meson + SCons | Ethernet | qemu |

Board selection is driven by `RTCLAW_BOARD` for ESP32 platforms.
Board-specific configs live under `platform/<chip>/boards/<board>/`.

## Build Flow

```
Makefile (entry point)
    |
    +---> scripts/gen-esp32{c3,s3}-cross.py   (generate Meson cross-file)
    |
    +---> meson setup + meson compile
    |         |
    |         +---> claw/      --> librtclaw.a
    |         +---> osal/      --> libosal.a
    |
    +---> platform native build
              |
              +---> esp32c3/esp32s3: idf.py build (CMakeLists.txt links .a)
              +---> vexpress-a9:     scons (SConstruct links .a)
              |
              +---> Final firmware binary in build/<platform>/
```

## Event Priority Model

| Priority | Class | Latency | Examples |
|----------|-------|---------|----------|
| P0 | Reflex | 1-10 ms | ISR handlers, hardware interrupts |
| P1 | Control | 10-50 ms | Motor control, sensor polling |
| P2 | Interaction | 50-150 ms | Gateway routing (thread prio 15), shell I/O |
| P3 | AI | Best-effort | AI engine (thread prio 5-10), swarm sync |

## Resource Budget (ESP32-C3)

| Module | SRAM | Notes |
|--------|------|-------|
| ESP-IDF + WiFi + TLS | ~110 KB | System overhead |
| Thread stacks (5 threads) | ~48 KB | main 16K + gateway 4K + swarm 4K + sched 8K + sched_ai 16K |
| Gateway + Scheduler | ~10 KB | MQ 16x260B + service registry + timer |
| AI Engine + Memory | ~15 KB | HTTP client + conversation ring buffer |
| Tools | ~5 KB | 24 tool descriptors (with caps/flags) + handlers |
| Swarm + Heartbeat | ~14 KB | UDP socket + node table (32 nodes) + timer |
| Shell + App | ~10 KB | Line buffer + command table |
| **Total** | **~212 KB** | ~100 KB free heap at runtime (43% usage measured) |
