# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/ProjectCompileOptions.cmake")

idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES core service web_ui mqtt_bridge matter_bridge app_hal common esp_timer
)

zg_apply_component_compile_policy(${COMPONENT_LIB})
