# 使用指南

[English](../en/usage.md) | **中文**

## Shell

以对话为核心的 UART REPL。输入任意文本即可直接发送给 AI 引擎。
命令以 `/` 开头。

### 命令参考

| 命令 | 说明 |
|------|------|
| *(文本)* | 发送给 AI |
| `/ai_set key\|url\|model <v>` | 设置 AI 配置（NVS） |
| `/ai_status` | 显示 AI 配置 |
| `/feishu_set <id> <secret>` | 设置飞书凭证（NVS，需重启） |
| `/feishu_status` | 显示飞书配置 |
| `/wifi_set <SSID> <PASS>` | 设置 WiFi（NVS） |
| `/wifi_status` | 显示 WiFi 状态 |
| `/wifi_scan` | 扫描接入点 |
| `/remember <key> <value>` | 保存到长期记忆 |
| `/forget <key>` | 从记忆中删除 |
| `/memories` | 列出所有记忆 |
| `/task` | 列出任务 |
| `/task rm <name>` | 移除任务 |
| `/skill` | 列出技能 |
| `/skill <name> [args]` | 执行技能 |
| `/nodes` | 显示集群节点 |
| `/log [on\|off]` | 切换日志输出 |
| `/log level <lvl>` | 设置日志级别（error/warn/info/debug） |
| `/history` | 显示消息数量 |
| `/clear` | 清空对话 |
| `/ota check` | 检查固件更新 |
| `/ota update [url]` | 安装更新（或直接 URL） |
| `/ota rollback` | 回滚到上一版本固件 |
| `/ota version` | 显示当前固件版本 |
| `/voice_enable on\|off` | 启用或关闭语音服务 |
| `/voice_set <field> <value>` | 设置语音运行时配置并持久化 |
| `/voice_status` | 显示当前语音配置和运行时状态 |
| `/help` | 列出命令 |

> WiFi 命令（`/wifi_*`）仅在支持 WiFi 的开发板上可用。
> OTA 命令（`/ota`）仅在有 OTA 分区的开发板上可用（xiaozhi-xmini、ESP32-S3 default）。

## 语音（Linux 端点）

当前语音 MVP 将输入/输出端点和语音业务逻辑解耦。在 Linux 上，
`platform/linux/web_voice_server.c` 负责面向浏览器的端点，
`platform/linux/local_voice_endpoint.c` 负责本机 ALSA 工具链端点，
`claw/services/voice/voice_service.c` 则负责状态机、STT 编排、AI 交接和 TTS 交接。

### 构建期开关

```bash
meson setup build/linux --reconfigure \
    -Dosal=linux \
    -Dvoice=true \
    -Dlinux_web_voice=true \
    -Dlinux_local_voice=true
```

`linux_web_voice` 和 `linux_local_voice` 都依赖 `voice=true`。

### 运行时配置

语音配置目前有三层来源，优先级如下：

1. 通过 OSAL KV 保存的 Shell 运行时配置（`/voice_set`、`/voice_enable`）
2. 通过 Meson 选项 / 环境变量注入的编译期默认值
3. `claw_config.h` 中的平台默认值

当前可用的编译期语音调优项：

- `RTCLAW_VOICE_MAX_TURN_BYTES` / `-Dvoice_max_turn_bytes=`
- `RTCLAW_VOICE_TEXT_BUF_SIZE` / `-Dvoice_text_buf_size=`
- `RTCLAW_VOICE_STT_RESP_SIZE` / `-Dvoice_stt_resp_size=`
- `RTCLAW_VOICE_STT_TIMEOUT_MS` / `-Dvoice_stt_timeout_ms=`
- `RTCLAW_VOICE_TTS_RESP_SIZE` / `-Dvoice_tts_resp_size=`
- `RTCLAW_VOICE_TTS_AUDIO_BUF_SIZE` / `-Dvoice_tts_audio_buf_size=`

Linux 的单轮音频上限、STT 响应缓冲和 TTS 缓冲默认值高于嵌入式平台。
MiMo TTS 在可用时会走流式响应路径，解码后的音频可以分块转发给端点，
不再强制依赖一次完整的解码输出缓冲。缓冲式回退路径仍然使用
`voice_tts_resp_size` 保存完整 HTTP JSON 响应，并使用
`voice_tts_audio_buf_size` 保存解码后的音频。如果日志出现
`MiMo response truncated`，需要调大 `voice_tts_resp_size`；如果日志出现
`MiMo audio decode exceeded output buffer`，需要调大 `voice_tts_audio_buf_size`。

### Shell 命令

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

`/voice_status` 会对敏感字段做掩码显示；在 Linux voice 构建下，还会显示
web/local 端点是否正在运行，以及本机音频输入/输出设备名。

### 运行流程

1. 端点附加一个 voice session。Web 端点由浏览器连接事件流触发，本机端点由
   `/voice_enable on` 或 `/voice_local start` 触发。
2. 端点发送带音频格式元数据的 `start_capture`。
3. Web 端点通过 HTTP 上传 PCM 数据块；本机端点通过 `arecord` 从 USB 麦克风
   读取 PCM 数据块。
4. `voice_service` 将数据块转发到当前选定的 STT session，并用
   `CONFIG_RTCLAW_VOICE_MAX_TURN_BYTES` 做单轮硬截断。
5. `end_capture` 触发 STT 收尾，再把 transcript 送入 `ai_chat()`，最后将
   assistant 文本和 TTS 音频回传给端点；本机端点通过 `aplay` 播放音频。

### 浏览器页面

在 Linux web voice 构建下，浏览器测试页面由以下地址提供：

- `http://127.0.0.1:<web_port>/voice.html`

远程/手机端麦克风可用性仍然受浏览器安全模型限制；仅使用局域网 HTTP 时，
手机浏览器通常无法直接通过 `getUserMedia()` 取麦克风。

### 树莓派 3 本机音频

在 64 位 Raspberry Pi OS 上，本机端点复用系统自带 ALSA 工具链：
`arecord` 负责 USB 麦克风输入，`aplay` 负责耳机口或系统默认输出。先用：

```bash
arecord -l
aplay -l
```

确认设备编号。常见配置是 USB 麦克风为 `plughw:1,0`，耳机口输出走系统默认
`default`，也可以通过 `raspi-config` 或桌面音频设置把默认输出切到 3.5mm
耳机口。运行时设置示例：

```text
/voice_set endpoint_backend local
/voice_local input plughw:1,0
/voice_local output default
/voice_enable on
/voice_local start
/voice_local stop
```

## Tool Use

LLM 通过函数调用来操控硬件。内置 24 个工具，分为 6 个类别。每个类别由一个
Kconfig 选项控制（Audio 除外，始终启用）。

### 音频工具（始终启用）

| 工具 | 参数 | 说明 |
|------|------|------|
| `audio_beep` | `freq_hz` (100–8000), `duration_ms`, `volume` (0–100) | 播放音调 |
| `audio_play_sound` | `name` (success / error / notify / alert / startup / click) | 播放预设音效 |
| `audio_volume` | `volume` (0–100) | 设置扬声器音量 |

### GPIO 工具 (CONFIG_RTCLAW_TOOL_GPIO)

| 工具 | 参数 | 说明 |
|------|------|------|
| `gpio_set` | `pin`, `level` (0/1) | 设置引脚输出电平 |
| `gpio_get` | `pin` | 读取引脚电平 |
| `gpio_config` | `pin`, `mode` (input / output) | 配置引脚方向 |
| `gpio_blink` | `pin`, `freq_hz`, `count` (-1 = 无限) | 闪烁引脚 |
| `gpio_blink_stop` | `pin` | 停止闪烁 |

### LCD 工具 (CONFIG_RTCLAW_TOOL_LCD)

| 工具 | 参数 | 说明 |
|------|------|------|
| `lcd_fill` | `color` (RGB565 hex) | 填充整个屏幕 |
| `lcd_text` | `x`, `y`, `text`, `color`, `bg_color` | 绘制文本 |
| `lcd_rect` | `x`, `y`, `w`, `h`, `color`, `filled` | 绘制矩形 |
| `lcd_line` | `x1`, `y1`, `x2`, `y2`, `color` | 绘制线段 |
| `lcd_circle` | `x`, `y`, `r`, `color`, `filled` | 绘制圆形 |

### 系统工具 (CONFIG_RTCLAW_TOOL_SYSTEM)

| 工具 | 参数 | 说明 |
|------|------|------|
| `system_info` | — | 显示系统信息 |
| `memory_info` | — | 显示堆内存使用、最大连续块、碎片率 |
| `clear_history` | — | 清空对话历史 |
| `system_restart` | — | 重启设备 |
| `save_memory` | `key`, `value` | 保存到长期记忆 |
| `delete_memory` | `key` | 从长期记忆中删除 |
| `list_memories` | — | 列出所有已存储的记忆 |

### 调度器工具 (CONFIG_RTCLAW_TOOL_SCHED)

| 工具 | 参数 | 说明 |
|------|------|------|
| `schedule_task` | `name`, `prompt`, `interval_sec`, `count` (-1 = 永久) | 创建周期性任务 |
| `list_tasks` | — | 列出已调度的任务 |
| `remove_task` | `name` | 移除任务 |

### 网络工具 (CONFIG_RTCLAW_TOOL_NET)

| 工具 | 参数 | 说明 |
|------|------|------|
| `http_request` | `url`, `method` (GET / POST), `headers` (可选 JSON), `body` (可选) | 发送 HTTP 请求 |

### OTA 工具 (CONFIG_RTCLAW_OTA_ENABLE)

| 工具 | 参数 | 说明 |
|------|------|------|
| `ota_check` | — | 检查是否有固件更新 |
| `ota_update` | `url`（可选） | 触发 OTA 更新（从服务器或直接 URL） |
| `ota_version` | — | 获取当前运行的固件版本 |
| `ota_rollback` | — | 回滚到上一版本固件并重启 |

## OTA（空中升级）

可通过网络更新固件，无需 USB 线。仅支持带 A/B OTA 分区的开发板：
**xiaozhi-xmini**（ESP32-C3，16 MB）和 **ESP32-S3 default**（16 MB）。

### 工作原理

1. 设备向版本检查 URL 发送 HTTP GET 请求
2. 服务器返回 JSON，包含新版本号、下载 URL、大小和 SHA256
3. 设备比较版本号 — 如有更新，下载固件并写入非活动 OTA 分区
4. 设备切换启动分区并重启
5. 如果新固件启动失败，Bootloader 自动回滚

### 配置

```bash
# 设置 OTA 版本检查 URL（任选一种方式）
meson configure build/<platform>/meson -Dota_url='http://server/version.json'
export RTCLAW_OTA_URL='http://server/version.json'

# 可选：每 5 分钟自动检查一次
meson configure build/<platform>/meson -Dota_check_interval_ms=300000
```

### 版本 JSON 格式

OTA 服务器需在配置的 URL 提供以下格式的 JSON：

```json
{
    "version": "0.2.0",
    "url": "http://server/rt-claw.bin",
    "size": 524288,
    "sha256": "abcdef1234567890..."
}
```

### 本地 OTA 服务器

项目自带开发用 OTA 服务器脚本：

```bash
make build-esp32c3-xiaozhi-xmini   # 先编译固件
make ota-server                     # 启动本地 OTA 服务器

# 或指定参数：
scripts/ota-server.py --platform esp32s3 --board default --port 9000
```

脚本会自动检测编译产物、从 `claw_config.h` 读取版本号、计算 SHA256，
并输出设备端配置说明。

### 触发 OTA

**Shell 命令：**

```
/ota check                          # 检查更新
/ota update                         # 检查并安装
/ota update http://host/fw.bin      # 直接 URL 安装
/ota rollback                       # 回滚到上一版本
```

**AI 对话：**

```
You> 帮我检查有没有固件更新
rt-claw> (调用 ota_check) 有更新: 0.1.0 → 0.2.0
You> 升级吧
rt-claw> (调用 ota_update) OTA 更新已启动，完成后自动重启...
```

### GitHub Releases

当推送版本标签（`v*`）时，CI 自动构建所有硬件固件并创建 GitHub Release，包含：

- `<target>-firmware.zip` — 完整刷写包（bootloader + 分区表 + 应用）
- `<target>-rt-claw.bin` — 应用二进制（OTA 用）
- `<target>-ota-version.json` — 版本元数据（含下载 URL 和 SHA256）

## 技能系统

可复用的提示词模板，将工具调用序列封装为单词命令。
固件内置 3 个技能；用户可在运行时创建更多技能。

### 内置技能

| 名称 | 依赖 | 模板 |
|------|------|------|
| `draw` | `TOOL_LCD` | "Draw the following on LCD using lcd_* tools: %s" |
| `monitor` | `TOOL_SYSTEM` | "Check system health via system_info and memory_info, brief summary. %s" |
| `greet` | — | "You are rt-claw on embedded RTOS. Greet and describe capabilities. %s" |

### 创建技能

- **通过 Tool Use** — AI 调用 `create_skill` 工具，传入名称、描述和提示词模板。
- **通过 Shell** — `/skill` 列出当前技能。
- 技能持久化存储在 NVS Flash 中，重启后仍然保留。
- 最多支持 16 个技能（内置 + 用户创建）。

## 对话记忆

### 短期记忆

RAM 环形缓冲区，默认保存最近 20 条消息。缓冲区满时，最旧的请求/响应对会被
丢弃。可通过 `/clear` 或 `clear_history` 工具手动清除。

### 长期记忆

NVS Flash 键值存储，重启后数据不丢失。

- **AI 端** — `save_memory`、`delete_memory`、`list_memories` 工具。
- **Shell 端** — `/remember <key> <value>`、`/forget <key>`、`/memories`。

长期记忆会注入到系统提示词中，使 AI 能够在断电重启后保持上下文。

## 飞书集成

WebSocket 长连接 — 无需公网 IP 或端口转发。设备在启动时连接到飞书并保持
连接。收到的消息会转发给 AI 引擎；回复通过 HTTP API 发送回飞书。

配置凭证：

```
/feishu_set <APP_ID> <APP_SECRET>
```

或在构建时通过环境变量设置：

```bash
export RTCLAW_FEISHU_APP_ID='cli_...'
export RTCLAW_FEISHU_APP_SECRET='...'
```

使用 `/feishu_status` 查看状态。

## Telegram 集成

HTTP 长轮询 — 无需公网 IP、Webhook 或端口转发。设备启动后会持续轮询
Telegram Bot API 获取新消息。收到的消息会转发给 AI 引擎；回复通过
sendMessage 发送回 Telegram。超过 4096 字符的消息会自动分块发送。

在构建时配置 Bot Token：

```bash
export RTCLAW_TELEGRAM_BOT_TOKEN='123456:ABC-DEF...'
meson configure build/<platform>/meson -Dtelegram=true
```

或通过 Meson 选项：

```bash
meson configure build/<platform>/meson \
    -Dtelegram=true \
    -Dtelegram_bot_token='123456:ABC-DEF...'
```

QEMU 测试时（无 TLS），需在单独终端启动 API 代理：

```bash
python3 scripts/api-proxy.py https://api.telegram.org 8889
```

并设置 `-Dtelegram_api_url='http://10.0.2.2:8889'`。
