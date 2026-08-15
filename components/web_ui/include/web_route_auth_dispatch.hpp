/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "capability.hpp"
#include "web_handler_common.hpp"

namespace web_ui {

// Plan S6 "Authorization and physical presence" #19: "Central middleware
// authenticates and authorizes before request-body parsing/use-case
// invocation." This is that middleware's HTTP-dispatch half: a thin
// registration-time wrapper around httpd_register_uri_handler(), NOT a
// change to any existing handler function's own body.
//
// Design rationale (why a dispatch wrapper rather than an auth check
// inline at the top of each of the ~13 already-built v1 handlers):
// every existing v1 handler's own host test calls that handler function
// DIRECTLY (`devices_get_handler_v1(&req)`), never through
// httpd_register_uri_handler()/a real dispatch path -- that's how this
// project's v1 handlers have been host-testable at all without a real
// esp_http_server. An inline auth check in the handler body would have
// forced every one of those pre-existing tests to first fabricate a
// valid authenticated session, purely to keep testing business logic
// that has nothing to do with authentication -- conflating two concerns
// that are better kept separate. Wrapping only the REGISTRATION call
// means: (1) existing handler bodies and their existing tests are
// unchanged, byte-for-byte; (2) the actual real-HTTP dispatch path (the
// only path a real client ever reaches) is fully gated; (3) the
// wrapper's own auth-enforcement behavior gets its own small, focused
// test suite instead of being duplicated across every handler's tests.
//
// Fixed-capacity by design (no malloc/new) -- registration happens once,
// at boot, for a small, known-in-advance route count.
inline constexpr uint32_t kMaxAuthenticatedRoutes = 24U;

// Registers `uri_def` (its own .handler/.user_ctx are the REAL handler
// and WebRouteContext*, exactly as every existing v1 registration already
// builds them) such that, at real request time, authorize_v1_request()
// must return service::RouteAuthResult::kAllowed before the real handler
// is ever invoked -- otherwise sends the golden 401/403 response itself
// and never calls through. Returns false (nothing registered) if the
// fixed binding table is full or any argument is invalid.
bool register_authenticated_uri_handler_v1(
    void* server_handle, const httpd_uri_t& uri_def, service::Capability required) noexcept;

// Test-only: resets the fixed binding table between test cases. Never
// called from production code (WebServer::start() runs exactly once per
// boot).
void reset_authenticated_route_bindings_for_test() noexcept;

}  // namespace web_ui
