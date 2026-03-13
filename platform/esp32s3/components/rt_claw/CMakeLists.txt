set(RT_CLAW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(MESON_BUILD_DIR "${RT_CLAW_ROOT}/build/esp32s3")

# Headers exported to dependent components (e.g. main)
set(CLAW_INC_DIRS
    "${RT_CLAW_ROOT}/include"
)

# ESP-IDF component dependencies (needed for symbol resolution at link time)
set(CLAW_REQUIRES
    freertos
    esp_wifi
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

if(CONFIG_RTCLAW_LCD_ENABLE)
    list(APPEND CLAW_REQUIRES esp_lcd)
endif()

# Stub source so the component produces a real static library
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/rt_claw_stub.c" "/* auto-generated */\n")

idf_component_register(
    SRCS "${CMAKE_CURRENT_BINARY_DIR}/rt_claw_stub.c"
    INCLUDE_DIRS    ${CLAW_INC_DIRS}
    REQUIRES        ${CLAW_REQUIRES}
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC CLAW_PLATFORM_ESP_IDF)

# Merge Meson pre-built objects into the component library so they
# participate in ESP-IDF's normal link group (resolves cross-references).
file(GLOB _rtclaw_objs "${MESON_BUILD_DIR}/claw/librtclaw.a.p/*.o")
file(GLOB _osal_objs   "${MESON_BUILD_DIR}/osal/libosal.a.p/*.o")
file(GLOB _cjson_objs  "${MESON_BUILD_DIR}/vendor/lib/cjson/libcjson.a.p/*.o")

set(_all_meson_objs ${_rtclaw_objs} ${_osal_objs} ${_cjson_objs})

if(_all_meson_objs)
    add_custom_command(TARGET ${COMPONENT_LIB} POST_BUILD
        COMMAND ${CMAKE_AR} q $<TARGET_FILE:${COMPONENT_LIB}> ${_all_meson_objs}
        COMMAND ${CMAKE_RANLIB} $<TARGET_FILE:${COMPONENT_LIB}>
        COMMENT "Merging Meson pre-built objects into ${COMPONENT_LIB}"
    )
else()
    message(WARNING "No Meson objects found in ${MESON_BUILD_DIR}. "
                    "Run 'meson compile -C build/esp32s3' first.")
endif()
