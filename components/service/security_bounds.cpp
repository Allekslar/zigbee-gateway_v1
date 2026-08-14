/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "security_bounds.hpp"

#include <cstddef>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

namespace service {

namespace {

// Single source of truth for the plan's own FD-13 (minimum, maximum,
// approved default) table -- security_bound_range() reads from this
// directly, and the host-build fallback in security_bounds() below is
// built from the same "approved_default" column, so the numbers are
// never duplicated a second time anywhere in this file.
constexpr SecurityBoundRange kRanges[10] = {
    /* kCommissioningWindowSeconds */ {60U, 600U, 600U},
    /* kJsonMaxBodyBytes         */ {512U, 2048U, 2048U},
    /* kJsonMaxDepth             */ {2U, 4U, 4U},
    /* kJsonMaxStringBytes       */ {64U, 512U, 512U},
    /* kJsonMaxKeys              */ {8U, 32U, 32U},
    /* kLoginAttemptsPerMinute   */ {1U, 5U, 5U},
    /* kCommandsPerMinute        */ {10U, 60U, 60U},
    /* kMutationsPerMinute       */ {1U, 5U, 5U},
    /* kFirmwareOpsPerHour       */ {1U, 2U, 2U},
    /* kAuditRingRecords         */ {32U, 128U, 128U},
};

// Approved-default bounds built entirely from `kRanges` above -- the one
// and only place these ten numbers live in this file. Used as-is for
// host builds (no Kconfig at all) and as the *fallback* for any
// individual `CONFIG_ZGW_*` macro that is not defined even when
// ESP_PLATFORM is set (see security_bounds() below).
SecurityBounds build_default_bounds() noexcept {
    SecurityBounds bounds{};
    bounds.commissioning_window_seconds = kRanges[static_cast<std::size_t>(SecurityBoundField::kCommissioningWindowSeconds)].approved_default;
    bounds.json_max_body_bytes = kRanges[static_cast<std::size_t>(SecurityBoundField::kJsonMaxBodyBytes)].approved_default;
    bounds.json_max_depth = kRanges[static_cast<std::size_t>(SecurityBoundField::kJsonMaxDepth)].approved_default;
    bounds.json_max_string_bytes = kRanges[static_cast<std::size_t>(SecurityBoundField::kJsonMaxStringBytes)].approved_default;
    bounds.json_max_keys = kRanges[static_cast<std::size_t>(SecurityBoundField::kJsonMaxKeys)].approved_default;
    bounds.login_attempts_per_minute = kRanges[static_cast<std::size_t>(SecurityBoundField::kLoginAttemptsPerMinute)].approved_default;
    bounds.commands_per_minute = kRanges[static_cast<std::size_t>(SecurityBoundField::kCommandsPerMinute)].approved_default;
    bounds.mutations_per_minute = kRanges[static_cast<std::size_t>(SecurityBoundField::kMutationsPerMinute)].approved_default;
    bounds.firmware_ops_per_hour = kRanges[static_cast<std::size_t>(SecurityBoundField::kFirmwareOpsPerHour)].approved_default;
    bounds.audit_ring_records = kRanges[static_cast<std::size_t>(SecurityBoundField::kAuditRingRecords)].approved_default;
    return bounds;
}

}  // namespace

const SecurityBounds& security_bounds() noexcept {
#ifdef ESP_PLATFORM
    // `CONFIG_ZGW_*` here come from `main/Kconfig.projbuild`'s "Security"
    // submenu, which only exists in the root firmware project (its
    // `main/` component). `components/service` is also linked into
    // `test/target`'s own, genuinely separate ESP-IDF project
    // (`test/target/CMakeLists.txt`'s own `project(hal_target_tests)`,
    // its own `main/`) -- that project has no Kconfig.projbuild of its
    // own and never sees the root one, so these macros are legitimately
    // undefined there even though ESP_PLATFORM is still set (it is a
    // real target build, just not the main app). Every other
    // `CONFIG_ZGW_*` consumer in this codebase already guards for this
    // (`#ifdef`/`#ifndef` fallback -- see e.g. ota_manager.cpp's
    // CONFIG_ZGW_MQTT_RESUME_AFTER_OTA_DELAY_MS); this accessor follows
    // the same discipline per-field via build_default_bounds() rather
    // than assuming every macro exists, which used to fail
    // target-tests's build with "was not declared in this scope" for
    // all ten symbols at once.
    static const SecurityBounds kBounds = [] {
        SecurityBounds bounds = build_default_bounds();
#ifdef CONFIG_ZGW_COMMISSIONING_WINDOW_SECONDS
        bounds.commissioning_window_seconds = CONFIG_ZGW_COMMISSIONING_WINDOW_SECONDS;
#endif
#ifdef CONFIG_ZGW_JSON_MAX_BODY_BYTES
        bounds.json_max_body_bytes = CONFIG_ZGW_JSON_MAX_BODY_BYTES;
#endif
#ifdef CONFIG_ZGW_JSON_MAX_DEPTH
        bounds.json_max_depth = CONFIG_ZGW_JSON_MAX_DEPTH;
#endif
#ifdef CONFIG_ZGW_JSON_MAX_STRING_BYTES
        bounds.json_max_string_bytes = CONFIG_ZGW_JSON_MAX_STRING_BYTES;
#endif
#ifdef CONFIG_ZGW_JSON_MAX_KEYS
        bounds.json_max_keys = CONFIG_ZGW_JSON_MAX_KEYS;
#endif
#ifdef CONFIG_ZGW_LOGIN_ATTEMPTS_PER_MINUTE
        bounds.login_attempts_per_minute = CONFIG_ZGW_LOGIN_ATTEMPTS_PER_MINUTE;
#endif
#ifdef CONFIG_ZGW_COMMANDS_PER_MINUTE
        bounds.commands_per_minute = CONFIG_ZGW_COMMANDS_PER_MINUTE;
#endif
#ifdef CONFIG_ZGW_MUTATIONS_PER_MINUTE
        bounds.mutations_per_minute = CONFIG_ZGW_MUTATIONS_PER_MINUTE;
#endif
#ifdef CONFIG_ZGW_FIRMWARE_OPS_PER_HOUR
        bounds.firmware_ops_per_hour = CONFIG_ZGW_FIRMWARE_OPS_PER_HOUR;
#endif
#ifdef CONFIG_ZGW_AUDIT_RING_RECORDS
        bounds.audit_ring_records = CONFIG_ZGW_AUDIT_RING_RECORDS;
#endif
        return bounds;
    }();
    return kBounds;
#else
    static const SecurityBounds kBounds = build_default_bounds();
    return kBounds;
#endif
}

SecurityBoundRange security_bound_range(SecurityBoundField field) noexcept {
    const auto index = static_cast<std::size_t>(field);
    if (index >= (sizeof(kRanges) / sizeof(kRanges[0]))) {
        return SecurityBoundRange{0U, 0U, 0U};
    }
    return kRanges[index];
}

bool security_bound_value_in_range(SecurityBoundField field, uint32_t value) noexcept {
    const SecurityBoundRange range = security_bound_range(field);
    return value >= range.minimum && value <= range.maximum;
}

}  // namespace service
