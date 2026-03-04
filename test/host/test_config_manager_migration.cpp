/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "config_manager.hpp"
#include "hal_nvs.h"

namespace {

constexpr const char* kKeySchemaVersion = "cfg_schema_ver";
constexpr const char* kKeyTimeoutMs = "cfg_cmd_tmo_ms";
constexpr const char* kKeyMaxRetries = "cfg_cmd_retry";
constexpr const char* kLegacyKeyTimeoutMs = "cmd_tmo_ms";
constexpr const char* kLegacyKeyMaxRetries = "cmd_retries";
constexpr const char* kKeyReportingProfileCount = "cfg_rpt_cnt";
constexpr const char* kLegacyV2KeyReportingProfileCount = "cfg_rpt_count";

void test_migrate_v1_legacy_values() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kLegacyKeyTimeoutMs, 9000U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kLegacyKeyMaxRetries, 4U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeySchemaVersion, 1U) == HAL_NVS_STATUS_OK);

    service::ConfigManager manager;
    assert(manager.load());
    assert(manager.schema_version() == service::ConfigManager::kCurrentSchemaVersion);
    assert(manager.command_timeout_ms() == 9000U);
    assert(manager.max_command_retries() == 4U);

    uint32_t persisted = 0U;
    assert(hal_nvs_get_u32(kKeySchemaVersion, &persisted) == HAL_NVS_STATUS_OK);
    assert(persisted == service::ConfigManager::kCurrentSchemaVersion);
    assert(hal_nvs_get_u32(kKeyTimeoutMs, &persisted) == HAL_NVS_STATUS_OK);
    assert(persisted == 9000U);
    assert(hal_nvs_get_u32(kKeyMaxRetries, &persisted) == HAL_NVS_STATUS_OK);
    assert(persisted == 4U);
}

void test_invalid_legacy_values_fallback_to_defaults() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kLegacyKeyTimeoutMs, 0U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kLegacyKeyMaxRetries, 99U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeySchemaVersion, 1U) == HAL_NVS_STATUS_OK);

    service::ConfigManager manager;
    assert(manager.load());
    assert(manager.schema_version() == service::ConfigManager::kCurrentSchemaVersion);
    assert(manager.command_timeout_ms() == service::ConfigManager::kDefaultCommandTimeoutMs);
    assert(manager.max_command_retries() == service::ConfigManager::kDefaultMaxCommandRetries);
}

void test_reject_future_schema() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeySchemaVersion, service::ConfigManager::kCurrentSchemaVersion + 1U) ==
           HAL_NVS_STATUS_OK);

    service::ConfigManager manager;
    assert(!manager.load());
}

void test_reporting_profile_persist_restore() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeySchemaVersion, service::ConfigManager::kCurrentSchemaVersion) == HAL_NVS_STATUS_OK);

    service::ConfigManager writer;
    assert(writer.load());

    service::ConfigManager::ReportingProfile profile{};
    profile.in_use = true;
    profile.key.short_addr = 0x2201U;
    profile.key.endpoint = 1U;
    profile.key.cluster_id = 0x0402U;
    profile.min_interval_seconds = 5U;
    profile.max_interval_seconds = 300U;
    profile.reportable_change = 10U;
    profile.capability_flags = 0x07U;

    assert(writer.set_reporting_profile(profile));
    assert(writer.save());

    service::ConfigManager reader;
    assert(reader.load());
    service::ConfigManager::ReportingProfile restored{};
    assert(reader.get_reporting_profile(profile.key, &restored));
    assert(restored.in_use);
    assert(restored.key.short_addr == profile.key.short_addr);
    assert(restored.key.endpoint == profile.key.endpoint);
    assert(restored.key.cluster_id == profile.key.cluster_id);
    assert(restored.min_interval_seconds == profile.min_interval_seconds);
    assert(restored.max_interval_seconds == profile.max_interval_seconds);
    assert(restored.reportable_change == profile.reportable_change);
    assert(restored.capability_flags == profile.capability_flags);
}

void test_migrate_v2_legacy_reporting_keys_idempotent() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeySchemaVersion, 2U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeyReportingProfileCount, 0U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kLegacyV2KeyReportingProfileCount, 1U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32("cfg_rpt_k00", 0x01013322U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32("cfg_rpt_c00", 0x00030402U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32("cfg_rpt_i00", 0x00780003U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32("cfg_rpt_r00", 25U) == HAL_NVS_STATUS_OK);

    service::ConfigManager first_boot;
    assert(first_boot.load());
    assert(first_boot.schema_version() == service::ConfigManager::kCurrentSchemaVersion);
    assert(first_boot.reporting_profile_count() == 1U);

    service::ConfigManager::ReportingProfileKey key{};
    key.short_addr = 0x3322U;
    key.endpoint = 1U;
    key.cluster_id = 0x0402U;

    service::ConfigManager::ReportingProfile profile{};
    assert(first_boot.get_reporting_profile(key, &profile));
    assert(profile.capability_flags == 0x03U);
    assert(profile.min_interval_seconds == 3U);
    assert(profile.max_interval_seconds == 120U);
    assert(profile.reportable_change == 25U);

    uint32_t persisted = 0U;
    assert(hal_nvs_get_u32(kKeyReportingProfileCount, &persisted) == HAL_NVS_STATUS_OK);
    assert(persisted == 1U);
    assert(hal_nvs_get_u32(kKeySchemaVersion, &persisted) == HAL_NVS_STATUS_OK);
    assert(persisted == service::ConfigManager::kCurrentSchemaVersion);

    service::ConfigManager second_boot;
    assert(second_boot.load());
    assert(second_boot.schema_version() == service::ConfigManager::kCurrentSchemaVersion);
    assert(second_boot.reporting_profile_count() == 1U);
    service::ConfigManager::ReportingProfile profile_after_reload{};
    assert(second_boot.get_reporting_profile(key, &profile_after_reload));
    assert(profile_after_reload.capability_flags == 0x03U);
}

void test_migrate_v2_without_legacy_reporting_keys_is_safe() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeySchemaVersion, 2U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeyReportingProfileCount, 0U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kLegacyV2KeyReportingProfileCount, 0U) == HAL_NVS_STATUS_OK);

    service::ConfigManager manager;
    assert(manager.load());
    assert(manager.schema_version() == service::ConfigManager::kCurrentSchemaVersion);
}

}  // namespace

int main() {
    test_migrate_v1_legacy_values();
    test_invalid_legacy_values_fallback_to_defaults();
    test_reject_future_schema();
    test_reporting_profile_persist_restore();
    test_migrate_v2_legacy_reporting_keys_idempotent();
    test_migrate_v2_without_legacy_reporting_keys_is_safe();
    return 0;
}
