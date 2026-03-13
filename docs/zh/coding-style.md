# rt-claw 编码风格

[English](../en/coding-style.md) | **中文**

本文定义 rt-claw 项目的 C 编码风格。
适用于 `claw/`、`osal/` 和 `include/` 下的所有代码——即平台无关的核心代码和操作系统抽象层。

提交前请使用 `scripts/check-patch.sh` 验证代码风格。

## 检查范围

| 目录        | 是否检查 | 说明                              |
|-------------|----------|-----------------------------------|
| `claw/`     | 是       | 核心服务、网关、AI 引擎           |
| `include/`  | 是       | 公共头文件                        |
| `osal/`     | 是       | 操作系统抽象层                    |
| `platform/` | 否       | 平台特定 BSP / 构建文件           |
| `vendor/`   | 否       | 第三方 RTOS 源码                  |
| `scripts/`  | 否       | 构建和启动脚本                    |

## 空白与缩进

- 使用 **4 个空格** 缩进，C 源码中**禁止使用 Tab**。
- 不要在行尾留下空白字符。
- 仓库包含 `.editorconfig` 文件，大多数编辑器原生支持或可通过插件使用。

## 行宽

- 目标：**80 字符**。
- 如果 80 字符换行导致可读性下降，可放宽到 ~100，但不要更长。
- `checkpatch.pl` 在 100 字符处会发出警告。

## 命名

### 变量

`lower_case_with_underscores` — 方便输入和阅读。

### 函数

- 公共 API 使用 `claw_` 前缀：`claw_os_thread_create()`、`claw_gateway_send()`。
- OSAL 函数使用 `claw_os_` 前缀：`claw_os_mutex_lock()`。
- 服务内部函数使用子系统前缀：`gateway_route_msg()`、`swarm_elect_leader()`。
- 静态（文件作用域）函数无需前缀，但必须具有描述性名称。
- 调用者需持有锁时，函数使用 `_locked` 后缀。

### 类型

- 结构体 / 枚举类型名使用 `CamelCase`：`GatewayMsg`、`SwarmNodeState`。
- 标量 typedef 使用 `lower_case_t`：`claw_err_t`、`node_id_t`。
- 枚举值使用 `UPPER_CASE`：`CLAW_OK`、`CLAW_ERR_TIMEOUT`。

## 块结构

即使单语句也必须加大括号：

```c
if (a == 5) {
    do_something();
} else {
    do_other();
}
```

左大括号与控制语句在同一行。
例外：函数定义的左大括号单独占一行：

```c
void claw_gateway_init(void)
{
    /* ... */
}
```

## 声明

- 声明放在块的顶部，不要与语句混合。
- 循环变量可在 `for` 中声明：

```c
for (int i = 0; i < count; i++) {
    /* ... */
}
```

## 条件语句

常量放在右侧：

```c
if (ret == CLAW_OK) {    /* 好 — 读起来自然 */
    ...
}
```

## 注释风格

使用传统 C 注释 `/* */`，避免使用 `//`。

多行注释：

```c
/*
 * Multi-line comment
 * uses stars on the left.
 */
```

## 头文件包含顺序

```c
#include "osal/claw_os.h"  /* OSAL 头文件优先（claw/ 中的源文件） */
#include <stdint.h>       /* 然后系统 / 标准头文件 */
#include <string.h>
#include "claw/core/gateway.h" /* 最后项目头文件 */
```

OSAL 实现文件（`osal/freertos/`、`osal/rtthread/`）中，
RTOS 头文件在 OSAL 头文件之前：

```c
#include <rtthread.h>     /* RTOS header */
#include "osal/claw_os.h"  /* OSAL interface */
```

## C 标准

使用 **C99**（gnu99）。避免使用 GCC 和 Clang 都不支持的编译器扩展。

## 类型

- 使用 `<stdint.h>` 固定宽度类型：`uint8_t`、`int32_t` 等。
- 内存大小用 `size_t`，有符号大小用 `ssize_t`。
- 布尔值用 `<stdbool.h>` 中的 `bool`。
- 在有更合适类型时，避免使用裸 `int` / `long`。
- 确保指针的 `const` 正确性。

## 内存管理

通过 OSAL 使用 RTOS 提供的内存分配，或标准 `malloc`/`free`。
`claw/` 代码中禁止直接调用 RTOS 特定的分配器。

## 字符串安全

- 禁止使用 `strncpy`（不保证 null 终止）。
- 用 `snprintf` 代替 `sprintf`。
- 若可用则使用 `strlcpy` / `strlcat`，否则显式进行有界拷贝。

## 错误处理

- 可失败操作返回 `claw_err_t`。
- 检查返回值，不要静默忽略错误。
- 成功为 `CLAW_OK`（0），错误为负值。

## 预处理器

- 头文件保护宏使用 `CLAW_<PATH>_<NAME>_H` 格式：

```c
#ifndef CLAW_CORE_GATEWAY_H
#define CLAW_CORE_GATEWAY_H
/* ... */
#endif /* CLAW_CORE_GATEWAY_H */
```

- 可变参数宏：

```c
#define CLAW_LOG(fmt, ...) \
    claw_os_printf("[CLAW] " fmt, ##__VA_ARGS__)
```

## 运行检查工具

```bash
# 检查 claw/、osal/ 和 include/ 下的所有文件
scripts/check-patch.sh

# 检查指定文件
scripts/check-patch.sh --file claw/core/gateway.c include/osal/claw_os.h

# 仅检查暂存区变更
scripts/check-patch.sh --staged

# 检查 main 之后的提交
scripts/check-patch.sh --branch main
```
