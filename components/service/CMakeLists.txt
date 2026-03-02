# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectCompileOptions.cmake")

idf_component_register(
    SRCS
        "command_manager.cpp"
        "connectivity_manager.cpp"
        "config_manager.cpp"
        "device_manager.cpp"
        "effect_executor.cpp"
        "network_manager.cpp"
        "network_policy_manager.cpp"
        "persistence_manager.cpp"
        "reporting_manager.cpp"
        "scan_manager.cpp"
        "service_runtime.cpp"
        "hal_event_adapter.cpp"
    INCLUDE_DIRS "include"
    REQUIRES core app_hal common
)

zg_apply_component_compile_policy(${COMPONENT_LIB})
