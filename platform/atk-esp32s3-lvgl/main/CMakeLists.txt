set(PLATFORM_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(PROJECT_ROOT "${PLATFORM_DIR}/../..")

set(UI_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/ui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_helpers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/screens/ui_Screen1.c
    ${CMAKE_CURRENT_SOURCE_DIR}/images/ui_img_logo_png.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts/siyuan.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_control.c

)

set(MAIN_SRCS "main.c")
set(MAIN_INCLUDES "." "${PROJECT_ROOT}")
# set(MAIN_REQUIRES rt_claw console esp_driver_uart esp_driver_usb_serial_jtag)
set(MAIN_REQUIRES rt_claw console esp_driver_uart esp_driver_usb_serial_jtag espressif__esp_codec_dev)

list(APPEND MAIN_SRCS
    "${PLATFORM_DIR}/boards/${RTCLAW_BOARD}/board.c"
    "${PROJECT_ROOT}/drivers/serial/espressif/console.c"
    "${PROJECT_ROOT}/platform/common/espressif/esp_shell.c"
    "${UI_SRCS}" )

if(CONFIG_ESP_WIFI_ENABLED)
    list(APPEND MAIN_SRCS
        "${PROJECT_ROOT}/platform/common/espressif/wifi_board.c"
        "${PROJECT_ROOT}/drivers/net/espressif/wifi_manager.c")
    list(APPEND MAIN_REQUIRES esp_wifi esp_netif nvs_flash)

    if(CONFIG_RTCLAW_WIFI_PROVISION_ENABLE)
        list(APPEND MAIN_SRCS
            "${PROJECT_ROOT}/platform/common/espressif/wifi_provision.c")
        list(APPEND MAIN_REQUIRES esp_http_server)
    endif()
endif()

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS ${MAIN_INCLUDES}
    REQUIRES ${MAIN_REQUIRES}
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
