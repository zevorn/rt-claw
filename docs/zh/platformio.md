# PlatformIO 构建指南

rt-claw 支持 PlatformIO 作为 ESP32-C3 和 ESP32-S3 目标的替代构建系统。
PlatformIO 自动管理 ESP-IDF 工具链，无需手动安装。

## 前置条件

- [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/installation.html)
- [Meson](https://mesonbuild.com/) 和 [Ninja](https://ninja-build.org/)

```bash
pip install platformio meson ninja
```

## 构建

从 platform 目录（`platformio.ini` 所在位置）运行：

```bash
# ESP32-C3
cd platform/esp32c3
pio run -e esp32c3_devkit          # 通用 4MB 开发板
pio run -e esp32c3_xiaozhi_xmini   # xiaozhi-xmini 16MB 板
pio run -e esp32c3_qemu            # QEMU 虚拟板

# ESP32-S3
cd platform/esp32s3
pio run -e esp32s3_default         # 实体硬件（16MB + PSRAM）
pio run -e esp32s3_qemu            # QEMU 虚拟板
```

构建过程使用两个额外脚本：

1. **预构建**（`pio_pre_build.py`）：检查工具（meson、ninja）并设置
   CMake 变量
2. **后配置**（`pio_post_configure.py`）：在 ESP-IDF cmake 配置完成后
   运行，调用 Meson 桥接助手从 PlatformIO 的 `compile_commands.json`
   生成交叉编译文件，验证编译器一致性，检测过期 Meson 构建，并运行
   `meson compile`

## 烧录

```bash
# ESP32-C3
cd platform/esp32c3
pio run -e esp32c3_devkit -t upload
pio run -e esp32c3_xiaozhi_xmini -t upload

# ESP32-S3
cd platform/esp32s3
pio run -e esp32s3_default -t upload
```

## 串口监控

```bash
# ESP32-C3
cd platform/esp32c3
pio run -e esp32c3_devkit -t monitor

# ESP32-S3
cd platform/esp32s3
pio run -e esp32s3_default -t monitor
```

## 环境变量

API 密钥和凭证通过环境变量配置（与 Makefile 构建相同）：

```bash
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://...'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'
```

## 与 Makefile 共存

PlatformIO 与现有的 Makefile/Meson/CMake 构建共存。两套系统共享相同的
源代码和 `build/<chip>-<board>/` 下的构建输出目录。PlatformIO 文件不会
影响 Makefile 构建。

## 平台版本

PlatformIO 锁定为 `espressif32@6.10.0`（ESP-IDF v5.4.0），以匹配
项目的工具链要求。
