/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace service {

// Plan S6 "Provisioning and credentials" #3: "Commissioning mode starts
// only after first-boot policy or trusted physical button action and
// expires according to ZGW_COMMISSIONING_WINDOW_SECONDS; approved
// default is 600 seconds and every production value must remain inside
// FD-13."
//
// This module is the window's start/query/expire state machine only. It
// does not itself detect "first boot" (that is
// commissioning_window_first_boot_policy_applies() below, built on
// admin_verifier.hpp's admin_credential_exists() -- the plan's own
// Migration/compatibility text: "Upgrade boots into restricted migration
// mode if no admin credential exists") or "trusted physical button
// action" (no button-driver plumbing exists yet in this repository; a
// caller with a real button interrupt/task would call
// commissioning_window_start() with kTrustedButtonAction once it has
// one). Nothing yet gates any actual commissioning-only behavior (e.g.
// which HTTP/MQTT operations are allowed) on
// commissioning_window_is_active() -- that gating is a later S6
// sub-slice's job, matching this repository's established
// "port/state-machine defined ahead of its full pipeline" discipline.
//
// Every duration function below takes `now_ms` explicitly (a monotonic
// hal_time_now_ms() reading) rather than reading the clock itself --
// matching security_bounds()'s own pure-accessor testability discipline,
// this lets host tests control elapsed time deterministically instead of
// sleeping for real seconds.

enum class CommissioningWindowTrigger : uint8_t {
    kFirstBootPolicy = 0,
    kTrustedButtonAction = 1,
};

struct CommissioningWindowState {
    bool active{false};
    uint64_t started_at_ms{0};
    CommissioningWindowTrigger trigger{CommissioningWindowTrigger::kFirstBootPolicy};
};

// Starts (or restarts) the commissioning window, recording `now_ms` as
// its new start time. Idempotent and always succeeds: a second trusted
// button press while the window is already active renews its expiry
// from `now_ms` rather than being rejected -- the plan names no
// "already active" error case, and rejecting a renewed physical button
// press would work against the feature's own purpose (giving an
// installer a fresh full window on demand).
void commissioning_window_start(
    CommissioningWindowState* state, CommissioningWindowTrigger trigger, uint64_t now_ms) noexcept;

// True iff `state.active` and fewer than
// security_bounds().commissioning_window_seconds have elapsed since
// `state.started_at_ms` as of `now_ms`. Does not mutate `state` -- a
// caller that wants the expired flag cleared should call
// commissioning_window_stop() once this returns false (this module
// favors explicit state transitions over a query function with side
// effects).
bool commissioning_window_is_active(const CommissioningWindowState& state, uint64_t now_ms) noexcept;

// Explicitly deactivates the window (e.g. once
// commissioning_window_is_active() has returned false, or an
// administrator enrolls and the window should close early). Idempotent.
void commissioning_window_stop(CommissioningWindowState* state) noexcept;

// Plan #3's "first-boot policy" trigger condition, verbatim from the
// plan's Migration/compatibility text: true iff no admin credential
// exists yet (admin_verifier.hpp's admin_credential_exists()).
bool commissioning_window_first_boot_policy_applies() noexcept;

}  // namespace service
