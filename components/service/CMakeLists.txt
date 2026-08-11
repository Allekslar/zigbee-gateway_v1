# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectCompileOptions.cmake")

idf_component_register(
    SRCS
        "application_command_mapper.cpp"
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
        "ota_manager.cpp"
        "ota_manifest.cpp"
        "ota_bootstrap.cpp"
        "ota_transport_policy.cpp"
        "ota_trust_store.cpp"
        "operation_result_store.cpp"
        "persistence_manager.cpp"
        "rcp_transport_policy.cpp"
        "rcp_update_manager.cpp"
        "reporting_manager.cpp"
        "scan_manager.cpp"
        "service_runtime.cpp"
        "state_persistence_coordinator.cpp"
        "persisted_device_state.cpp"
        "persisted_state_store.cpp"
        "zigbee_lifecycle_coordinator.cpp"
        "device_descriptor_store.cpp"
        "device_locator_registry.cpp"
        "matter_endpoint_registry.cpp"
        "nvs_namespace_registry.cpp"
        "secure_storage_port.cpp"
        "secure_storage_migration.cpp"
        "tls_provisioning_storage_port.cpp"
        "secure_log_redaction.cpp"
        "factory_reset_namespace_erase.cpp"
        "reset_journal_storage_port.cpp"
        "tuya_fingerprint.cpp"
        "tuya_dp_parser.cpp"
        "tuya_contact_sensor_plugin.cpp"
        "tuya_switch_plugin.cpp"
        "tuya_plugin_registry.cpp"
        "tuya_translator.cpp"
        "tuya_init_coordinator.cpp"
        "hal_event_adapter.cpp"
    EMBED_TXTFILES
        "../app_hal/ota_release_manifest_pub.pem"
        "../app_hal/ota_release_manifest_pub_next.pem"
    INCLUDE_DIRS "include"
    REQUIRES core app_hal common
)

zg_apply_component_compile_policy(${COMPONENT_LIB})
