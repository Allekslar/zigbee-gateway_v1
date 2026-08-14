/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "session_store.hpp"

namespace service {

// Plan S6 "HTTPS and sessions" #14, #15, #16:
// #14: "Use cookie name `zgw_session` with `Secure`, `HttpOnly`,
//     `SameSite=Strict` and path `/api/v1`. CSRF token is a separate
//     256-bit session-bound value returned only by the authenticated
//     session endpoint."
// #15: "Require session-bound CSRF token and same-origin validation for
//     every state-changing browser request."
// #16: "Reject permissive CORS; default to same-origin only."
//
// Builds on session_store.hpp's SessionStoreState. Nothing in this
// repository calls any function below yet -- the actual HTTP
// request/response plumbing (reading a request's Cookie/Origin headers,
// writing a Set-Cookie response header) belongs to the production HTTPS
// listener, plan #7, a separate not-yet-implemented S6 sub-slice. This
// is the policy decision logic only, deliberately kept independent of
// any concrete HTTP server type so it is host-testable without one.

inline constexpr const char* kSessionCookieName = "zgw_session";  // plan #14, exact name
inline constexpr const char* kSessionCookiePath = "/api/v1";      // plan #14, exact path

// Builds the exact Set-Cookie header value plan #14 names:
// "zgw_session=<session_id_hex>; Secure; HttpOnly; SameSite=Strict;
// Path=/api/v1". Returns false (out untouched) if `session_id_hex` is
// null/empty or `out_capacity` is too small for the formatted header.
bool build_session_cookie_header(const char* session_id_hex, char* out, uint32_t out_capacity) noexcept;

// The logout-response counterpart: a Set-Cookie header value that
// immediately clears the zgw_session cookie via `Max-Age=0` (the only
// mechanism a browser actually recognizes as "delete this cookie now" --
// an empty value alone does not delete it). Same name/attributes as
// build_session_cookie_header() so the browser clears the right cookie;
// `Max-Age=0` is a necessary technique for this specific clear-cookie
// response, not a change to the general session cookie's own attribute
// set, which remains exactly plan #14's four named attributes.
bool build_session_cookie_clear_header(char* out, uint32_t out_capacity) noexcept;

// Plan #15's CSRF half: true iff `session_id_hex` names a valid,
// non-expired session (as of `now_ms`) whose bound CSRF token exactly
// matches `csrf_token_hex_from_request`, compared in constant time
// (never short-circuits on the first mismatched byte -- the same
// timing-attack-resistance discipline admin_verifier.cpp's password
// comparison already established). False for a null/empty/wrong-length
// `csrf_token_hex_from_request`, or an unknown/expired session.
bool session_csrf_token_matches(
    const SessionStoreState& state, const char* session_id_hex, const char* csrf_token_hex_from_request,
    uint64_t now_ms) noexcept;

// Plan #15's same-origin half: true iff `request_origin` (the raw value
// of a request's `Origin` header, e.g.
// "https://zigbee-gateway-abcdef.local") case-insensitively equals
// `expected_origin` (this gateway's own origin -- scheme + production
// mDNS host, built by a future caller once plan #7/#9's real HTTPS
// listener and mDNS host derivation exist). A null/empty
// `request_origin` -- no `Origin` header present at all -- is treated as
// a FAILURE (fail closed): a same-origin browser always sends `Origin`
// on a state-changing (non-GET) request, so its absence gets no
// benefit-of-the-doubt pass, matching plan #16's "default to same-origin
// only" text. `expected_origin` itself is never treated as optional --
// a null/empty `expected_origin` also fails closed (a caller passing an
// unconfigured expected origin is a programming error, not a normal
// missing-value case).
bool is_same_origin_request(const char* request_origin, const char* expected_origin) noexcept;

// Plan #16's CORS policy, exposed as one centralized, grep-able decision
// point rather than left implicit at each future handler: this project's
// production listener issues no `Access-Control-Allow-Origin` header for
// any cross-origin request -- always returns false. A future CORS-
// handling call site should consult this rather than ever echoing a
// request's `Origin` header back (the permissive anti-pattern plan #16
// names).
bool cors_cross_origin_allowed() noexcept;

}  // namespace service
