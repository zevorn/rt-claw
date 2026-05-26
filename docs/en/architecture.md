# Architecture

**English** | [中文](../zh/architecture.md)

## Overview

rt-claw is an OpenClaw-inspired AI assistant targeting embedded RTOS platforms.
It achieves multi-RTOS portability through an OS Abstraction Layer (OSAL) --
the same core code runs on FreeRTOS (ESP-IDF), RT-Thread, Zephyr, and Linux
unmodified.

All core logic lives in `claw/`. Meson cross-compiles it into `librtclaw.a`
and `libosal.a`, which each platform's native build system (CMake or SCons)
links into the final firmware binary.

## Layer Diagram

```
+----------------------------------------------------------------------+
|  Application                                                         |
|  gateway | swarm | net | ai_engine | tools | shell | sched | im      |
|  heartbeat | voice                                                   |
+----------------------------------------------------------------------+
|  OSAL                                                                |
|  claw_os.h  |  claw_net.h                                            |
+----------------------------------------------------------------------+
|  RTOS                                                                |
|  FreeRTOS (ESP-IDF)  |  RT-Thread  |  Zephyr  |  Linux              |
+----------------------------------------------------------------------+
|  Hardware                                                            |
|  ESP32-C3/S3  |  vexpress-a9  |  Zephyr qemu_cortex_a9/m3  |  x86   |
+----------------------------------------------------------------------+
```

## OSAL (OS Abstraction Layer)

Interface header: `include/osal/claw_os.h`

Abstracted primitives: Thread, Mutex, Semaphore, Message Queue, Timer,
Memory (malloc/free), Logging (CLAW_LOGI/LOGW/LOGE/LOGD), Tick/Time.

Implementations:

- `osal/freertos/claw_os_freertos.c` -- linked on ESP-IDF platforms
- `osal/rtthread/claw_os_rtthread.c` -- linked on RT-Thread platforms
- `osal/zephyr/claw_os_zephyr.c` -- compiled by Zephyr CMake on Zephyr platforms

Network abstraction: `include/osal/claw_net.h` -- HTTP POST interface.

KV storage abstraction: `include/osal/claw_kv.h` -- key-value persistence
(str/blob/u8 set/get/delete). ESP-IDF backend uses NVS Flash; RT-Thread
backend uses an in-memory hash table; Zephyr backend uses Settings subsystem
with NVS. All business code accesses persistent storage through this
interface -- no direct NVS calls in `claw/`.

Design: link-time binding. Zero overhead. No function pointers in core code.
No conditional compilation (`#ifdef`) in `claw/` source files.

```
claw/*.c  --->  #include "osal/claw_os.h"
                        |
          +-------------+-------------+-------------+
          |             |             |             |
  claw_os_freertos.c  claw_os_rtthread.c  claw_os_zephyr.c  claw_os_linux.c
  (linked on ESP-IDF) (linked on RT-Thread) (Zephyr CMake)  (linked on Linux)
```

## Core Services

### Gateway (`claw/services/gateway.c`)

Message routing hub with pipeline handler chain and service registry.
Incoming messages pass through registered handlers (netfilter-style hooks)
before service dispatch. Handlers can pass (0), consume (1), or reject (<0)
a message. Unconsumed messages are delivered to services matching the
type_mask bitmap. Message types: DATA, CMD, EVENT, SWARM, AI_REQ.
Queue: 16 messages x 256 bytes. Dedicated thread at priority 15.
Built-in statistics: total/per-type/dropped/no-consumer/filtered counts.

### Scheduler (`claw/services/sched.c`)

Timer-driven task execution with 1-second tick resolution. Supports up to
8 concurrent tasks (one-shot and repeating). AI can create, list, and
remove tasks via tool calls. Persistent across reboots via NVS storage.
A dedicated AI worker thread processes scheduled AI tasks with round-robin
pending queue -- tasks that arrive while the worker is busy are queued and
executed in turn, preventing task starvation.

### Heartbeat (`claw/services/heartbeat.c`)

Optional periodic AI check-in every 5 minutes. Three-layer tick logic:
(1) events pending -- AI summary via `ai_chat_raw()`;
(2) no events -- device health check (heap > 80% triggers auto-alert)
    followed by lightweight LLM connectivity ping (`ai_ping()`,
    max_tokens=1); (3) state change (online/offline) -- notification
delivered to IM or console. Depends on the scheduler service for timing.

### AI Engine (`claw/services/ai/`)

Claude/OpenAI-compatible API HTTP client with Tool Use support. 24 built-in
tools covering GPIO, system info, LCD, audio, scheduler, HTTP requests, and
long-term memory. Each tool declares required capabilities (`SWARM_CAP_*`
bitmap) and flags (`CLAW_TOOL_LOCAL_ONLY`) for swarm routing decisions.

Concurrency: multi-channel requests (shell, Feishu, Telegram, scheduler)
are serialized through a request queue processed by a dedicated AI worker
thread. Queue-full returns "busy" immediately without blocking other channels.
Each request snapshots its channel, channel_hint, and status_cb to prevent
cross-thread races.

Conversation memory: per-channel RAM ring buffer (short-term) + OSAL KV
storage (long-term persistent). When memory approaches capacity, AI-generated
summary compression replaces the oldest half with a concise summary,
preserving key context instead of simple FIFO deletion.
Skill system for reusable prompt templates.

### Swarm (`claw/services/swarm/`)

Distributed node coordination. UDP broadcast discovery on port 5300.
20-byte heartbeat packets carry capability bitmap, load percentage, node
role (WORKER / THINKER / COORDINATOR / OBSERVER), and active task count.
Load-aware node selection picks the least-loaded capable node for RPC.
Exponential-backoff RPC retry (3 attempts, 500ms / 1s / 2s). Tools marked
`CLAW_TOOL_LOCAL_ONLY` are never delegated remotely. Tool capability
matching uses `struct claw_tool.required_caps` with prefix-based fallback.

### Network (`claw/services/net/`)

Platform-aware HTTP client. ESP-IDF: `esp_http_client` with mbedTLS for
HTTPS. RT-Thread: BSD sockets routed through `scripts/api-proxy.py`
(HTTP-to-HTTPS proxy for environments without native TLS). Zephyr: Zephyr
HTTP Client with mbedTLS support (TLS Kconfig not yet enabled by default;
use `scripts/api-proxy.py` until TLS is fully configured).

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

### Voice (`claw/services/voice/`)

Optional voice service that keeps the existing AI path intact: input backends
submit `start_capture` / `audio_chunk` / `end_capture` events to
`voice_service`, STT converts the audio to text, then the transcript is passed
into `ai_chat()`. The returned assistant text is sent both to the endpoint and
then into TTS, whose decoded audio is handed back to the active output backend.
Current implementation includes config-driven provider selection for both STT
and TTS, per-turn byte cutoff, audio format metadata, queue-based event
handoff, MiMo TTS response streaming with buffered fallback, and a Linux web
endpoint backend under `platform/linux/web_voice_server.c`. The Linux web path
uses browser HTTP POST for PCM upload and SSE for state / transcript /
assistant text / TTS audio updates.

At the interface level, `include/claw/services/voice/voice_endpoint.h` defines
session states, endpoint event types, and the backend callback contract.
Capture-side backends submit `ATTACH`, `DETACH`, `START_CAPTURE`,
`AUDIO_CHUNK`, `END_CAPTURE`, `CANCEL`, and `PLAYBACK_DONE` events through
`voice_submit_event()`. `AUDIO_CHUNK` uses a pointer-based payload
(`data_ptr`/`data_len`) plus `data_owns` ownership transfer, so large PCM data
is not copied inline into the queue item. Output-side callbacks
(`send_state`, `send_transcript`, `send_assistant_text`, `send_tts_audio`,
`send_error`) are borrow-only: asynchronous backends must copy the referenced
data before returning.

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
| Zephyr Cortex-A9 | ARM Cortex-A9 | Zephyr v4.4.0 | CMake (Zephyr) | Ethernet | qemu_cortex_a9 |
| Zephyr Cortex-M3 | ARM Cortex-M3 | Zephyr v4.4.0 | CMake (Zephyr) | Ethernet | qemu_cortex_m3 |

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
              +---> zephyr:          cmake + ninja (CMake compiles sources directly)
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
| Thread stacks (6 threads) | ~56 KB | main 16K + gateway 4K + swarm 4K + sched 8K + sched_ai 16K + ai_worker 8K |
| Gateway + Scheduler | ~10 KB | MQ 16x260B + service registry + pipeline handler table + timer |
| AI Engine + Memory | ~15 KB | Request queue (4 slot) + conversation ring buffer + KV persistence |
| Tools | ~5 KB | 24 tool descriptors (with caps/flags) + handlers |
| Swarm + Heartbeat | ~14 KB | UDP socket + node table (32 nodes) + timer |
| Shell + App | ~10 KB | Line buffer + command table |
| **Total** | **~220 KB** | ~90 KB free heap at runtime |
