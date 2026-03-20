set(RT_CLAW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(MESON_BUILD_DIR "${RT_CLAW_ROOT}/build/esp32s3-${RTCLAW_BOARD}/meson")

# Headers exported to dependent components (e.g. main)
set(CLAW_INC_DIRS
    "${RT_CLAW_ROOT}/include"
)

# ESP-IDF component dependencies (needed for symbol resolution at link time)
set(CLAW_REQUIRES
    freertos
    esp_netif
    esp_event
    esp_http_client
    esp_websocket_client
    esp-tls
    json
    esp_driver_gpio
    esp_timer
    lwip
    nvs_flash
    esp_hw_support
)

# Unconditional: ESP-IDF processes component REQUIRES before sdkconfig
# is available, so CONFIG_* conditionals cannot gate REQUIRES here.
# These components always exist; unused ones add only include paths.
list(APPEND CLAW_REQUIRES esp_eth esp_wifi esp_lcd app_update)

# Stub source so the component produces a real static library
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/rt_claw_stub.c" "/* auto-generated */\n")
set(CLAW_SRCS "${CMAKE_CURRENT_BINARY_DIR}/rt_claw_stub.c")

# USB HID mouse — requires esp_tinyusb managed component
if(CONFIG_RTCLAW_USB_HID_MOUSE)
    list(APPEND CLAW_SRCS
        "${RT_CLAW_ROOT}/drivers/input/espressif/usb_hid_mouse.c")
endif()

# Platform OTA implementation — must live in rt_claw (not main) so the
# linker resolves symbols referenced by Meson-compiled ota_service.o.
if(CONFIG_RTCLAW_OTA_ENABLE)
    list(APPEND CLAW_SRCS "${RT_CLAW_ROOT}/platform/common/espressif/esp_ota.c")
endif()

idf_component_register(
    SRCS ${CLAW_SRCS}
    INCLUDE_DIRS    ${CLAW_INC_DIRS}
    REQUIRES        ${CLAW_REQUIRES}
    LDFRAGMENTS     "rtclaw.lf"
    WHOLE_ARCHIVE
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC CLAW_PLATFORM_ESP_IDF)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)

execute_process(
    COMMAND git describe --tags --dirty=-dirty
    WORKING_DIRECTORY ${RT_CLAW_ROOT}
    OUTPUT_VARIABLE _GIT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE _GIT_RC
)
if(_GIT_RC EQUAL 0)
    string(REGEX REPLACE "^v" "" _GIT_VERSION "${_GIT_VERSION}")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        RT_CLAW_VERSION="${_GIT_VERSION}")
endif()

# Merge Meson pre-built objects into the component library so they
# participate in ESP-IDF's normal link group (resolves cross-references).
#
# The glob runs at BUILD time (not configure time) via a helper script,
# because Meson objects may not exist yet when CMake configures.
set(_merge_script "${CMAKE_CURRENT_BINARY_DIR}/merge_meson_objs.cmake")
file(WRITE "${_merge_script}" "
file(GLOB _objs
    \"${MESON_BUILD_DIR}/claw/librtclaw.a.p/*.o\"
    \"${MESON_BUILD_DIR}/osal/libosal.a.p/*.o\"
    \"${MESON_BUILD_DIR}/vendor/lib/cjson/libcjson.a.p/*.o\"
)
if(_objs)
    execute_process(COMMAND \${AR} q \${LIB} \${_objs})
    execute_process(COMMAND \${RANLIB} \${LIB})
else()
    message(FATAL_ERROR
        \"No Meson objects in ${MESON_BUILD_DIR}. \"
        \"Run 'meson compile -C build/esp32s3-${RTCLAW_BOARD}/meson' first.\")
endif()
")

add_custom_command(TARGET ${COMPONENT_LIB} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DAR=${CMAKE_AR}
        -DRANLIB=${CMAKE_RANLIB}
        -DLIB=$<TARGET_FILE:${COMPONENT_LIB}>
        -P "${_merge_script}"
    COMMENT "Merging Meson pre-built objects into ${COMPONENT_LIB}"
)
