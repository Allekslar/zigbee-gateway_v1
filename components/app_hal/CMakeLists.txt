# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectCompileOptions.cmake")

idf_component_register(
    SRCS
        "hal_zigbee.c"
        "hal_matter.c"
        "hal_nvs.c"
        "hal_wifi.c"
        "hal_mdns.c"
        "hal_spiffs.c"
        "hal_rcp.c"
        "hal_led.c"
    INCLUDE_DIRS "include"
    REQUIRES
        common
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        nvs_flash
        esp_driver_gpio
        espressif__esp-zigbee-lib
        espressif__esp-zboss-lib
        mdns
        spiffs
)

zg_apply_component_compile_policy(${COMPONENT_LIB})
