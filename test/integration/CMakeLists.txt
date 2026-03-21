# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

cmake_minimum_required(VERSION 3.16)
project(zigbee_gateway_integration_tests C CXX)

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

add_library(core_integration STATIC ${CORE_SOURCES})
target_include_directories(core_integration
    PUBLIC
        ../../components/core/include
)
zg_apply_component_compile_policy(core_integration)

set(SERVICE_SOURCES
    ../../components/service/application_command_mapper.cpp
    ../../components/service/bridge_snapshot_builder.cpp
    ../../components/service/command_manager.cpp
    ../../components/service/connectivity_manager.cpp
    ../../components/service/config_manager.cpp
    ../../components/service/device_manager.cpp
    ../../components/service/devices_api_snapshot_builder.cpp
    ../../components/service/effect_executor.cpp
    ../../components/service/network_manager.cpp
    ../../components/service/network_policy_manager.cpp
    ../../components/service/operation_result_store.cpp
    ../../components/service/ota_manager.cpp
    ../../components/service/ota_manifest.cpp
    ../../components/service/ota_bootstrap.cpp
    ../../components/service/ota_trust_store.cpp
    ../../components/service/persistence_manager.cpp
    ../../components/service/read_model_coordinator.cpp
    ../../components/service/rcp_update_manager.cpp
    ../../components/service/reporting_manager.cpp
    ../../components/service/scan_manager.cpp
    ../../components/service/service_runtime.cpp
    ../../components/service/state_persistence_coordinator.cpp
    ../../components/service/hal_event_adapter.cpp
    ../../components/service/zigbee_lifecycle_coordinator.cpp
)

set(APP_HAL_SERVICE_SOURCES
    ../../components/app_hal/hal_led.c
    ../../components/app_hal/hal_nvs.c
    ../../components/app_hal/hal_ota.c
    ../../components/app_hal/hal_rcp.c
    ../../components/app_hal/hal_wifi.c
    ../../components/app_hal/hal_zigbee.c
)

add_library(service_integration STATIC ${SERVICE_SOURCES} ${APP_HAL_SERVICE_SOURCES})
target_include_directories(service_integration
    PUBLIC
        ../../components/service/include
        ../../components/app_hal/include
        ../../components/common/include
        ../../components/core/include
)
target_compile_definitions(service_integration PUBLIC SERVICE_RUNTIME_TEST_HOOKS=1)
target_link_libraries(service_integration PUBLIC core_integration)
zg_apply_component_compile_policy(service_integration)

add_library(web_ui_integration STATIC
    ../../components/web_ui/web_handler_common.cpp
)
target_include_directories(web_ui_integration
    PUBLIC
        ../../components/web_ui/include
        ../../components/web_ui
)
zg_apply_component_compile_policy(web_ui_integration)

set(APP_HAL_PLATFORM_SOURCES
    ../../components/app_hal/hal_mdns.c
    ../../components/app_hal/hal_spiffs.c
    ../../components/app_hal/hal_matter.c
    ../../components/app_hal/hal_ota.c
    ../../components/app_hal/hal_rcp.c
)

add_library(app_hal_platform_integration STATIC ${APP_HAL_PLATFORM_SOURCES})
target_include_directories(app_hal_platform_integration
    PUBLIC
        ../../components/app_hal/include
)
zg_apply_component_compile_policy(app_hal_platform_integration)

add_executable(test_integration_hal_platform_shims test_integration_hal_platform_shims.cpp)
target_link_libraries(test_integration_hal_platform_shims PRIVATE app_hal_platform_integration)
zg_apply_component_compile_policy(test_integration_hal_platform_shims)
add_test(NAME test_integration_hal_platform_shims COMMAND test_integration_hal_platform_shims)

add_executable(test_integration_web_routes test_integration_web_routes.cpp)
target_include_directories(test_integration_web_routes PRIVATE ../../components/web_ui/include)
zg_apply_component_compile_policy(test_integration_web_routes)
add_test(NAME test_integration_web_routes COMMAND test_integration_web_routes)

add_executable(test_integration_web_server test_integration_web_server.cpp)
target_include_directories(test_integration_web_server
    PRIVATE
        ../../components/web_ui/include
        ../../components/service/include
        ../../components/core/include
)
target_link_libraries(test_integration_web_server PRIVATE service_integration)
zg_apply_component_compile_policy(test_integration_web_server)
add_test(NAME test_integration_web_server COMMAND test_integration_web_server)

add_executable(test_integration_web_handlers_device test_integration_web_handlers_device.cpp)
target_include_directories(test_integration_web_handlers_device
    PRIVATE
        ../../components/web_ui/include
        ../../components/web_ui
        ../../components/service/include
        ../../components/app_hal/include
        ../../components/common/include
        ../../components/core/include
)
target_link_libraries(test_integration_web_handlers_device PRIVATE service_integration web_ui_integration)
zg_apply_component_compile_policy(test_integration_web_handlers_device)
add_test(NAME test_integration_web_handlers_device COMMAND test_integration_web_handlers_device)

add_executable(test_integration_web_handlers_static test_integration_web_handlers_static.cpp)
target_include_directories(test_integration_web_handlers_static
    PRIVATE
        ../../components/web_ui/include
        ../../components/web_ui
)
zg_apply_component_compile_policy(test_integration_web_handlers_static)
add_test(NAME test_integration_web_handlers_static COMMAND test_integration_web_handlers_static)

add_executable(test_integration_web_handlers_ota test_integration_web_handlers_ota.cpp)
target_include_directories(test_integration_web_handlers_ota
    PRIVATE
        ../../components/web_ui/include
        ../../components/web_ui
        ../../components/service/include
        ../../components/app_hal/include
        ../../components/common/include
        ../../components/core/include
)
target_link_libraries(test_integration_web_handlers_ota PRIVATE web_ui_integration)
zg_apply_component_compile_policy(test_integration_web_handlers_ota)
add_test(NAME test_integration_web_handlers_ota COMMAND test_integration_web_handlers_ota)

add_executable(test_integration_web_handlers_rcp test_integration_web_handlers_rcp.cpp)
target_include_directories(test_integration_web_handlers_rcp
    PRIVATE
        ../../components/web_ui/include
        ../../components/web_ui
        ../../components/service/include
        ../../components/app_hal/include
        ../../components/common/include
        ../../components/core/include
)
target_link_libraries(test_integration_web_handlers_rcp PRIVATE web_ui_integration)
zg_apply_component_compile_policy(test_integration_web_handlers_rcp)
add_test(NAME test_integration_web_handlers_rcp COMMAND test_integration_web_handlers_rcp)
