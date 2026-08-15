/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "session_store.hpp"

namespace service {

// Plan S6 "Authorization and physical presence" #20, #21:
// #20: "join, remove, Wi-Fi credential replacement, certificate
//     rotation, OTA, RCP and factory reset require a recent one-time
//     physical-presence grant."
// #21: "Grant is created only from trusted GPIO/button event, has
//     maximum 60-second lifetime, is bound to gateway boot/session/
//     action class and is consumed once."
//
// Every function takes `now_ms` explicitly (a monotonic hal_time_now_ms()
// reading) rather than reading the clock itself, matching
// commissioning_window.hpp/session_store.hpp's own testability
// discipline.

inline constexpr uint32_t kPhysicalPresenceGrantMaxLifetimeSeconds = 60U;  // plan #21, exact

// The seven action classes plan #20 names verbatim, plus two more real
// routes plan #17 independently requires "recent physical presence" for
// (`POST /api/v1/provisioning/enroll`'s "proof of possession and
// physical presence", `POST /api/v1/auth/password`'s "current credential
// and recent physical presence") without listing them in #20's own
// seven-item enumeration. Grouping all nine under the one mechanism #21
// describes -- rather than leaving the two #17-only routes ungated (which
// would contradict #17's own explicit text) or forcing them into an
// existing, semantically-wrong class -- is a real, documented judgment
// call, not a plan-text-only enumeration.
enum class PhysicalPresenceActionClass : uint8_t {
    kJoinDevice = 0,
    kRemoveDevice = 1,
    kWifiCredentialReplacement = 2,
    kCertificateRotation = 3,
    kOta = 4,
    kRcpUpdate = 5,
    kFactoryReset = 6,
    // plan #17's `POST /api/v1/provisioning/enroll`, not one of #20's own
    // seven -- see the enum's own top comment.
    kProvisioningEnroll = 7,
    // plan #17's `POST /api/v1/auth/password`, likewise not one of #20's
    // own seven.
    kAdminPasswordChange = 8,
};

struct PhysicalPresenceGrantState {
    bool active{false};
    uint64_t created_at_ms{0};
    // True for a grant created by physical_presence_grant_create_from_
    // button() -- see that function's own comment for why "ambient"
    // (matches any action class, any session) is the only usable
    // semantics for a real trusted-button-event grant. False (the
    // default) for a grant created by physical_presence_grant_create()
    // with an explicit class/session, the finer-grained form kept for a
    // possible future two-phase arm/confirm flow (not built) and used
    // directly by this module's own tests.
    bool ambient{false};
    PhysicalPresenceActionClass action_class{PhysicalPresenceActionClass::kJoinDevice};
    // Plan #21's "bound to ... session": the exact session (by session ID
    // hex, session_store.hpp's own kSessionIdHexChars length) the grant
    // was created for. A grant created for one session can never be
    // consumed by a different one, even if both are otherwise valid.
    // Empty ("") for a grant not tied to any particular session. Ignored
    // entirely when `ambient` is true.
    char session_id_hex[kSessionIdHexChars + 1U]{};
};

// Creates (or replaces) a physical-presence grant scoped to exactly
// `action_class`/`session_id_hex` (copied; pass nullptr or "" for a grant
// not tied to a particular session), recording `now_ms` as its start.
// Plan #21's "bound to gateway boot" is satisfied for free the same way
// session_store.hpp's own "boot invalidation" is: this state is RAM-only,
// so a PhysicalPresenceGrantState constructed fresh at boot starts
// inactive -- no explicit reboot-invalidation step is needed. Always
// succeeds and always overwrites any still-active grant (a second
// trusted button press replaces, never stacks with, an earlier one --
// mirroring commissioning_window_start()'s own idempotent-restart
// precedent: the plan names no "already active" error case for either).
void physical_presence_grant_create(
    PhysicalPresenceGrantState* state, PhysicalPresenceActionClass action_class, const char* session_id_hex,
    uint64_t now_ms) noexcept;

// The real trusted-GPIO/button-event constructor (plan #21's own named
// trigger) -- creates an AMBIENT grant, matching any action class and any
// session for the next physical_presence_grant_consume() call within the
// window. A single physical button cannot itself communicate which of
// plan #20's nine action classes an installer intends to perform, or
// which browser tab/session they will submit it from -- the real,
// physically-present-installer flow this enables is "press the button on
// the device, then within 60 seconds perform the sensitive action from
// the web UI", the same "prove presence first, act second" shape common
// WPS-style pairing buttons already use. Otherwise identical to
// physical_presence_grant_create() (replaces any still-active grant,
// records `now_ms`).
void physical_presence_grant_create_from_button(PhysicalPresenceGrantState* state, uint64_t now_ms) noexcept;

// True iff `state.active`, fewer than
// kPhysicalPresenceGrantMaxLifetimeSeconds have elapsed since
// `state.created_at_ms` as of `now_ms`, and -- unless the grant is
// `ambient` (see physical_presence_grant_create_from_button()), in which
// case both of the following are skipped entirely -- `action_class`
// matches exactly what the grant was created for and `session_id_hex`
// matches the grant's own bound session (case-sensitive; both null/empty
// counts as a match, for a grant deliberately not tied to any session).
// A monotonic clock that appears to have gone backwards (`now_ms` before
// `created_at_ms`) is treated as expired, the same defensive convention
// session_store.cpp's own is_expired() already established. Does not
// mutate `state` or consume the grant -- see
// physical_presence_grant_consume() below for the actual one-time-use
// semantics.
bool physical_presence_grant_is_valid(
    const PhysicalPresenceGrantState& state, PhysicalPresenceActionClass action_class, const char* session_id_hex,
    uint64_t now_ms) noexcept;

// Plan #21's "is consumed once": atomically checks validity (exactly
// physical_presence_grant_is_valid()'s own rule) and, only on success,
// deactivates the grant so no second action can ever reuse it -- even an
// identical second call one millisecond later fails. Returns true iff the
// grant was valid and has now been consumed; `state` is left unchanged on
// a false return (an invalid/expired/mismatched grant is never
// half-consumed).
bool physical_presence_grant_consume(
    PhysicalPresenceGrantState* state, PhysicalPresenceActionClass action_class, const char* session_id_hex,
    uint64_t now_ms) noexcept;

}  // namespace service
