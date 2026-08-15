/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "route_authorization.hpp"

#include "session_security_policy.hpp"

namespace service {

RouteAuthResult authorize_read_request(
    SessionStoreState* sessions, const char* session_id_hex, uint64_t now_ms) noexcept {
    if (sessions == nullptr || !session_store_is_valid(*sessions, session_id_hex, now_ms)) {
        return RouteAuthResult::kUnauthenticated;
    }
    (void)session_store_touch(sessions, session_id_hex, now_ms);
    return RouteAuthResult::kAllowed;
}

RouteAuthResult authorize_mutation_request(
    SessionStoreState* sessions, const char* session_id_hex, const char* csrf_token_hex_from_request,
    const char* request_origin, const char* expected_origin, uint64_t now_ms) noexcept {
    if (sessions == nullptr || !session_store_is_valid(*sessions, session_id_hex, now_ms)) {
        return RouteAuthResult::kUnauthenticated;
    }
    if (!session_csrf_token_matches(*sessions, session_id_hex, csrf_token_hex_from_request, now_ms) ||
        !is_same_origin_request(request_origin, expected_origin)) {
        return RouteAuthResult::kCsrfOrOriginInvalid;
    }
    (void)session_store_touch(sessions, session_id_hex, now_ms);
    return RouteAuthResult::kAllowed;
}

}  // namespace service
