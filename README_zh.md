<p align="center">
  <img src="images/logo.png" alt="RT-Claw" width="500">
</p>

<p align="center">
  <strong>让 AI 助理触手可及</strong>
</p>

<p align="center">
  <a href="https://qm.qq.com/q/heSPPC9De8"><img src="https://img.shields.io/badge/Join%20QQ-GTOC-brightgreen?style=for-the-badge&logo=QQ&logoColor=76bad9&color=76bad9" alt="QQ 群"></a>
  <a href="https://t.me/gevico_channel"><img src="https://img.shields.io/badge/Telegram-GTOC-blue?style=for-the-badge&logo=telegram" alt="Telegram"></a>
  <a href="https://space.bilibili.com/483048140"><img src="https://img.shields.io/badge/Bilibili-%E7%BB%9D%E5%AF%B9%E6%98%AF%E6%B3%BD%E6%96%87%E5%95%A6-FB7299?style=for-the-badge&logo=bilibili" alt="Bilibili"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT License"></a>
</p>

<p align="center"><strong>中文</strong> | <a href="README.md">English</a></p>

**RT-Claw** — 受 [OpenClaw](https://github.com/openclaw/openclaw) 启发，面向嵌入式设备的智能助手。
通过 OSAL 支持多 RTOS，以组网节点构建蜂群智能。

> 仅需一美元的硬件成本，即可快速部署你的专属 AI 助理——无缝融入工作与生活，高效连接物理世界。

<p align="center">
  <img src="images/demo.png" alt="RT-Claw 演示 — AI 通过 Tool Use 在 LCD 上绘图" width="700">
</p>

[架构设计](docs/zh/architecture.md) · [ESP32-C3 QEMU 指南](docs/zh/esp32c3-qemu.md) · [贡献指南](docs/zh/contributing.md) · [编码风格](docs/zh/coding-style.md)

## 核心理念

rt-claw 通过低成本嵌入式节点与蜂群组网，让智能从云端走向边缘。
每一个节点都可以感知世界、与其他节点协作，并实时执行控制任务。

RT-Claw 将硬件能力原子化——GPIO、传感器、LCD、网络——作为工具供 LLM
动态编排。无需重复编写、编译和烧录嵌入式代码，即可适配任意应用场景。

## 功能特性

| 功能 | 描述 | 状态 |
|------|------|------|
| LLM 对话引擎 | 通过 HTTP 调用 Claude API 进行交互式对话 | 已完成 |
| Tool Use | LLM 驱动的硬件控制（GPIO、系统信息、LCD），基于函数调用 | 已完成 |
| LCD 图形 | 320x240 RGB565 帧缓冲，支持文字、图形绘制原语；AI 可通过工具调用在屏幕上绘图 | 已完成 |
| 对话优先 Shell | UART 交互终端，直接输入发送 AI 对话，/命令 执行系统操作；支持 UTF-8 | 已完成 |
| OSAL | 一次编写，在 FreeRTOS 和 RT-Thread 上零修改运行 | 已完成 |
| Gateway | 服务间线程安全的消息路由 | 已完成 |
| 网络 | ESP32-C3 QEMU 上支持以太网 + HTTP 客户端；真实硬件使用 WiFi | 已完成 |
| 多模型 API | 支持主流 LLM API：Claude、GPT、Gemini、DeepSeek、GLM（智谱）、MiniMax、Grok、Moonshot（Kimi）、百川、通义千问、豆包、Llama（Ollama） | 计划中 |
| Web 配置页面 | 内置轻量 Web 页面，支持在线配置 API Key、选择模型、调整参数 | 计划中 |
| 蜂群智能 | 节点发现、心跳检测、分布式任务调度 | 进行中 |
| 对话记忆 | 短期 RAM 环形缓冲 + 长期 NVS Flash 持久化存储 | 已完成 |
| 技能记忆 | 节点学习并记忆常用操作模式 | 进行中 |
| 定时任务 | 定时触发任务执行与周期性自动化 | 已完成 |
| IM 集成 | 接入飞书、钉钉、QQ、Telegram 作为消息通道 | 计划中 |
| Claw 技能提供者 | 作为其他 Claw 的技能插件，赋予其感知和控制物理世界的能力 | 计划中 |

## 架构

```
+---------------------------------------------------+
|                rt-claw Application                |
|  gateway | swarm | net | ai_engine | tools | lcd  |
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

**1. 安装系统依赖**

```bash
# Ubuntu / Debian
sudo apt install git wget flex bison gperf python3 python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 libgcrypt20-dev libglib2.0-dev libpixman-1-dev \
    libsdl2-dev libslirp-dev

# Arch Linux
sudo pacman -S --needed libgcrypt glib2 pixman sdl2 libslirp \
    python cmake ninja gcc git wget flex bison
```

**2. 安装 ESP-IDF + QEMU**

```bash
# 一键安装（克隆 ESP-IDF v5.4，安装工具链 + QEMU）
./tools/setup-esp-env.sh
```

**3. 配置 API 密钥**

```bash
source $HOME/esp/esp-idf/export.sh
cd platform/esp32c3
idf.py set-target esp32c3

# 配置 LLM API 密钥（AI 对话功能必需）
idf.py menuconfig
# 路径：rt-claw Configuration → AI Engine
#   - LLM API Key:          <你的 API 密钥>
#   - LLM API endpoint URL: https://api.anthropic.com/v1/messages
#   - LLM model name:       claude-sonnet-4-6
```

**4. 构建与运行**

```bash
# 编译（自动检测 sdkconfig 中的目标芯片）
./tools/esp32c3-build.sh

# 在 QEMU 上运行（自动生成 flash 镜像并启动模拟器）
./tools/esp32c3-qemu-run.sh --raw

# 或烧录到真实硬件
idf.py -p /dev/ttyUSB0 flash monitor
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
│   └── tools/                   #   Tool Use 框架（GPIO、系统信息、LCD）
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
└── tools/                       # 构建、启动与开发脚本
```

## 社区

加入 GTOC（格维开源社区）交流频道：

- **QQ 群**：[加入](https://qm.qq.com/q/heSPPC9De8)
- **Telegram**：[GTOC 频道](https://t.me/gevico_channel)
- **Bilibili**：[绝对是泽文啦](https://space.bilibili.com/483048140)
- **微信**：GTOC 微信公众号

## 文档

- [编码风格](docs/zh/coding-style.md)
- [贡献指南](docs/zh/contributing.md)
- [架构设计](docs/zh/architecture.md)
- [ESP32-C3 QEMU 指南](docs/zh/esp32c3-qemu.md)

## 许可证

MIT
