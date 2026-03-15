<p align="center">
  <img src="images/logo.png" alt="RT-Claw" width="500">
</p>

<p align="center">
  <strong>让 AI 助理触手可及</strong>
</p>

<p align="center">
  <a href="https://discord.gg/BZ9nFVzX"><img src="https://img.shields.io/badge/Discord-RT--Claw-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Discord"></a>
  <a href="https://qm.qq.com/q/heSPPC9De8"><img src="https://img.shields.io/badge/Join%20QQ-GTOC-brightgreen?style=for-the-badge&logo=QQ&logoColor=76bad9&color=76bad9" alt="QQ 群"></a>
  <a href="https://t.me/gevico_channel"><img src="https://img.shields.io/badge/Telegram-GTOC-blue?style=for-the-badge&logo=telegram" alt="Telegram"></a>
  <a href="https://space.bilibili.com/483048140"><img src="https://img.shields.io/badge/Bilibili-%E7%BB%9D%E5%AF%B9%E6%98%AF%E6%B3%BD%E6%96%87%E5%95%A6-FB7299?style=for-the-badge&logo=bilibili" alt="Bilibili"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT License"></a>
</p>

<p align="center"><strong>中文</strong> | <a href="README.md">English</a></p>

**RT-Claw** — 受 [OpenClaw](https://github.com/openclaw/openclaw) 启发，面向嵌入式设备的智能助手。
通过 OSAL 支持多 RTOS，以组网节点构建蜂群智能。
ESP32-S3 WiFi 支持参考了 [MimiClaw](https://github.com/memovai/mimiclaw)。

> 仅需一美元的硬件成本，即可快速部署你的专属 AI 助理——无缝融入工作与生活，高效连接物理世界。

<p align="center">
  <img src="images/demo.png" alt="RT-Claw 演示 — AI 通过 Tool Use 在 LCD 上绘图" width="700">
</p>

[快速开始](docs/zh/getting-started.md) · [使用指南](docs/zh/usage.md) · [架构设计](docs/zh/architecture.md) · [移植与扩展](docs/zh/porting.md) · [裁剪与优化](docs/zh/tuning.md) · [贡献指南](docs/zh/contributing.md)

## 核心理念

rt-claw 通过低成本嵌入式节点与蜂群组网，让智能从云端走向边缘。
每一个节点都可以感知世界、与其他节点协作，并实时执行控制任务。

RT-Claw 将硬件能力原子化——GPIO、传感器、LCD、网络——作为工具供 LLM
动态编排。无需重复编写、编译和烧录嵌入式代码，即可适配任意应用场景。

## 功能特性

| 功能 | 描述 | 状态 |
|------|------|------|
| LLM 对话引擎 | 通过 HTTP 调用 Claude API 进行交互式对话 | 已完成 |
| Tool Use | LLM 驱动的硬件控制（GPIO、系统信息、LCD、音频、调度器、HTTP），基于函数调用；30+ 内置工具 | 已完成 |
| 技能系统 | 可组合的多工具工作流；AI 可创建、持久化（NVS）并执行融合多个工具的技能 | 已完成 |
| LCD 图形 | 320x240 RGB565 帧缓冲，支持文字、图形绘制原语；AI 可通过工具调用在屏幕上绘图 | 已完成 |
| OLED 显示 | SSD1306 I2C OLED 驱动，适配 xiaozhi-xmini 开发板 | 已完成 |
| 音频 | ES8311 编解码器驱动，预设音效（成功、错误、通知、警报）；AI 可控制音量和蜂鸣 | 已完成 |
| 对话优先 Shell | UART 交互终端，支持插入模式编辑、Tab 补全、UTF-8；直接输入发送 AI 对话，/命令 执行系统操作 | 已完成 |
| OSAL | 一次编写，在 FreeRTOS 和 RT-Thread 上零修改运行 | 已完成 |
| Gateway | 服务间线程安全的消息路由 | 已完成 |
| 网络 | ESP32-C3 QEMU 上支持以太网 + HTTP 客户端；真实硬件使用 WiFi | 已完成 |
| 蜂群智能 | 节点发现、心跳检测、能力位图、跨节点远程工具调用 | 已完成 |
| 对话记忆 | 短期 RAM 环形缓冲 + 长期 NVS Flash 持久化存储；AI 可保存/删除/查询记忆 | 已完成 |
| 定时任务 | 定时触发任务执行与周期性自动化；AI 可创建/查看/删除任务 | 已完成 |
| IM 集成 | 飞书（Lark）WebSocket 长连接；计划中：钉钉、QQ、Telegram | 进行中 |
| Web 刷写与串口 | 浏览器端固件刷写（esptool-js）+ 串口终端（ANSI 彩色渲染） | 已完成 |
| 多模型 API | 支持主流 LLM API：Claude、GPT、Gemini、DeepSeek、GLM（智谱）、MiniMax、Grok、Moonshot（Kimi）、百川、通义千问、豆包、Llama（Ollama） | 计划中 |
| Web 配置页面 | 内置轻量 Web 页面，支持在线配置 API Key、选择模型、调整参数 | 计划中 |
| Claw 技能提供者 | 作为其他 Claw 的技能插件，赋予其感知和控制物理世界的能力 | 计划中 |

## 架构

```
+--------------------------------------------------------------+
|                     rt-claw Application                      |
| gateway | net | swarm | ai_engine | shell | sched | feishu   |
+--------------------------------------------------------------+
|                      skills (AI Skills)                      |
|            (one skill composes multiple tools)               |
+--------------------------------------------------------------+
|                     tools (Tool Use)                         |
| gpio | system | lcd | audio | http | scheduler | memory      |
+--------------------------------------------------------------+
|                   drivers (Hardware BSP)                     |
| WiFi | ES8311 | SSD1306 | serial | LCD framebuffer           |
+--------------------------------------------------------------+
|               osal/claw_os.h  (OSAL API)                     |
+-------------------+------------------------------------------+
| FreeRTOS (IDF)    |             RT-Thread                    |
+-------------------+------------------------------------------+
| ESP32-C3 / S3     |  QEMU vexpress-a9                        |
+-------------------+------------------------------------------+
```

## 支持平台

| 平台 | 运行目标 | RTOS | 构建系统 | 状态 |
|------|---------|------|---------|------|
| ESP32-C3 | QEMU（Espressif 分支） | ESP-IDF + FreeRTOS | Meson + CMake | AI + 以太网已验证 |
| ESP32-S3 | QEMU（Espressif 分支） | ESP-IDF + FreeRTOS | Meson + CMake | AI + 以太网已验证 |
| ESP32-C3 | 真实硬件（xiaozhi-xmini） | ESP-IDF + FreeRTOS | Meson + CMake | 已验证 |
| ESP32-S3 | 真实硬件 | ESP-IDF + FreeRTOS | Meson + CMake | 未测试 |
| QEMU vexpress-a9 | QEMU | RT-Thread | Meson + SCons | 启动 + 以太网已验证 |

> 说明：我们已在 QEMU 10.2.x 上检查过上游 STM32 machine，但由于其以太网
> 设备仍未实现，rt-claw 目前无法在这些 STM32 QEMU 板型上提供联网能力，因此
> 暂未将其列入支持列表。

## 快速开始

### ESP32-S3 真实硬件（WiFi + PSRAM）

> 需要 **16 MB Flash** + **8 MB PSRAM** 的 ESP32-S3 开发板（如 ESP32-S3-DevKitC-1）。

**1. 安装系统依赖 + ESP-IDF**

```bash
# Ubuntu / Debian
sudo apt install git wget flex bison gperf python3 python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 meson

# 一键安装 ESP-IDF（克隆 ESP-IDF v5.4，安装工具链）
./scripts/setup-esp-env.sh
```

**2. 配置 API 密钥**

方式 A — 环境变量（推荐）：

```bash
export RTCLAW_AI_API_KEY='<你的 API 密钥>'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'
```

方式 B — 运行时通过 Shell 命令（写入 NVS，重启保持）：

```
/ai_set key <你的 API 密钥>
/ai_set url https://api.anthropic.com/v1/messages
/ai_set model claude-sonnet-4-6
```

**3. 配置 WiFi**

方式 A — 编译时写入（menuconfig）：

```bash
idf.py -C platform/esp32s3 menuconfig
# 路径：Component config → rt-claw Configuration → WiFi
#   → Default WiFi SSID
#   → Default WiFi password
```

方式 B — 运行时通过 Shell 命令（写入 NVS，重启保持）：

```
/wifi_set <SSID> <PASSWORD>
```

NVS 凭据（方式 B）优先级高于编译时默认值。
两者都未配置时，设备以离线模式启动，随时可通过 `/wifi_set` 补配。

**4. 编译**

```bash
make esp32s3
```

**5. 烧录与监视**

```bash
# 烧录（自动检测串口）
make flash-esp32s3

# 串口监视（Ctrl+] 退出）
make monitor-esp32s3

# 或手动指定串口
idf.py -C platform/esp32s3 -p /dev/ttyUSB0 flash monitor
```

**6. Shell 命令**

| 命令 | 说明 |
|------|------|
| *（直接输入）* | 发送消息给 AI |
| `/ai_set key\|url\|model <值>` | 设置 AI API 配置（持久化到 NVS） |
| `/ai_status` | 查看当前 AI 配置 |
| `/feishu_set <id> <secret>` | 设置飞书凭据（重启生效） |
| `/feishu_status` | 查看飞书配置 |
| `/wifi_set <SSID> <密码>` | 保存 WiFi 凭据到 NVS |
| `/wifi_status` | 查看连接状态和 IP |
| `/wifi_scan` | 扫描附近热点 |
| `/remember <key> <value>` | 保存事实到长期记忆 |
| `/forget <key>` | 从长期记忆中删除事实 |
| `/memories` | 列出所有长期记忆 |
| `/task` | 列出定时任务（或 `/task rm <名称>` 删除） |
| `/skill` | 列出或执行技能 |
| `/nodes` | 查看蜂群节点表 |
| `/log [on\|off]` | 开关日志输出 |
| `/history` | 查看对话消息数量 |
| `/clear` | 清除对话记忆 |
| `/help` | 列出所有命令 |

### ESP32-C3 (ESP-IDF + QEMU)

> **没有硬件？没关系。** 打开
> [cnb.cool/gevico.online/rtclaw/rt-claw](https://cnb.cool/gevico.online/rtclaw/rt-claw)
> 即可启动 CNB 云原生开发环境，所有工具链已预装。
> 在浏览器中即可编译并在 QEMU 上运行 RT-Claw。

**1. 安装系统依赖**

```bash
# Ubuntu / Debian
sudo apt install git wget flex bison gperf python3 python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 libgcrypt20-dev libglib2.0-dev libpixman-1-dev \
    libsdl2-dev libslirp-dev meson

# Arch Linux
sudo pacman -S --needed libgcrypt glib2 pixman sdl2 libslirp \
    python cmake ninja gcc git wget flex bison meson
```

**2. 安装 ESP-IDF + QEMU**

```bash
# 一键安装（克隆 ESP-IDF v5.4，安装工具链 + QEMU）
./scripts/setup-esp-env.sh
```

**3. 选择配置预设**

| 预设 | 文件 | 终端 | 飞书 | 说明 |
|------|------|------|------|------|
| **快速体验** | `sdkconfig.defaults.demo` | 开 | 关 | 交互式终端 + 完整 AI Agent |
| **飞书机器人** | `sdkconfig.defaults.feishu` | 关 | 开 | 无终端 IM 机器人，节省内存 |
| **默认** | `sdkconfig.defaults` | 关 | 关 | 最小基础配置，按需定制 |

```bash
source $HOME/esp/esp-idf/export.sh

# 选择一个预设：
cp platform/esp32c3/boards/qemu/sdkconfig.defaults.demo \
   platform/esp32c3/boards/qemu/sdkconfig.defaults        # 快速体验
# cp platform/esp32c3/boards/qemu/sdkconfig.defaults.feishu \
#    platform/esp32c3/boards/qemu/sdkconfig.defaults       # 飞书机器人

idf.py -C platform/esp32c3 -B build/esp32c3-qemu/idf -DRTCLAW_BOARD=qemu set-target esp32c3
```

所有预设均包含：AI 引擎、Tool Use、蜂群心跳、调度器、LCD、技能系统、上电 AI 连接测试。

**4. 配置 API 密钥**

方式 A — 环境变量（推荐）：

```bash
export RTCLAW_AI_API_KEY='<你的 API 密钥>'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'
```

方式 B — Meson 选项：

```bash
meson configure build/esp32c3-qemu/meson -Dai_api_key='<你的 API 密钥>'
```

方式 C — 运行时通过 Shell 命令（写入 NVS，重启保持）：

```
/ai_set key <你的 API 密钥>
/ai_set url https://api.anthropic.com/v1/messages
/ai_set model claude-sonnet-4-6
```

**5.（可选）配置飞书机器人**

方式 A — 环境变量：

```bash
export RTCLAW_FEISHU_APP_ID='<你的 App ID>'
export RTCLAW_FEISHU_APP_SECRET='<你的 App Secret>'
```

方式 B — ESP-IDF menuconfig：

```bash
idf.py -C platform/esp32c3 -B build/esp32c3-qemu/idf -DRTCLAW_BOARD=qemu menuconfig
# 路径：Component config → rt-claw Configuration → Feishu (Lark) Integration
```

在[飞书开放平台](https://open.feishu.cn)创建应用，开启**事件订阅 → 长连接**模式，
订阅 `im.message.receive_v1` 事件。设备启动后自动建立 WebSocket 长连接，
无需公网 IP。

**6. 构建与运行**

```bash
# 统一构建（推荐）
make build-esp32c3-qemu

# 在 QEMU 上运行
make run-esp32c3-qemu

# 或烧录到真实硬件（未测试）
idf.py -C platform/esp32c3 -p /dev/ttyUSB0 flash monitor
```

### QEMU vexpress-a9 (RT-Thread)

```bash
# 依赖：arm-none-eabi-gcc, qemu-system-arm, scons, meson, ninja

# 通过环境变量配置（构建时读取）
export RTCLAW_AI_API_KEY='<your-key>'

# 统一构建
make vexpress-a9-qemu

# 启动 API 代理（RT-Thread 无 TLS，代理转发 HTTP→HTTPS）
python3 scripts/api-proxy.py https://api.anthropic.com &

# 运行
make run-vexpress-a9-qemu
```

## 项目结构

```
rt-claw/
├── meson.build                  # Meson 构建定义（交叉编译 claw + osal）
├── meson_options.txt            # Meson 构建选项（OSAL 后端、功能开关、AI 配置）
├── Makefile                     # 统一构建入口（make build-esp32c3-qemu / make vexpress-a9-qemu）
├── include/                     # 统一公共头文件（与 claw/、osal/ 目录对齐）
│   ├── claw/                   #   claw/ 对应的公共头文件
│   │   ├── claw_config.h       #     项目配置
│   │   ├── claw_init.h         #     启动入口 API
│   │   ├── core/               #     网关、调度器、服务接口
│   │   ├── services/           #     AI、网络、蜂群、IM 服务头文件
│   │   ├── shell/              #     公共 shell 命令头文件
│   │   └── tools/              #     Tool Use 框架头文件
│   └── osal/                   #   osal/ 对应的公共头文件
│       ├── claw_os.h           #     OSAL API
│       └── claw_net.h          #     网络抽象层
├── osal/                        # 操作系统抽象层
│   ├── freertos/                #   FreeRTOS 实现
│   └── rtthread/                #   RT-Thread 实现
├── claw/                        # 平台无关核心代码
│   ├── claw_init.c             #   启动入口
│   ├── core/                   #   网关、调度器、心跳
│   ├── services/ai/            #   LLM 对话引擎、记忆、技能
│   ├── services/net/           #   网络服务
│   ├── services/swarm/         #   蜂群智能
│   ├── services/im/            #   IM 集成（飞书）
│   ├── shell/                  #   UART 交互终端
│   └── tools/                  #   Tool Use 框架（GPIO、系统、LCD、音频、调度器、HTTP）
├── drivers/                     # 硬件驱动（Linux 内核风格）
│   ├── audio/espressif/        #   ES8311 音频编解码器
│   ├── display/espressif/      #   SSD1306 OLED 显示
│   ├── net/espressif/          #   共享 WiFi 驱动（C3/S3）
│   └── serial/espressif/       #   串行控制台
├── platform/
│   ├── common/espressif/       # 共享 Espressif 板辅助（WiFi 初始化）
│   ├── esp32c3/                # ESP32-C3 统一平台（boards/qemu/devkit/xiaozhi-xmini/）
│   ├── esp32s3/                # ESP32-S3 统一平台（boards/qemu/default/）
│   └── vexpress-a9/            # RT-Thread BSP（Meson + SCons）
├── vendor/
│   ├── lib/cjson/              # cJSON 库
│   └── os/
│       ├── freertos/           # FreeRTOS-Kernel（子模块）
│       └── rt-thread/          # RT-Thread（子模块）
├── docs/
│   ├── en/                     # 英文文档
│   └── zh/                     # 中文文档
└── scripts/
    ├── api-proxy.py            # HTTP→HTTPS 代理（RT-Thread QEMU 无 TLS）
    ├── setup-esp-env.sh        # 安装 ESP-IDF + QEMU
    ├── gen-esp32c3-cross.py    # 从 ESP-IDF 自动生成 Meson 交叉编译文件
    └── ...
```

## 社区

加入 GTOC（格维开源社区）交流频道：

- **Discord**：[RT-Claw](https://discord.gg/BZ9nFVzX)
- **QQ 群**：[加入](https://qm.qq.com/q/heSPPC9De8)
- **Telegram**：[GTOC 频道](https://t.me/gevico_channel)
- **Bilibili**：[绝对是泽文啦](https://space.bilibili.com/483048140)
- **微信**：GTOC 微信公众号

## 文档

- [快速开始](docs/zh/getting-started.md) — 编译、烧录、运行（全平台）
- [使用指南](docs/zh/usage.md) — Shell 命令、Tool Use、技能、记忆
- [架构设计](docs/zh/architecture.md) — OSAL、服务、驱动、构建流程
- [移植与扩展](docs/zh/porting.md) — 新平台、工具、技能、驱动
- [裁剪与优化](docs/zh/tuning.md) — 模块裁剪、内存优化、配置参数
- [编码风格](docs/zh/coding-style.md)
- [贡献指南](docs/zh/contributing.md)

## 许可证

MIT
