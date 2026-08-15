/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "route_authorization.hpp"
#include "session_security_policy.hpp"

namespace {

constexpr const char* kOrigin = "https://zigbee-gateway-abcdef.local";

}  // namespace

int main() {
    // --- authorize_read_request() ---

    // Null sessions pointer: fails closed, no crash.
    assert(
        service::authorize_read_request(nullptr, "anything", 1000ULL) == service::RouteAuthResult::kUnauthenticated);

    // No session cookie at all (null session_id_hex).
    {
        service::SessionStoreState state{};
        assert(
            service::authorize_read_request(&state, nullptr, 1000ULL) == service::RouteAuthResult::kUnauthenticated);
    }

    // Valid session -> allowed, and idle timeout gets extended (touch).
    {
        service::SessionStoreState state{};
        service::SessionRecord session{};
        assert(service::session_store_create(&state, 1000ULL, &session) == service::SessionCreateResult::kCreated);

        const uint64_t just_before_idle_timeout =
            1000ULL + (static_cast<uint64_t>(service::kSessionIdleTimeoutSeconds) * 1000ULL) - 1ULL;
        assert(
            service::authorize_read_request(&state, session.session_id_hex, just_before_idle_timeout) ==
            service::RouteAuthResult::kAllowed);

        // Because the read above touched the session, idle timeout is
        // measured from just_before_idle_timeout now, not the original
        // creation time -- still valid well past the original deadline.
        const uint64_t after_original_deadline =
            just_before_idle_timeout + (static_cast<uint64_t>(service::kSessionIdleTimeoutSeconds) * 1000ULL) - 1ULL;
        assert(
            service::authorize_read_request(&state, session.session_id_hex, after_original_deadline) ==
            service::RouteAuthResult::kAllowed);
    }

    // Expired session (idle timeout exceeded, never touched) -> rejected.
    {
        service::SessionStoreState state{};
        service::SessionRecord session{};
        assert(service::session_store_create(&state, 1000ULL, &session) == service::SessionCreateResult::kCreated);
        const uint64_t after_idle_timeout =
            1000ULL + (static_cast<uint64_t>(service::kSessionIdleTimeoutSeconds) * 1000ULL) + 1ULL;
        assert(
            service::authorize_read_request(&state, session.session_id_hex, after_idle_timeout) ==
            service::RouteAuthResult::kUnauthenticated);
    }

    // --- authorize_mutation_request() ---

    // Null sessions pointer: fails closed.
    assert(
        service::authorize_mutation_request(nullptr, "x", "y", kOrigin, kOrigin, 1000ULL) ==
        service::RouteAuthResult::kUnauthenticated);

    // Valid session, correct CSRF token, matching origin -> allowed.
    {
        service::SessionStoreState state{};
        service::SessionRecord session{};
        assert(service::session_store_create(&state, 1000ULL, &session) == service::SessionCreateResult::kCreated);
        assert(
            service::authorize_mutation_request(
                &state, session.session_id_hex, session.csrf_token_hex, kOrigin, kOrigin, 1000ULL) ==
            service::RouteAuthResult::kAllowed);
    }

    // Valid session, WRONG CSRF token -> kCsrfOrOriginInvalid, not
    // kUnauthenticated (plan #23: the session itself is real, only the
    // CSRF/origin check failed).
    {
        service::SessionStoreState state{};
        service::SessionRecord session{};
        assert(service::session_store_create(&state, 1000ULL, &session) == service::SessionCreateResult::kCreated);
        assert(
            service::authorize_mutation_request(
                &state, session.session_id_hex, "0000000000000000000000000000000000000000000000000000000000000000",
                kOrigin, kOrigin, 1000ULL) == service::RouteAuthResult::kCsrfOrOriginInvalid);
    }

    // Valid session, correct CSRF, WRONG origin -> kCsrfOrOriginInvalid.
    {
        service::SessionStoreState state{};
        service::SessionRecord session{};
        assert(service::session_store_create(&state, 1000ULL, &session) == service::SessionCreateResult::kCreated);
        assert(
            service::authorize_mutation_request(
                &state, session.session_id_hex, session.csrf_token_hex, "https://evil.example", kOrigin, 1000ULL) ==
            service::RouteAuthResult::kCsrfOrOriginInvalid);
    }

    // Valid session, no Origin header at all -> fails closed (plan #16:
    // no benefit-of-the-doubt for a missing Origin on a mutation).
    {
        service::SessionStoreState state{};
        service::SessionRecord session{};
        assert(service::session_store_create(&state, 1000ULL, &session) == service::SessionCreateResult::kCreated);
        assert(
            service::authorize_mutation_request(
                &state, session.session_id_hex, session.csrf_token_hex, nullptr, kOrigin, 1000ULL) ==
            service::RouteAuthResult::kCsrfOrOriginInvalid);
    }

    // A CSRF/origin failure does not itself extend the session's idle
    // timeout (only a fully-allowed request may touch).
    {
        service::SessionStoreState state{};
        service::SessionRecord session{};
        assert(service::session_store_create(&state, 1000ULL, &session) == service::SessionCreateResult::kCreated);
        (void)service::authorize_mutation_request(
            &state, session.session_id_hex, "wrong-token", kOrigin, kOrigin, 1000ULL);
        const uint64_t after_idle_timeout =
            1000ULL + (static_cast<uint64_t>(service::kSessionIdleTimeoutSeconds) * 1000ULL) + 1ULL;
        assert(
            service::authorize_read_request(&state, session.session_id_hex, after_idle_timeout) ==
            service::RouteAuthResult::kUnauthenticated);
    }

    // Unknown session id -> kUnauthenticated, not kCsrfOrOriginInvalid
    // (there is no session to have a CSRF mismatch against).
    {
        service::SessionStoreState state{};
        assert(
            service::authorize_mutation_request(&state, "0123456789abcdef0123456789abcdef", "token", kOrigin, kOrigin, 1000ULL) ==
            service::RouteAuthResult::kUnauthenticated);
    }

    return 0;
}
