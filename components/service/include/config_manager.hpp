/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace service {

class ConfigManager {
public:
    static constexpr uint32_t kCurrentSchemaVersion = 2;
    static constexpr uint32_t kDefaultCommandTimeoutMs = 5000;
    static constexpr uint8_t kDefaultMaxCommandRetries = 1;
    static constexpr uint8_t kMaxCommandRetries = 5;
    static constexpr std::size_t kMaxReportingProfiles = 16;

    struct ReportingProfileKey {
        uint16_t short_addr{0};
        uint8_t endpoint{0};
        uint16_t cluster_id{0};
    };

    struct ReportingProfile {
        bool in_use{false};
        ReportingProfileKey key{};
        uint16_t min_interval_seconds{0};
        uint16_t max_interval_seconds{0};
        uint32_t reportable_change{0};
        uint8_t capability_flags{0};
    };

    bool load() noexcept;
    bool save() noexcept;
    uint32_t schema_version() const noexcept;
    bool set_command_timeout_ms(uint32_t timeout_ms) noexcept;
    uint32_t command_timeout_ms() const noexcept;
    bool set_max_command_retries(uint8_t retries) noexcept;
    uint8_t max_command_retries() const noexcept;
    bool set_reporting_profile(const ReportingProfile& profile) noexcept;
    bool clear_reporting_profile(const ReportingProfileKey& key) noexcept;
    bool get_reporting_profile(const ReportingProfileKey& key, ReportingProfile* out) const noexcept;
    std::size_t reporting_profile_count() const noexcept;
    bool dirty() const noexcept;

private:
    bool migrate_to_current(uint32_t from_version) noexcept;
    void load_current_values() noexcept;
    bool save_reporting_profiles() noexcept;
    void load_reporting_profiles() noexcept;
    static bool profile_key_equal(const ReportingProfileKey& lhs, const ReportingProfileKey& rhs) noexcept;
    int find_profile_index(const ReportingProfileKey& key) const noexcept;
    int find_free_profile_index() const noexcept;

    uint32_t schema_version_{kCurrentSchemaVersion};
    uint32_t command_timeout_ms_{kDefaultCommandTimeoutMs};
    uint8_t max_command_retries_{kDefaultMaxCommandRetries};
    std::array<ReportingProfile, kMaxReportingProfiles> reporting_profiles_{};
    bool dirty_{false};
};

}  // namespace service
