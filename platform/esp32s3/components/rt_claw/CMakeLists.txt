set(RT_CLAW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
if(DEFINED RTCLAW_MESON_BUILD_DIR)
    set(MESON_BUILD_DIR "${RTCLAW_MESON_BUILD_DIR}")
else()
    set(MESON_BUILD_DIR "${RT_CLAW_ROOT}/build/esp32s3-${RTCLAW_BOARD}/meson")
endif()

set(CLAW_INC_DIRS
    "${RT_CLAW_ROOT}/include"
)

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

list(APPEND CLAW_REQUIRES esp_eth esp_wifi esp_lcd app_update esp_http_server)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/rt_claw_stub.c" "/* auto-generated */\n")
set(CLAW_SRCS "${CMAKE_CURRENT_BINARY_DIR}/rt_claw_stub.c")

if(CONFIG_RTCLAW_USB_HID_MOUSE)
    list(APPEND CLAW_SRCS
        "${RT_CLAW_ROOT}/drivers/input/espressif/usb_hid_mouse.c")
endif()

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
        RT_CLAW_VERSION=\"${_GIT_VERSION}\")
endif()

if(RTCLAW_PLATFORMIO)
    set(_gen_cross "${RT_CLAW_ROOT}/scripts/gen-esp32s3-cross.py")
    set(_board_build_dir "${MESON_BUILD_DIR}/..")

    set(_meson_stamp "${MESON_BUILD_DIR}/.pio_meson_done")
    set(_meson_script "${CMAKE_CURRENT_BINARY_DIR}/run_meson.cmake")
    file(WRITE "${_meson_script}" "
        execute_process(
            COMMAND ${Python3_EXECUTABLE} \"${_gen_cross}\"
                --idf-build-dir \"${CMAKE_BINARY_DIR}\"
                --board-build-dir \"${_board_build_dir}\"
                \"${RTCLAW_BOARD}\"
            WORKING_DIRECTORY \"${RT_CLAW_ROOT}\"
            RESULT_VARIABLE _rc
        )
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR \"gen-esp32s3-cross.py failed\")
        endif()
        set(_cross_ini \"${_board_build_dir}/cross.ini\")
        set(_build_ninja \"${MESON_BUILD_DIR}/build.ninja\")
        if(NOT EXISTS \${_build_ninja})
            execute_process(
                COMMAND meson setup \"${MESON_BUILD_DIR}\"
                    --cross-file=\${_cross_ini}
                WORKING_DIRECTORY \"${RT_CLAW_ROOT}\"
                RESULT_VARIABLE _rc
            )
            if(NOT _rc EQUAL 0)
                message(FATAL_ERROR \"meson setup failed\")
            endif()
        endif()
        execute_process(
            COMMAND meson compile -C \"${MESON_BUILD_DIR}\"
            WORKING_DIRECTORY \"${RT_CLAW_ROOT}\"
            RESULT_VARIABLE _rc
        )
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR \"meson compile failed\")
        endif()
        file(WRITE \"${_meson_stamp}\" \"done\")
    ")

    add_custom_command(
        OUTPUT "${_meson_stamp}"
        COMMAND ${CMAKE_COMMAND} -DPython3_EXECUTABLE=${Python3_EXECUTABLE}
            -P "${_meson_script}"
        COMMENT "Building rt-claw Meson libraries for PlatformIO"
        VERBATIM
    )
    add_custom_target(rtclaw_meson_build DEPENDS "${_meson_stamp}")
    add_dependencies(${COMPONENT_LIB} rtclaw_meson_build)

    set(_rtclaw_a "${MESON_BUILD_DIR}/claw/librtclaw.a")
    set(_osal_a   "${MESON_BUILD_DIR}/osal/libosal.a")
    set(_cjson_a  "${MESON_BUILD_DIR}/vendor/lib/cjson/libcjson.a")

    target_link_libraries(${COMPONENT_LIB} INTERFACE
        -Wl,--whole-archive
        "${_rtclaw_a}" "${_osal_a}" "${_cjson_a}"
        -Wl,--no-whole-archive
    )
else()
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
endif()
