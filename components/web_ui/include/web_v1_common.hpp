/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "capability.hpp"
#include "route_authorization.hpp"
#include "web_handler_common.hpp"

namespace web_ui {

// schema_version for every v1 response that is a durable client contract
// (plan S4 HTTP #5).
constexpr uint32_t kApiV1SchemaVersion = 1U;

// Stable v1 error vocabulary (plan S4 HTTP #4) plus the golden HTTP
// status/error matrix (#9): every v1 handler must resolve its failure to
// exactly one of these, never a bare/generic status.
enum class ApiV1ErrorCode : uint8_t {
    kDeviceNotFound,          // 404
    kIdentityUnresolved,      // 404
    kOperationNotFound,       // 404 -- unknown/expired GET /api/v1/operations/{operation_id}
    kDeviceOffline,           // 409
    kStaleLocator,            // 409
    kCapabilityUnavailable,   // 503
    kNoCapacity,              // 503
    kConflict,                // 409
    kInvalidRequest,          // 400
    kLegacyMutationDisabled,  // 410
    // Plan S6 "Authorization and physical presence" #19/#23: no session
    // cookie, or the named session does not exist / has expired.
    kUnauthenticated,         // 401
    // Plan #15/#23: session is valid, but a state-changing request's CSRF
    // token or Origin header did not check out.
    kCsrfOrOriginInvalid,     // 403
    // Plan #20/#21: no valid, matching, unconsumed physical-presence
    // grant exists for this session/action class.
    kPhysicalPresenceRequired,  // 403
    // Plan #17's `provisioning/enroll` (commissioning mode not active) or
    // `auth/password`/`provisioning/enroll` (submitted proof-of-
    // possession/current-password did not match).
    kProvisioningNotActive,     // 409
};

const char* api_v1_error_token(ApiV1ErrorCode code) noexcept;
const char* api_v1_error_status(ApiV1ErrorCode code) noexcept;

// Sends {"schema_version":1,"error":"<token>"} with the status the golden
// matrix assigns to `code`.
esp_err_t send_api_v1_error(httpd_req_t* req, ApiV1ErrorCode code) noexcept;

// Sends 202 Accepted with {"schema_version":1,"accepted":true,"request_id":N}
// -- the golden matrix's "newly accepted asynchronous operation" response,
// used by every v1 mutation that has a pollable request_id/correlation_id.
esp_err_t send_api_v1_accepted(httpd_req_t* req, uint32_t request_id) noexcept;

// Sends 200 OK with {"schema_version":1,"accepted":true} -- for v1
// mutations that queue work with no pollable request_id (config PATCH,
// reporting PUT), matching the legacy handlers' synchronous-acceptance
// contract for the same operations.
esp_err_t send_api_v1_ok(httpd_req_t* req) noexcept;

// Canonical DeviceId text length (matches the Core layer's device-id hex
// length constant, redefined here rather than included so this adapter
// layer never has to name a Core-namespaced symbol -- INV-M030).
constexpr std::size_t kApiV1DeviceIdHexLength = 16U;

// Parses "<prefix><16 lowercase hex chars><suffix>" out of a request URI
// (prefix/suffix are literal, exact matches; suffix may be "" to mean "the
// device_id is the final path segment"). Rejects anything that is not
// exactly kApiV1DeviceIdHexLength lowercase hex characters -- callers get a
// clean malformed-request signal, never a silently truncated identity.
// out_hex_capacity must be >= kApiV1DeviceIdHexLength + 1.
bool extract_uri_device_id_hex(
    const char* uri, const char* prefix, const char* suffix, char* out_hex, std::size_t out_hex_capacity) noexcept;

// Parses "<prefix><16 hex chars>/reporting/<endpoint>/<cluster_id>" -- the
// one v1 route with a device_id AND two further decimal path segments.
bool extract_uri_device_id_and_reporting_segments(
    const char* uri,
    const char* prefix,
    char* out_hex,
    std::size_t out_hex_capacity,
    uint32_t* out_endpoint,
    uint32_t* out_cluster_id) noexcept;

// Parses "<prefix><decimal digits>" -- the whole remainder after prefix
// must be one or more decimal digits with no separator/suffix (used by
// GET /api/v1/operations/{operation_id}). Rejects empty, non-digit,
// leading-zero-only or out-of-range (> UINT32_MAX) segments.
bool extract_uri_decimal_segment(const char* uri, const char* prefix, uint32_t* out_value) noexcept;

// Plan S6 "Authorization and physical presence" #19: the one real call
// site every v1 handler that requires authentication uses, first thing,
// before any request-body parsing/use-case invocation. Reads the
// `zgw_session` cookie (session_security_policy.hpp's kSessionCookieName)
// via the real esp_http_server `httpd_req_get_cookie_val()`; for a
// state-changing request (`req->method != HTTP_GET`) also reads the
// `X-CSRF-Token` and `Origin` request headers and applies
// service::authorize_mutation_request()'s CSRF+origin check --
// otherwise applies service::authorize_read_request() (session validity
// only). `context->sessions`/`context->expected_origin` must be non-null
// (WebServer::start() wires both before any route is registered).
// `required` is accepted for the call site's own self-documentation (see
// capability.hpp) but does not change this function's decision -- see
// that header's comment for why.
service::RouteAuthResult authorize_v1_request(
    httpd_req_t* req, WebRouteContext* context, service::Capability required) noexcept;

// Sends the golden-matrix 401/403 response for a non-kAllowed
// authorize_v1_request() result -- a terse, stable, non-leaky
// {"schema_version":1,"error":"<token>"} body (plan #23), same shape and
// call convention as send_api_v1_error() above.
esp_err_t send_v1_auth_error(httpd_req_t* req, service::RouteAuthResult result) noexcept;

}  // namespace web_ui
