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

void test_migrate_v1_legacy_values() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kLegacyKeyTimeoutMs, 9000U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kLegacyKeyMaxRetries, 4U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeySchemaVersion, 1U) == HAL_NVS_STATUS_OK);

    service::ConfigManager manager;
    assert(manager.load());
    assert(manager.schema_version() == 2U);
    assert(manager.command_timeout_ms() == 9000U);
    assert(manager.max_command_retries() == 4U);

    uint32_t persisted = 0U;
    assert(hal_nvs_get_u32(kKeySchemaVersion, &persisted) == HAL_NVS_STATUS_OK);
    assert(persisted == 2U);
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

}  // namespace

int main() {
    test_migrate_v1_legacy_values();
    test_invalid_legacy_values_fallback_to_defaults();
    test_reject_future_schema();
    return 0;
}
