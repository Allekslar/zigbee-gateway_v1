/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// Plan S6 "Authorization and physical presence" #17 (basic slice only):
// POST /api/v1/auth/login, POST /api/v1/auth/logout,
// GET /api/v1/auth/session. The other #17 routes (provisioning/enroll,
// auth/password, certificate rotation, factory-reset) all need the
// not-yet-built physical-presence grant (#20-22) or later infrastructure
// (FD-17 rotation) and are deliberately not added here -- see
// docs/security/CONTROL_PLANE_SECURITY.md's own section for this
// sub-slice.

#include "web_routes.hpp"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#endif
#include "admin_verifier.hpp"
#include "hal_time.h"
#include "route_authorization.hpp"
#include "service_runtime_api.hpp"
#include "session_security_policy.hpp"
#include "web_handler_common.hpp"
#include "web_route_auth_dispatch.hpp"
#include "web_v1_common.hpp"

namespace web_ui {

namespace {

// GET /api/v1/auth/session's "capabilities" array reuses this instead of
// a second hand-written token list -- capability_token() (capability.hpp)
// is the one place these exact plan #18 strings are spelled.
int append_capabilities_json(
    char* out, std::size_t out_capacity, const service::RuntimeCapabilities& runtime_caps) noexcept {
    service::Capability granted[service::kCapabilityCount]{};
    const uint32_t count = service::granted_capabilities(runtime_caps, granted, service::kCapabilityCount);

    std::size_t offset = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        const int written = std::snprintf(
            out + offset, out_capacity - offset, "%s\"%s\"", i == 0U ? "" : ",", service::capability_token(granted[i]));
        if (written <= 0 || static_cast<std::size_t>(written) >= out_capacity - offset) {
            return -1;
        }
        offset += static_cast<std::size_t>(written);
    }
    return static_cast<int>(offset);
}

esp_err_t auth_login_post_handler_v1(httpd_req_t* req) {
    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    if (context == nullptr || context->sessions == nullptr) {
        return ESP_FAIL;
    }

    char body[256]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    char password[128]{};
    if (!find_json_string_field(body, "password", password, sizeof(password)) || password[0] == '\0') {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    // Fail closed if no admin credential has ever been enrolled -- same
    // "absence of evidence is not proof of authenticity" posture
    // gateway_identity_verification.cpp already applies. No enrollment
    // flow exists yet (needs #20-22's physical-presence grant, plan
    // #17's provisioning/enroll route), so this is the real, current
    // state on a fresh device, not a defensive-only branch.
    if (!service::admin_credential_exists()) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }

    service::AdminVerifierRecord record{};
    if (service::get_stored_admin_verifier(&record) != service::SecureStorageStatus::kAvailable) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }

    // Never distinguishes "wrong password" from "no such account" --
    // there is only ever one account, and plan #23 forbids leaking which
    // check failed.
    if (!service::verify_admin_password(password, record)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kUnauthenticated);
    }

    const uint64_t now_ms = hal_time_now_ms();
    service::SessionRecord new_session{};
    const service::SessionCreateResult create_result =
        service::session_store_create(context->sessions, now_ms, &new_session);
    if (create_result != service::SessionCreateResult::kCreated) {
        // kFull (already 4 live sessions) or kRngFailed -- neither is the
        // caller's fault and neither should look like a wrong password;
        // the closest existing golden-matrix code is kNoCapacity (503).
        return send_api_v1_error(req, ApiV1ErrorCode::kNoCapacity);
    }

    char cookie_header[128]{};
    if (!service::build_session_cookie_header(new_session.session_id_hex, cookie_header, sizeof(cookie_header))) {
        service::session_store_revoke(context->sessions, new_session.session_id_hex);
        return ESP_FAIL;
    }
    if (httpd_resp_set_hdr(req, "Set-Cookie", cookie_header) != ESP_OK) {
        service::session_store_revoke(context->sessions, new_session.session_id_hex);
        return ESP_FAIL;
    }

    // Plan #14: "CSRF token is a separate 256-bit session-bound value
    // returned only by the authenticated session endpoint" -- login's own
    // response does not include it; the client's next call is
    // GET /api/v1/auth/session, exactly the route that text names.
    char payload[64]{};
    const int written = std::snprintf(
        payload, sizeof(payload), "{\"schema_version\":%u,\"logged_in\":true}",
        static_cast<unsigned>(kApiV1SchemaVersion));
    if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
        return ESP_FAIL;
    }
    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

esp_err_t auth_logout_post_handler_v1(httpd_req_t* req) {
    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    if (context == nullptr || context->sessions == nullptr) {
        return ESP_FAIL;
    }

    char session_id_hex[service::kSessionIdHexChars + 1U]{};
    size_t session_id_capacity = sizeof(session_id_hex);
    if (httpd_req_get_cookie_val(req, service::kSessionCookieName, session_id_hex, &session_id_capacity) == ESP_OK) {
        service::session_store_revoke(context->sessions, session_id_hex);
    }

    char clear_header[96]{};
    if (!service::build_session_cookie_clear_header(clear_header, sizeof(clear_header))) {
        return ESP_FAIL;
    }
    if (httpd_resp_set_hdr(req, "Set-Cookie", clear_header) != ESP_OK) {
        return ESP_FAIL;
    }
    return send_api_v1_ok(req);
}

esp_err_t auth_session_get_handler_v1(httpd_req_t* req) {
    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    if (context == nullptr || context->sessions == nullptr || context->runtime == nullptr) {
        return ESP_FAIL;
    }

    char session_id_hex[service::kSessionIdHexChars + 1U]{};
    size_t session_id_capacity = sizeof(session_id_hex);
    if (httpd_req_get_cookie_val(req, service::kSessionCookieName, session_id_hex, &session_id_capacity) != ESP_OK) {
        // The registration wrapper already required a valid session to
        // reach this handler at all in production; a missing cookie here
        // means a direct/test call bypassing that wrapper.
        return send_api_v1_error(req, ApiV1ErrorCode::kUnauthenticated);
    }

    const uint64_t now_ms = hal_time_now_ms();
    char csrf_token_hex[service::kCsrfTokenHexChars + 1U]{};
    if (!service::session_store_get_csrf_token(
            *context->sessions, session_id_hex, now_ms, csrf_token_hex, sizeof(csrf_token_hex))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kUnauthenticated);
    }

    char capabilities_json[256]{};
    if (append_capabilities_json(capabilities_json, sizeof(capabilities_json), context->runtime->capabilities()) < 0) {
        return ESP_FAIL;
    }

    char payload[400]{};
    const int written = std::snprintf(
        payload, sizeof(payload), "{\"schema_version\":%u,\"csrf_token\":\"%s\",\"capabilities\":[%s]}",
        static_cast<unsigned>(kApiV1SchemaVersion), csrf_token_hex, capabilities_json);
    if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
        return ESP_FAIL;
    }
    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

}  // namespace

bool register_auth_routes_v1(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr || context->sessions == nullptr) {
        return false;
    }
    const auto handle = static_cast<httpd_handle_t>(server_handle);

    // Login is deliberately NOT wrapped by register_authenticated_uri_
    // handler_v1() -- it is the one route a caller reaches WITHOUT an
    // existing session; wrapping it would make login itself require
    // being already logged in.
    httpd_uri_t login_uri{};
    login_uri.uri = "/api/v1/auth/login";
    login_uri.method = HTTP_POST;
    login_uri.handler = auth_login_post_handler_v1;
    login_uri.user_ctx = context;
    if (httpd_register_uri_handler(handle, &login_uri) != ESP_OK) {
        return false;
    }

    httpd_uri_t logout_uri{};
    logout_uri.uri = "/api/v1/auth/logout";
    logout_uri.method = HTTP_POST;
    logout_uri.handler = auth_logout_post_handler_v1;
    logout_uri.user_ctx = context;
    if (!register_authenticated_uri_handler_v1(handle, logout_uri, service::Capability::kReadStatus)) {
        return false;
    }

    httpd_uri_t session_uri{};
    session_uri.uri = "/api/v1/auth/session";
    session_uri.method = HTTP_GET;
    session_uri.handler = auth_session_get_handler_v1;
    session_uri.user_ctx = context;
    return register_authenticated_uri_handler_v1(handle, session_uri, service::Capability::kReadStatus);
}

}  // namespace web_ui
