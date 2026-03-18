# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectCompileOptions.cmake")

idf_component_register(
    SRCS
        "bridge_snapshot_builder.cpp"
        "command_manager.cpp"
        "connectivity_manager.cpp"
        "config_manager.cpp"
        "devices_api_snapshot_builder.cpp"
        "device_manager.cpp"
        "effect_executor.cpp"
        "read_model_coordinator.cpp"
        "network_manager.cpp"
        "network_policy_manager.cpp"
        "ota_bootstrap.cpp"
        "operation_result_store.cpp"
        "persistence_manager.cpp"
        "reporting_manager.cpp"
        "scan_manager.cpp"
        "service_runtime.cpp"
        "state_persistence_coordinator.cpp"
        "zigbee_lifecycle_coordinator.cpp"
        "hal_event_adapter.cpp"
    INCLUDE_DIRS "include"
    REQUIRES core app_hal common
)

zg_apply_component_compile_policy(${COMPONENT_LIB})
