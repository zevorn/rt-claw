# RT-Claw — 让 AI 助理触手可及

**中文** | [English](README.md)

Real-Time Claw — 受 OpenClaw 启发，面向嵌入式设备的智能助手。

通过 OSAL 支持多 RTOS，以组网节点构建蜂群智能。

## 核心理念

rt-claw 通过低成本嵌入式节点与蜂群组网，让智能从云端走向边缘。
每一个节点都可以感知世界、与其他节点协作，并实时执行控制任务。

## 功能特性

- **LLM 对话引擎** — 通过 HTTP 调用 Claude API 进行交互式对话
- **Tool Use** — LLM 驱动的硬件控制（GPIO、系统信息），基于函数调用
- **OSAL** — 一次编写，在 FreeRTOS 和 RT-Thread 上零修改运行
- **Gateway** — 服务间线程安全的消息路由
- **网络** — ESP32-C3 QEMU 上支持以太网 + HTTP 客户端；真实硬件使用 WiFi
- **蜂群**（计划中）— 节点发现、心跳检测、任务分发

## 架构

```
+---------------------------------------------------+
|                rt-claw Application                |
|  gateway | swarm | net | ai_engine | tool_use    |
+---------------------------------------------------+
|               claw_os.h  (OSAL API)               |
+-----------------+---------------------------------+
| FreeRTOS (IDF)  |          RT-Thread              |
+-----------------+---------------------------------+
| ESP32-C3        |  QEMU vexpress-a9               |
| WiFi / BLE      |  Ethernet / UART                |
+-----------------+---------------------------------+
```

## 支持平台

| 平台 | RTOS | 构建系统 | 状态 |
|------|------|---------|------|
| ESP32-C3 | ESP-IDF + FreeRTOS | CMake (idf.py) | 网络 + AI 已在 QEMU 上运行 |
| QEMU vexpress-a9 | RT-Thread | SCons | 启动验证通过 |

## 快速开始

### ESP32-C3 (ESP-IDF + QEMU)

```bash
# 依赖：ESP-IDF v5.x, Espressif QEMU
source $HOME/esp/esp-idf/export.sh
cd platform/esp32c3
idf.py set-target esp32c3
idf.py build
idf.py qemu monitor         # QEMU 仿真
idf.py -p /dev/ttyUSB0 flash monitor  # 真实硬件
```

### QEMU vexpress-a9 (RT-Thread)

```bash
# 依赖：arm-none-eabi-gcc, qemu-system-arm, scons
cd platform/qemu-a9-rtthread
scons -j$(nproc)
../../tools/qemu-run.sh
```

## 项目结构

```
rt-claw/
├── osal/                        # 操作系统抽象层
│   ├── include/claw_os.h       #   统一 RTOS API
│   ├── freertos/                #   FreeRTOS 实现
│   └── rtthread/                #   RT-Thread 实现
├── src/                         # 平台无关核心代码
│   ├── claw_init.*              #   启动入口
│   ├── claw_config.h            #   项目配置
│   ├── core/gateway.*           #   消息路由
│   ├── services/ai/             #   LLM 对话引擎（Claude API）
│   ├── services/net/            #   网络服务
│   ├── services/swarm/          #   蜂群智能
│   └── tools/                   #   Tool Use 框架（GPIO、系统信息）
├── platform/
│   ├── esp32c3/                 # ESP-IDF 工程 (CMake)
│   └── qemu-a9-rtthread/       # RT-Thread BSP (SCons)
├── vendor/
│   ├── freertos/                # FreeRTOS-Kernel (子模块)
│   └── rt-thread/               # RT-Thread (子模块)
├── docs/
│   ├── en/                      # 英文文档
│   └── zh/                      # 中文文档
├── scripts/                     # 代码风格与开发工具
└── tools/                       # 构建与启动脚本
```

## 文档

- [编码风格](docs/zh/coding-style.md)
- [贡献指南](docs/zh/contributing.md)
- [架构设计](docs/zh/architecture.md)
- [ESP32-C3 QEMU 指南](docs/zh/esp32c3-qemu.md)

## 许可证

MIT
