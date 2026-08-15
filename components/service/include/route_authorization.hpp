/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "session_store.hpp"

namespace service {

// Plan S6 "Authorization and physical presence" #19, #23:
// #19: "Central middleware authenticates and authorizes before
//     request-body parsing/use-case invocation."
// #23: "Direct route access without capability returns non-leaky stable
//     errors and does not reveal private state."
//
// This is the middleware's DECISION logic only, kept independent of any
// concrete HTTP server type (same discipline session_security_policy.hpp
// already established) so it is host-testable without one. The actual
// HTTP-layer glue -- reading the session cookie/CSRF/Origin headers from a
// real httpd_req_t and calling this -- lives in
// `components/web_ui/web_v1_common.cpp`'s authorize_v1_request(), the one
// real call site for every v1 handler (plan #19's "central" framing).
//
// See capability.hpp's own header comment for why a Capability value is
// not itself branched on here: this single-admin-role system has no
// per-user capability subset to check yet, so the one real,
// currently-enforceable rule is simply "is there a valid, authenticated
// session" -- plus, for a state-changing request, CSRF+origin (plan #15).

enum class RouteAuthResult : uint8_t {
    kAllowed = 0,
    // No session cookie, or the named session does not exist / has
    // expired (idle or absolute timeout). Maps to HTTP 401 -- see
    // web_v1_common.hpp's ApiV1ErrorCode::kUnauthenticated.
    kUnauthenticated = 1,
    // The session itself is valid, but a state-changing request's CSRF
    // token did not match its session-bound value, or its Origin header
    // did not match this gateway's own origin. Maps to HTTP 403 -- see
    // ApiV1ErrorCode::kCsrfOrOriginInvalid. Deliberately distinct from
    // kUnauthenticated (plan #23's "non-leaky stable errors" still holds:
    // neither result echoes which specific check failed back to the
    // caller beyond this one bit, and both are equally terse JSON error
    // tokens -- see send_v1_auth_error()).
    kCsrfOrOriginInvalid = 2,
};

// Read-grade check: valid, non-expired session only. Used by every GET
// v1 handler that requires authentication. Extends the session's idle
// timeout on success (one "touch" per authenticated request), matching
// session_store_touch()'s own documented per-request-extension contract.
RouteAuthResult authorize_read_request(
    SessionStoreState* sessions, const char* session_id_hex, uint64_t now_ms) noexcept;

// Mutation-grade check (plan #15's "every state-changing browser
// request"): everything authorize_read_request() checks, plus a matching
// session-bound CSRF token and a same-origin Origin header. `sessions`
// is mutated (touched) only on a fully-allowed result -- a request that
// fails the CSRF/origin check does not get to extend its own session's
// idle timeout.
RouteAuthResult authorize_mutation_request(
    SessionStoreState* sessions, const char* session_id_hex, const char* csrf_token_hex_from_request,
    const char* request_origin, const char* expected_origin, uint64_t now_ms) noexcept;

}  // namespace service
