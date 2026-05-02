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
