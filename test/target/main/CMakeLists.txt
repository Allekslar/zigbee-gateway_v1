# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/ProjectCompileOptions.cmake")

idf_component_register(
    SRCS
        "test_main.c"
        "../test_hal_nvs.c"
        "../test_hal_zigbee.c"
        "../test_reporting_faults.c"
        "../test_config_manager_runtime_policies.cpp"
        "../test_web_api_integration.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_hal core service web_ui unity esp_http_client
)

zg_apply_component_compile_policy(${COMPONENT_LIB})
