cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rtclaw)

set(RTCLAW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(RTCLAW_BOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD})

# rt-claw include paths
target_include_directories(app PRIVATE
    ${RTCLAW_ROOT}
    ${RTCLAW_ROOT}/include
    ${RTCLAW_ROOT}/include/osal
    ${RTCLAW_ROOT}/vendor/lib/cjson
)

# Zephyr platform: disable IM, OTA, swarm; enable core + AI + net + KV
target_compile_definitions(app PRIVATE
    CLAW_PLATFORM_ZEPHYR
)

# Compile options for rt-claw C99 code within Zephyr's c17 build
target_compile_options(app PRIVATE
    -std=gnu99
    -Wno-double-promotion
)

# --- rt-claw core sources ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/init.c
    ${RTCLAW_ROOT}/claw/core/errno.c
    ${RTCLAW_ROOT}/claw/core/console.c
    ${RTCLAW_ROOT}/claw/core/service.c
    ${RTCLAW_ROOT}/claw/core/driver.c
    ${RTCLAW_ROOT}/claw/core/tool.c
)

# --- rt-claw services ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/services/gateway.c
    ${RTCLAW_ROOT}/claw/services/sched.c
    ${RTCLAW_ROOT}/claw/services/ai/ai_engine.c
    ${RTCLAW_ROOT}/claw/services/ai/ai_memory.c
    ${RTCLAW_ROOT}/claw/services/ai/ai_skill.c
    ${RTCLAW_ROOT}/claw/services/im/im_util.c
    ${RTCLAW_ROOT}/claw/services/im/feishu.c
    ${RTCLAW_ROOT}/claw/services/im/telegram.c
    ${RTCLAW_ROOT}/claw/services/net/net_service.c
    ${RTCLAW_ROOT}/claw/services/swarm/swarm.c
    ${RTCLAW_ROOT}/claw/services/ota/ota_service.c
)

# --- rt-claw tools ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/services/tools/tools.c
    ${RTCLAW_ROOT}/claw/services/tools/lcd.c
    ${RTCLAW_ROOT}/claw/services/tools/audio.c
    ${RTCLAW_ROOT}/claw/services/tools/sched.c
    ${RTCLAW_ROOT}/claw/services/tools/system.c
    ${RTCLAW_ROOT}/claw/services/tools/net.c
    ${RTCLAW_ROOT}/claw/services/tools/ota.c
)

# --- rt-claw shell ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/shell/shell_commands.c
    ${RTCLAW_ROOT}/claw/shell/shell_history.c
)

# --- OTA platform stub (non-ESP, non-Linux) ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/platform/linux/ota_stub.c
)

# Espressif-specific drivers excluded: SSD1306, ES8311, WiFi
# (not applicable on Zephyr platforms)

# --- OSAL Zephyr backend ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/osal/zephyr/claw_os_zephyr.c
    ${RTCLAW_ROOT}/osal/zephyr/claw_net_zephyr.c
    ${RTCLAW_ROOT}/osal/zephyr/claw_kv_zephyr.c
)

# --- cJSON ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/vendor/lib/cjson/cJSON.c
)

# --- Platform sources ---
target_sources(app PRIVATE
    src/main.c
    src/board.c
    src/shell.c
)

# Board-specific sources
if(EXISTS ${RTCLAW_BOARD_DIR}/board.c)
    target_sources(app PRIVATE ${RTCLAW_BOARD_DIR}/board.c)
endif()

# Custom linker sections for rt-claw OOP auto-registration.
zephyr_linker_sources(SECTIONS
    ${CMAKE_CURRENT_SOURCE_DIR}/claw_sections.ld
)
