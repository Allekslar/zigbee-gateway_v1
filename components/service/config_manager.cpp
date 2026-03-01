/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "config_manager.hpp"

#include "hal_nvs.h"

namespace service {

namespace {

// NVS key length is limited on ESP targets, keep keys short and stable.
constexpr const char* kKeySchemaVersion = "cfg_schema_ver";
constexpr const char* kKeyTimeoutMs = "cfg_cmd_tmo_ms";
constexpr const char* kKeyMaxRetries = "cfg_cmd_retry";

// Legacy v1 keys kept for backward-compatible migration.
constexpr const char* kLegacyKeyTimeoutMs = "cmd_tmo_ms";
constexpr const char* kLegacyKeyMaxRetries = "cmd_retries";

bool is_valid_timeout_ms(uint32_t timeout_ms) noexcept {
    return timeout_ms > 0;
}

bool is_valid_max_retries(uint32_t retries) noexcept {
    return retries <= ConfigManager::kMaxCommandRetries;
}

}  // namespace

bool ConfigManager::load() noexcept {
    schema_version_ = kCurrentSchemaVersion;
    command_timeout_ms_ = kDefaultCommandTimeoutMs;
    max_command_retries_ = kDefaultMaxCommandRetries;
    dirty_ = false;

    uint32_t persisted_schema = 1;
    if (hal_nvs_get_u32(kKeySchemaVersion, &persisted_schema) != HAL_NVS_STATUS_OK) {
        persisted_schema = 1;
    }

    if (persisted_schema == 0 || persisted_schema > kCurrentSchemaVersion) {
        return false;
    }

    if (persisted_schema < kCurrentSchemaVersion) {
        if (!migrate_to_current(persisted_schema)) {
            return false;
        }
        persisted_schema = kCurrentSchemaVersion;
    }

    schema_version_ = persisted_schema;
    load_current_values();

    dirty_ = false;
    return true;
}

bool ConfigManager::save() noexcept {
    if (hal_nvs_set_u32(kKeyTimeoutMs, command_timeout_ms_) != HAL_NVS_STATUS_OK) {
        return false;
    }
    if (hal_nvs_set_u32(kKeyMaxRetries, static_cast<uint32_t>(max_command_retries_)) != HAL_NVS_STATUS_OK) {
        return false;
    }
    if (hal_nvs_set_u32(kKeySchemaVersion, kCurrentSchemaVersion) != HAL_NVS_STATUS_OK) {
        return false;
    }

    schema_version_ = kCurrentSchemaVersion;
    dirty_ = false;
    return true;
}

uint32_t ConfigManager::schema_version() const noexcept {
    return schema_version_;
}

bool ConfigManager::set_command_timeout_ms(uint32_t timeout_ms) noexcept {
    if (!is_valid_timeout_ms(timeout_ms)) {
        return false;
    }

    if (command_timeout_ms_ == timeout_ms) {
        return true;
    }

    command_timeout_ms_ = timeout_ms;
    dirty_ = true;
    return true;
}

uint32_t ConfigManager::command_timeout_ms() const noexcept {
    return command_timeout_ms_;
}

bool ConfigManager::set_max_command_retries(uint8_t retries) noexcept {
    if (!is_valid_max_retries(retries)) {
        return false;
    }

    if (max_command_retries_ == retries) {
        return true;
    }

    max_command_retries_ = retries;
    dirty_ = true;
    return true;
}

uint8_t ConfigManager::max_command_retries() const noexcept {
    return max_command_retries_;
}

bool ConfigManager::dirty() const noexcept {
    return dirty_;
}

bool ConfigManager::migrate_to_current(uint32_t from_version) noexcept {
    uint32_t version = from_version;
    while (version < kCurrentSchemaVersion) {
        if (version == 1) {
            uint32_t timeout_ms = kDefaultCommandTimeoutMs;
            uint32_t retries = kDefaultMaxCommandRetries;

            if (hal_nvs_get_u32(kLegacyKeyTimeoutMs, &timeout_ms) != HAL_NVS_STATUS_OK) {
                (void)hal_nvs_get_u32(kKeyTimeoutMs, &timeout_ms);
            }
            if (!is_valid_timeout_ms(timeout_ms)) {
                timeout_ms = kDefaultCommandTimeoutMs;
            }

            if (hal_nvs_get_u32(kLegacyKeyMaxRetries, &retries) != HAL_NVS_STATUS_OK) {
                (void)hal_nvs_get_u32(kKeyMaxRetries, &retries);
            }
            if (!is_valid_max_retries(retries)) {
                retries = kDefaultMaxCommandRetries;
            }

            if (hal_nvs_set_u32(kKeyTimeoutMs, timeout_ms) != HAL_NVS_STATUS_OK) {
                return false;
            }
            if (hal_nvs_set_u32(kKeyMaxRetries, retries) != HAL_NVS_STATUS_OK) {
                return false;
            }
            if (hal_nvs_set_u32(kKeySchemaVersion, 2) != HAL_NVS_STATUS_OK) {
                return false;
            }

            version = 2;
            continue;
        }

        return false;
    }

    return true;
}

void ConfigManager::load_current_values() noexcept {
    uint32_t timeout_ms = kDefaultCommandTimeoutMs;
    if (hal_nvs_get_u32(kKeyTimeoutMs, &timeout_ms) == HAL_NVS_STATUS_OK && is_valid_timeout_ms(timeout_ms)) {
        command_timeout_ms_ = timeout_ms;
    } else {
        command_timeout_ms_ = kDefaultCommandTimeoutMs;
    }

    uint32_t retries = kDefaultMaxCommandRetries;
    if (hal_nvs_get_u32(kKeyMaxRetries, &retries) == HAL_NVS_STATUS_OK && is_valid_max_retries(retries)) {
        max_command_retries_ = static_cast<uint8_t>(retries);
    } else {
        max_command_retries_ = kDefaultMaxCommandRetries;
    }

}

}  // namespace service
