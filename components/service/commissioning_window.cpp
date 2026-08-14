/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "commissioning_window.hpp"

#include "admin_verifier.hpp"
#include "security_bounds.hpp"

namespace service {

void commissioning_window_start(
    CommissioningWindowState* state, CommissioningWindowTrigger trigger, uint64_t now_ms) noexcept {
    if (state == nullptr) {
        return;
    }
    state->active = true;
    state->started_at_ms = now_ms;
    state->trigger = trigger;
}

bool commissioning_window_is_active(const CommissioningWindowState& state, uint64_t now_ms) noexcept {
    if (!state.active) {
        return false;
    }
    // now_ms < started_at_ms would mean the monotonic clock went
    // backwards -- impossible for hal_time_now_ms()'s own contract, but
    // guarded defensively so this never underflows into a huge elapsed
    // value and reports "active" when it should not.
    if (now_ms < state.started_at_ms) {
        return false;
    }
    const uint64_t elapsed_ms = now_ms - state.started_at_ms;
    const uint64_t window_ms = (uint64_t)security_bounds().commissioning_window_seconds * 1000ULL;
    return elapsed_ms < window_ms;
}

void commissioning_window_stop(CommissioningWindowState* state) noexcept {
    if (state == nullptr) {
        return;
    }
    state->active = false;
}

bool commissioning_window_first_boot_policy_applies() noexcept {
    return !admin_credential_exists();
}

}  // namespace service
