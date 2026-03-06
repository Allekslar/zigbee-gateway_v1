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
    ../../components/service/command_manager.cpp
    ../../components/service/connectivity_manager.cpp
    ../../components/service/config_manager.cpp
    ../../components/service/device_manager.cpp
    ../../components/service/effect_executor.cpp
    ../../components/service/network_manager.cpp
    ../../components/service/network_policy_manager.cpp
    ../../components/service/persistence_manager.cpp
    ../../components/service/reporting_manager.cpp
    ../../components/service/scan_manager.cpp
    ../../components/service/service_runtime.cpp
    ../../components/service/hal_event_adapter.cpp
)

set(APP_HAL_SOURCES
    ../../components/app_hal/hal_led.c
    ../../components/app_hal/hal_nvs.c
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
    ../../components/core/include
)
zg_apply_component_compile_policy(test_mqtt_serializer)
add_test(NAME test_mqtt_serializer COMMAND test_mqtt_serializer)
