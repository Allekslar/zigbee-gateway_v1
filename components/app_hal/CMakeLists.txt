# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectCompileOptions.cmake")

idf_component_register(
    SRCS
        "hal_mqtt.c"
        "hal_zigbee.c"
        "hal_matter.c"
        "hal_ota.c"
        "hal_nvs.c"
        "hal_wifi.c"
        "hal_mdns.c"
        "hal_spiffs.c"
        "hal_rcp.c"
        "hal_rcp_uart_backend.c"
        "hal_led.c"
    EMBED_TXTFILES
        "ota_server_root_ca.pem"
        "ota_release_manifest_pub.pem"
        "ota_release_manifest_pub_next.pem"
    INCLUDE_DIRS "include"
    REQUIRES
        common
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        esp_system
        app_update
        bootloader_support
        esp_http_client
        esp_https_ota
        mbedtls
        mqtt
        nvs_flash
        esp_driver_gpio
        esp_driver_uart
        espressif__esp-zigbee-lib
        espressif__esp-zboss-lib
        mdns
        spiffs
)

zg_apply_component_compile_policy(${COMPONENT_LIB})
