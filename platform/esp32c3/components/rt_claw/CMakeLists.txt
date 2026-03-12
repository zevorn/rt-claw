set(RT_CLAW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRC_DIRS
        "${RT_CLAW_ROOT}/osal/freertos"
        "${RT_CLAW_ROOT}/src"
        "${RT_CLAW_ROOT}/src/core"
        "${RT_CLAW_ROOT}/src/services/swarm"
        "${RT_CLAW_ROOT}/src/services/net"
        "${RT_CLAW_ROOT}/src/services/ai"
        "${RT_CLAW_ROOT}/src/services/im"
        "${RT_CLAW_ROOT}/src/tools"
    INCLUDE_DIRS
        "${RT_CLAW_ROOT}/osal/include"
        "${RT_CLAW_ROOT}/src"
        "${RT_CLAW_ROOT}/src/core"
        "${RT_CLAW_ROOT}/src/services/swarm"
        "${RT_CLAW_ROOT}/src/services/net"
        "${RT_CLAW_ROOT}/src/services/ai"
        "${RT_CLAW_ROOT}/src/services/im"
        "${RT_CLAW_ROOT}/src/tools"
    REQUIRES
        freertos
        esp_eth
        esp_netif
        esp_event
        esp_http_client
        esp_websocket_client
        esp-tls
        json
        esp_driver_gpio
        esp_timer
        esp_lcd
        lwip
        nvs_flash
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC CLAW_PLATFORM_ESP_IDF)
