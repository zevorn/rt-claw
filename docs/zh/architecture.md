# 架构

[English](../en/architecture.md) | **中文**

## 概述

rt-claw 是一个受 OpenClaw 启发的 AI 助手，面向嵌入式 RTOS 平台。
它通过操作系统抽象层（OSAL）实现多 RTOS 可移植性——相同的核心代码无需
修改即可运行在 FreeRTOS（ESP-IDF）和 RT-Thread 上。

所有核心逻辑位于 `claw/` 目录。Meson 交叉编译将其生成 `librtclaw.a`
和 `libosal.a`，各平台的原生构建系统（CMake 或 SCons）将它们链接到最终
固件二进制文件中。

## 层次图

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

## OSAL（操作系统抽象层）

接口头文件：`include/osal/claw_os.h`

已抽象的基本原语：线程、互斥锁、信号量、消息队列、定时器、
内存管理（malloc/free）、日志（CLAW_LOGI/LOGW/LOGE/LOGD）、时钟节拍/时间。

实现：

- `osal/freertos/claw_os_freertos.c` -- 在 ESP-IDF 平台上链接
- `osal/rtthread/claw_os_rtthread.c` -- 在 RT-Thread 平台上链接

网络抽象：`include/osal/claw_net.h` -- HTTP POST 接口。

设计理念：链接时绑定，零开销，核心代码中不使用函数指针，
`claw/` 源文件中不使用条件编译（`#ifdef`）。

```
claw/*.c  --->  #include "osal/claw_os.h"
                        |
          +-------------+-------------+
          |                           |
  claw_os_freertos.c          claw_os_rtthread.c
  (linked on ESP-IDF)         (linked on RT-Thread)
```

## 核心服务

### 网关（`claw/core/gateway.c`）

消息路由中心，支持服务注册表和基于类型的消息分发。服务通过 type_mask 位图
和自有消息队列注册；网关将传入消息投递到所有匹配的消费者。
消息类型：DATA、CMD、EVENT、SWARM、AI_REQ。
队列容量：16 条消息 x 256 字节。专用线程优先级为 15。

### 调度器（`claw/core/scheduler.c`）

定时器驱动的任务执行框架，时钟分辨率为 1 秒。支持最多 8 个并发任务
（一次性和重复性任务）。AI 可通过工具调用创建、列出和删除任务。
通过 NVS 存储实现跨重启持久化。专用 AI 工作线程通过轮转待处理队列处理
定时 AI 任务——工作线程忙时到达的任务会被排队并依次执行，防止任务饿死。

### 心跳（`claw/core/heartbeat.c`）

可选的周期性 AI 签到功能，每 5 分钟执行一次。向 AI 引擎发送心跳提示，
使助手能够执行后台监控。依赖调度器服务进行计时。

### AI 引擎（`claw/services/ai/`）

Claude/OpenAI 兼容 API HTTP 客户端，支持 Tool Use。24 个内置工具，涵盖 GPIO、
系统信息、LCD、音频、调度器、HTTP 请求和长期记忆。每个工具声明所需能力
（`SWARM_CAP_*` 位图）和标志（`CLAW_TOOL_LOCAL_ONLY`），用于蜂群路由决策。
对话记忆：RAM 环形缓冲区（短期）+ NVS 存储（长期）。技能系统支持可复用的提示模板。

### 集群（`claw/services/swarm/`）

分布式节点协调。基于 UDP 广播的节点发现，端口 5300。
20 字节心跳包携带能力位图、负载百分比、节点角色
（WORKER / THINKER / COORDINATOR / OBSERVER）和活跃任务数。
负载感知节点选择：挑选能力匹配且负载最低的节点进行 RPC。
指数退避 RPC 重试（3 次尝试，500ms / 1s / 2s）。标记为
`CLAW_TOOL_LOCAL_ONLY` 的工具永远不会被远程委托。工具能力匹配使用
`claw_tool_t.required_caps`，以前缀匹配作为降级方案。

### 网络（`claw/services/net/`）

平台感知的 HTTP 客户端。ESP-IDF：使用 `esp_http_client` 配合 mbedTLS
实现 HTTPS。RT-Thread：BSD 套接字通过 `scripts/api-proxy.py`
（HTTP 转 HTTPS 代理，用于无原生 TLS 的环境）路由。

### 飞书 IM（`claw/services/im/feishu.c`）

通过 WebSocket 长连接接入飞书/Lark 消息平台。无需公网 IP 或 Webhook
端点。事件订阅：`im.message.receive_v1`。在飞书用户与 AI 引擎之间
进行双向消息转发。

### Telegram IM（`claw/services/im/telegram.c`）

通过 HTTP 长轮询集成 Telegram Bot API。三线程架构：轮询线程
（getUpdates，30 秒超时）、AI 工作线程（ai_chat + 频道提示）、
发送线程（sendMessage，超过 4096 字符自动分块）。Bot Token 认证，
无需 Webhook 或公网 IP。支持通过 sendChatAction 发送输入中指示器。

### Shell（`claw/shell/`）

基于 UART 的 REPL，采用聊天优先设计。直接文本输入发送到 AI 引擎。
`/commands` 触发系统操作。支持插入模式行编辑和 Tab 补全。支持 UTF-8。

## 驱动架构

采用 Linux 内核风格的组织方式：

```
drivers/<subsystem>/<vendor>/<driver>.c      -- implementation
include/drivers/<subsystem>/<vendor>/<hdr>.h -- public header
```

| 驱动 | 路径 | 描述 |
|------|------|------|
| WiFi 管理器 | `drivers/net/espressif/` | ESP32 WiFi STA 管理（C3/S3 共享） |
| ES8311 音频 | `drivers/audio/espressif/` | I2C 音频编解码器，带预设音效 |
| SSD1306 OLED | `drivers/display/espressif/` | I2C OLED 显示屏（128x64） |
| 串口控制台 | `drivers/serial/espressif/` | 串口控制台驱动 |

## 平台

| 平台 | CPU | RTOS | 构建系统 | 网络 | 开发板 |
|------|-----|------|----------|------|--------|
| ESP32-C3 | RISC-V 160MHz | FreeRTOS (ESP-IDF) | Meson + CMake | WiFi / Ethernet (QEMU) | qemu, devkit, xiaozhi-xmini |
| ESP32-S3 | Xtensa LX7 240MHz 双核 | FreeRTOS (ESP-IDF) | Meson + CMake | WiFi + PSRAM / Ethernet (QEMU) | qemu, default |
| vexpress-a9 | ARM Cortex-A9 | RT-Thread | Meson + SCons | Ethernet | qemu |

ESP32 平台的开发板选择由 `RTCLAW_BOARD` 驱动。
开发板特定配置位于 `platform/<chip>/boards/<board>/` 目录下。

## 构建流程

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

## 事件优先级模型

| 优先级 | 类别 | 延迟 | 示例 |
|--------|------|------|------|
| P0 | 反射 | 1-10 ms | ISR 处理、硬件中断 |
| P1 | 控制 | 10-50 ms | 电机控制、传感器轮询 |
| P2 | 交互 | 50-150 ms | 网关路由（线程优先级 15）、Shell I/O |
| P3 | AI | 尽力而为 | AI 引擎（线程优先级 5-10）、集群同步 |

## 资源预算（ESP32-C3）

| 模块 | SRAM | 备注 |
|------|------|------|
| ESP-IDF + WiFi + TLS | ~110 KB | 系统开销 |
| 线程栈（5 线程） | ~48 KB | main 16K + gateway 4K + swarm 4K + sched 8K + sched_ai 16K |
| 网关 + 调度器 | ~10 KB | 消息队列 16x260B + 服务注册表 + 定时器 |
| AI 引擎 + 记忆 | ~15 KB | HTTP 客户端 + 对话环形缓冲区 |
| 工具 | ~5 KB | 24 个工具描述符（含能力/标志） + 处理函数 |
| 集群 + 心跳 | ~14 KB | UDP 套接字 + 节点表（32 节点） + 定时器 |
| Shell + 应用 | ~10 KB | 行缓冲区 + 命令表 |
| **总计** | **~212 KB** | 运行时约 ~100 KB 空闲堆（实测使用率 43%） |
