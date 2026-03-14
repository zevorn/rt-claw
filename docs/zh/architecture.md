# rt-claw 架构设计

[English](../en/architecture.md) | **中文**

## 概述

rt-claw 将 OpenClaw 个人助手概念引入嵌入式 RTOS 设备。
每个 rt-claw 节点是一个轻量、具备实时能力的单元，可独立运行，
也可加入蜂群节点网络进行分布式智能协作。

通过 OSAL（操作系统抽象层）支持多 RTOS——相同的核心代码
在 FreeRTOS（ESP-IDF）和 RT-Thread 上运行，无需任何修改。

## OSAL — 操作系统抽象层

关键架构决策：所有 rt-claw 核心代码仅依赖 `osal/claw_os.h`。

```
claw/*.c  --->  #include "osal/claw_os.h"  (编译时接口)
                        |
          +-------------+-------------+
          |                           |
  claw_os_freertos.c          claw_os_rtthread.c
  (ESP-IDF 平台链接)          (RT-Thread 平台链接)
```

抽象的原语：
- 线程、互斥锁、信号量、消息队列、定时器
- 内存分配（malloc/free）
- 日志（CLAW_LOGI/LOGW/LOGE/LOGD）
- Tick / 时间

设计：接口头文件 + 每个 RTOS 一个实现文件（链接时绑定）。
零运行时开销。无函数指针。核心代码中无条件编译。

## 核心服务

### 网关（claw/core/gateway）

面向蜂群通信的节点间消息路由骨架。

- 线程安全的消息队列（通过 `claw_mq_*`）
- 消息类型：DATA、CMD、EVENT、SWARM
- 路由逻辑尚未实现——当前仅记录日志

### 蜂群服务（claw/services/swarm）

节点发现与协调，用于构建 rt-claw 设备网格。

- 局域网 UDP 广播发现
- ESP-NOW 超低延迟点对点通信（ESP32 平台）
- 基于心跳的存活检测
- 能力广播
- 跨蜂群任务分发

### 网络服务（claw/services/net）

平台感知的网络支持：

- ESP32-C3：WiFi（802.11 b/g/n）+ MQTT + mbedTLS
- QEMU-A9：以太网（smc911x）+ lwIP + MQTT
- 通用：基于 MQTT topic 的通道系统（映射到 OpenClaw 的多通道架构）

### AI 引擎（claw/services/ai）

LLM API 客户端，支持 Tool Use：

- Claude API 对话与函数调用（Tool Use）
- 对话记忆（短期 RAM 环形缓冲 + 长期存储）
- HTTP/HTTPS 传输（ESP-IDF 使用 esp_http_client + TLS；RT-Thread 使用 BSD socket + 代理）

## 平台

### ESP32-C3（platform/esp32c3/）

- CPU：RISC-V 32 位（rv32imc），160MHz
- RAM：400KB SRAM（约 240KB 可用于应用）
- WiFi：802.11 b/g/n
- BLE：Bluetooth 5.0 LE
- RTOS：ESP-IDF + FreeRTOS
- 构建：Meson（交叉编译）+ CMake/idf.py（链接 + 烧录）
- QEMU：Espressif 分支（qemu-riscv32），仅 UART（无 WiFi 仿真）

### vexpress-a9 QEMU（platform/vexpress-a9/）

- CPU：ARM Cortex-A9（双核）
- RTOS：RT-Thread
- 构建：Meson（交叉编译）+ SCons（链接）
- 外设：UART、以太网、LCD、SD 卡

## 通信流程

```
外部（MQTT/HTTP）
       |
       v
  +---------+
  | net_svc |
  +----+----+
       |
       v
  +---------+     +---------+
  | gateway |---->| swarm   |---- 其他 rt-claw 节点
  +----+----+     +---------+
       |
       v
  +---------+
  |ai_engine|
  +---------+
```

## ESP32-C3 资源预算

| 模块 | SRAM | 说明 |
|------|------|------|
| ESP-IDF + WiFi + TLS | ~160KB | 系统开销 |
| Gateway | ~8KB | MQ 16x256B + 线程 |
| Swarm | ~12KB | 32 节点 + ESP-NOW |
| Net（MQTT） | ~25KB | 客户端 + 缓冲区 |
| AI Engine | ~15KB | LLM API 客户端 + Tool Use |
| App + CLI | ~10KB | 主程序 + shell |
| **合计** | **~230KB** | 约 170KB 剩余空间 |
