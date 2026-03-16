# Tuning & Optimization

**English** | [中文](../zh/tuning.md)

## Module Trimming

Every module can be independently disabled to save RAM and Flash.

### Via ESP-IDF menuconfig

```bash
idf.py menuconfig
# Navigate: Component config → rt-claw Configuration → Module Selection
```

### Via Meson Options

```bash
meson configure build/<platform>/meson -Dswarm=false -Dheartbeat=false
```

### Feature Flag Reference

| Flag | Default | RAM Savings | Description |
|------|---------|-------------|-------------|
| CONFIG_RTCLAW_SHELL_ENABLE | on | ~10KB | UART REPL shell |
| CONFIG_RTCLAW_LCD_ENABLE | off | ~8KB | LCD framebuffer |
| CONFIG_RTCLAW_SWARM_ENABLE | on | ~14KB | Swarm networking |
| CONFIG_RTCLAW_SCHED_ENABLE | on | ~9KB | Task scheduler |
| CONFIG_RTCLAW_SKILL_ENABLE | on | ~4KB | Skill system |
| CONFIG_RTCLAW_HEARTBEAT_ENABLE | off | ~9KB | Periodic AI patrol |
| CONFIG_RTCLAW_FEISHU_ENABLE | off | ~20KB | Feishu IM + WebSocket + TLS |
| CONFIG_RTCLAW_TELEGRAM_ENABLE | off | ~16KB | Telegram Bot + HTTP polling |
| CONFIG_RTCLAW_TOOL_GPIO | on | ~2KB | GPIO tools |
| CONFIG_RTCLAW_TOOL_SYSTEM | on | ~3KB | System info tools |
| CONFIG_RTCLAW_TOOL_LCD | off | ~3KB | LCD drawing tools |
| CONFIG_RTCLAW_TOOL_SCHED | on | ~2KB | Scheduler tools |
| CONFIG_RTCLAW_TOOL_NET | on | ~2KB | HTTP request tool |

### Minimal Profile Example

For a bare AI chat terminal on ESP32-C3 with ~50KB headroom:

```
Shell=on, Swarm=off, Sched=off, Skill=off, Heartbeat=off, Feishu=off, Telegram=off
Tools: GPIO=on, System=on, others=off
```

### Feishu-only Bot Profile

Headless IM bot, no shell:

```
Shell=off, Feishu=on, Telegram=off, Swarm=off, Sched=on, Skill=on
Tools: System=on, Sched=on, Net=on, GPIO=off, LCD=off
```

### Telegram Bot Profile

Headless Telegram bot, no shell:

```
Shell=off, Telegram=on, Feishu=off, Swarm=off, Sched=on, Skill=on
Tools: System=on, Sched=on, Net=on, GPIO=off, LCD=off
```

## Memory Optimization

### ESP32-C3 (400KB SRAM, ~200KB available after WiFi/TLS)

Measured runtime: ~43% usage (182KB free / 321KB total heap). Key tuning:

- `NET_RESP_MAX` -- HTTP tool response buffer (default 4KB, was 16KB)
- `sched_ai_ctx` -- prompt/reply buffers are heap-allocated and freed after use
- `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` -- TLS input buffer (default 16KB, reduce to 8KB)
- `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN` -- TLS output buffer (default 16KB, reduce to 4KB)
- `CONFIG_ESP_WIFI_IRAM_OPT` -- WiFi IRAM optimization (disable to save IRAM)
- `CONFIG_ESP_WIFI_RX_IRAM_OPT` -- WiFi RX IRAM (disable to save IRAM)
- `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM` -- Dynamic RX buffers (reduce to 16)

### ESP32-S3 (512KB SRAM + 8MB PSRAM)

- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=2048` -- Only small allocs in internal SRAM
- `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` -- TLS buffers in PSRAM
- Dual-core binding: WiFi+TLS on Core 0, App on Core 1

### AI Engine Tuning (claw_config.h or meson options)

| Parameter | Default | Effect |
|-----------|---------|--------|
| ai_max_tokens | 1024 | Max response length. Lower = less RAM + faster |
| ai_context_size | 8192 | HTTP response buffer. 4096 for constrained devices |
| ai_memory_max_msgs | 20 | Conversation history depth. 10 for low RAM |

### Conversation Memory

- Short-term: `ai_memory_max_msgs` controls RAM ring buffer size
- Long-term: NVS Flash, ~4KB. No RAM cost beyond read operations.

## Configuration Parameters Reference

### claw_config.h Constants

| Constant | Default | Description |
|----------|---------|-------------|
| CLAW_GW_MSG_POOL_SIZE | 16 | Gateway message queue depth |
| CLAW_GW_MSG_MAX_LEN | 256 | Max message size in bytes |
| CLAW_SWARM_MAX_NODES | 32 | Max discoverable swarm nodes |
| CLAW_SWARM_HEARTBEAT_MS | 5000 | Heartbeat broadcast interval (20-byte packets) |
| CLAW_SWARM_PORT | 5300 | UDP discovery port |
| SWARM_RPC_MAX_RETRIES | 3 | RPC retry count (exponential backoff) |
| SWARM_RPC_RETRY_BASE_MS | 500 | Base retry delay (doubles each attempt) |
| CLAW_SCHED_MAX_TASKS | 8 | Max concurrent scheduled tasks |
| CLAW_SCHED_TICK_MS | 1000 | Scheduler resolution |
| CLAW_HEARTBEAT_INTERVAL_MS | 300000 | AI patrol interval (5 min) |

### Build-time AI Defaults

| Parameter | Default | Env Var | Meson Option |
|-----------|---------|---------|--------------|
| API Key | "" | RTCLAW_AI_API_KEY | ai_api_key |
| API URL | http://10.0.2.2:8888/v1/messages | RTCLAW_AI_API_URL | ai_api_url |
| Model | claude-opus-4-6 | RTCLAW_AI_MODEL | ai_model |
| Max Tokens | 1024 | RTCLAW_AI_MAX_TOKENS | ai_max_tokens |
| Context Size | 8192 | RTCLAW_AI_CONTEXT_SIZE | ai_context_size |
| Memory Messages | 20 | RTCLAW_AI_MEMORY_MAX_MSGS | ai_memory_max_msgs |

### Build-time Telegram Defaults

| Parameter | Default | Env Var | Meson Option |
|-----------|---------|---------|--------------|
| Bot Token | "" | RTCLAW_TELEGRAM_BOT_TOKEN | telegram_bot_token |
| API URL | https://api.telegram.org | RTCLAW_TELEGRAM_API_URL | telegram_api_url |

## Network Notes

- QEMU uses virtual Ethernet (OpenCores MAC), NOT WiFi
- vexpress-a9 has no TLS: use `scripts/api-proxy.py` (HTTP to HTTPS proxy)
- ESP32 real hardware: WiFi with mbedTLS for HTTPS
