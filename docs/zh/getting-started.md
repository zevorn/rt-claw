# 快速入门

[English](../en/getting-started.md) | **中文**

本指南涵盖在所有支持平台上构建、烧录和运行 rt-claw 的完整流程，
包括 ESP32-C3、ESP32-S3 和 vexpress-a9（RT-Thread）。

## 前置条件

### 系统依赖

**Ubuntu / Debian：**

```bash
sudo apt-get install -y git wget flex bison gperf python3 python3-pip \
    python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 gcc-arm-none-eabi qemu-system-arm scons meson \
    libgcrypt20-dev libglib2.0-dev libpixman-1-dev libsdl2-dev libslirp-dev
```

**Arch Linux：**

```bash
sudo pacman -S --needed git wget flex bison python python-pip cmake ninja \
    ccache dfu-util libusb arm-none-eabi-gcc arm-none-eabi-newlib \
    qemu-system-arm scons meson \
    libgcrypt glib2 pixman sdl2 libslirp
```

### ESP-IDF + QEMU

所有 ESP32-C3 和 ESP32-S3 目标都需要此环境。安装脚本会自动安装
ESP-IDF v5.4、工具链以及 Espressif QEMU（riscv32 + xtensa）：

```bash
./scripts/setup-esp-env.sh
source $HOME/esp/esp-idf/export.sh    # 每次构建前都需要执行
```

### 仅 vexpress-a9

上述系统依赖中的 ARM QEMU 和 SCons 包即可满足需求，无需安装 ESP-IDF。

## 配置

### AI 凭证

通过环境变量（推荐）或 Meson 选项进行设置。

| 变量 | 用途 | 示例 |
|------|------|------|
| `RTCLAW_AI_API_KEY` | API 密钥 | `sk-ant-...` |
| `RTCLAW_AI_API_URL` | API 端点 | `https://api.anthropic.com/v1/messages` |
| `RTCLAW_AI_MODEL` | 模型名称 | `claude-sonnet-4-6` |

```bash
# 环境变量（写入 shell 配置文件或在构建前导出）
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

# 或直接传递给 Meson（优先级最高）
meson setup build/... --cross-file ... -Dai_api_key='sk-...' -Dai_api_url='...'
```

**优先级：** Meson 选项 > 环境变量 > `claw_config.h` 默认值。

### 飞书（Lark）即时通讯

使用飞书机器人网关的步骤：

1. 在 [open.feishu.cn](https://open.feishu.cn) 创建应用
2. 启用**事件订阅**并选择长连接模式
3. 订阅 `im.message.receive_v1` 事件
4. 导出凭证：

```bash
export RTCLAW_FEISHU_APP_ID='cli_...'
export RTCLAW_FEISHU_APP_SECRET='...'
```

或使用 Meson 选项：`-Dfeishu=true -Dfeishu_app_id=... -Dfeishu_app_secret=...`

### Telegram Bot

使用 Telegram 机器人集成的步骤：

1. 在 Telegram 中通过 [@BotFather](https://t.me/BotFather) 创建机器人
2. 复制 Bot Token（格式如 `123456:ABC-DEF...`）
3. 导出凭证：

```bash
export RTCLAW_TELEGRAM_BOT_TOKEN='123456:ABC-DEF...'
```

或使用 Meson 选项：`-Dtelegram=true -Dtelegram_bot_token='...'`

QEMU 测试时（无 TLS），需在单独端口启动 Telegram API 代理：

```bash
python3 scripts/api-proxy.py https://api.telegram.org 8889
```

并将 API URL 指向代理：

```bash
meson configure build/<platform>/meson \
    -Dtelegram=true \
    -Dtelegram_bot_token='...' \
    -Dtelegram_api_url='http://10.0.2.2:8889'
```

**真实硬件**（ESP32-C3/S3，带 WiFi）支持原生 TLS，无需代理：

```bash
export RTCLAW_TELEGRAM_BOT_TOKEN='123456:ABC-DEF...'

# ESP32-C3 xiaozhi-xmini 示例
make build-esp32c3-xiaozhi-xmini
make flash-esp32c3-xiaozhi-xmini
make run-esp32c3-xiaozhi-xmini    # 串口监视器 — 观察 [telegram] 日志

# ESP32-S3 示例
make esp32s3
make flash-esp32s3
make monitor-esp32s3
```

烧录后，通过 `/wifi_set <SSID> <PASS>` 配置 WiFi，然后重启。
设备连接 WiFi 后会自动开始轮询 Telegram，即可接收来自机器人的消息。

### WiFi（仅限硬件开发板）

构建前通过 `idf.py menuconfig` 配置，或在运行时设置：

```
/wifi_set <SSID> <PASSWORD>
```

## ESP32-C3

### QEMU（无需硬件）

QEMU 开发板附带三种配置预设：

| 预设 | 文件 | Shell | 飞书 | 使用场景 |
|------|------|-------|------|----------|
| 快速演示 | `sdkconfig.defaults.demo` | 开启 | 关闭 | 交互式 Shell + 完整 AI 代理 |
| 飞书机器人 | `sdkconfig.defaults.feishu` | 关闭 | 开启 | 无头 IM 机器人 |
| 默认 | `sdkconfig.defaults` | — | — | 最小化 / 自定义 |

切换预设时，在构建前复制所需的文件：

```bash
source $HOME/esp/esp-idf/export.sh

# 选择预设（可选 — 跳过则使用当前默认配置）
cp platform/esp32c3/boards/qemu/sdkconfig.defaults.demo \
   platform/esp32c3/boards/qemu/sdkconfig.defaults

# 构建并运行
make build-esp32c3-qemu
make run-esp32c3-qemu               # 仅串口
make run-esp32c3-qemu GRAPHICS=1    # 带 LCD 显示窗口
make run-esp32c3-qemu GDB=1         # 调试模式（GDB 端口 1234）
```

### devkit（4 MB，真实硬件）

通用 ESP32-C3 开发板，支持 WiFi。

```bash
source $HOME/esp/esp-idf/export.sh
make build-esp32c3-devkit
make flash-esp32c3-devkit
make run-esp32c3-devkit              # 串口监视器
```

### xiaozhi-xmini（16 MB，真实硬件）

小智 Mini 开发板，配备 16 MB Flash、WiFi 和 ES8311 音频编解码器。

```bash
source $HOME/esp/esp-idf/export.sh
make build-esp32c3-xiaozhi-xmini
make flash-esp32c3-xiaozhi-xmini
make run-esp32c3-xiaozhi-xmini      # 串口监视器
```

## ESP32-S3

### QEMU

```bash
source $HOME/esp/esp-idf/export.sh
make build-esp32s3-qemu
make run-esp32s3-qemu
make run-esp32s3-qemu GRAPHICS=1    # 带 LCD 显示窗口
make run-esp32s3-qemu GDB=1         # 调试模式（GDB 端口 1234）
```

### 真实硬件（16 MB Flash + 8 MB PSRAM）

```bash
source $HOME/esp/esp-idf/export.sh
make esp32s3                         # build-esp32s3-default 的别名
make flash-esp32s3
make monitor-esp32s3                 # 串口监视器
```

WiFi 配置：构建前使用 `idf.py menuconfig`，或在运行时使用 `/wifi_set`。

## QEMU vexpress-a9（RT-Thread）

无需 ESP-IDF，使用 Meson + SCons 构建。

```bash
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

make vexpress-a9-qemu

# RT-Thread 不支持 TLS — 启动 HTTP 转 HTTPS 代理
python3 scripts/api-proxy.py https://api.anthropic.com &

make run-vexpress-a9-qemu
make run-vexpress-a9-qemu GDB=1     # 调试模式（GDB 端口 1234）
```

> **注意：** vexpress-a9 的 RT-Thread 移植不支持 TLS。项目自带的
> `scripts/api-proxy.py` 负责将 HTTP（QEMU 侧）桥接到 HTTPS（上游 API）。
> 固件发起 AI 请求前必须先启动该代理。

## 使用 GDB 调试

所有 QEMU 目标都支持 `GDB=1` 参数，该参数会在启动时暂停 CPU 并在
端口 1234 上开启 GDB 服务器。

**ESP32-C3 示例：**

```bash
# 终端 1 — 以调试模式启动 QEMU
make run-esp32c3-qemu GDB=1

# 终端 2 — 连接 GDB
riscv32-esp-elf-gdb build/esp32c3-qemu/idf/rt-claw.elf \
    -ex 'target remote :1234'
```

**ESP32-S3 示例：**

```bash
# 终端 1
make run-esp32s3-qemu GDB=1

# 终端 2
xtensa-esp32s3-elf-gdb build/esp32s3-qemu/idf/rt-claw.elf \
    -ex 'target remote :1234'
```

**vexpress-a9 示例：**

```bash
# 终端 1
make run-vexpress-a9-qemu GDB=1

# 终端 2
arm-none-eabi-gdb build/vexpress-a9-qemu/rtthread.elf \
    -ex 'target remote :1234'
```

## CNB 云端 IDE

如果希望在浏览器中开发而无需本地环境配置，可在 CNB 上打开本项目：

<https://cnb.cool/gevico.online/rtclaw/rt-claw>

云端环境已预装 ESP-IDF 和 QEMU，开箱即用。
