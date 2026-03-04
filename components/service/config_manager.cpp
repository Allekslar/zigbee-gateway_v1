/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "config_manager.hpp"

#include <cstdio>

#include "hal_nvs.h"

namespace service {

namespace {

// NVS key length is limited on ESP targets, keep keys short and stable.
constexpr const char* kKeySchemaVersion = "cfg_schema_ver";
constexpr const char* kKeyTimeoutMs = "cfg_cmd_tmo_ms";
constexpr const char* kKeyMaxRetries = "cfg_cmd_retry";
constexpr const char* kKeyReportingProfileCount = "cfg_rpt_cnt";
constexpr const char* kLegacyV2KeyReportingProfileCount = "cfg_rpt_count";

// Legacy v1 keys kept for backward-compatible migration.
constexpr const char* kLegacyKeyTimeoutMs = "cmd_tmo_ms";
constexpr const char* kLegacyKeyMaxRetries = "cmd_retries";
constexpr uint32_t kCurrentSchemaVersion = ConfigManager::kCurrentSchemaVersion;

constexpr uint32_t kProfileKeyValidBit = (1UL << 24);
constexpr uint32_t kProfileKeyShortMask = 0x0000FFFFUL;
constexpr uint32_t kProfileKeyEndpointMask = 0x00FF0000UL;
constexpr uint32_t kProfileClusterMask = 0x0000FFFFUL;
constexpr uint32_t kProfileCapsMask = 0x00FF0000UL;

bool build_profile_nvs_key(char prefix, std::size_t index, char* out, std::size_t out_size) noexcept {
    if (out == nullptr || out_size == 0 || index >= ConfigManager::kMaxReportingProfiles) {
        return false;
    }

    const int written = std::snprintf(out, out_size, "rptp_%c%02u", prefix, static_cast<unsigned>(index));
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

bool build_legacy_v2_profile_nvs_key(char prefix, std::size_t index, char* out, std::size_t out_size) noexcept {
    if (out == nullptr || out_size == 0 || index >= ConfigManager::kMaxReportingProfiles) {
        return false;
    }

    const int written = std::snprintf(out, out_size, "cfg_rpt_%c%02u", prefix, static_cast<unsigned>(index));
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

bool decode_profile_key_word(
    uint32_t key_word,
    ConfigManager::ReportingProfileKey* out_key) noexcept {
    if (out_key == nullptr) {
        return false;
    }

    if ((key_word & kProfileKeyValidBit) == 0U) {
        return false;
    }

    out_key->short_addr = static_cast<uint16_t>(key_word & kProfileKeyShortMask);
    out_key->endpoint = static_cast<uint8_t>((key_word & kProfileKeyEndpointMask) >> 16U);
    return out_key->short_addr != 0U && out_key->endpoint != 0U;
}

uint32_t encode_profile_key_word(const ConfigManager::ReportingProfileKey& key) noexcept {
    uint32_t value = 0U;
    value |= static_cast<uint32_t>(key.short_addr);
    value |= static_cast<uint32_t>(key.endpoint) << 16U;
    value |= kProfileKeyValidBit;
    return value;
}

uint32_t encode_profile_cluster_caps_word(uint16_t cluster_id, uint8_t capability_flags) noexcept {
    uint32_t value = 0U;
    value |= static_cast<uint32_t>(cluster_id);
    value |= static_cast<uint32_t>(capability_flags) << 16U;
    return value;
}

bool is_valid_timeout_ms(uint32_t timeout_ms) noexcept {
    return timeout_ms > 0;
}

bool is_valid_max_retries(uint32_t retries) noexcept {
    return retries <= ConfigManager::kMaxCommandRetries;
}

bool is_valid_reporting_policy(const ConfigManager::ReportingPolicyDefault& policy) noexcept {
    if (!policy.in_use) {
        return false;
    }
    if (policy.cluster_id == 0U) {
        return false;
    }
    if (policy.max_interval_seconds != 0U && policy.min_interval_seconds > policy.max_interval_seconds) {
        return false;
    }
    return true;
}

}  // namespace

bool ConfigManager::load() noexcept {
    schema_version_ = kCurrentSchemaVersion;
    command_timeout_ms_ = kDefaultCommandTimeoutMs;
    max_command_retries_ = kDefaultMaxCommandRetries;
    reporting_profiles_ = {};
    reporting_policy_defaults_[reporting_device_class_index(ReportingDeviceClass::kTemperature)] =
        default_policy_for_class(ReportingDeviceClass::kTemperature);
    reporting_policy_defaults_[reporting_device_class_index(ReportingDeviceClass::kMotion)] =
        default_policy_for_class(ReportingDeviceClass::kMotion);
    reporting_policy_defaults_[reporting_device_class_index(ReportingDeviceClass::kContact)] =
        default_policy_for_class(ReportingDeviceClass::kContact);
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
    load_reporting_profiles();

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
    if (!save_reporting_profiles()) {
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

bool ConfigManager::profile_key_equal(const ReportingProfileKey& lhs, const ReportingProfileKey& rhs) noexcept {
    return lhs.short_addr == rhs.short_addr && lhs.endpoint == rhs.endpoint && lhs.cluster_id == rhs.cluster_id;
}

int ConfigManager::find_profile_index(const ReportingProfileKey& key) const noexcept {
    for (std::size_t i = 0; i < reporting_profiles_.size(); ++i) {
        if (!reporting_profiles_[i].in_use) {
            continue;
        }
        if (profile_key_equal(reporting_profiles_[i].key, key)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int ConfigManager::find_free_profile_index() const noexcept {
    for (std::size_t i = 0; i < reporting_profiles_.size(); ++i) {
        if (!reporting_profiles_[i].in_use) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool ConfigManager::set_reporting_profile(const ReportingProfile& profile) noexcept {
    if (!profile.in_use || profile.key.short_addr == 0U || profile.key.endpoint == 0U || profile.key.cluster_id == 0U) {
        return false;
    }

    int index = find_profile_index(profile.key);
    if (index < 0) {
        index = find_free_profile_index();
    }
    if (index < 0) {
        return false;
    }

    reporting_profiles_[static_cast<std::size_t>(index)] = profile;
    reporting_profiles_[static_cast<std::size_t>(index)].in_use = true;
    dirty_ = true;
    return true;
}

bool ConfigManager::clear_reporting_profile(const ReportingProfileKey& key) noexcept {
    const int index = find_profile_index(key);
    if (index < 0) {
        return false;
    }

    reporting_profiles_[static_cast<std::size_t>(index)] = ReportingProfile{};
    dirty_ = true;
    return true;
}

bool ConfigManager::get_reporting_profile(const ReportingProfileKey& key, ReportingProfile* out) const noexcept {
    if (out == nullptr) {
        return false;
    }
    const int index = find_profile_index(key);
    if (index < 0) {
        return false;
    }

    *out = reporting_profiles_[static_cast<std::size_t>(index)];
    return true;
}

std::size_t ConfigManager::reporting_profile_count() const noexcept {
    std::size_t count = 0;
    for (const ReportingProfile& profile : reporting_profiles_) {
        if (profile.in_use) {
            ++count;
        }
    }
    return count;
}

bool ConfigManager::set_reporting_policy_default(
    ReportingDeviceClass device_class,
    const ReportingPolicyDefault& policy) noexcept {
    if (!valid_reporting_device_class(device_class) || !is_valid_reporting_policy(policy)) {
        return false;
    }

    const std::size_t index = reporting_device_class_index(device_class);
    reporting_policy_defaults_[index] = policy;
    dirty_ = true;
    return true;
}

bool ConfigManager::get_reporting_policy_default(
    ReportingDeviceClass device_class,
    ReportingPolicyDefault* out) const noexcept {
    if (out == nullptr || !valid_reporting_device_class(device_class)) {
        return false;
    }

    *out = reporting_policy_defaults_[reporting_device_class_index(device_class)];
    return out->in_use;
}

bool ConfigManager::resolve_reporting_profile(
    const ReportingProfileKey& key,
    ReportingDeviceClass device_class,
    ReportingProfile* out) const noexcept {
    if (out == nullptr || key.short_addr == 0U || key.endpoint == 0U) {
        return false;
    }

    if (get_reporting_profile(key, out)) {
        return true;
    }

    ReportingPolicyDefault policy{};
    if (!get_reporting_policy_default(device_class, &policy)) {
        return false;
    }

    const uint16_t resolved_cluster = (key.cluster_id != 0U) ? key.cluster_id : policy.cluster_id;
    if (resolved_cluster == 0U) {
        return false;
    }
    if (key.cluster_id != 0U && key.cluster_id != policy.cluster_id) {
        return false;
    }

    out->in_use = true;
    out->key.short_addr = key.short_addr;
    out->key.endpoint = key.endpoint;
    out->key.cluster_id = resolved_cluster;
    out->min_interval_seconds = policy.min_interval_seconds;
    out->max_interval_seconds = policy.max_interval_seconds;
    out->reportable_change = policy.reportable_change;
    out->capability_flags = policy.capability_flags;
    return true;
}

bool ConfigManager::dirty() const noexcept {
    return dirty_;
}

bool ConfigManager::valid_reporting_device_class(ReportingDeviceClass device_class) noexcept {
    return device_class == ReportingDeviceClass::kTemperature ||
           device_class == ReportingDeviceClass::kMotion ||
           device_class == ReportingDeviceClass::kContact;
}

std::size_t ConfigManager::reporting_device_class_index(ReportingDeviceClass device_class) noexcept {
    switch (device_class) {
        case ReportingDeviceClass::kTemperature:
            return 0U;
        case ReportingDeviceClass::kMotion:
            return 1U;
        case ReportingDeviceClass::kContact:
            return 2U;
        case ReportingDeviceClass::kUnknown:
        default:
            return 0U;
    }
}

ConfigManager::ReportingPolicyDefault ConfigManager::default_policy_for_class(ReportingDeviceClass device_class) noexcept {
    ReportingPolicyDefault policy{};
    policy.in_use = true;
    switch (device_class) {
        case ReportingDeviceClass::kTemperature:
            // Temperature Measurement cluster (0x0402), measuredValue (0x0000) in 0.01 C.
            policy.cluster_id = 0x0402U;
            policy.min_interval_seconds = 5U;
            policy.max_interval_seconds = 300U;
            policy.reportable_change = 10U;
            policy.capability_flags = 0x01U;
            break;
        case ReportingDeviceClass::kMotion:
            // Occupancy Sensing cluster (0x0406), occupancy (0x0000).
            policy.cluster_id = 0x0406U;
            policy.min_interval_seconds = 1U;
            policy.max_interval_seconds = 120U;
            policy.reportable_change = 0U;
            policy.capability_flags = 0x02U;
            break;
        case ReportingDeviceClass::kContact:
            // IAS Zone cluster (0x0500), zone status (0x0002).
            policy.cluster_id = 0x0500U;
            policy.min_interval_seconds = 1U;
            policy.max_interval_seconds = 300U;
            policy.reportable_change = 0U;
            policy.capability_flags = 0x04U;
            break;
        case ReportingDeviceClass::kUnknown:
        default:
            policy = ReportingPolicyDefault{};
            break;
    }
    return policy;
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

        if (version == 2) {
            // If current reporting profile key-space is already populated, only bump schema.
            uint32_t current_count = 0U;
            if (hal_nvs_get_u32(kKeyReportingProfileCount, &current_count) == HAL_NVS_STATUS_OK &&
                current_count > 0U) {
                if (hal_nvs_set_u32(kKeySchemaVersion, 3U) != HAL_NVS_STATUS_OK) {
                    return false;
                }
                version = 3;
                continue;
            }

            // Legacy v2 reporting key-space migration (best-effort, tolerant to partial/missing keys).
            uint32_t legacy_count = 0U;
            if (hal_nvs_get_u32(kLegacyV2KeyReportingProfileCount, &legacy_count) != HAL_NVS_STATUS_OK) {
                legacy_count = 0U;
            }
            if (legacy_count > kMaxReportingProfiles) {
                legacy_count = kMaxReportingProfiles;
            }

            std::size_t migrated_count = 0;
            for (std::size_t i = 0; i < static_cast<std::size_t>(legacy_count); ++i) {
                char legacy_key_slot[16]{};
                char legacy_cluster_slot[16]{};
                char legacy_interval_slot[16]{};
                char legacy_reportable_slot[16]{};
                if (!build_legacy_v2_profile_nvs_key('k', i, legacy_key_slot, sizeof(legacy_key_slot)) ||
                    !build_legacy_v2_profile_nvs_key('c', i, legacy_cluster_slot, sizeof(legacy_cluster_slot)) ||
                    !build_legacy_v2_profile_nvs_key('i', i, legacy_interval_slot, sizeof(legacy_interval_slot)) ||
                    !build_legacy_v2_profile_nvs_key('r', i, legacy_reportable_slot, sizeof(legacy_reportable_slot))) {
                    continue;
                }

                uint32_t key_word = 0U;
                uint32_t cluster_caps_word = 0U;
                if (hal_nvs_get_u32(legacy_key_slot, &key_word) != HAL_NVS_STATUS_OK ||
                    hal_nvs_get_u32(legacy_cluster_slot, &cluster_caps_word) != HAL_NVS_STATUS_OK) {
                    continue;
                }

                ReportingProfileKey key{};
                if (!decode_profile_key_word(key_word, &key)) {
                    continue;
                }

                const uint16_t cluster_id = static_cast<uint16_t>(cluster_caps_word & kProfileClusterMask);
                if (cluster_id == 0U) {
                    continue;
                }

                uint32_t interval_word = 0U;
                uint32_t reportable_change = 0U;
                (void)hal_nvs_get_u32(legacy_interval_slot, &interval_word);
                (void)hal_nvs_get_u32(legacy_reportable_slot, &reportable_change);

                char current_key_slot[16]{};
                char current_cluster_slot[16]{};
                char current_interval_slot[16]{};
                char current_reportable_slot[16]{};
                if (!build_profile_nvs_key('k', migrated_count, current_key_slot, sizeof(current_key_slot)) ||
                    !build_profile_nvs_key('c', migrated_count, current_cluster_slot, sizeof(current_cluster_slot)) ||
                    !build_profile_nvs_key('i', migrated_count, current_interval_slot, sizeof(current_interval_slot)) ||
                    !build_profile_nvs_key('r', migrated_count, current_reportable_slot, sizeof(current_reportable_slot))) {
                    continue;
                }

                if (hal_nvs_set_u32(current_key_slot, key_word) != HAL_NVS_STATUS_OK ||
                    hal_nvs_set_u32(current_cluster_slot, cluster_caps_word) != HAL_NVS_STATUS_OK ||
                    hal_nvs_set_u32(current_interval_slot, interval_word) != HAL_NVS_STATUS_OK ||
                    hal_nvs_set_u32(current_reportable_slot, reportable_change) != HAL_NVS_STATUS_OK) {
                    return false;
                }
                ++migrated_count;
            }

            if (hal_nvs_set_u32(kKeyReportingProfileCount, static_cast<uint32_t>(migrated_count)) != HAL_NVS_STATUS_OK) {
                return false;
            }
            if (hal_nvs_set_u32(kKeySchemaVersion, 3U) != HAL_NVS_STATUS_OK) {
                return false;
            }

            version = 3;
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

bool ConfigManager::save_reporting_profiles() noexcept {
    std::size_t used_count = 0;
    for (const ReportingProfile& profile : reporting_profiles_) {
        if (profile.in_use) {
            ++used_count;
        }
    }

    std::size_t write_index = 0;
    for (const ReportingProfile& profile : reporting_profiles_) {
        if (!profile.in_use) {
            continue;
        }

        char key_slot[16]{};
        char cluster_slot[16]{};
        char interval_slot[16]{};
        char reportable_slot[16]{};
        if (!build_profile_nvs_key('k', write_index, key_slot, sizeof(key_slot)) ||
            !build_profile_nvs_key('c', write_index, cluster_slot, sizeof(cluster_slot)) ||
            !build_profile_nvs_key('i', write_index, interval_slot, sizeof(interval_slot)) ||
            !build_profile_nvs_key('r', write_index, reportable_slot, sizeof(reportable_slot))) {
            return false;
        }

        const uint32_t key_word = encode_profile_key_word(profile.key);
        const uint32_t cluster_caps_word = encode_profile_cluster_caps_word(
            profile.key.cluster_id,
            profile.capability_flags);
        const uint32_t interval_word = static_cast<uint32_t>(profile.min_interval_seconds) |
                                       (static_cast<uint32_t>(profile.max_interval_seconds) << 16U);

        if (hal_nvs_set_u32(key_slot, key_word) != HAL_NVS_STATUS_OK ||
            hal_nvs_set_u32(cluster_slot, cluster_caps_word) != HAL_NVS_STATUS_OK ||
            hal_nvs_set_u32(interval_slot, interval_word) != HAL_NVS_STATUS_OK ||
            hal_nvs_set_u32(reportable_slot, profile.reportable_change) != HAL_NVS_STATUS_OK) {
            return false;
        }
        ++write_index;
    }

    if (hal_nvs_set_u32(kKeyReportingProfileCount, static_cast<uint32_t>(used_count)) != HAL_NVS_STATUS_OK) {
        return false;
    }

    return true;
}

void ConfigManager::load_reporting_profiles() noexcept {
    reporting_profiles_ = {};
    uint32_t persisted_count = 0U;
    if (hal_nvs_get_u32(kKeyReportingProfileCount, &persisted_count) != HAL_NVS_STATUS_OK) {
        persisted_count = 0U;
    }
    if (persisted_count > reporting_profiles_.size()) {
        persisted_count = static_cast<uint32_t>(reporting_profiles_.size());
    }

    for (std::size_t i = 0; i < static_cast<std::size_t>(persisted_count); ++i) {
        char key_slot[16]{};
        char cluster_slot[16]{};
        char interval_slot[16]{};
        char reportable_slot[16]{};
        if (!build_profile_nvs_key('k', i, key_slot, sizeof(key_slot)) ||
            !build_profile_nvs_key('c', i, cluster_slot, sizeof(cluster_slot)) ||
            !build_profile_nvs_key('i', i, interval_slot, sizeof(interval_slot)) ||
            !build_profile_nvs_key('r', i, reportable_slot, sizeof(reportable_slot))) {
            continue;
        }

        uint32_t key_word = 0U;
        if (hal_nvs_get_u32(key_slot, &key_word) != HAL_NVS_STATUS_OK || key_word == 0U) {
            continue;
        }

        ReportingProfile profile{};
        if (!decode_profile_key_word(key_word, &profile.key)) {
            continue;
        }

        uint32_t cluster_caps_word = 0U;
        if (hal_nvs_get_u32(cluster_slot, &cluster_caps_word) != HAL_NVS_STATUS_OK) {
            continue;
        }
        profile.key.cluster_id = static_cast<uint16_t>(cluster_caps_word & kProfileClusterMask);
        profile.capability_flags = static_cast<uint8_t>((cluster_caps_word & kProfileCapsMask) >> 16U);
        if (profile.key.cluster_id == 0U) {
            continue;
        }

        uint32_t interval_word = 0U;
        if (hal_nvs_get_u32(interval_slot, &interval_word) == HAL_NVS_STATUS_OK) {
            profile.min_interval_seconds = static_cast<uint16_t>(interval_word & 0xFFFFU);
            profile.max_interval_seconds = static_cast<uint16_t>((interval_word >> 16U) & 0xFFFFU);
        }

        uint32_t reportable_change = 0U;
        if (hal_nvs_get_u32(reportable_slot, &reportable_change) == HAL_NVS_STATUS_OK) {
            profile.reportable_change = reportable_change;
        }

        profile.in_use = true;
        reporting_profiles_[i] = profile;
    }
}

}  // namespace service
