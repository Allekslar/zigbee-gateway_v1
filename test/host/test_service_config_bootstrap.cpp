/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_nvs.h"
#include "service_runtime.hpp"

namespace {

constexpr const char* kKeySchemaVersion = "cfg_schema_ver";
constexpr const char* kKeyTimeoutMs = "cfg_cmd_tmo_ms";
constexpr const char* kKeyMaxRetries = "cfg_cmd_retry";
constexpr const char* kLegacyKeyTimeoutMs = "cmd_tmo_ms";
constexpr const char* kLegacyKeyMaxRetries = "cmd_retries";

}  // namespace

int main() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeySchemaVersion, service::ConfigManager::kCurrentSchemaVersion + 1U) ==
           HAL_NVS_STATUS_OK);

    core::CoreRegistry failed_registry;
    service::EffectExecutor failed_effect_executor;
    service::ServiceRuntime failed_runtime(failed_registry, failed_effect_executor);
    assert(!failed_runtime.config_bootstrap_ok());
    assert(failed_runtime.config_load_report().status == service::ConfigManager::LoadStatus::kFailed);
    assert(!failed_runtime.start());

    assert(hal_nvs_set_u32(kLegacyKeyTimeoutMs, 9000U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kLegacyKeyMaxRetries, 2U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeySchemaVersion, 1U) == HAL_NVS_STATUS_OK);

    core::CoreRegistry migrated_registry;
    service::EffectExecutor migrated_effect_executor;
    service::ServiceRuntime migrated_runtime(migrated_registry, migrated_effect_executor);
    assert(migrated_runtime.config_bootstrap_ok());
    assert(migrated_runtime.config_load_report().status == service::ConfigManager::LoadStatus::kMigrated);
    assert(migrated_runtime.config_load_report().from_schema_version == 1U);
    assert(migrated_runtime.config_load_report().to_schema_version == service::ConfigManager::kCurrentSchemaVersion);
    assert(migrated_runtime.start());

    assert(hal_nvs_set_u32(kKeySchemaVersion, 0U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeyTimeoutMs, 7000U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32(kKeyMaxRetries, 2U) == HAL_NVS_STATUS_OK);

    core::CoreRegistry repaired_registry;
    service::EffectExecutor repaired_effect_executor;
    service::ServiceRuntime repaired_runtime(repaired_registry, repaired_effect_executor);
    assert(repaired_runtime.config_bootstrap_ok());
    assert(repaired_runtime.config_load_report().status == service::ConfigManager::LoadStatus::kReady);
    assert(repaired_runtime.config_load_report().from_schema_version == service::ConfigManager::kCurrentSchemaVersion);
    assert(repaired_runtime.start());

    assert(hal_nvs_set_u32(kKeySchemaVersion, service::ConfigManager::kCurrentSchemaVersion) == HAL_NVS_STATUS_OK);
    core::CoreRegistry reloaded_registry;
    service::EffectExecutor reloaded_effect_executor;
    service::ServiceRuntime reloaded_runtime(reloaded_registry, reloaded_effect_executor);
    assert(reloaded_runtime.config_bootstrap_ok());
    assert(hal_nvs_set_u32(kKeySchemaVersion, service::ConfigManager::kCurrentSchemaVersion + 1U) ==
           HAL_NVS_STATUS_OK);
    assert(reloaded_runtime.initialize_hal_adapter());
    assert(!reloaded_runtime.config_bootstrap_ok());
    assert(reloaded_runtime.config_load_report().status == service::ConfigManager::LoadStatus::kFailed);
    assert(!reloaded_runtime.start());

    return 0;
}
