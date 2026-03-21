# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

cmake_minimum_required(VERSION 3.16)
project(zigbee_gateway_host_tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectCompileOptions.cmake")

enable_testing()

set(CORE_SOURCES
    ../../components/core/core_event_bus.cpp
    ../../components/core/core_registry.cpp
    ../../components/core/core_reducer.cpp
    ../../components/core/core_command_dispatcher.cpp
)

add_library(core_host STATIC ${CORE_SOURCES})
target_include_directories(core_host
    PUBLIC
        ../../components/core/include
)
zg_apply_component_compile_policy(core_host)

set(SERVICE_SOURCES
    ../../components/service/application_command_mapper.cpp
    ../../components/service/bridge_snapshot_builder.cpp
    ../../components/service/command_manager.cpp
    ../../components/service/connectivity_manager.cpp
    ../../components/service/config_manager.cpp
    ../../components/service/devices_api_snapshot_builder.cpp
    ../../components/service/device_manager.cpp
    ../../components/service/effect_executor.cpp
    ../../components/service/read_model_coordinator.cpp
    ../../components/service/network_manager.cpp
    ../../components/service/network_policy_manager.cpp
    ../../components/service/ota_manager.cpp
    ../../components/service/ota_manifest.cpp
    ../../components/service/ota_bootstrap.cpp
    ../../components/service/ota_transport_policy.cpp
    ../../components/service/ota_trust_store.cpp
    ../../components/service/operation_result_store.cpp
    ../../components/service/persistence_manager.cpp
    ../../components/service/rcp_transport_policy.cpp
    ../../components/service/rcp_update_manager.cpp
    ../../components/service/reporting_manager.cpp
    ../../components/service/scan_manager.cpp
    ../../components/service/service_runtime.cpp
    ../../components/service/state_persistence_coordinator.cpp
    ../../components/service/zigbee_lifecycle_coordinator.cpp
    ../../components/service/device_identity_store.cpp
    ../../components/service/tuya_fingerprint.cpp
    ../../components/service/tuya_dp_parser.cpp
    ../../components/service/tuya_contact_sensor_plugin.cpp
    ../../components/service/tuya_plugin_registry.cpp
    ../../components/service/tuya_translator.cpp
    ../../components/service/hal_event_adapter.cpp
)

set(APP_HAL_SOURCES
    ../../components/app_hal/hal_led.c
    ../../components/app_hal/hal_nvs.c
    ../../components/app_hal/hal_ota.c
    ../../components/app_hal/hal_rcp.c
    ../../components/app_hal/hal_wifi.c
    ../../components/app_hal/hal_zigbee.c
)

add_library(service_host STATIC ${SERVICE_SOURCES} ${APP_HAL_SOURCES})
target_include_directories(service_host
    PUBLIC
        ../../components/service/include
        ../../components/app_hal/include
        ../../components/common/include
        ../../components/core/include
)
target_compile_definitions(service_host PUBLIC SERVICE_RUNTIME_TEST_HOOKS=1)
target_link_libraries(service_host PUBLIC core_host)
zg_apply_component_compile_policy(service_host)

add_library(web_ui_host STATIC 
    ../../components/web_ui/web_handler_common.cpp
)
target_include_directories(web_ui_host PUBLIC 
    ../../components/web_ui/include
    ../../components/web_ui
)
zg_apply_component_compile_policy(web_ui_host)

add_executable(test_core_registry test_core_registry.cpp)
target_link_libraries(test_core_registry PRIVATE core_host)
zg_apply_component_compile_policy(test_core_registry)
add_test(NAME test_core_registry COMMAND test_core_registry)

add_executable(test_core_reducer test_core_reducer.cpp)
target_link_libraries(test_core_reducer PRIVATE core_host)
zg_apply_component_compile_policy(test_core_reducer)
add_test(NAME test_core_reducer COMMAND test_core_reducer)

add_executable(test_core_effects test_core_effects.cpp)
target_link_libraries(test_core_effects PRIVATE core_host)
zg_apply_component_compile_policy(test_core_effects)
add_test(NAME test_core_effects COMMAND test_core_effects)

add_executable(test_service_runtime test_service_runtime.cpp)
target_link_libraries(test_service_runtime PRIVATE service_host)
zg_apply_component_compile_policy(test_service_runtime)
add_test(NAME test_service_runtime COMMAND test_service_runtime)

add_executable(test_service_hal_adapter test_service_hal_adapter.cpp)
target_link_libraries(test_service_hal_adapter PRIVATE service_host)
zg_apply_component_compile_policy(test_service_hal_adapter)
add_test(NAME test_service_hal_adapter COMMAND test_service_hal_adapter)

add_executable(test_config_manager_migration test_config_manager_migration.cpp)
target_link_libraries(test_config_manager_migration PRIVATE service_host)
zg_apply_component_compile_policy(test_config_manager_migration)
add_test(NAME test_config_manager_migration COMMAND test_config_manager_migration)

add_executable(test_service_config_bootstrap test_service_config_bootstrap.cpp)
target_link_libraries(test_service_config_bootstrap PRIVATE service_host)
zg_apply_component_compile_policy(test_service_config_bootstrap)
add_test(NAME test_service_config_bootstrap COMMAND test_service_config_bootstrap)

add_executable(test_hal_wifi_scan test_hal_wifi_scan.cpp)
target_link_libraries(test_hal_wifi_scan PRIVATE service_host)
zg_apply_component_compile_policy(test_hal_wifi_scan)
add_test(NAME test_hal_wifi_scan COMMAND test_hal_wifi_scan)

add_executable(test_service_join_policy test_service_join_policy.cpp)
target_link_libraries(test_service_join_policy PRIVATE service_host)
zg_apply_component_compile_policy(test_service_join_policy)
add_test(NAME test_service_join_policy COMMAND test_service_join_policy)

add_executable(test_service_runtime_lifecycle test_service_runtime_lifecycle.cpp)
target_link_libraries(test_service_runtime_lifecycle PRIVATE service_host)
zg_apply_component_compile_policy(test_service_runtime_lifecycle)
add_test(NAME test_service_runtime_lifecycle COMMAND test_service_runtime_lifecycle)

add_executable(test_service_scan_status test_service_scan_status.cpp)
target_link_libraries(test_service_scan_status PRIVATE service_host)
zg_apply_component_compile_policy(test_service_scan_status)
add_test(NAME test_service_scan_status COMMAND test_service_scan_status)

add_executable(test_service_network_connect test_service_network_connect.cpp)
target_link_libraries(test_service_network_connect PRIVATE service_host)
zg_apply_component_compile_policy(test_service_network_connect)
add_test(NAME test_service_network_connect COMMAND test_service_network_connect)

add_executable(test_service_network_timeout test_service_network_timeout.cpp)
target_link_libraries(test_service_network_timeout PRIVATE service_host)
zg_apply_component_compile_policy(test_service_network_timeout)
add_test(NAME test_service_network_timeout COMMAND test_service_network_timeout)

add_executable(test_service_network_policy_state_access test_service_network_policy_state_access.cpp)
target_link_libraries(test_service_network_policy_state_access PRIVATE service_host)
zg_apply_component_compile_policy(test_service_network_policy_state_access)
add_test(NAME test_service_network_policy_state_access COMMAND test_service_network_policy_state_access)

add_executable(test_operation_result_store test_operation_result_store.cpp)
target_link_libraries(test_operation_result_store PRIVATE service_host)
zg_apply_component_compile_policy(test_operation_result_store)
add_test(NAME test_operation_result_store COMMAND test_operation_result_store)

add_executable(test_read_model_coordinator test_read_model_coordinator.cpp)
target_link_libraries(test_read_model_coordinator PRIVATE service_host)
zg_apply_component_compile_policy(test_read_model_coordinator)
add_test(NAME test_read_model_coordinator COMMAND test_read_model_coordinator)

add_executable(test_zigbee_lifecycle_coordinator test_zigbee_lifecycle_coordinator.cpp)
target_link_libraries(test_zigbee_lifecycle_coordinator PRIVATE service_host)
zg_apply_component_compile_policy(test_zigbee_lifecycle_coordinator)
add_test(NAME test_zigbee_lifecycle_coordinator COMMAND test_zigbee_lifecycle_coordinator)

add_executable(test_state_persistence_coordinator test_state_persistence_coordinator.cpp)
target_link_libraries(test_state_persistence_coordinator PRIVATE service_host)
zg_apply_component_compile_policy(test_state_persistence_coordinator)
add_test(NAME test_state_persistence_coordinator COMMAND test_state_persistence_coordinator)

add_executable(test_service_network_reconnect test_service_network_reconnect.cpp)
target_link_libraries(test_service_network_reconnect PRIVATE service_host)
zg_apply_component_compile_policy(test_service_network_reconnect)
add_test(NAME test_service_network_reconnect COMMAND test_service_network_reconnect)

add_executable(test_service_network_scan test_service_network_scan.cpp)
target_link_libraries(test_service_network_scan PRIVATE service_host)
zg_apply_component_compile_policy(test_service_network_scan)
add_test(NAME test_service_network_scan COMMAND test_service_network_scan)

add_executable(test_service_network_no_reconnect test_service_network_no_reconnect.cpp)
target_link_libraries(test_service_network_no_reconnect PRIVATE service_host)
zg_apply_component_compile_policy(test_service_network_no_reconnect)
add_test(NAME test_service_network_no_reconnect COMMAND test_service_network_no_reconnect)

add_executable(test_service_network_autoconnect_limit test_service_network_autoconnect_limit.cpp)
target_link_libraries(test_service_network_autoconnect_limit PRIVATE service_host)
zg_apply_component_compile_policy(test_service_network_autoconnect_limit)
add_test(NAME test_service_network_autoconnect_limit COMMAND test_service_network_autoconnect_limit)

add_executable(test_ota_bootstrap test_ota_bootstrap.cpp)
target_link_libraries(test_ota_bootstrap PRIVATE service_host)
zg_apply_component_compile_policy(test_ota_bootstrap)
add_test(NAME test_ota_bootstrap COMMAND test_ota_bootstrap)

add_executable(test_ota_manifest test_ota_manifest.cpp)
target_link_libraries(test_ota_manifest PRIVATE service_host)
zg_apply_component_compile_policy(test_ota_manifest)
add_test(NAME test_ota_manifest COMMAND test_ota_manifest)

add_executable(test_service_rcp test_service_rcp.cpp)
target_link_libraries(test_service_rcp PRIVATE service_host)
zg_apply_component_compile_policy(test_service_rcp)
add_test(NAME test_service_rcp COMMAND test_service_rcp)

add_executable(test_application_command_mapper test_application_command_mapper.cpp)
target_link_libraries(test_application_command_mapper PRIVATE service_host)
zg_apply_component_compile_policy(test_application_command_mapper)
add_test(NAME test_application_command_mapper COMMAND test_application_command_mapper)

add_executable(test_service_saved_credentials_zigbee_start test_service_saved_credentials_zigbee_start.cpp)
target_link_libraries(test_service_saved_credentials_zigbee_start PRIVATE service_host)
zg_apply_component_compile_policy(test_service_saved_credentials_zigbee_start)
add_test(NAME test_service_saved_credentials_zigbee_start COMMAND test_service_saved_credentials_zigbee_start)

add_executable(test_service_zigbee_formation_retry test_service_zigbee_formation_retry.cpp)
target_link_libraries(test_service_zigbee_formation_retry PRIVATE service_host)
zg_apply_component_compile_policy(test_service_zigbee_formation_retry)
add_test(NAME test_service_zigbee_formation_retry COMMAND test_service_zigbee_formation_retry)

add_executable(test_web_handlers_config test_web_handlers_config.cpp)
target_link_libraries(test_web_handlers_config PRIVATE service_host web_ui_host)
zg_apply_component_compile_policy(test_web_handlers_config)
add_test(NAME test_web_handlers_config COMMAND test_web_handlers_config)

add_executable(test_web_handlers_config_post test_web_handlers_config_post.cpp)
target_link_libraries(test_web_handlers_config_post PRIVATE service_host web_ui_host)
zg_apply_component_compile_policy(test_web_handlers_config_post)
add_test(NAME test_web_handlers_config_post COMMAND test_web_handlers_config_post)

add_executable(test_core_event_bus test_core_event_bus.cpp)
target_link_libraries(test_core_event_bus PRIVATE core_host)
zg_apply_component_compile_policy(test_core_event_bus)
add_test(NAME test_core_event_bus COMMAND test_core_event_bus)

add_executable(test_service_force_remove test_service_force_remove.cpp)
target_link_libraries(test_service_force_remove PRIVATE service_host)
zg_apply_component_compile_policy(test_service_force_remove)
add_test(NAME test_service_force_remove COMMAND test_service_force_remove)

add_executable(test_service_remove_device test_service_remove_device.cpp)
target_link_libraries(test_service_remove_device PRIVATE service_host)
zg_apply_component_compile_policy(test_service_remove_device)
add_test(NAME test_service_remove_device COMMAND test_service_remove_device)

add_executable(test_web_json_helpers test_web_json_helpers.cpp)
target_link_libraries(test_web_json_helpers PRIVATE service_host web_ui_host) 
zg_apply_component_compile_policy(test_web_json_helpers)
add_test(NAME test_web_json_helpers COMMAND test_web_json_helpers)

add_executable(test_web_handlers_network test_web_handlers_network.cpp)
target_link_libraries(test_web_handlers_network PRIVATE service_host web_ui_host)
zg_apply_component_compile_policy(test_web_handlers_network)
add_test(NAME test_web_handlers_network COMMAND test_web_handlers_network)

add_executable(test_web_handlers_network_routes test_web_handlers_network_routes.cpp)
target_link_libraries(test_web_handlers_network_routes PRIVATE service_host web_ui_host)
zg_apply_component_compile_policy(test_web_handlers_network_routes)
add_test(NAME test_web_handlers_network_routes COMMAND test_web_handlers_network_routes)

add_executable(test_web_handlers_rcp test_web_handlers_rcp.cpp)
target_link_libraries(test_web_handlers_rcp PRIVATE service_host web_ui_host)
zg_apply_component_compile_policy(test_web_handlers_rcp)
add_test(NAME test_web_handlers_rcp COMMAND test_web_handlers_rcp)

add_executable(test_service_reporting_manager test_service_reporting_manager.cpp)
target_link_libraries(test_service_reporting_manager PRIVATE service_host)
zg_apply_component_compile_policy(test_service_reporting_manager)
add_test(NAME test_service_reporting_manager COMMAND test_service_reporting_manager)

add_executable(test_service_reporting_retry test_service_reporting_retry.cpp)
target_link_libraries(test_service_reporting_retry PRIVATE service_host)
zg_apply_component_compile_policy(test_service_reporting_retry)
add_test(NAME test_service_reporting_retry COMMAND test_service_reporting_retry)

add_executable(test_service_reporting_stale test_service_reporting_stale.cpp)
target_link_libraries(test_service_reporting_stale PRIVATE service_host)
zg_apply_component_compile_policy(test_service_reporting_stale)
add_test(NAME test_service_reporting_stale COMMAND test_service_reporting_stale)

add_executable(test_service_reporting_flow test_service_reporting_flow.cpp)
target_link_libraries(test_service_reporting_flow PRIVATE service_host)
zg_apply_component_compile_policy(test_service_reporting_flow)
add_test(NAME test_service_reporting_flow COMMAND test_service_reporting_flow)

add_executable(test_service_reporting_profiles test_service_reporting_profiles.cpp)
target_link_libraries(test_service_reporting_profiles PRIVATE service_host)
zg_apply_component_compile_policy(test_service_reporting_profiles)
add_test(NAME test_service_reporting_profiles COMMAND test_service_reporting_profiles)

add_executable(test_service_reporting_semantics test_service_reporting_semantics.cpp)
target_link_libraries(test_service_reporting_semantics PRIVATE service_host)
zg_apply_component_compile_policy(test_service_reporting_semantics)
add_test(NAME test_service_reporting_semantics COMMAND test_service_reporting_semantics)

add_executable(test_service_reporting_faults test_service_reporting_faults.cpp)
target_link_libraries(test_service_reporting_faults PRIVATE service_host)
zg_apply_component_compile_policy(test_service_reporting_faults)
add_test(NAME test_service_reporting_faults COMMAND test_service_reporting_faults)

add_executable(test_mqtt_topics
    test_mqtt_topics.cpp
    ../../components/mqtt_bridge/mqtt_topics.cpp
)
target_include_directories(test_mqtt_topics PRIVATE
    ../../components/mqtt_bridge/include
)
zg_apply_component_compile_policy(test_mqtt_topics)
add_test(NAME test_mqtt_topics COMMAND test_mqtt_topics)

add_executable(test_mqtt_serializer
    test_mqtt_serializer.cpp
    ../../components/mqtt_bridge/mqtt_serializer.cpp
)
target_include_directories(test_mqtt_serializer PRIVATE
    ../../components/mqtt_bridge/include
    ../../components/service/include
    ../../components/core/include
)
zg_apply_component_compile_policy(test_mqtt_serializer)
add_test(NAME test_mqtt_serializer COMMAND test_mqtt_serializer)

add_executable(test_mqtt_discovery
    test_mqtt_discovery.cpp
    ../../components/mqtt_bridge/mqtt_discovery.cpp
    ../../components/mqtt_bridge/mqtt_topics.cpp
)
target_include_directories(test_mqtt_discovery PRIVATE
    ../../components/mqtt_bridge/include
    ../../components/service/include
    ../../components/core/include
)
zg_apply_component_compile_policy(test_mqtt_discovery)
add_test(NAME test_mqtt_discovery COMMAND test_mqtt_discovery)

add_executable(test_mqtt_device_sync
    test_mqtt_device_sync.cpp
    ../../components/mqtt_bridge/mqtt_bridge.cpp
    ../../components/mqtt_bridge/mqtt_device_sync.cpp
    ../../components/mqtt_bridge/mqtt_topics.cpp
    ../../components/mqtt_bridge/mqtt_serializer.cpp
    ../../components/mqtt_bridge/mqtt_discovery.cpp
)
target_include_directories(test_mqtt_device_sync PRIVATE
    ../../components/mqtt_bridge/include
    ../../components/core/include
    ../../components/service/include
    ../../components/app_hal/include
    ../../components/common/include
)
target_link_libraries(test_mqtt_device_sync PRIVATE service_host)
zg_apply_component_compile_policy(test_mqtt_device_sync)
add_test(NAME test_mqtt_device_sync COMMAND test_mqtt_device_sync)

add_executable(test_mqtt_bridge_commands
    test_mqtt_bridge_commands.cpp
    ../../components/mqtt_bridge/mqtt_bridge.cpp
    ../../components/mqtt_bridge/mqtt_discovery.cpp
    ../../components/mqtt_bridge/mqtt_device_sync.cpp
    ../../components/mqtt_bridge/mqtt_topics.cpp
    ../../components/mqtt_bridge/mqtt_serializer.cpp
)
target_include_directories(test_mqtt_bridge_commands PRIVATE
    ../../components/mqtt_bridge/include
    ../../components/service/include
    ../../components/core/include
    ../../components/app_hal/include
    ../../components/common/include
)
target_link_libraries(test_mqtt_bridge_commands PRIVATE service_host)
zg_apply_component_compile_policy(test_mqtt_bridge_commands)
add_test(NAME test_mqtt_bridge_commands COMMAND test_mqtt_bridge_commands)

add_executable(test_mqtt_runtime_feed
    test_mqtt_runtime_feed.cpp
    ../../components/mqtt_bridge/mqtt_bridge.cpp
    ../../components/mqtt_bridge/mqtt_device_sync.cpp
    ../../components/mqtt_bridge/mqtt_topics.cpp
    ../../components/mqtt_bridge/mqtt_serializer.cpp
    ../../components/mqtt_bridge/mqtt_discovery.cpp
)
target_include_directories(test_mqtt_runtime_feed PRIVATE
    ../../components/mqtt_bridge/include
    ../../components/service/include
    ../../components/core/include
    ../../components/app_hal/include
    ../../components/common/include
)
target_link_libraries(test_mqtt_runtime_feed PRIVATE service_host)
zg_apply_component_compile_policy(test_mqtt_runtime_feed)
add_test(NAME test_mqtt_runtime_feed COMMAND test_mqtt_runtime_feed)

add_executable(test_mqtt_status_runtime_feed
    test_mqtt_status_runtime_feed.cpp
    ../../components/mqtt_bridge/mqtt_bridge.cpp
    ../../components/mqtt_bridge/mqtt_device_sync.cpp
    ../../components/mqtt_bridge/mqtt_topics.cpp
    ../../components/mqtt_bridge/mqtt_serializer.cpp
    ../../components/mqtt_bridge/mqtt_discovery.cpp
)
target_include_directories(test_mqtt_status_runtime_feed PRIVATE
    ../../components/mqtt_bridge/include
    ../../components/service/include
    ../../components/core/include
    ../../components/app_hal/include
    ../../components/common/include
)
target_compile_definitions(test_mqtt_status_runtime_feed PRIVATE MQTT_BRIDGE_TEST_HOOKS=1)
target_link_libraries(test_mqtt_status_runtime_feed PRIVATE service_host)
zg_apply_component_compile_policy(test_mqtt_status_runtime_feed)
add_test(NAME test_mqtt_status_runtime_feed COMMAND test_mqtt_status_runtime_feed)

add_executable(test_matter_device_map
    test_matter_device_map.cpp
    ../../components/matter_bridge/matter_device_map.cpp
)
target_include_directories(test_matter_device_map PRIVATE
    ../../components/matter_bridge/include
)
zg_apply_component_compile_policy(test_matter_device_map)
add_test(NAME test_matter_device_map COMMAND test_matter_device_map)

add_executable(test_matter_bridge
    test_matter_bridge.cpp
    ../../components/matter_bridge/matter_bridge.cpp
    ../../components/matter_bridge/matter_device_map.cpp
)
target_include_directories(test_matter_bridge PRIVATE
    ../../components/matter_bridge/include
    ../../components/core/include
    ../../components/service/include
    ../../components/app_hal/include
    ../../components/common/include
)
zg_apply_component_compile_policy(test_matter_bridge)
add_test(NAME test_matter_bridge COMMAND test_matter_bridge)

add_executable(test_matter_runtime_feed
    test_matter_runtime_feed.cpp
    ../../components/matter_bridge/matter_bridge.cpp
    ../../components/matter_bridge/matter_device_map.cpp
)
target_include_directories(test_matter_runtime_feed PRIVATE
    ../../components/matter_bridge/include
    ../../components/service/include
    ../../components/core/include
    ../../components/app_hal/include
    ../../components/common/include
)
target_link_libraries(test_matter_runtime_feed PRIVATE service_host)
zg_apply_component_compile_policy(test_matter_runtime_feed)
add_test(NAME test_matter_runtime_feed COMMAND test_matter_runtime_feed)

add_executable(test_matter_snapshot_semantics
    test_matter_snapshot_semantics.cpp
    ../../components/matter_bridge/matter_bridge.cpp
    ../../components/matter_bridge/matter_device_map.cpp
)
target_include_directories(test_matter_snapshot_semantics PRIVATE
    ../../components/matter_bridge/include
    ../../components/service/include
    ../../components/core/include
    ../../components/app_hal/include
    ../../components/common/include
)
zg_apply_component_compile_policy(test_matter_snapshot_semantics)
add_test(NAME test_matter_snapshot_semantics COMMAND test_matter_snapshot_semantics)

add_executable(test_matter_runtime_attach_contract
    test_matter_runtime_attach_contract.cpp
    ../../components/matter_bridge/matter_bridge.cpp
    ../../components/matter_bridge/matter_device_map.cpp
)
target_include_directories(test_matter_runtime_attach_contract PRIVATE
    ../../components/matter_bridge/include
    ../../components/service/include
    ../../components/core/include
    ../../components/app_hal/include
    ../../components/common/include
)
target_link_libraries(test_matter_runtime_attach_contract PRIVATE service_host)
zg_apply_component_compile_policy(test_matter_runtime_attach_contract)
add_test(NAME test_matter_runtime_attach_contract COMMAND test_matter_runtime_attach_contract)

add_executable(test_matter_runtime_robustness
    test_matter_runtime_robustness.cpp
    ../../components/matter_bridge/matter_bridge.cpp
    ../../components/matter_bridge/matter_device_map.cpp
)
target_include_directories(test_matter_runtime_robustness PRIVATE
    ../../components/matter_bridge/include
    ../../components/service/include
    ../../components/core/include
    ../../components/app_hal/include
    ../../components/common/include
)
zg_apply_component_compile_policy(test_matter_runtime_robustness)
add_test(NAME test_matter_runtime_robustness COMMAND test_matter_runtime_robustness)

add_executable(test_matter_runtime_command_loop
    test_matter_runtime_command_loop.cpp
    ../../components/matter_bridge/matter_bridge.cpp
    ../../components/matter_bridge/matter_device_map.cpp
)
target_include_directories(test_matter_runtime_command_loop PRIVATE
    ../../components/matter_bridge/include
    ../../components/service/include
    ../../components/core/include
    ../../components/app_hal/include
    ../../components/common/include
)
target_link_libraries(test_matter_runtime_command_loop PRIVATE service_host)
zg_apply_component_compile_policy(test_matter_runtime_command_loop)
add_test(NAME test_matter_runtime_command_loop COMMAND test_matter_runtime_command_loop)

add_executable(test_service_ota test_service_ota.cpp)
target_link_libraries(test_service_ota PRIVATE service_host)
zg_apply_component_compile_policy(test_service_ota)
add_test(NAME test_service_ota COMMAND test_service_ota)

add_executable(test_web_handlers_ota test_web_handlers_ota.cpp)
target_link_libraries(test_web_handlers_ota PRIVATE service_host web_ui_host)
zg_apply_component_compile_policy(test_web_handlers_ota)
add_test(NAME test_web_handlers_ota COMMAND test_web_handlers_ota)

add_executable(test_device_identity_store test_device_identity_store.cpp)
target_link_libraries(test_device_identity_store PRIVATE service_host)
zg_apply_component_compile_policy(test_device_identity_store)
add_test(NAME test_device_identity_store COMMAND test_device_identity_store)

add_executable(test_tuya_fingerprint test_tuya_fingerprint.cpp)
target_link_libraries(test_tuya_fingerprint PRIVATE service_host)
zg_apply_component_compile_policy(test_tuya_fingerprint)
add_test(NAME test_tuya_fingerprint COMMAND test_tuya_fingerprint)

add_executable(test_tuya_dp_parser test_tuya_dp_parser.cpp)
target_link_libraries(test_tuya_dp_parser PRIVATE service_host)
zg_apply_component_compile_policy(test_tuya_dp_parser)
add_test(NAME test_tuya_dp_parser COMMAND test_tuya_dp_parser)

add_executable(test_tuya_contact_sensor_plugin test_tuya_contact_sensor_plugin.cpp)
target_link_libraries(test_tuya_contact_sensor_plugin PRIVATE service_host)
zg_apply_component_compile_policy(test_tuya_contact_sensor_plugin)
add_test(NAME test_tuya_contact_sensor_plugin COMMAND test_tuya_contact_sensor_plugin)

add_executable(test_tuya_translator test_tuya_translator.cpp)
target_link_libraries(test_tuya_translator PRIVATE service_host)
zg_apply_component_compile_policy(test_tuya_translator)
add_test(NAME test_tuya_translator COMMAND test_tuya_translator)
