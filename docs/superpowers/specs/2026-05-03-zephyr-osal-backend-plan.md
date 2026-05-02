# Zephyr OSAL 后端实现计划

## 目标描述

为 rt-claw 添加 Zephyr RTOS 作为第四个 OSAL 后端，使其成为第一优先级支持的 RTOS。实现 `osal/zephyr/`（OS 原语、网络、KV 存储）、`platform/zephyr/`（Zephyr Module 封装层，首个板级 `qemu_cortex_a9`）、构建基础设施（Meson 交叉编译文件生成、Makefile 目标），以及端到端验证（包括原生 HTTPS 和 QEMU 上的 KV 持久化存储）。

Zephyr v4.4.0 作为 git submodule 固定在 `vendor/os/zephyr/`。mbedTLS 单独 vendor。第一阶段范围：shell + AI 对话 + 网络 + KV。Feishu、Telegram、Swarm、OTA 禁用。

## 验收标准

- AC-1: OSAL Zephyr 后端编译和链接
  - 正向测试（预期通过）:
    - `meson setup` 使用 `-Dosal=zephyr` 在 `-Werror` 下零警告成功
    - `meson compile` 产出 `libclaw.a`，包含 `claw_os_zephyr.o`、`claw_net_zephyr.o`、`claw_kv_zephyr.o`
    - `claw_os.h`、`claw_net.h`、`claw_kv.h` 中所有 API 函数在 `osal/zephyr/` 中有实现
  - 反向测试（预期失败）:
    - 未提供 Zephyr 交叉编译文件时使用 `-Dosal=zephyr` 构建失败并给出明确错误
    - 引用未实现的 OSAL 函数导致链接错误
  - AC-1.1: 现有后端不受影响
    - 正向: `make build-linux`、`make vexpress-a9-qemu`、`make build-zynq-a9-qemu` 仍然成功
    - 反向: 删除 `osal/zephyr/` 目录不影响其他后端

- AC-2: Zephyr 固件端到端构建
  - 正向测试（预期通过）:
    - `make build-zephyr-cortex-a9-qemu` 从全新 checkout 产出 `zephyr.elf`
    - `scripts/check-patch.sh --staged` 对所有新增/修改文件通过
    - `build/zephyr-cortex-a9-qemu/` 中包含 `zephyr.elf` 和 `flash.bin`
  - 反向测试（预期失败）:
    - 未安装 Zephyr SDK 时构建失败并给出诊断错误信息
    - 在 `osal/zephyr/` 中引入 `-Werror` 警告导致构建失败

- AC-3: QEMU 启动并可进行 shell 交互
  - 正向测试（预期通过）:
    - `make run-zephyr-cortex-a9-qemu` 在 10 秒内显示 rt-claw banner
    - `/help` 命令列出可用命令
    - `/version` 显示固件版本
    - 非命令文本转发到 AI 引擎（无 API key 时显示错误）
  - 反向测试（预期失败）:
    - 空输入不产生崩溃或异常输出
    - 无效命令（如 `/nonexistent`）显示"未知命令"消息

- AC-4: 服务框架正常运行
  - 正向测试（预期通过）:
    - `claw_init()` 完成：driver probe、拓扑排序、service start —— 无崩溃
    - 至少收集到一个 driver、一个 service、一个 tool（来自 linker section）
    - Gateway 服务在服务之间路由消息
  - 反向测试（预期失败）:
    - 缺少依赖的服务触发 `CLAW_ERR_DEPEND`（非崩溃）

- AC-5: 网络连通
  - 正向测试（预期通过）:
    - DHCP 在 10.0.2.x 范围获取 IP 地址（SLIRP NAT）
    - `claw_net_get()` 对已知 HTTP 端点返回 HTTP 200
    - `claw_net_post()` 对 HTTPS 端点完成 TLS 握手并返回有效响应
  - 反向测试（预期失败）:
    - `claw_net_post()` 对无效主机名返回 `CLAW_ERR_IO`（非崩溃）
    - `claw_net_get()` 响应超过缓冲区时返回截断数据并保证 NUL 终止
    - 通过 HTTPS URL 连接非 TLS 端口时优雅失败

- AC-6: KV 存储跨 QEMU 重启持久化
  - 正向测试（预期通过）:
    - `claw_kv_set_str("test", "key", "value")` 后 `claw_kv_get_str("test", "key", ...)` 返回 "value"
    - `claw_kv_set_blob()` / `claw_kv_get_blob()` 二进制数据往返正确
    - QEMU 重启后，之前写入的 KV 数据仍可从 pflash 读取
    - `claw_kv_delete("test", "key")` 删除条目；后续 get 返回 `CLAW_ERR_NOENT`
  - 反向测试（预期失败）:
    - `claw_kv_get_str()` 获取不存在的 key 返回 `CLAW_ERR_NOENT`（非崩溃）
    - `claw_kv_erase_ns()` 返回 `CLAW_ERR_NOENT`（不支持，非崩溃）
    - 超过 64 字符的 key 被优雅处理

- AC-7: 端到端 AI 对话（需 secret）
  - 正向测试（预期通过）:
    - 设置 `RTCLAW_AI_API_KEY` 时：在 shell 输入问题，收到 AI 回复
    - AI 回复在 shell 输出中显示
  - 反向测试（预期失败）:
    - 无 API key 时：AI 请求返回错误消息（非崩溃）
  - 注：此 AC 仅在 API 凭据可用时验证（CI 使用 secret 门控测试）

- AC-8: CI 冒烟测试通过
  - 正向测试（预期通过）:
    - `make test-smoke-zephyr-cortex-a9` 启动 QEMU，检测 banner，验证 shell 响应，正常退出
  - 反向测试（预期失败）:
    - 超时内未检测到 banner 则冒烟测试失败

## 路径边界

### 上界（最大可接受范围）

实现包含全部三个 OSAL 文件（完整 API 覆盖）、完整功能的平台层（Zephyr Shell 集成：斜杠命令、AI fallback、claw_printf 捕获）、原生 HTTPS（内嵌 CA 证书）、Settings+NVS 持久化 KV（pflash）、带 GDB 支持的 Makefile 目标、CI 冒烟测试。Shell 通过 Zephyr Shell 子系统支持基本行编辑、历史记录和 Tab 补全。

### 下界（最小可接受范围）

实现包含全部三个 OSAL 文件、基本 shell 的平台层（命令分发工作，高级编辑委托给 Zephyr Shell）、HTTP 网络（如 mbedTLS 集成受阻可退回代理）、KV 存储（跨重启持久化已验证）、构建/运行 Makefile 目标。CI 冒烟测试仅覆盖启动 + shell。

### 允许的选择

- 可以使用：Zephyr 内核 API、Zephyr Shell 子系统、Zephyr HTTP Client、zsock_* socket API、Zephyr 集成的 mbedTLS、Zephyr Settings 子系统、NVS flash 后端、`zephyr_linker_sources()` 自定义链接段、`k_thread_stack_alloc()`（动态，实验性）、`k_malloc`/`k_free`
- 不能使用：POSIX API shim（增加不必要的层）、构建时依赖 west（仅 CMake 路径）、Zephyr 的 STRUCT_SECTION_ITERABLE（命名与 rt-claw 的 `__start_`/`__stop_` 约定不兼容）、ESP-IDF API

## 可行性提示与建议

> **注**：本节仅供参考，非强制要求。

### 概念方法

两阶段构建，镜像 ESP-IDF 平台模式：
1. Zephyr CMake 配置阶段提取工具链信息 → `gen-zephyr-cross.py` 写入 Meson 交叉编译文件
2. Meson 交叉编译 `claw/` + `osal/zephyr/` → `libclaw.a`
3. Zephyr CMake 构建 `platform/zephyr/` 应用，使用 `--whole-archive` 链接 `libclaw.a`，通过 `zephyr_linker_sources(SECTIONS)` 代码片段注入自定义链接段
4. 输出：`zephyr.elf`（固件）+ `flash.bin`（NVS 用 pflash 镜像）

Shell 集成通过 `shell_set_bypass()` 拦截原始输入，然后内部通过 rt-claw 的 `shell_dispatch()` 分发。这保留了"输入 `/cmd` 或自然语言"的交互体验。

线程生命周期：wrapper 函数将 Zephyr 3 参数入口适配为 1 参数。两个独立标志：`exit_requested`（由 claw_thread_delete 调用者设置）和 `exited`（wrapper 返回时设置）。`claw_thread_should_exit()` 读取 `exit_requested`。wrapper 返回后，`claw_thread_delete()` 调用 `k_thread_join()` → `k_thread_stack_free()` → `k_free()`。

### 相关参考

- `osal/freertos/claw_os_freertos.c` — 参考后端（结构体嵌入、线程 wrapper 模式）
- `osal/linux/claw_os_linux.c` — 双退出标志参考（exit_flag + join）
- `osal/linux/claw_net_linux.c` — HTTP 实现参考（libcurl）
- `osal/rtthread/claw_net_rtthread.c` — 裸 socket HTTP 参考（URL 解析）
- `platform/zynq-a9/meson.build` — Meson + CMake 桥接参考，whole-archive 链接
- `platform/zynq-a9/link.lds` — 自定义链接段参考（KEEP）
- `platform/zynq-a9/shell.c` — Shell 参考（UTF-8、历史、思考动画）
- `scripts/gen-esp32c3-cross.py` — 交叉编译文件生成模式参考
- `include/claw/core/class.h` — 注册宏，链接段机制
- `include/claw/shell/shell_cmd.h` — Shell 命令分发，shell_cmd_t 类型
- `claw/init.c` — 启动序列：collect → probe → start
- `claw_config.h` — 线程优先级、队列深度、栈大小
- `meson_options.txt` — 功能开关，osal combo 选项

## 依赖与顺序

### 里程碑

1. **构建基础设施**：仓库设置和构建系统集成
   - 阶段 A: 添加 Zephyr v4.4.0 submodule + mbedTLS submodule
   - 阶段 B: Meson 集成（`osal='zephyr'`、`CLAW_PLATFORM_ZEPHYR`）
   - 阶段 C: `gen-zephyr-cross.py` 脚本
   - 阶段 D: Makefile 目标

2. **OSAL OS 原语**：核心内核抽象
   - 阶段 A: 线程、互斥锁、信号量、消息队列、定时器实现
   - 阶段 B: 内存分配、日志、时间 API
   - 阶段 C: 编译验证（libclaw.a 构建成功）

3. **平台层**：Zephyr 应用封装
   - 阶段 A: CMakeLists.txt、module.yml、Kconfig
   - 阶段 B: main.c、board.c（qemu_cortex_a9）
   - 阶段 C: 链接段代码片段 + whole-archive
   - 阶段 D: prj.conf + DTS overlay（内核、网络、flash、NVS、shell）
   - 阶段 E: 启动验证（QEMU 启动，claw_init 完成）

4. **Shell 集成**：命令系统
   - 阶段 A: shell.c，使用 Zephyr Shell bypass 模式
   - 阶段 B: claw_printf 适配器、命令分发
   - 阶段 C: 交互验证

5. **网络 + HTTPS**：带 TLS 的 HTTP 客户端
   - 阶段 A: claw_net_zephyr.c — URL 解析、socket 创建、HTTP 流程
   - 阶段 B: TLS 设置（凭据注册、SNI、证书包）
   - 阶段 C: HTTP GET/POST 验证
   - 阶段 D: HTTPS 验证

6. **KV 存储**：持久化设置
   - 阶段 A: claw_kv_zephyr.c — Settings 子系统集成
   - 阶段 B: Makefile 中 pflash 镜像创建
   - 阶段 C: 读写验证
   - 阶段 D: 跨 QEMU 重启持久化验证

7. **端到端 + CI**：集成测试
   - 阶段 A: 完整 AI 对话测试（secret 门控）
   - 阶段 B: CI 冒烟测试目标
   - 阶段 C: 现有平台回归检查

里程碑 1 阻塞所有后续里程碑。里程碑 2 和 3 紧密耦合（OSAL 需要平台层来验证）。里程碑 4 依赖 3。里程碑 5 和 6 依赖 2+3 但彼此独立。里程碑 7 依赖所有先前里程碑。

## 任务分解

| Task ID | 描述 | 目标 AC | 标签 | 依赖 |
|---------|------|---------|------|------|
| task1 | 添加 Zephyr v4.4.0 为 git submodule（`vendor/os/zephyr/`）；添加 mbedTLS submodule（`vendor/lib/mbedtls/`） | AC-2 | coding | - |
| task2 | 在 `meson_options.txt` 的 osal combo 添加 `'zephyr'`；在 `osal/meson.build` 添加对应分支；在根 `meson.build` 的平台宏体系中添加 `CLAW_PLATFORM_ZEPHYR` | AC-1 | coding | task1 |
| task3 | 审计代码库中所有 `CLAW_PLATFORM_*` 条件编译；在 `net_service.c`、`tool_system`、`class.h` 等处添加 `CLAW_PLATFORM_ZEPHYR`；在 meson.build 中将 zephyr 加入 bare-metal 检查（禁用 feishu/telegram）；第一阶段禁用 swarm/OTA | AC-1.1, AC-4 | coding | task2 |
| task4 | 实现 `osal/zephyr/claw_os_zephyr.c`：所有原语的结构体嵌入（线程使用双标志 exit_requested/exited、互斥锁、信号量、消息队列、定时器）；k_thread_stack_alloc 动态栈；优先级裁剪；LOG_MODULE 日志；k_malloc/k_free；k_uptime_get | AC-1, AC-4 | coding | task2 |
| task5 | 创建 `scripts/gen-zephyr-cross.py`：从 Zephyr CMake cache 提取工具链（编译器、标志、sysroot、Zephyr 生成的头文件路径）；输出 Meson 交叉编译文件到 `build/zephyr-cortex-a9-qemu/cross.ini` | AC-2 | coding | task1 |
| task6 | 创建 `platform/zephyr/CMakeLists.txt`（Zephyr Module 入口：find_package Zephyr、whole-archive 链接 libclaw.a、include 路径、mbedTLS 的 ZEPHYR_MODULES）；`zephyr/module.yml`；`Kconfig` 暴露 rt-claw 功能开关 | AC-2, AC-4 | coding | task4, task5 |
| task7 | 通过 `zephyr_linker_sources(SECTIONS)` 创建链接段代码片段：定义 `__start_claw_services`/`__stop_claw_services`、`__start_claw_drivers`/`__stop_claw_drivers`、`__start_claw_tools`/`__stop_claw_tools`，使用 KEEP 和指针对齐 | AC-4 | coding | task6 |
| task8 | 创建 `platform/zephyr/src/main.c`（调用 claw_init，返回）；`src/board.c`（board_early_init 分发）；`boards/qemu_cortex_a9/board.c`（网络初始化、通过 tls_credential_add 注册 TLS 证书） | AC-3, AC-4 | coding | task6 |
| task9 | 创建 `boards/qemu_cortex_a9/prj.conf`（内核：栈/堆大小含 CONFIG_HEAP_MEM_POOL_SIZE、CONFIG_DYNAMIC_THREAD、CONFIG_NUM_PREEMPT_PRIORITIES=32；网络：IPv4/TCP/DHCP/DNS；TLS：mbedTLS；flash/NVS/Settings；shell；日志）和 `app.overlay`（flash0 上 64KB storage_partition） | AC-2, AC-3 | coding | task8 |
| task10 | 添加 Makefile 目标：`build-zephyr-cortex-a9-qemu`（两阶段：cmake 配置 → gen-cross → meson → west build → pflash 镜像）、`run-zephyr-cortex-a9-qemu`（构建 + QEMU 启动，-nic user,model=lan9118 + pflash）、GDB=1 支持 | AC-2, AC-3 | coding | task5, task9 |
| task11 | 验证 QEMU 启动：claw_init 完成，banner 显示，服务框架运行（driver probe、拓扑排序、service start），无崩溃 | AC-3, AC-4 | coding | task10 |
| task12 | 实现 `platform/zephyr/src/shell.c`：Zephyr Shell 使用 shell_set_bypass 拦截原始输入；通过 shell_dispatch() 分发；claw_printf 封装 shell_fprintf；斜杠命令、非命令输入的 AI fallback、claw_printf_capture 保留；board_platform_commands 集成 | AC-3 | coding | task11 |
| task13 | 实现 `osal/zephyr/claw_net_zephyr.c`：URL 解析（scheme/host/port/path）、zsock_getaddrinfo、socket 创建（IPPROTO_TCP 或 IPPROTO_TLS_1_2）、TLS_SEC_TAG_LIST + TLS_HOSTNAME setsockopt、zsock_connect、http_client_req 响应回调、NUL 终止截断（resp_size-1）、所有路径 zsock_close、状态码提取 | AC-5 | coding | task11 |
| task14 | 将 CA 根证书（Let's Encrypt + DigiCert 根证书）嵌入为 C 数组；在 board init 中通过 tls_credential_add() 注册；验证 HTTPS 握手 | AC-5, AC-7 | coding | task13 |
| task15 | 实现 `osal/zephyr/claw_kv_zephyr.c`：settings_subsys_init、settings_save_one 写入、settings_load_subtree + 注册 handler 读取、key 格式 "ns/key"（最大 64 字符）、字符串 NUL 处理、erase_ns 返回 CLAW_ERR_NOENT | AC-6 | coding | task11 |
| task16 | 在 Makefile 中添加 pflash 镜像创建（dd 创建空白 flash.bin）；验证 KV 读写往返；验证跨 QEMU 重启持久化 | AC-6 | coding | task15 |
| task17 | 端到端 AI 对话测试：shell 输入 → AI 服务 → HTTPS POST → 响应显示（secret 门控：仅在 RTCLAW_AI_API_KEY 环境变量设置时） | AC-7 | coding | task12, task14, task16 |
| task18 | 添加 `make test-smoke-zephyr-cortex-a9` CI 目标：QEMU 启动、banner 检测、shell 响应检查、超时退出 | AC-8 | coding | task12 |
| task19 | 回归检查：验证 `make build-linux`、`make vexpress-a9-qemu`、`make build-zynq-a9-qemu` 仍然通过；`scripts/check-patch.sh --staged` 对所有变更通过 | AC-1.1, AC-2 | coding | task18 |
| task20 | 分析 Zephyr qemu_cortex_a9 板级 DTS 和 GEM 以太网驱动的网络兼容性；验证 flash0 设备用于 NVS；检查 CONFIG_DYNAMIC_THREAD 稳定性 | AC-3, AC-5, AC-6 | analyze | task1 |

## Claude-Codex 讨论记录

### 共识

- `qemu_cortex_a9` 是合理的首个 Zephyr 目标（Zephyr 中存在该板级，配有 Xilinx GEM 以太网 + 256MB flash）
- 自定义链接段代码片段 + whole-archive 是 rt-claw 注册段的正确方法
- Zephyr HTTP Client 需要预连接的 socket —— OSAL 必须管理 socket 生命周期
- 通过 `tls_credential_add()`、`TLS_SEC_TAG_LIST`、`TLS_HOSTNAME` 实现 TLS 是正确路径
- 使用 Zephyr Shell 进行行编辑/历史/补全是合理的，配合 bypass 模式拦截原始输入
- 第一阶段禁用 Feishu/Telegram 是正确的（bare-metal 约束）
- 第一阶段范围：shell + AI + net + KV（无 swarm/OTA/IM）
- Meson 构建 libclaw.a；Zephyr CMake 拥有最终固件
- Vendor 方式管理 Zephyr + mbedTLS，构建时不依赖 west 是可接受的
- Settings + NVS 用于 KV，erase_ns 延后实现

### 已解决的分歧

- **Zephyr 版本**：Codex 指出 v4.1.0 已 EOL。解决：固定 v4.4.0（最新稳定版，匹配用户本地 dev 版本）。
- **West 模块策略**：Codex 认为"运行 west update 或单独 submodule"太松散。解决：mbedTLS 作为独立 submodule 在 `vendor/lib/mbedtls/`，设置 `ZEPHYR_MODULES` CMake 变量。构建时不依赖 west。
- **动态分配**：Codex 要求静态池或明确例外。解决：动态分配（k_malloc、k_thread_stack_alloc）与三个现有后端一致（FreeRTOS: pvPortMalloc，RT-Thread: rt_malloc，Linux: malloc）。prj.conf 中明确设置 `CONFIG_HEAP_MEM_POOL_SIZE` 和 `CONFIG_DYNAMIC_THREAD`。
- **优先级映射**：Codex 指出透明映射不安全。解决：`CONFIG_NUM_PREEMPT_PRIORITIES=32`，OSAL 裁剪到有效范围，两个系统方向一致（越小越高）。
- **线程状态标志**：Codex 指出单个 volatile 标志混淆两种含义。解决：拆分为 `exit_requested`（由 delete 调用者设置，should_exit 读取）和 `exited`（wrapper 返回时设置）。
- **Shell UX**：Codex 指出单个 "claw" 命令不保留 UX。解决：使用 `shell_set_bypass()` 拦截原始输入，内部通过 rt-claw shell_dispatch() 分发。
- **Settings runtime API**：Codex 指出 settings_runtime_get 有局限。解决：写入使用 settings_save_one + 读取使用 settings_load_subtree 配合注册 handler。
- **响应截断**：Codex 指出必须 NUL 终止。解决：body 长度限制为 resp_size-1，始终 NUL 终止。
- **CI 中的端到端 AI**：Codex 指出外部网络依赖。解决：secret 门控 —— 仅在 RTCLAW_AI_API_KEY 环境变量设置时运行。
- **链接段命名**：调查发现 Zephyr 使用 `_list_start`/`_list_end` 命名，与 rt-claw 的 `__start_`/`__stop_` 不兼容。解决：通过 `zephyr_linker_sources(SECTIONS)` 自定义链接段代码片段创建 rt-claw 所需的精确符号名。

### 收敛状态

- 最终状态：`converged`
- 轮次：2（第 1 轮：11 个必须修改，第 2 轮：6 个小澄清 —— 全部解决）

## 待定用户决策

所有决策已在头脑风暴和收敛阶段解决。无待定项。

## 实现注意事项

### 代码风格要求
- 实现代码和注释中不得包含计划特定术语如 "AC-"、"Milestone"、"Step"、"Phase" 或类似的工作流标记
- 这些术语仅用于计划文档，不属于最终代码库
- 代码中使用描述性的、领域相关的命名
- 遵循 rt-claw 编码规范：C99 (gnu99)、4 空格缩进、约 80 字符行宽、snake_case、`/* C 风格注释 */`、SPDX MIT 头
- 启用 `-Werror` —— 零警告

### 版本固定
- Zephyr: v4.4.0（git tag `v4.4.0`）
- mbedTLS: 匹配 Zephyr v4.4.0 west.yml 引用的版本
- Zephyr SDK: 与 v4.4.0 兼容的版本（查看 Zephyr 发行说明）

### qemu_cortex_a9 关键 Kconfig
- `CONFIG_HEAP_MEM_POOL_SIZE=65536`
- `CONFIG_COMMON_LIBC_MALLOC=y` + `CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE=32768`
- `CONFIG_DYNAMIC_THREAD=y` + `CONFIG_DYNAMIC_THREAD_ALLOC=y`
- `CONFIG_NUM_PREEMPT_PRIORITIES=32`
- `CONFIG_MAIN_STACK_SIZE=4096`
- `CONFIG_NETWORKING=y` + `CONFIG_NET_IPV4=y` + `CONFIG_NET_TCP=y` + `CONFIG_NET_SOCKETS=y`
- `CONFIG_NET_DHCPV4=y` + `CONFIG_DNS_RESOLVER=y`
- `CONFIG_MBEDTLS=y` + `CONFIG_NET_SOCKETS_SOCKOPT_TLS=y`
- `CONFIG_FLASH=y` + `CONFIG_FLASH_MAP=y` + `CONFIG_NVS=y`
- `CONFIG_SETTINGS=y` + `CONFIG_SETTINGS_NVS=y` + `CONFIG_SETTINGS_RUNTIME=y`
- `CONFIG_SHELL=y` + `CONFIG_LOG=y`

--- Original Design Draft Start ---

# Zephyr OSAL Backend Design Spec

Date: 2026-05-03
Status: Draft
Target: Zephyr as first-priority RTOS for rt-claw

## Goals

- Add Zephyr as a new OSAL backend (`osal/zephyr/`), alongside FreeRTOS, RT-Thread, and Linux
- Make Zephyr the recommended default RTOS and primary platform for new feature validation
- First board: `qemu_cortex_a9` (validates flow against existing vexpress-a9)
- Second phase: ESP32-C3/S3 via Zephyr

## Constraints

- OSAL thin wrapper philosophy preserved — Zephyr is just another backend behind `claw_os.h`
- Existing FreeRTOS, RT-Thread, Linux backends continue to be maintained
- Build: Meson compiles `claw/` + `osal/zephyr/` as `libclaw.a`; platform layer provides a thin Zephyr Module wrapper; Zephyr CMake links `libclaw.a`
- Native HTTPS required (mbedTLS), no proxy dependency
- KV persistent on QEMU via emulated pflash
- Shell must be supported in first phase
- Zephyr source as git submodule at `vendor/os/zephyr/`, locked to a stable release

## Architecture Overview

Three new components:

1. **`osal/zephyr/`** — OSAL backend (3 files), struct embedding + container_of pattern
2. **`platform/zephyr/`** — Zephyr Module wrapper with board subdirectories
3. **`scripts/gen-zephyr-cross.py`** — Meson cross-file generator from Zephyr CMake cache

## Section 1: OSAL Zephyr Backend

Three source files in `osal/zephyr/`, symmetric with existing backends.

### `claw_os_zephyr.c` — OS Primitives

Each OSAL primitive wraps a Zephyr kernel object via struct embedding (base struct as first member, recovered via `container_of`).

Thread struct embeds `struct k_thread` + dynamically allocated `k_thread_stack_t`. A wrapper function adapts Zephyr's 3-argument thread entry to rt-claw's 1-argument convention.

Mapping:

| claw API | Zephyr kernel API | Notes |
|----------|-------------------|-------|
| `claw_thread_create()` | `k_thread_stack_alloc()` + `k_thread_create()` | Dynamic stack allocation; entry wrapper |
| `claw_thread_delete()` | `k_thread_join()` + `k_thread_stack_free()` | Join then free stack |
| `claw_thread_delay_ms()` | `k_msleep()` | Direct mapping |
| `claw_thread_yield()` | `k_yield()` | Direct mapping |
| `claw_thread_should_exit()` | Volatile `exited` flag | Same as FreeRTOS backend |
| `claw_mutex_create()` | `k_malloc()` + `k_mutex_init()` | Dynamic allocation |
| `claw_mutex_lock(timeout)` | `k_mutex_lock(K_MSEC(timeout))` | `CLAW_WAIT_FOREVER` maps to `K_FOREVER` |
| `claw_mutex_unlock()` | `k_mutex_unlock()` | Direct mapping |
| `claw_sem_create(init, max)` | `k_malloc()` + `k_sem_init(init, max)` | Counting semaphore |
| `claw_sem_take(timeout)` | `k_sem_take(K_MSEC(timeout))` | Timeout support built-in |
| `claw_sem_give()` | `k_sem_give()` | Direct mapping |
| `claw_mq_create(msg_size, max)` | `k_malloc()` msgq + buffer, `k_msgq_init()` | Buffer = msg_size * max_msgs |
| `claw_mq_send()` | `k_msgq_put()` | With timeout |
| `claw_mq_recv()` | `k_msgq_get()` | With timeout |
| `claw_timer_create()` | `k_malloc()` + `k_timer_init()` | Expiry callback via `CONTAINER_OF` to recover user context |
| `claw_timer_start()` | `k_timer_start(duration, period)` | One-shot: period=K_NO_WAIT; repeating: period=K_MSEC(ms) |
| `claw_timer_stop()` | `k_timer_stop()` | Direct mapping |
| `claw_malloc/free` | `k_malloc()` / `k_free()` | Requires CONFIG_COMMON_LIBC_MALLOC |
| `claw_tick_ms()` | `k_uptime_get()` | Direct mapping |
| `claw_log()` | Zephyr `LOG_MODULE_REGISTER` + `LOG_ERR/WRN/INF/DBG` | Maps to Zephyr native logging |

Priority mapping: `claw_thread.priority` (uint32_t, lower = higher) transparently maps to Zephyr preemptive priorities (int, lower = higher). Range clamped to `[0, CONFIG_NUM_PREEMPT_PRIORITIES-1]`.

### `claw_net_zephyr.c` — Networking

Uses Zephyr HTTP Client API (`http_client_req()`) with native HTTPS via mbedTLS.

Flow for `claw_net_post()` / `claw_net_get()`:

1. `zsock_getaddrinfo()` to resolve hostname
2. `zsock_socket()` — `IPPROTO_TLS_1_2` for HTTPS, `IPPROTO_TCP` for HTTP (determined by URL scheme)
3. For HTTPS: `zsock_setsockopt(SOL_TLS, TLS_SEC_TAG_LIST, ...)` and `zsock_setsockopt(SOL_TLS, TLS_HOSTNAME, ...)`
4. `zsock_connect()`
5. Populate `struct http_request` with method, URL path, headers, payload
6. `http_client_req()` — blocking call, response collected via callback
7. `zsock_close()`
8. Return HTTP status code or negative `CLAW_ERROR`

Response callback adapts Zephyr's callback model to rt-claw's synchronous `claw_net_post` semantics. Since `http_client_req()` blocks internally (poll loop), the callback executes in the calling thread's context — no extra synchronization needed.

CA root certificates registered via `tls_credential_add()` during board init. Certificate data compiled into the firmware image.

HTTPS capability matrix after this change:

| Backend | HTTP | HTTPS Native |
|---------|------|--------------|
| FreeRTOS (ESP-IDF) | Yes | Yes (esp_http_client + mbedTLS) |
| RT-Thread | Yes | No |
| Linux | Yes | Yes (libcurl) |
| **Zephyr** | **Yes** | **Yes (Zephyr HTTP Client + mbedTLS)** |

### `claw_kv_zephyr.c` — KV Storage

Uses Zephyr Settings subsystem over NVS backend.

Key mapping: `claw_kv_set_str("ns", "key", val)` becomes `settings_save_one("ns/key", val, len)`. The mapping is a single `snprintf` concatenation of namespace and key with `/` separator.

| claw API | Settings API | Notes |
|----------|-------------|-------|
| `claw_kv_init()` | `settings_subsys_init()` | One-time init, backend selected by Kconfig |
| `claw_kv_set_str()` | `settings_save_one("ns/key", val, len+1)` | +1 for null terminator |
| `claw_kv_get_str()` | `settings_runtime_get("ns/key", buf, size)` | Returns CLAW_OK or CLAW_ERR_NOENT |
| `claw_kv_set_blob()` | `settings_save_one("ns/key", data, len)` | Binary safe |
| `claw_kv_get_blob()` | `settings_runtime_get("ns/key", data, *len)` | Updates *len to actual length |
| `claw_kv_set_u8()` | `settings_save_one("ns/key", &val, 1)` | |
| `claw_kv_get_u8()` | `settings_runtime_get("ns/key", val, 1)` | |
| `claw_kv_delete()` | `settings_delete("ns/key")` | |
| `claw_kv_erase_ns()` | Returns `CLAW_ERR_NOENT` | Not natively supported; implement if needed later |

`erase_ns` returns unsupported for now — the function is rarely used in rt-claw business code, and implementing it would require a Settings handler with export enumeration. Revisit if needed.

This is expected to be the thinnest backend implementation (~80 lines).

## Section 2: Platform Layer

### Directory Structure

```
platform/zephyr/
├── CMakeLists.txt                  # Zephyr Module entry point
├── zephyr/
│   └── module.yml                  # Zephyr Module declaration
├── Kconfig                         # rt-claw feature flags exposed to Zephyr Kconfig
├── src/
│   ├── main.c                      # Zephyr app entry → claw_init()
│   ├── board.c                     # board_early_init() dispatch to board-level code
│   └── shell.c                     # Zephyr Shell integration (rt-claw shell commands)
└── boards/
    └── qemu_cortex_a9/
        ├── prj.conf                # Kconfig: kernel, networking, TLS, flash, NVS, shell
        ├── app.overlay             # DTS: flash partition for NVS storage
        └── board.c                 # Board init: network, TLS cert registration
```

One Zephyr Module, multiple boards via subdirectories. Shared `CMakeLists.txt`, `main.c`, `shell.c`; differences isolated to `prj.conf` + DTS overlay + `board.c`. Matches the pattern established by `platform/esp32c3/boards/`.

### Zephyr Module

`CMakeLists.txt` links `libclaw.a` (Meson output), adds rt-claw include paths, and declares custom linker sections for driver/service/tool registration.

`main.c` calls `claw_init()` and returns. Zephyr keeps the system running after main returns — service threads spawned by `claw_init()` continue to execute.

`shell.c` integrates rt-claw shell commands with the Zephyr Shell subsystem. Board-specific shell commands registered via `board_platform_commands()`.

### Board: `qemu_cortex_a9`

Kconfig enables: kernel tuning, heap, networking (IPv4 + TCP + DHCP + DNS), HTTPS (mbedTLS TLS 1.2), flash + NVS + Settings, logging, and shell.

DTS overlay defines a `storage_partition` on `flash0` for NVS (64KB).

Board init registers CA root certificates via `tls_credential_add()` for native HTTPS.

### QEMU Launch

```
qemu-system-arm -M vexpress-a9 -nographic \
    -kernel zephyr.elf \
    -nic user,model=lan9118 \
    -drive if=pflash,file=flash.bin,format=raw
```

Same machine model and NIC as existing vexpress-a9, with pflash for KV persistence.

## Section 3: Registration Mechanism

rt-claw uses custom linker sections (`claw_drivers`, `claw_services`, `claw_tools`) with GNU ld auto-generated `__start_` / `__stop_` boundary symbols.

Zephyr uses its own linker script generation system and does not auto-generate boundary symbols for unknown sections. Solution: use Zephyr's `zephyr_linker_section()` CMake API to declare the three sections with `KEEP()` to prevent GC.

Combined with `--whole-archive` on `libclaw.a` to ensure all registration entries are preserved (same approach as zynq-a9 platform).

Platform macro: `CLAW_PLATFORM_ZEPHYR` is NOT `CLAW_PLATFORM_ESP_IDF`, so `class.h` linker section macros activate automatically. No constructor fallback needed.

| Platform | Registration Path | Section Declaration |
|----------|-------------------|---------------------|
| zynq-a9 | Linker section | Hand-written link.ld with KEEP |
| vexpress-a9 | Linker section | SCons link script |
| ESP-IDF | `__attribute__((constructor))` | Not needed |
| Linux | Linker section | GNU ld auto-generates |
| **Zephyr** | **Linker section** | **`zephyr_linker_section()` CMake API** |

## Section 4: Build Flow

### Two-Phase Build

**Phase 1: Toolchain discovery**

A lightweight `west build` or `cmake` configuration pass produces `CMakeCache.txt`. `scripts/gen-zephyr-cross.py` extracts the C compiler path, sysroot, CPU flags, and Zephyr SDK toolchain path, then writes `build/zephyr-cortex-a9-qemu/cross.ini`.

**Phase 2: Meson compile + Zephyr link**

1. Meson uses `cross.ini` to cross-compile `claw/` + `osal/zephyr/` into `libclaw.a`
2. `west build` compiles `platform/zephyr/` as a Zephyr application, linking `libclaw.a`
3. Output: `zephyr.elf` + pflash image

### Meson Integration

New value `'zephyr'` added to the `osal` combo option in `meson_options.txt`. The `osal/meson.build` gains a corresponding branch compiling `osal/zephyr/claw_os_zephyr.c`, `claw_net_zephyr.c`, and `claw_kv_zephyr.c`.

Platform define: `-DCLAW_PLATFORM_ZEPHYR` added to the platform macro system.

### Zephyr as Git Submodule

Zephyr source pinned as a git submodule at `vendor/os/zephyr/`, locked to the latest stable release tag. Symmetric with `vendor/os/freertos/` and `vendor/os/rt-thread/`.

`ZEPHYR_BASE` is set internally by the Makefile targets to `$(PROJECT_ROOT)/vendor/os/zephyr/`. No user environment setup required (unlike ESP-IDF's `source export.sh`).

Remaining external dependencies: Zephyr SDK (toolchain), west (pip), CMake >= 3.20, Python >= 3.8.

### Makefile Targets

New entries following existing naming convention:

- `build-zephyr-cortex-a9-qemu` — Full build (cross-file generation + Meson + west build + pflash image)
- `run-zephyr-cortex-a9-qemu` — Build + QEMU launch
- `run-zephyr-cortex-a9-qemu GDB=1` — Debug mode (GDB port 1234)

## Section 5: Verification Strategy

### Validation Layers

**Layer 1: Compilation** — `make build-zephyr-cortex-a9-qemu` succeeds. Zero warnings under `-Werror`. No unresolved symbols at link time.

**Layer 2: Boot + Shell** — QEMU starts, rt-claw banner displayed. Shell interactive with basic commands (`help`, `version`, `list`). Service framework completes: driver probe, topological sort, service start — no crashes.

**Layer 3: Network** — DHCP obtains IP in SLIRP 10.0.2.x range. `claw_net_get()` HTTP request succeeds. `claw_net_post()` HTTPS to AI API succeeds (mbedTLS TLS handshake verified).

**Layer 4: KV Persistence** — `claw_kv_set_str()` / `claw_kv_get_str()` round-trip correct. Data survives QEMU restart via pflash image.

**Layer 5: End-to-End** — Full AI conversation: shell input, AI service processes, HTTPS POST to API, response parsed, shell output displayed.

### CI Integration

New smoke test: `make test-smoke-zephyr-cortex-a9` — QEMU boot, banner check, shell response, timeout exit. Symmetric with existing `test-smoke-vexpress` / `test-smoke-esp32c3`.

### Out of Scope for First Phase

- ESP32-C3/S3 via Zephyr (second phase)
- Swarm / OTA / IM services (after networking is stable)
- Performance benchmarks

## Platform Comparison After This Work

| Aspect | vexpress-a9 | zynq-a9 | ESP32-C3 | ESP32-S3 | Linux | **Zephyr cortex-a9** |
|--------|-------------|---------|----------|----------|-------|----------------------|
| RTOS | RT-Thread | FreeRTOS | FreeRTOS+IDF | FreeRTOS+IDF | pthreads | **Zephyr** |
| Build bridge | Meson+SCons | Meson full | Meson+CMake(idf) | Meson+CMake(idf) | Meson | **Meson+CMake(west)** |
| HTTPS | No | No | Yes | Yes | Yes | **Yes** |
| KV persistence | RAM stub | None | NVS Flash | NVS Flash | File-based | **Settings+NVS (pflash)** |
| Shell | RT-Thread FinSH | Custom | ESP console | ESP console | stdin | **Zephyr Shell** |
| QEMU NIC | LAN9118 | Cadence GEM | OpenCores | OpenCores | N/A | **LAN9118** |
| Registration | Linker section | Linker section | Constructor | Constructor | Linker section | **Linker section** |

--- Original Design Draft End ---
