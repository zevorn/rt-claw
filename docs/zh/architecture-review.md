# 架构审查与优化计划

[English](../en/architecture-review.md) | **中文**

日期：2026-03-13
范围：全代码库审查（OSAL、Core、Services、Tools、Platform）

## 概要

rt-claw 架构设计方向正确，OSAL 抽象模式清晰。审查发现 10 个问题，
分为三个优先级：2 个 Bug（P0）、2 个代码规范问题（P1）、
6 个设计/架构改进（P2）。本文档描述每个问题的影响和修复方案。

## 问题清单

### P0 — Bug（立即修复）

#### P0-1：FreeRTOS Timer 回调类型不安全

**文件：** `osal/freertos/claw_os_freertos.c:190-194`

**问题：** FreeRTOS timer 回调签名是 `void cb(TimerHandle_t xTimer)`，
但 OSAL 声明的是 `void (*callback)(void *arg)`。当前代码强制类型转换：

```c
TimerHandle_t t = xTimerCreate(name, pdMS_TO_TICKS(period_ms),
                                repeat ? pdTRUE : pdFALSE,
                                arg,
                                (TimerCallbackFunction_t)callback);
```

用户的 `arg` 存在 `pvTimerID` 中，但回调收到的是 `TimerHandle_t` 而不是
`arg`。当前所有调用者都忽略了参数（`(void)arg`），侥幸未崩溃。但未来任何
使用 `arg` 的 timer 回调都会读到错误数据。

**修复：** 添加 trampoline 结构体，通过 `pvTimerGetTimerID()` 取回真正的
用户回调和 arg。

#### P0-2：RT-Thread MQ Send 冗余分支

**文件：** `osal/rtthread/claw_os_rtthread.c:128-131`

**问题：** if/else 两个分支代码完全相同，属于死代码。

**修复：** 删除冗余分支，保留单行调用。

---

### P1 — 代码规范（尽快修复）

#### P1-1：Header Guard 命名不统一

**规范（coding-style.md）：** `CLAW_<PATH>_<NAME>_H`

**违规：** 多个头文件使用 `__CLAW_*_H__`（双下划线前缀在 C 标准中为保留
命名空间）。

| 文件 | 当前 | 应改为 |
|------|------|--------|
| `include/claw_os.h` | `__CLAW_OS_H__` | `CLAW_OS_H` |
| `include/claw_init.h` | `__CLAW_INIT_H__` | `CLAW_INIT_H` |
| `include/core/gateway.h` | `__CLAW_GATEWAY_H__` | `CLAW_CORE_GATEWAY_H` |
| `include/services/net/net_service.h` | `__CLAW_NET_SERVICE_H__` | `CLAW_SERVICES_NET_SERVICE_H` |
| `include/services/swarm/swarm.h` | `__CLAW_SWARM_H__` | `CLAW_SERVICES_SWARM_H` |
| `include/services/ai/ai_engine.h` | `__CLAW_AI_ENGINE_H__` | `CLAW_SERVICES_AI_ENGINE_H` |
| `include/tools/claw_tools.h` | `__CLAW_TOOLS_H__` | `CLAW_TOOLS_H` |

**修复：** 统一重命名所有 header guard。

#### P1-2：日志默认关闭

**问题：** 两个 OSAL 实现中 `s_log_enabled` 默认值为 0。
`claw_log_raw()` 绕过此检查（启动 banner 正常），但所有
`CLAW_LOGI`/`CLAW_LOGW`/`CLAW_LOGE` 在日志启用前静默丢弃。

**修复：** 将 `s_log_enabled` 默认值改为 1。

---

### P2 — 架构改进（规划讨论）

#### P2-1：OSAL 缺少网络抽象

**问题：** `claw_os.h` 覆盖了线程/同步/内存/日志/时间，但没有网络 API。
`claw/` 中所有网络相关代码都使用 `#ifdef` 平台分支：

- `ai_engine.c` — ~350 行平台特定 HTTP 传输
- `net_service.c` — ~240 行，各平台完全不同
- `swarm.c` — 平台特定的节点 ID 生成和 socket 引用

**建议：** 在 OSAL 中增加最小 HTTP 客户端 API，将传输逻辑下沉到
`osal/` 目录。

#### P2-2：Gateway 是空壳路由器 ✅

**问题：** `gateway.c` 收到消息只打日志，不做路由。无 handler 注册、
无分发表、无订阅模式。服务之间直接调用，绕过 Gateway。

**已解决：** 采用方案 2 — Gateway 改为轻量事件总线，提供
`gateway_subscribe(type, handler, arg)` 注册接口和按类型分发。
移除未使用的 `src_channel` / `dst_channel` 字段。
`CLAW_GW_MAX_HANDLERS` 移至 `claw_config.h` 支持平台级调整。
静态 handler 表（无堆分配），适合资源受限设备。

#### P2-3：无统一服务接口

**问题：** 服务生命周期模式不一致。部分只有 `init()`，部分有
`init()` + `start()`，均无 `stop()` 或 `deinit()`。

**建议：** 定义 `struct claw_service { name, init, start, stop }`
接口。

#### P2-4：启动时阻塞式 AI 调用

**问题：** `claw_init()` 同步调用 `ai_chat_raw()`，最坏阻塞 ~18 秒。

**修复：** 将 AI 连通性测试移到独立线程异步执行。

#### P2-5：Gateway 消息固定 256 字节 Payload

**问题：** 每条消息 ~268 字节，16 条队列 = ~4.3KB。当前可接受。

**建议：** 暂不修改，内存压力增大时再优化。

#### P2-6：Tool 注册表线程安全

**问题：** `s_tools[]` 无互斥保护。当前安全（注册在 init 时完成），
但运行时读取发生在多线程中。

**建议：** 当前风险低，动态注册时再加锁。

## 实施顺序

| 阶段 | 问题 | 范围 |
|------|------|------|
| 第一阶段 | P0-1, P0-2, P1-1, P1-2 | Bug 修复 + 代码规范 |
| 第二阶段 | P2-4, P2-2 | 启动优化 + Gateway 重设计 |
| 第三阶段 | P2-1, P2-3 | OSAL 网络抽象 + 服务接口 |
