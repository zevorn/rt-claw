cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED RTCLAW_BOARD)
    set(RTCLAW_BOARD "qemu")
endif()

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/boards/${RTCLAW_BOARD}/sdkconfig.defaults")

# Put sdkconfig in the build directory (supports parallel multi-board builds)
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rt-claw)
