# Usage Guide

**English** | [中文](../zh/usage.md)

## Shell

Chat-first UART REPL. Type any text and it goes straight to the AI engine.
Commands start with `/`.

### Command Reference

| Command | Description |
|---------|-------------|
| *(text)* | Send to AI |
| `/ai_set key\|url\|model <v>` | Set AI config (NVS) |
| `/ai_status` | Show AI config |
| `/feishu_set <id> <secret>` | Set Feishu creds (NVS, reboot) |
| `/feishu_status` | Show Feishu config |
| `/wifi_set <SSID> <PASS>` | Set WiFi (NVS) |
| `/wifi_status` | Show WiFi state |
| `/wifi_scan` | Scan APs |
| `/remember <key> <value>` | Save to long-term memory |
| `/forget <key>` | Delete from memory |
| `/memories` | List all memories |
| `/task` | List tasks |
| `/task rm <name>` | Remove task |
| `/skill` | List skills |
| `/skill <name> [args]` | Execute skill |
| `/nodes` | Show swarm nodes |
| `/log [on\|off]` | Toggle log output |
| `/log level <lvl>` | Set log level (error/warn/info/debug) |
| `/history` | Show message count |
| `/clear` | Clear conversation |
| `/ota check` | Check for firmware update |
| `/ota update [url]` | Install update (or direct URL) |
| `/ota rollback` | Roll back to previous firmware |
| `/ota version` | Show running firmware version |
| `/voice_enable on\|off` | Enable or disable the voice service |
| `/voice_set <field> <value>` | Set voice runtime config and persist it |
| `/voice_status` | Show current voice config and runtime state |
| `/help` | List commands |

> WiFi commands (`/wifi_*`) only available on WiFi-capable boards.
> OTA commands (`/ota`) only available on boards with OTA partitions (xiaozhi-xmini, ESP32-S3 default).

## Voice (Linux endpoints)

The current voice MVP keeps input/output endpoints separate from voice business
logic. On Linux, `platform/linux/web_voice_server.c` provides a browser-facing
endpoint, `platform/linux/local_voice_endpoint.c` provides a local ALSA toolchain
endpoint, and `claw/services/voice/voice_service.c` owns the state machine,
STT orchestration, AI handoff, and TTS handoff.

### Build-time switches

```bash
meson setup build/linux --reconfigure \
    -Dosal=linux \
    -Dvoice=true \
    -Dlinux_web_voice=true \
    -Dlinux_local_voice=true
```

`linux_web_voice` and `linux_local_voice` both require `voice=true`.

### Runtime configuration

Voice config can come from three places, in this priority order:

1. Shell runtime config stored via OSAL KV (`/voice_set`, `/voice_enable`)
2. Meson options / environment variables for compile-time defaults
3. `claw_config.h` platform defaults

Current compile-time voice tuning entries:

- `RTCLAW_VOICE_MAX_TURN_BYTES` / `-Dvoice_max_turn_bytes=`
- `RTCLAW_VOICE_TEXT_BUF_SIZE` / `-Dvoice_text_buf_size=`
- `RTCLAW_VOICE_STT_RESP_SIZE` / `-Dvoice_stt_resp_size=`
- `RTCLAW_VOICE_STT_TIMEOUT_MS` / `-Dvoice_stt_timeout_ms=`
- `RTCLAW_VOICE_TTS_RESP_SIZE` / `-Dvoice_tts_resp_size=`
- `RTCLAW_VOICE_TTS_AUDIO_BUF_SIZE` / `-Dvoice_tts_audio_buf_size=`

Linux defaults are larger than embedded defaults for turn bytes, STT response
buffer sizing, and TTS buffers. `voice_tts_resp_size` is the full HTTP JSON
response buffer for MiMo TTS, including base64 audio and JSON overhead;
`voice_tts_audio_buf_size` is the decoded audio output buffer after base64
decode. If logs show `MiMo response truncated`, increase
`voice_tts_resp_size`. If logs show `MiMo audio decode exceeded output buffer`,
increase `voice_tts_audio_buf_size`.

### Shell commands

```text
/voice_set endpoint_backend local
/voice_enable on
/voice_local input plughw:1,0
/voice_local output default
/voice_set stt_provider xfyun
/voice_set stt_xfyun_app_id <APPID>
/voice_set stt_xfyun_api_key <APIKey>
/voice_set stt_xfyun_api_secret <APISecret>
/voice_local start
/voice_local stop
/voice_status
```

`/voice_status` masks sensitive fields and, on Linux voice builds, also shows
whether the web/local endpoint is currently running and which local devices are
configured.

### Runtime flow

1. An endpoint attaches a voice session. The web endpoint is attached by the
   browser event stream; the local endpoint is attached by `/voice_enable on` or
   `/voice_local start`.
2. The endpoint sends `start_capture` with audio format metadata.
3. The web endpoint streams PCM chunks over HTTP; the local endpoint reads PCM
   chunks from a USB microphone through `arecord`.
4. `voice_service` forwards chunks to the selected STT session and enforces
   `CONFIG_RTCLAW_VOICE_MAX_TURN_BYTES` as a hard cutoff.
5. `end_capture` finalizes STT, sends the transcript into `ai_chat()`, then
   returns assistant text and TTS audio back to the endpoint. The local endpoint
   plays TTS audio through `aplay`.

### Browser page

For Linux web voice builds, the browser test page is served from:

- `http://127.0.0.1:<web_port>/voice.html`

Remote/mobile microphone access still depends on the browser security model;
raw LAN HTTP is typically not enough for `getUserMedia()` on phones.

### Raspberry Pi 3 local audio

On 64-bit Raspberry Pi OS, the local endpoint reuses the system ALSA toolchain:
`arecord` captures from a USB microphone and `aplay` plays to the headphone jack
or the system default output. First list devices with:

```bash
arecord -l
aplay -l
```

A common setup is `plughw:1,0` for the USB microphone and `default` for output,
with the system default output selected as the 3.5 mm headphone jack through
`raspi-config` or desktop audio settings. Runtime setup example:

```text
/voice_set endpoint_backend local
/voice_local input plughw:1,0
/voice_local output default
/voice_enable on
/voice_local start
/voice_local stop
```

## Tool Use

The LLM orchestrates hardware via function calling. 24 built-in tools across
6 categories. Each category is gated by a Kconfig option (except Audio, which
is always enabled).

### Audio Tools (always enabled)

| Tool | Parameters | Description |
|------|------------|-------------|
| `audio_beep` | `freq_hz` (100–8000), `duration_ms`, `volume` (0–100) | Play a tone |
| `audio_play_sound` | `name` (success / error / notify / alert / startup / click) | Play preset sound |
| `audio_volume` | `volume` (0–100) | Set speaker volume |

### GPIO Tools (CONFIG_RTCLAW_TOOL_GPIO)

| Tool | Parameters | Description |
|------|------------|-------------|
| `gpio_set` | `pin`, `level` (0/1) | Set pin output level |
| `gpio_get` | `pin` | Read pin level |
| `gpio_config` | `pin`, `mode` (input / output) | Configure pin direction |
| `gpio_blink` | `pin`, `freq_hz`, `count` (-1 = infinite) | Blink a pin |
| `gpio_blink_stop` | `pin` | Stop blinking |

### LCD Tools (CONFIG_RTCLAW_TOOL_LCD)

| Tool | Parameters | Description |
|------|------------|-------------|
| `lcd_fill` | `color` (RGB565 hex) | Fill entire screen |
| `lcd_text` | `x`, `y`, `text`, `color`, `bg_color` | Draw text |
| `lcd_rect` | `x`, `y`, `w`, `h`, `color`, `filled` | Draw rectangle |
| `lcd_line` | `x1`, `y1`, `x2`, `y2`, `color` | Draw line |
| `lcd_circle` | `x`, `y`, `r`, `color`, `filled` | Draw circle |

### System Tools (CONFIG_RTCLAW_TOOL_SYSTEM)

| Tool | Parameters | Description |
|------|------------|-------------|
| `system_info` | — | Show system information |
| `memory_info` | — | Show heap usage, largest block, fragmentation |
| `clear_history` | — | Clear conversation history |
| `system_restart` | — | Reboot the device |
| `save_memory` | `key`, `value` | Save to long-term memory |
| `delete_memory` | `key` | Delete from long-term memory |
| `list_memories` | — | List all stored memories |

### Scheduler Tools (CONFIG_RTCLAW_TOOL_SCHED)

| Tool | Parameters | Description |
|------|------------|-------------|
| `schedule_task` | `name`, `prompt`, `interval_sec`, `count` (-1 = forever) | Create recurring task |
| `list_tasks` | — | List scheduled tasks |
| `remove_task` | `name` | Remove a task |

### Network Tools (CONFIG_RTCLAW_TOOL_NET)

| Tool | Parameters | Description |
|------|------------|-------------|
| `http_request` | `url`, `method` (GET / POST), `headers` (optional JSON), `body` (optional) | Send HTTP request |

### OTA Tools (CONFIG_RTCLAW_OTA_ENABLE)

| Tool | Parameters | Description |
|------|------------|-------------|
| `ota_check` | — | Check if a firmware update is available |
| `ota_update` | `url` (optional) | Trigger OTA update (from server or direct URL) |
| `ota_version` | — | Get running firmware version |
| `ota_rollback` | — | Roll back to previous firmware and reboot |

## OTA (Over-The-Air Update)

Firmware can be updated over the network without a USB cable. Only boards with
A/B OTA partitions are supported: **xiaozhi-xmini** (ESP32-C3, 16 MB) and
**ESP32-S3 default** (16 MB).

### How It Works

1. Device sends HTTP GET to a version-check URL
2. Server returns a JSON with the new version, download URL, size, and SHA256
3. Device compares versions — if newer, downloads the binary and writes it to
   the inactive OTA partition
4. Device switches boot partition and reboots
5. If the new firmware fails to boot, the bootloader automatically rolls back

### Configuration

```bash
# Set the OTA version-check URL (pick one method)
meson configure build/<platform>/meson -Dota_url='http://server/version.json'
export RTCLAW_OTA_URL='http://server/version.json'

# Optional: auto-check every 5 minutes
meson configure build/<platform>/meson -Dota_check_interval_ms=300000
```

### Version JSON Format

The OTA server must serve a JSON file at the configured URL:

```json
{
    "version": "0.2.0",
    "url": "http://server/rt-claw.bin",
    "size": 524288,
    "sha256": "abcdef1234567890..."
}
```

### Local OTA Server

A development server script is included:

```bash
make build-esp32c3-xiaozhi-xmini   # build firmware first
make ota-server                     # start local OTA server

# Or with options:
scripts/ota-server.py --platform esp32s3 --board default --port 9000
```

The script auto-detects the build firmware, reads the version from
`claw_config.h`, computes SHA256, and prints device configuration
instructions.

### Triggering OTA

**Shell:**

```
/ota check                          # check for update
/ota update                         # check + install
/ota update http://host/fw.bin      # direct URL install
/ota rollback                       # revert to previous firmware
```

**AI conversation:**

```
You> Check if there's a firmware update
rt-claw> (calls ota_check) Update available: 0.1.0 → 0.2.0
You> Go ahead and update
rt-claw> (calls ota_update) OTA started, rebooting when done...
```

### GitHub Releases

When a version tag is pushed (`v*`), CI automatically builds all hardware
firmware and creates a GitHub Release with:

- `<target>-firmware.zip` — full flash package (bootloader + partition table + app)
- `<target>-rt-claw.bin` — app binary for OTA
- `<target>-ota-version.json` — version metadata with download URL and SHA256

## Skill System

Reusable prompt templates that wrap tool sequences into single-word commands.
3 built-in skills ship with every firmware; users can create more at runtime.

### Built-in Skills

| Name | Requires | Template |
|------|----------|----------|
| `draw` | `TOOL_LCD` | "Draw the following on LCD using lcd_* tools: %s" |
| `monitor` | `TOOL_SYSTEM` | "Check system health via system_info and memory_info, brief summary. %s" |
| `greet` | — | "You are rt-claw on embedded RTOS. Greet and describe capabilities. %s" |

### Creating Skills

- **Via Tool Use** — the AI calls the `create_skill` tool with a name,
  description, and prompt template.
- **Via Shell** — `/skill` to list current skills.
- Skills persist in NVS Flash across reboots.
- Maximum 16 skills total (built-in + user-created).

## Conversation Memory

### Short-term

RAM ring buffer holding the last 20 messages (default). When full, the oldest
request/response pair is dropped. Clear manually with `/clear` or the
`clear_history` tool.

### Long-term

NVS Flash key-value store that survives reboot.

- **AI side** — `save_memory`, `delete_memory`, `list_memories` tools.
- **Shell side** — `/remember <key> <value>`, `/forget <key>`, `/memories`.

Long-term memories are injected into the system prompt so the AI retains
context across power cycles.

## Feishu (Lark) Integration

WebSocket long connection — no public IP or port forwarding needed. The device
connects to Feishu on boot and keeps the link alive. Incoming messages are
forwarded to the AI engine; replies are sent back via HTTP API.

Configure credentials:

```
/feishu_set <APP_ID> <APP_SECRET>
```

Or via environment variables at build time:

```bash
export RTCLAW_FEISHU_APP_ID='cli_...'
export RTCLAW_FEISHU_APP_SECRET='...'
```

Check status with `/feishu_status`.

## Telegram Integration

HTTP long polling -- no public IP, webhook, or port forwarding needed.
The device polls the Telegram Bot API for new messages on boot. Incoming
messages are forwarded to the AI engine; replies are sent back via
sendMessage. Messages longer than 4096 characters are automatically split.

Configure the bot token at build time:

```bash
export RTCLAW_TELEGRAM_BOT_TOKEN='123456:ABC-DEF...'
meson configure build/<platform>/meson -Dtelegram=true
```

Or via Meson options:

```bash
meson configure build/<platform>/meson \
    -Dtelegram=true \
    -Dtelegram_bot_token='123456:ABC-DEF...'
```

For QEMU (no TLS), run the API proxy in a separate terminal:

```bash
python3 scripts/api-proxy.py https://api.telegram.org 8889
```

And set `-Dtelegram_api_url='http://10.0.2.2:8889'`.
