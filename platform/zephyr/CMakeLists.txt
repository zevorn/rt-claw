cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rtclaw)

set(RTCLAW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(RTCLAW_BOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD})

# --- Generated rt-claw config (equivalent to Meson claw_gen_config.h) ---
set(RTCLAW_AI_API_KEY "$ENV{RTCLAW_AI_API_KEY}")
set(RTCLAW_AI_API_URL "$ENV{RTCLAW_AI_API_URL}")
set(RTCLAW_AI_MODEL "$ENV{RTCLAW_AI_MODEL}")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/claw_gen_config.h.cmake
    ${CMAKE_BINARY_DIR}/claw_gen_config.h
    @ONLY
)

# rt-claw include paths
target_include_directories(app PRIVATE
    ${RTCLAW_ROOT}
    ${CMAKE_BINARY_DIR}
    ${RTCLAW_ROOT}/include
    ${RTCLAW_ROOT}/include/osal
    ${RTCLAW_ROOT}/vendor/lib/cjson
)

# Zephyr first-stage feature profile:
#   enabled:  AI, net, KV, system tool, net tool, sched
#   disabled: Feishu, Telegram, Swarm, OTA, GPIO, LCD, audio, mouse, script
target_compile_definitions(app PRIVATE
    CLAW_PLATFORM_ZEPHYR
    CLAW_HAS_GENERATED_CONFIG
    CONFIG_RTCLAW_SCHED_ENABLE
    CONFIG_RTCLAW_TOOL_SYSTEM
    CONFIG_RTCLAW_TOOL_NET
    CONFIG_RTCLAW_TOOL_SCHED
)

target_compile_options(app PRIVATE
    -std=gnu99
    -Wno-double-promotion
    -Wno-unused-function
)

# --- rt-claw core ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/init.c
    ${RTCLAW_ROOT}/claw/core/errno.c
    ${RTCLAW_ROOT}/claw/core/console.c
    ${RTCLAW_ROOT}/claw/core/service.c
    ${RTCLAW_ROOT}/claw/core/driver.c
    ${RTCLAW_ROOT}/claw/core/tool.c
)

# --- rt-claw services (first-stage only) ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/services/gateway.c
    ${RTCLAW_ROOT}/claw/services/sched.c
    ${RTCLAW_ROOT}/claw/services/ai/ai_engine.c
    ${RTCLAW_ROOT}/claw/services/ai/ai_memory.c
    ${RTCLAW_ROOT}/claw/services/ai/ai_skill.c
    ${RTCLAW_ROOT}/claw/services/net/net_service.c
    ${RTCLAW_ROOT}/claw/services/im/im_util.c
    ${RTCLAW_ROOT}/claw/services/im/feishu.c
    ${RTCLAW_ROOT}/claw/services/im/telegram.c
    ${RTCLAW_ROOT}/claw/services/swarm/swarm.c
    ${RTCLAW_ROOT}/claw/services/ota/ota_service.c
    ${RTCLAW_ROOT}/claw/services/tools/ota.c
    ${RTCLAW_ROOT}/platform/linux/ota_stub.c
)

# --- rt-claw tools ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/services/tools/tools.c
    ${RTCLAW_ROOT}/claw/services/tools/system.c
    ${RTCLAW_ROOT}/claw/services/tools/net.c
    ${RTCLAW_ROOT}/claw/services/tools/sched.c
    ${RTCLAW_ROOT}/claw/services/tools/lcd.c
    ${RTCLAW_ROOT}/claw/services/tools/audio.c
)

# --- Shell ---
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/shell/shell_commands.c
    ${RTCLAW_ROOT}/claw/shell/shell_history.c
)

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
    src/zephyr_driver.c
)

# Board-specific sources
if(EXISTS ${RTCLAW_BOARD_DIR}/board.c)
    target_sources(app PRIVATE ${RTCLAW_BOARD_DIR}/board.c)
endif()

# Custom linker sections for rt-claw OOP auto-registration.
zephyr_linker_sources(SECTIONS
    ${CMAKE_CURRENT_SOURCE_DIR}/claw_sections.ld
)
