# RT-Claw 项目说明书

> 首届中关村北纬龙虾大赛 · 生产力龙虾赛道

---

## 一、项目概述

**RT-Claw** 是一个开源的嵌入式 AI 助手框架，目标是让大语言模型（LLM）的能力真正落地到低成本微控制器上。项目名称来源于 "Real-Time Claw"——实时操作系统上的 AI 爪子，能够抓取和操控一切硬件资源。

项目灵感来自 OpenClaw 社区，采用纯 C99 实现，通过操作系统抽象层（OSAL）实现跨 RTOS 运行，目前支持 FreeRTOS 和 RT-Thread 两大主流嵌入式操作系统，覆盖 ESP32-C3、ESP32-S3、Zynq-A9、vexpress-A9 四个硬件平台。

### 定位

| 维度 | RT-Claw | 传统 AI 助手 |
|------|---------|------------|
| 运行环境 | MCU (ESP32, ARM Cortex) | 云端 / 手机 / PC |
| 硬件成本 | ~¥8 ($1) | ~¥2000+ |
| 内存需求 | < 400KB | > 4GB |
| 硬件控制 | 原生（GPIO、SPI、I2C） | 需额外适配 |
| 实时性 | RTOS 保证 | 无保证 |
| 联网需求 | 仅 API 调用时需要 | 始终在线 |
| 部署方式 | 烧录固件 / 浏览器刷机 | 安装 App |

---

## 二、系统架构

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
│   Gateway (消息路由)  ·  Scheduler (定时任务)    │
│   Heartbeat (心跳)   ·  Swarm (蜂群智能)        │
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

### 2.1 OSAL（操作系统抽象层）

OSAL 是 RT-Claw 的基石。通过统一的 `claw_os.h` 接口，上层业务代码不包含任何 RTOS 特定的头文件或 API 调用，实现真正的"一次编写，多 RTOS 运行"。

**抽象接口**：
- `claw_os.h` — 线程、互斥锁、信号量、队列、定时器、内存
- `claw_net.h` — HTTP POST 网络请求
- `claw_kv.h` — 键值存储（NVS Flash）

**绑定方式**：链接时选择 OSAL 实现文件（`claw_os_freertos.c` 或 `claw_os_rtthread.c`），零 `#ifdef` 污染。

### 2.2 Gateway（消息路由中心）

Gateway 是 RT-Claw 的消息总线，采用 Pipeline 处理链 + 服务注册表架构：

- **消息类型**：DATA、CMD、EVENT、SWARM、AI_REQ
- **队列容量**：16 条消息 × 256 字节
- **线程优先级**：15（高优先级确保消息及时处理）
- **统计能力**：总消息数、按类型统计、丢弃数、无消费者数

### 2.3 AI 服务

#### AI Engine（对话引擎）
- 支持 HTTP API 调用任意 LLM（Claude、GPT、Gemini、DeepSeek 等）
- 自动构建 Tool Use 格式的 JSON 请求
- 解析 LLM 返回的工具调用指令并执行

#### Skill System（技能系统）
- AI 可组合多个工具调用为一个"技能"
- 技能可持久化到 NVS Flash，跨重启生效
- 通过 Shell 命令 `/skill` 管理技能的创建、执行、删除

#### AI Memory（对话记忆）
- **短期记忆**：RAM 环形缓冲区，保持最近对话上下文
- **长期记忆**：NVS Flash 持久化，AI 可主动保存重要信息
- 记忆检索：AI 工具调用 `save_memory` / `list_memory` / `delete_memory`

### 2.4 Tool Use（工具调用框架）

RT-Claw 的核心创新之一。LLM 不直接控制硬件，而是通过标准化的工具调用接口间接操作，实现"对话即编程"。

**内置工具列表（30+）**：

| 分类 | 工具 | 功能 |
|------|------|------|
| GPIO | `gpio_set`, `gpio_get`, `gpio_config`, `gpio_blink`, `gpio_blink_stop` | 引脚控制 |
| LCD | `lcd_fill`, `lcd_text`, `lcd_rect`, `lcd_line`, `lcd_circle` | 屏幕绘制 |
| 音频 | `beep`, `play_sound`, `volume` | 声音控制 |
| 网络 | `http_request` | HTTP 请求 |
| 系统 | `system_info`, `memory_info`, `restart`, `clear_history` | 系统管理 |
| 调度 | `schedule_task`, `list_tasks`, `remove_task` | 定时任务 |
| 记忆 | `save_memory`, `list_memory`, `delete_memory` | 持久记忆 |
| OTA | `ota_update` | 固件升级 |

**工具注册机制**：每个工具声明能力位图（`SWARM_CAP_*`），蜂群协议据此路由跨节点调用。

### 2.5 Swarm Intelligence（蜂群智能）

多个 RT-Claw 节点通过网络自动组成协作集群：

- **节点发现**：UDP 广播自动发现局域网内的节点
- **能力广播**：每个节点广播自身的工具能力位图
- **心跳检测**：周期性心跳确认节点存活
- **远程调用**：节点 A 缺少某工具时，自动路由到拥有该工具的节点 B 执行

### 2.6 IM 集成

- **飞书（Lark）**：通过 WebSocket 长连接接入飞书机器人，用户在飞书群聊中发消息即可远程操控设备
- **Telegram**：规划中，架构已预留接口

---

## 三、技术特色

### 3.1 纯 C99，零臃肿

- 不使用 C++ / Python / MicroPython
- 核心框架约 8000 行 C 代码
- 唯一第三方库：cJSON（JSON 解析）
- 可在 400KB RAM 设备上完整运行

### 3.2 多 RTOS 一次编写

```
claw/ 目录：业务代码 ──→ #include "claw_os.h" ──→ 零 RTOS 特定代码
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
  claw_os_freertos.c   claw_os_rtthread.c    (future RTOS...)
```

### 3.3 AI 驱动的硬件控制

传统方式 vs RT-Claw 方式：

```
传统：编写 C 代码 → 交叉编译 → 烧录 → 测试 → 重复

RT-Claw：对 AI 说话 → AI 选择工具 → 工具执行 → 完成
         "让 LED 闪烁"  → gpio_blink   → 硬件动作
```

### 3.4 浏览器一键刷机

基于 Web Serial API + esptool-js，用户无需安装任何开发工具：
1. 打开 RT-Claw 官网
2. 选择平台和开发板
3. 点击"Install RT-Claw"
4. 选择串口 → 自动烧录

### 3.5 完整模拟器支持

每个平台都支持 QEMU 模拟器运行，开发调试无需真实硬件：

```bash
make run-esp32c3-qemu     # ESP32-C3 模拟器
make run-zynq-a9-qemu     # Zynq-A9 模拟器
make run-vexpress-a9-qemu # vexpress-A9 模拟器
```

---

## 四、应用场景

### 4.1 智能家居网关

多个 ESP32 节点分布在家中不同房间，通过蜂群协议自动组网：
- 客厅节点控制灯光和显示屏
- 卧室节点控制窗帘和空调
- 用户通过飞书机器人一句话控制全屋设备

### 4.2 工业边缘 AI

在工厂产线部署低成本 AI 节点：
- 实时采集传感器数据
- AI 判断异常并自动触发报警
- 定时任务自动生成巡检报告

### 4.3 教育与创客

- QEMU 模拟器零硬件门槛学习嵌入式 AI
- 浏览器刷机降低新手入门难度
- 对话式编程让非专业开发者也能控制硬件

### 4.4 分布式 AI 传感网

- 多节点协作采集环境数据
- AI 智能调度任务分配
- 低成本大规模部署（单节点 ¥8）

---

## 五、项目现状与路线图

### 已完成功能

| 模块 | 状态 | 说明 |
|------|------|------|
| OSAL 抽象层 | ✅ | FreeRTOS + RT-Thread 双实现 |
| LLM 对话引擎 | ✅ | HTTP API，多模型支持 |
| Tool Use 框架 | ✅ | 30+ 内置工具 |
| Skill 技能系统 | ✅ | 多工具编排 + NVS 持久化 |
| AI Memory | ✅ | 短期 RAM + 长期 Flash |
| Gateway 消息路由 | ✅ | Pipeline + 服务注册表 |
| Scheduler 定时任务 | ✅ | 1s 分辨率，8 并发 |
| Swarm 蜂群智能 | ✅ | 节点发现 + 能力路由 |
| 飞书机器人 | ✅ | WebSocket 长连接 |
| Web Flash 刷机 | ✅ | esptool-js + Web Serial |
| 交互式 Shell | ✅ | UART REPL + Tab 补全 |
| ESP32-C3 支持 | ✅ | QEMU + 真机（3 款开发板） |
| ESP32-S3 支持 | ✅ | QEMU + 真机 |
| Zynq-A9 支持 | ✅ | QEMU + FreeRTOS+TCP |
| vexpress-A9 支持 | ✅ | QEMU + RT-Thread |

### 规划中功能

| 模块 | 优先级 | 说明 |
|------|--------|------|
| Telegram 机器人 | 高 | 已预留架构 |
| 多模型 API 统一 | 高 | Claude/GPT/Gemini/DeepSeek |
| Web 配置门户 | 中 | 运行时配置 API Key、模型选择 |
| 语音交互 | 中 | ES8311 音频编解码 + ASR |
| Claw Skill Provider | 低 | 节点作为其他 Claw 的技能插件 |

---

## 六、技术栈

| 层次 | 技术 |
|------|------|
| 编程语言 | C (gnu99) |
| 构建系统 | Meson + CMake (ESP-IDF) + SCons (RT-Thread) |
| RTOS | FreeRTOS v11 + RT-Thread v5.3.0 |
| 网络栈 | lwIP (ESP-IDF) / FreeRTOS+TCP / RT-Thread SAL |
| JSON | cJSON |
| AI API | HTTP POST (OpenAI-compatible format) |
| IM | 飞书 WebSocket SDK |
| Web 工具 | esptool-js + Web Serial API |
| 模拟器 | QEMU (ESP32 / ARM) |
| 版本控制 | Git |
| 文档 | Markdown + marked.js SPA |
| 开源协议 | MIT |

---

## 七、项目亮点总结

1. **极致低成本**：$1 硬件即可运行完整 AI 助手
2. **硬件原生 AI**：不是"手机 AI 连硬件"，而是"AI 就跑在硬件里"
3. **对话即编程**：自然语言直接操控 GPIO、LCD、网络等硬件资源
4. **蜂群智能**：多节点自动组网、能力共享、协作执行
5. **跨 RTOS**：同一代码库支持 FreeRTOS + RT-Thread，覆盖主流嵌入式生态
6. **零门槛部署**：浏览器打开即刷机，QEMU 模拟器零硬件开发
7. **完全开源**：MIT 协议，代码透明可审计

---

## 八、项目信息

| 项 | 值 |
|------|------|
| 项目名称 | RT-Claw |
| 全称 | Real-Time Claw — Embedded AI Assistant Framework |
| 开源协议 | MIT |
| 代码仓库 | [github.com/zevorn/rt-claw](https://github.com/zevorn/rt-claw) |
| 官方网站 | [zevorn.github.io/rt-claw](https://zevorn.github.io/rt-claw) |
| 主要语言 | C (gnu99) |
| 作者 | Chao Liu |
| 参赛赛道 | 生产力龙虾 |
| 报名日期 | 2026 年 3 月 |
