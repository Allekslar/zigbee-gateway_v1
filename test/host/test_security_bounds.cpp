/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "security_bounds.hpp"

namespace {

using service::SecurityBoundField;
using service::SecurityBounds;

// On a host build there is no real Kconfig -- security_bounds() falls
// back to the plan's own approved-default values (see
// components/service/security_bounds.cpp's build_host_bounds()). This is
// exactly what every later S6 host-tested component (JSON parsing, rate
// limiting, audit ring) will see when built and tested here.
void test_host_bounds_match_approved_defaults() {
    const SecurityBounds& bounds = service::security_bounds();
    assert(bounds.commissioning_window_seconds == 600U);
    assert(bounds.json_max_body_bytes == 2048U);
    assert(bounds.json_max_depth == 4U);
    assert(bounds.json_max_string_bytes == 512U);
    assert(bounds.json_max_keys == 32U);
    assert(bounds.login_attempts_per_minute == 5U);
    assert(bounds.commands_per_minute == 60U);
    assert(bounds.mutations_per_minute == 5U);
    assert(bounds.firmware_ops_per_hour == 2U);
    assert(bounds.audit_ring_records == 128U);
}

// Plan text, verbatim: "The provisioning passphrase length remains a
// non-configurable 16 Base32 characters, and login backoff remains a
// non-configurable 2-second start with a 60-second maximum." -- these are
// not Kconfig-derived at all, on any build.
void test_fixed_non_configurable_constants() {
    assert(SecurityBounds::kProvisioningPassphraseBase32Chars == 16U);
    assert(SecurityBounds::kLoginBackoffStartSeconds == 2U);
    assert(SecurityBounds::kLoginBackoffMaxSeconds == 60U);
}

void test_security_bounds_returns_a_stable_reference() {
    const SecurityBounds& first = service::security_bounds();
    const SecurityBounds& second = service::security_bounds();
    assert(&first == &second);
}

// Every one of the plan's own ten (minimum, maximum, approved_default)
// triples, checked directly against the plan text rather than just
// against whatever the accessor itself returns (which would be a
// tautology).
void test_security_bound_range_matches_plan_table() {
    struct Expected {
        SecurityBoundField field;
        uint32_t minimum;
        uint32_t maximum;
        uint32_t approved_default;
    };
    const Expected expectations[10] = {
        {SecurityBoundField::kCommissioningWindowSeconds, 60U, 600U, 600U},
        {SecurityBoundField::kJsonMaxBodyBytes, 512U, 2048U, 2048U},
        {SecurityBoundField::kJsonMaxDepth, 2U, 4U, 4U},
        {SecurityBoundField::kJsonMaxStringBytes, 64U, 512U, 512U},
        {SecurityBoundField::kJsonMaxKeys, 8U, 32U, 32U},
        {SecurityBoundField::kLoginAttemptsPerMinute, 1U, 5U, 5U},
        {SecurityBoundField::kCommandsPerMinute, 10U, 60U, 60U},
        {SecurityBoundField::kMutationsPerMinute, 1U, 5U, 5U},
        {SecurityBoundField::kFirmwareOpsPerHour, 1U, 2U, 2U},
        {SecurityBoundField::kAuditRingRecords, 32U, 128U, 128U},
    };
    for (const Expected& expected : expectations) {
        const auto range = service::security_bound_range(expected.field);
        assert(range.minimum == expected.minimum);
        assert(range.maximum == expected.maximum);
        assert(range.approved_default == expected.approved_default);
    }
}

void test_security_bound_value_in_range() {
    assert(service::security_bound_value_in_range(SecurityBoundField::kJsonMaxDepth, 2U));
    assert(service::security_bound_value_in_range(SecurityBoundField::kJsonMaxDepth, 3U));
    assert(service::security_bound_value_in_range(SecurityBoundField::kJsonMaxDepth, 4U));
    assert(!service::security_bound_value_in_range(SecurityBoundField::kJsonMaxDepth, 1U));
    assert(!service::security_bound_value_in_range(SecurityBoundField::kJsonMaxDepth, 5U));
}

}  // namespace

int main() {
    test_host_bounds_match_approved_defaults();
    test_fixed_non_configurable_constants();
    test_security_bounds_returns_a_stable_reference();
    test_security_bound_range_matches_plan_table();
    test_security_bound_value_in_range();
    return 0;
}
