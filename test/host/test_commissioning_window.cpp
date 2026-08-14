/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "admin_verifier.hpp"
#include "commissioning_window.hpp"
#include "hal_security_state_test.h"
#include "security_bounds.hpp"

// commissioning_window_first_boot_policy_applies() reads
// admin_credential_exists(), which shares hal_nvs.c's single static
// in-process host store with test_admin_verifier.cpp -- but that is a
// separate test executable/process, so no cross-file ordering concern.
// Within this file, the "no admin credential yet" tests still must run
// before the one test that stores a verifier.

namespace {

using service::CommissioningWindowState;
using service::CommissioningWindowTrigger;

void test_first_boot_policy_applies_before_any_admin_credential_exists() {
    assert(service::commissioning_window_first_boot_policy_applies());
}

void test_window_is_active_immediately_after_start() {
    CommissioningWindowState state{};
    service::commissioning_window_start(&state, CommissioningWindowTrigger::kFirstBootPolicy, 1000ULL);
    assert(service::commissioning_window_is_active(state, 1000ULL));
}

void test_window_is_active_just_before_expiry() {
    CommissioningWindowState state{};
    service::commissioning_window_start(&state, CommissioningWindowTrigger::kTrustedButtonAction, 0ULL);
    const uint64_t window_ms = (uint64_t)service::security_bounds().commissioning_window_seconds * 1000ULL;
    assert(service::commissioning_window_is_active(state, window_ms - 1ULL));
}

void test_window_is_inactive_at_and_after_expiry() {
    CommissioningWindowState state{};
    service::commissioning_window_start(&state, CommissioningWindowTrigger::kTrustedButtonAction, 0ULL);
    const uint64_t window_ms = (uint64_t)service::security_bounds().commissioning_window_seconds * 1000ULL;
    assert(!service::commissioning_window_is_active(state, window_ms));
    assert(!service::commissioning_window_is_active(state, window_ms + 1000ULL));
}

void test_window_is_inactive_before_being_started() {
    const CommissioningWindowState state{};  // default-constructed, never started
    assert(!service::commissioning_window_is_active(state, 5000ULL));
}

void test_window_defensively_reports_inactive_if_now_precedes_start() {
    CommissioningWindowState state{};
    service::commissioning_window_start(&state, CommissioningWindowTrigger::kFirstBootPolicy, 10000ULL);
    assert(!service::commissioning_window_is_active(state, 5000ULL));  // now_ms < started_at_ms
}

void test_commissioning_window_stop_deactivates() {
    CommissioningWindowState state{};
    service::commissioning_window_start(&state, CommissioningWindowTrigger::kFirstBootPolicy, 0ULL);
    assert(service::commissioning_window_is_active(state, 0ULL));
    service::commissioning_window_stop(&state);
    assert(!service::commissioning_window_is_active(state, 0ULL));
}

void test_restarting_the_window_renews_its_expiry() {
    CommissioningWindowState state{};
    const uint64_t window_ms = (uint64_t)service::security_bounds().commissioning_window_seconds * 1000ULL;
    service::commissioning_window_start(&state, CommissioningWindowTrigger::kFirstBootPolicy, 0ULL);
    assert(!service::commissioning_window_is_active(state, window_ms));  // expired relative to t=0

    // A second trusted button press at t=window_ms renews the window.
    service::commissioning_window_start(&state, CommissioningWindowTrigger::kTrustedButtonAction, window_ms);
    assert(service::commissioning_window_is_active(state, window_ms));
    assert(service::commissioning_window_is_active(state, window_ms + window_ms - 1ULL));
}

void test_first_boot_policy_does_not_apply_once_an_admin_credential_exists() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    service::AdminVerifierRecord record{};
    assert(service::create_admin_verifier("correct horse battery staple", &record));
    assert(service::set_stored_admin_verifier(record) == service::SecureStorageWriteResult::kWritten);

    assert(!service::commissioning_window_first_boot_policy_applies());

    hal_security_state_reset_mock_flash_encryption_enabled();
}

}  // namespace

int main() {
    test_first_boot_policy_applies_before_any_admin_credential_exists();
    test_window_is_active_immediately_after_start();
    test_window_is_active_just_before_expiry();
    test_window_is_inactive_at_and_after_expiry();
    test_window_is_inactive_before_being_started();
    test_window_defensively_reports_inactive_if_now_precedes_start();
    test_commissioning_window_stop_deactivates();
    test_restarting_the_window_renews_its_expiry();
    test_first_boot_policy_does_not_apply_once_an_admin_credential_exists();
    return 0;
}
