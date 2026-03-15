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
| `/history` | 显示消息数量 |
| `/clear` | 清空对话 |
| `/help` | 列出命令 |

> WiFi 命令（`/wifi_*`）仅在支持 WiFi 的开发板上可用。

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
| `memory_info` | — | 显示堆内存使用情况 |
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
