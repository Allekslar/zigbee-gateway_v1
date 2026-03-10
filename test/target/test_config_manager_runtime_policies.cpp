/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cstdint>

#include "config_manager.hpp"
#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_nvs.h"
#include "hal_zigbee.h"
#include "hal_zigbee_test.h"
#include "service_runtime.hpp"
#include "unity.h"

namespace {

constexpr const char* kKeySchemaVersion = "cfg_schema_ver";
constexpr const char* kKeyTimeoutMs = "cfg_cmd_tmo_ms";
constexpr const char* kKeyMaxRetries = "cfg_cmd_retry";
constexpr const char* kLegacyKeyTimeoutMs = "cmd_tmo_ms";
constexpr const char* kLegacyKeyMaxRetries = "cmd_retries";

}  // namespace

extern "C" void test_config_manager_migrates_legacy_v1(void) {
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_init());
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_set_u32(kLegacyKeyTimeoutMs, 9000U));
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_set_u32(kLegacyKeyMaxRetries, 4U));
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_set_u32(kKeySchemaVersion, 1U));

    service::ConfigManager manager;
    TEST_ASSERT_TRUE(manager.load());
    TEST_ASSERT_EQUAL_UINT32(service::ConfigManager::kCurrentSchemaVersion, manager.schema_version());
    TEST_ASSERT_EQUAL_UINT32(9000U, manager.command_timeout_ms());
    TEST_ASSERT_EQUAL_UINT32(4U, manager.max_command_retries());

    uint32_t persisted = 0U;
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_get_u32(kKeySchemaVersion, &persisted));
    TEST_ASSERT_EQUAL_UINT32(service::ConfigManager::kCurrentSchemaVersion, persisted);
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_get_u32(kKeyTimeoutMs, &persisted));
    TEST_ASSERT_EQUAL_UINT32(9000U, persisted);
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_get_u32(kKeyMaxRetries, &persisted));
    TEST_ASSERT_EQUAL_UINT32(4U, persisted);
}

extern "C" void test_service_join_policy_deduplicates_candidates(void) {
    static core::CoreRegistry registry;
    static service::EffectExecutor effect_executor;
    static service::ServiceRuntime runtime(registry, effect_executor);
    TEST_ASSERT_TRUE(runtime.initialize_hal_adapter());

    hal_zigbee_notify_device_joined(0x2201);
    hal_zigbee_notify_device_joined(0x2201);
    hal_zigbee_notify_device_joined(0x2201);
    TEST_ASSERT_EQUAL_UINT32(1U, runtime.process_pending());
    TEST_ASSERT_EQUAL_UINT16(1U, runtime.state().device_count);

    hal_zigbee_notify_device_joined(0x2201);
    TEST_ASSERT_EQUAL_UINT32(0U, runtime.process_pending());
    TEST_ASSERT_EQUAL_UINT16(1U, runtime.state().device_count);

    hal_zigbee_notify_device_joined(0x2202);
    TEST_ASSERT_EQUAL_UINT32(1U, runtime.process_pending());
    TEST_ASSERT_EQUAL_UINT16(2U, runtime.state().device_count);
}

extern "C" void test_config_manager_reporting_profile_persist_restore(void) {
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_init());
    TEST_ASSERT_EQUAL_INT(
        HAL_NVS_STATUS_OK,
        hal_nvs_set_u32(kKeySchemaVersion, service::ConfigManager::kCurrentSchemaVersion));

    service::ConfigManager writer;
    TEST_ASSERT_TRUE(writer.load());

    service::ConfigManager::ReportingProfile profile{};
    profile.in_use = true;
    profile.key.short_addr = 0x2233U;
    profile.key.endpoint = 1U;
    profile.key.cluster_id = 0x0402U;
    profile.min_interval_seconds = 3U;
    profile.max_interval_seconds = 120U;
    profile.reportable_change = 25U;
    profile.capability_flags = 0x03U;

    TEST_ASSERT_TRUE(writer.set_reporting_profile(profile));
    TEST_ASSERT_TRUE(writer.save());

    service::ConfigManager reader;
    TEST_ASSERT_TRUE(reader.load());
    service::ConfigManager::ReportingProfile restored{};
    TEST_ASSERT_TRUE(reader.get_reporting_profile(profile.key, &restored));
    TEST_ASSERT_TRUE(restored.in_use);
    TEST_ASSERT_EQUAL_UINT16(profile.key.short_addr, restored.key.short_addr);
    TEST_ASSERT_EQUAL_UINT8(profile.key.endpoint, restored.key.endpoint);
    TEST_ASSERT_EQUAL_UINT16(profile.key.cluster_id, restored.key.cluster_id);
    TEST_ASSERT_EQUAL_UINT16(profile.min_interval_seconds, restored.min_interval_seconds);
    TEST_ASSERT_EQUAL_UINT16(profile.max_interval_seconds, restored.max_interval_seconds);
    TEST_ASSERT_EQUAL_UINT32(profile.reportable_change, restored.reportable_change);
    TEST_ASSERT_EQUAL_UINT8(profile.capability_flags, restored.capability_flags);
}
