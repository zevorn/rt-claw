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

# Suppress -Werror for rt-claw code compiled by Zephyr CMake.
# rt-claw uses gnu99 conventions; Zephyr defaults to c17.
target_compile_options(app PRIVATE
    -std=gnu99
    -Wno-double-promotion
)

# rt-claw OSAL backend (Zephyr)
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/osal/zephyr/claw_os_zephyr.c
    ${RTCLAW_ROOT}/osal/zephyr/claw_net_zephyr.c
    ${RTCLAW_ROOT}/osal/zephyr/claw_kv_zephyr.c
)

# rt-claw core
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/core/service.c
    ${RTCLAW_ROOT}/claw/core/driver.c
    ${RTCLAW_ROOT}/claw/core/tool.c
    ${RTCLAW_ROOT}/claw/core/console.c
    ${RTCLAW_ROOT}/claw/init.c
)

# rt-claw services
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/services/gateway.c
    ${RTCLAW_ROOT}/claw/services/ai/ai_engine.c
    ${RTCLAW_ROOT}/claw/services/ai/ai_memory.c
    ${RTCLAW_ROOT}/claw/services/net/net_service.c
)

# rt-claw shell commands
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/claw/shell/shell_commands.c
)

# cJSON
target_sources(app PRIVATE
    ${RTCLAW_ROOT}/vendor/lib/cjson/cJSON.c
)

# Platform defines
target_compile_definitions(app PRIVATE
    CLAW_PLATFORM_ZEPHYR
)

# Platform sources
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
