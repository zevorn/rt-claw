# 调优与优化

[English](../en/tuning.md) | **中文**

## 模块裁剪

每个模块都可以独立禁用，以节省 RAM 和 Flash 空间。

### 通过 ESP-IDF menuconfig

```bash
idf.py menuconfig
# Navigate: Component config → rt-claw Configuration → Module Selection
```

### 通过 Meson 选项

```bash
meson configure build/<platform>/meson -Dswarm=false -Dheartbeat=false
```

### 功能标志参考

| 标志 | 默认值 | RAM 节省 | 描述 |
|------|--------|----------|------|
| CONFIG_RTCLAW_SHELL_ENABLE | on | ~10KB | UART REPL 命令行 |
| CONFIG_RTCLAW_LCD_ENABLE | off | ~8KB | LCD 帧缓冲 |
| CONFIG_RTCLAW_SWARM_ENABLE | on | ~14KB | 集群网络 |
| CONFIG_RTCLAW_SCHED_ENABLE | on | ~9KB | 任务调度器 |
| CONFIG_RTCLAW_SKILL_ENABLE | on | ~4KB | 技能系统 |
| CONFIG_RTCLAW_HEARTBEAT_ENABLE | off | ~9KB | 周期性 AI 巡检 |
| CONFIG_RTCLAW_FEISHU_ENABLE | off | ~20KB | 飞书 IM + WebSocket + TLS |
| CONFIG_RTCLAW_TELEGRAM_ENABLE | off | ~16KB | Telegram Bot + HTTP 长轮询 |
| CONFIG_RTCLAW_TOOL_GPIO | on | ~2KB | GPIO 工具 |
| CONFIG_RTCLAW_TOOL_SYSTEM | on | ~3KB | 系统信息工具 |
| CONFIG_RTCLAW_TOOL_LCD | off | ~3KB | LCD 绘图工具 |
| CONFIG_RTCLAW_TOOL_SCHED | on | ~2KB | 调度器工具 |
| CONFIG_RTCLAW_TOOL_NET | on | ~2KB | HTTP 请求工具 |

### 最小配置示例

适用于 ESP32-C3 上的纯 AI 聊天终端，可预留约 50KB 空间：

```
Shell=on, Swarm=off, Sched=off, Skill=off, Heartbeat=off, Feishu=off, Telegram=off
Tools: GPIO=on, System=on, others=off
```

### 飞书机器人配置

无界面 IM 机器人，无命令行：

```
Shell=off, Feishu=on, Telegram=off, Swarm=off, Sched=on, Skill=on
Tools: System=on, Sched=on, Net=on, GPIO=off, LCD=off
```

### Telegram 机器人配置

无界面 Telegram 机器人，无命令行：

```
Shell=off, Telegram=on, Feishu=off, Swarm=off, Sched=on, Skill=on
Tools: System=on, Sched=on, Net=on, GPIO=off, LCD=off
```

## 内存优化

### ESP32-C3（400KB SRAM，WiFi/TLS 后约 200KB 可用）

实测运行时：约 43% 使用率（182KB 空闲 / 321KB 总堆）。关键调优：

- `NET_RESP_MAX` -- HTTP 工具响应缓冲区（默认 4KB，原为 16KB）
- `sched_ai_ctx` -- prompt/reply 缓冲区改为堆分配，用后即释放
- `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` -- TLS 输入缓冲区（默认 16KB，可降至 8KB）
- `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN` -- TLS 输出缓冲区（默认 16KB，可降至 4KB）
- `CONFIG_ESP_WIFI_IRAM_OPT` -- WiFi IRAM 优化（禁用可节省 IRAM）
- `CONFIG_ESP_WIFI_RX_IRAM_OPT` -- WiFi RX IRAM（禁用可节省 IRAM）
- `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM` -- 动态 RX 缓冲区数量（可降至 16）

### ESP32-S3（512KB SRAM + 8MB PSRAM）

- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=2048` -- 仅小内存分配使用内部 SRAM
- `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` -- TLS 缓冲区放入 PSRAM
- 双核绑定：WiFi+TLS 绑定 Core 0，应用绑定 Core 1

### AI 引擎调优（claw_config.h 或 meson 选项）

| 参数 | 默认值 | 影响 |
|------|--------|------|
| ai_max_tokens | 1024 | 最大响应长度。降低可减少 RAM 占用并加快响应 |
| ai_context_size | 8192 | HTTP 响应缓冲区。受限设备可设为 4096 |
| ai_memory_max_msgs | 20 | 对话历史深度。低内存设备可设为 10 |

### 对话记忆

- 短期记忆：`ai_memory_max_msgs` 控制 RAM 环形缓冲区大小
- 长期记忆：NVS Flash，约 4KB。除读取操作外无 RAM 开销。

## 配置参数参考

### claw_config.h 常量

| 常量 | 默认值 | 描述 |
|------|--------|------|
| CLAW_GW_MSG_POOL_SIZE | 16 | 网关消息队列深度 |
| CLAW_GW_MSG_MAX_LEN | 256 | 最大消息大小（字节） |
| CLAW_SWARM_MAX_NODES | 32 | 最大可发现集群节点数 |
| CLAW_SWARM_HEARTBEAT_MS | 5000 | 心跳广播间隔（20 字节数据包） |
| CLAW_SWARM_PORT | 5300 | UDP 发现端口 |
| SWARM_RPC_MAX_RETRIES | 3 | RPC 重试次数（指数退避） |
| SWARM_RPC_RETRY_BASE_MS | 500 | 重试基准延迟（每次翻倍） |
| CLAW_SCHED_MAX_TASKS | 8 | 最大并发调度任务数 |
| CLAW_SCHED_TICK_MS | 1000 | 调度器分辨率 |
| CLAW_HEARTBEAT_INTERVAL_MS | 300000 | AI 巡检间隔（5 分钟） |

### 构建时 AI 默认值

| 参数 | 默认值 | 环境变量 | Meson 选项 |
|------|--------|----------|------------|
| API Key | "" | RTCLAW_AI_API_KEY | ai_api_key |
| API URL | http://10.0.2.2:8888/v1/messages | RTCLAW_AI_API_URL | ai_api_url |
| Model | claude-opus-4-6 | RTCLAW_AI_MODEL | ai_model |
| Max Tokens | 1024 | RTCLAW_AI_MAX_TOKENS | ai_max_tokens |
| Context Size | 8192 | RTCLAW_AI_CONTEXT_SIZE | ai_context_size |
| Memory Messages | 20 | RTCLAW_AI_MEMORY_MAX_MSGS | ai_memory_max_msgs |

### 构建时 Telegram 默认值

| 参数 | 默认值 | 环境变量 | Meson 选项 |
|------|--------|----------|------------|
| Bot Token | "" | RTCLAW_TELEGRAM_BOT_TOKEN | telegram_bot_token |
| API URL | https://api.telegram.org | RTCLAW_TELEGRAM_API_URL | telegram_api_url |

## 网络说明

- QEMU 使用虚拟以太网（OpenCores MAC），不支持 WiFi
- vexpress-a9 不支持 TLS：使用 `scripts/api-proxy.py`（HTTP 转 HTTPS 代理）
- ESP32 实际硬件：WiFi 搭配 mbedTLS 实现 HTTPS
