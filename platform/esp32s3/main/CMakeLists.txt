set(PLATFORM_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(PROJECT_ROOT "${PLATFORM_DIR}/../..")

set(MAIN_SRCS "main.c")
set(MAIN_INCLUDES "." "${PROJECT_ROOT}")
set(MAIN_REQUIRES rt_claw console esp_driver_uart)

list(APPEND MAIN_SRCS
    "${PLATFORM_DIR}/boards/${RTCLAW_BOARD}/board.c")

if(CONFIG_ESP_WIFI_ENABLED)
    list(APPEND MAIN_SRCS
        "${PROJECT_ROOT}/platform/common/espressif/wifi_board.c"
        "${PROJECT_ROOT}/drivers/net/espressif/wifi_manager.c")
    list(APPEND MAIN_REQUIRES esp_wifi esp_netif nvs_flash)
endif()

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS ${MAIN_INCLUDES}
    REQUIRES ${MAIN_REQUIRES}
)
