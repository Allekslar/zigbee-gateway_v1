/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace service {

// Plan S6 "HTTPS and sessions" #13: "Add a bounded store of four
// concurrent sessions. Each session has a 15-minute idle timeout, 8-hour
// absolute timeout, logout/revocation and boot invalidation."
//
// State is RAM-only by design, deliberately not persisted to NVS despite
// nvs_namespace_registry.cpp's kSessionSeed entry (added speculatively
// during S5, "session-token seed material for S6's bounded session
// store") -- see this sub-slice's evidence file for the explicit
// deviation this is. Plan #13's own "boot invalidation" trigger is
// satisfied for free by RAM-only storage: a SessionStoreState constructed
// fresh at boot starts empty, no explicit invalidation step is needed.
// Session IDs and CSRF tokens are drawn directly from hardware RNG via
// provisioning_secrets.hpp's generate_random_secret_hex() -- the exact
// reusable primitive plan #6 ("Generate session and CSRF secrets from
// hardware RNG") built for this exact later consumer -- rather than
// derived from any persisted seed/signing key.
//
// Every duration function below takes `now_ms` explicitly (a monotonic
// hal_time_now_ms() reading) rather than reading the clock itself --
// the same testability discipline commissioning_window.hpp already
// established this sub-slice.
//
// Nothing in this repository creates a real SessionStoreState or calls
// any function below yet -- the actual `POST /api/v1/auth/login` route
// (plan #17) that would create a session, and the production HTTPS
// listener (plan #7) that would host it, are both separate, not-yet-
// implemented S6 sub-slices. This is the session store's own state
// machine only, matching this project's established "port/state-machine
// defined ahead of its full pipeline" precedent.

inline constexpr uint32_t kMaxConcurrentSessions = 4U;  // plan #13, exact count

inline constexpr uint32_t kSessionIdBytes = 16U;   // 128 bits
inline constexpr uint32_t kSessionIdHexChars = kSessionIdBytes * 2U;
inline constexpr uint32_t kCsrfTokenBytes = 32U;   // 256 bits -- plan #14's exact text
inline constexpr uint32_t kCsrfTokenHexChars = kCsrfTokenBytes * 2U;

inline constexpr uint32_t kSessionIdleTimeoutSeconds = 15U * 60U;          // plan #13: 15-minute idle timeout
inline constexpr uint32_t kSessionAbsoluteTimeoutSeconds = 8U * 60U * 60U;  // plan #13: 8-hour absolute timeout

struct SessionRecord {
    bool in_use{false};
    char session_id_hex[kSessionIdHexChars + 1U]{};
    char csrf_token_hex[kCsrfTokenHexChars + 1U]{};
    uint64_t created_at_ms{0};
    uint64_t last_active_at_ms{0};
};

struct SessionStoreState {
    SessionRecord sessions[kMaxConcurrentSessions]{};
};

enum class SessionCreateResult : uint8_t {
    kCreated = 0,
    // The store already holds kMaxConcurrentSessions live (non-expired)
    // sessions. Deliberately does NOT evict the oldest session to make
    // room -- plan #13's "bounded store of four" is treated as a hard
    // cap, not a sliding window, so an already-authenticated session is
    // never involuntarily logged out just because a new login occurred.
    // A caller wanting to log in a 5th time must first log out (or wait
    // for one to idle/absolute-timeout) -- see this sub-slice's evidence
    // file for the full rationale.
    kFull = 1,
    // hal_random_fill_bytes() (via generate_random_secret_hex()) failed.
    kRngFailed = 2,
};

// Creates a new session: draws a fresh session ID and CSRF token from
// hardware RNG, records `now_ms` as both created_at_ms and
// last_active_at_ms. Runs an implicit expiry sweep first (any already-
// expired slot is freed before capacity is checked), so an expired
// session never blocks a new login. `out_new_session` receives a copy of
// the created record (including the caller-needed session ID and CSRF
// token) -- untouched on any non-kCreated result.
SessionCreateResult session_store_create(
    SessionStoreState* state, uint64_t now_ms, SessionRecord* out_new_session) noexcept;

// True iff a session with `session_id_hex` exists, is in_use, and has
// exceeded neither its 15-minute idle timeout nor its 8-hour absolute
// timeout as of `now_ms`. Does not mutate state or extend the idle
// timeout -- see session_store_touch() below for that. False for a
// null/empty `session_id_hex`.
bool session_store_is_valid(const SessionStoreState& state, const char* session_id_hex, uint64_t now_ms) noexcept;

// Updates last_active_at_ms to `now_ms` for a valid, non-expired session
// -- call once per authenticated request to extend the idle timeout.
// Returns false (no-op) if the session does not exist or is already
// expired (an expired session must log in again, never silently revived
// by a later touch -- the absolute timeout in particular must never be
// extended by touching).
bool session_store_touch(SessionStoreState* state, const char* session_id_hex, uint64_t now_ms) noexcept;

// Explicit logout/revocation (plan #13's own "logout/revocation"
// trigger). Idempotent: revoking an already-absent/already-revoked
// session is a no-op, not an error -- matches
// secure_storage_erase()'s own established idempotent-erase convention.
void session_store_revoke(SessionStoreState* state, const char* session_id_hex) noexcept;

// Looks up the CSRF token bound to a session -- the read side of plan
// #15's "session-bound CSRF token" check (session_csrf_token_matches()
// in session_security_policy.hpp is the actual comparison). Returns
// false (out untouched) if the session does not exist, has expired as of
// `now_ms`, or `out_capacity` is too small.
bool session_store_get_csrf_token(
    const SessionStoreState& state, const char* session_id_hex, uint64_t now_ms, char* out_csrf_token_hex,
    uint32_t out_capacity) noexcept;

// Sweeps every expired session out of the store (idle or absolute
// timeout as of `now_ms`). session_store_is_valid()/_touch() already
// independently enforce expiry per-lookup, so this is an explicit
// maintenance convenience (e.g. a periodic background task), not itself
// a correctness requirement.
void session_store_sweep_expired(SessionStoreState* state, uint64_t now_ms) noexcept;

}  // namespace service
