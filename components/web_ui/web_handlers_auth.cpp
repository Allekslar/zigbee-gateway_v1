/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// Plan S6 "Authorization and physical presence" #17 (all but certificate
// rotation): POST /api/v1/auth/login, POST /api/v1/auth/logout,
// GET /api/v1/auth/session, POST /api/v1/auth/password,
// POST /api/v1/provisioning/enroll,
// POST /api/v1/system/factory-reset/operations. Certificate rotation
// (`POST /api/v1/security/certificates/operations`, plan #12's FD-17
// atomic-slot-rotation machinery) is its own, much larger sub-slice --
// see docs/security/CONTROL_PLANE_SECURITY.md's own section for the
// scope line drawn here.
//
// Every route below that needs a physical-presence grant
// (auth/password, provisioning/enroll, factory-reset/operations) will
// always fail closed on that check today: nothing in this repository yet
// turns a real button press into a grant (no debounce/edge-detection
// task exists -- see hal_button.h/physical_presence_grant.hpp's own
// comments). These routes are real, complete, and correctly wired against
// that real primitive, honestly non-functional pending that future
// wiring -- the same "consumer built before the primitive's upstream
// input exists" pattern this project used for TLS certificate
// validation (Section 2.10) before any real certificate existed.

#include "web_routes.hpp"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#endif
#include "admin_verifier.hpp"
#include "commissioning_window.hpp"
#include "gateway_id.hpp"
#include "gateway_identity_verification.hpp"
#include "hal_time.h"
#include "physical_presence_grant.hpp"
#include "provisioning_secret_provider.hpp"
#include "route_authorization.hpp"
#include "service_runtime_api.hpp"
#include "session_security_policy.hpp"
#include "web_handler_common.hpp"
#include "web_route_auth_dispatch.hpp"
#include "web_v1_common.hpp"

namespace web_ui {

namespace {

// Decodes a lowercase-hex string (as submitted in a JSON body field) into
// raw bytes. Rejects odd length, any non-hex character, and an output
// that would not fit `out_capacity` -- returns false (out/out_len
// untouched) on any of those, never a partial decode.
bool decode_hex(const char* hex, uint8_t* out, uint32_t out_capacity, uint32_t* out_len) noexcept {
    if (hex == nullptr || out == nullptr || out_len == nullptr) {
        return false;
    }
    const std::size_t hex_len = std::strlen(hex);
    if (hex_len == 0U || (hex_len % 2U) != 0U || (hex_len / 2U) > out_capacity) {
        return false;
    }

    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    };

    for (std::size_t i = 0; i < hex_len / 2U; ++i) {
        const int high = nibble(hex[i * 2U]);
        const int low = nibble(hex[i * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        out[i] = static_cast<uint8_t>((high << 4) | low);
    }
    *out_len = static_cast<uint32_t>(hex_len / 2U);
    return true;
}

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

// Plan #17: "POST /api/v1/auth/password requiring the current credential
// and recent physical presence." Wrapped mutation-grade (kSecurityAdmin)
// -- the registration wrapper already proved a valid session + CSRF +
// origin before this handler ever runs.
esp_err_t auth_password_post_handler_v1(httpd_req_t* req) {
    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    if (context == nullptr || context->sessions == nullptr || context->physical_presence == nullptr) {
        return ESP_FAIL;
    }

    char body[256]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    char current_password[128]{};
    char new_password[128]{};
    if (!find_json_string_field(body, "current_password", current_password, sizeof(current_password)) ||
        current_password[0] == '\0' ||
        !find_json_string_field(body, "new_password", new_password, sizeof(new_password)) ||
        new_password[0] == '\0') {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    // Verify the CURRENT credential first, before ever consuming the
    // one-time physical-presence grant -- a wrong-password attempt must
    // not burn a real installer's grant.
    service::AdminVerifierRecord record{};
    if (service::get_stored_admin_verifier(&record) != service::SecureStorageStatus::kAvailable ||
        !service::verify_admin_password(current_password, record)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kUnauthenticated);
    }

    char session_id_hex[service::kSessionIdHexChars + 1U]{};
    size_t session_id_capacity = sizeof(session_id_hex);
    if (httpd_req_get_cookie_val(req, service::kSessionCookieName, session_id_hex, &session_id_capacity) != ESP_OK) {
        return send_api_v1_error(req, ApiV1ErrorCode::kUnauthenticated);
    }

    const uint64_t now_ms = hal_time_now_ms();
    if (!service::physical_presence_grant_consume(
            context->physical_presence, service::PhysicalPresenceActionClass::kAdminPasswordChange, session_id_hex,
            now_ms)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kPhysicalPresenceRequired);
    }

    service::AdminVerifierRecord new_record{};
    if (!service::create_admin_verifier(new_password, &new_record) ||
        service::set_stored_admin_verifier(new_record) != service::SecureStorageWriteResult::kWritten) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }

    return send_api_v1_ok(req);
}

// Plan #17: "POST /api/v1/provisioning/enroll available only in active
// commissioning mode with proof of possession and physical presence."
// NOT wrapped by register_authenticated_uri_handler_v1() -- like login,
// this is a route reached WITHOUT an existing session (it is what
// creates the very first admin credential).
esp_err_t provisioning_enroll_post_handler_v1(httpd_req_t* req) {
    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    if (context == nullptr || context->commissioning_window == nullptr || context->physical_presence == nullptr ||
        context->provisioning_secret == nullptr || context->runtime == nullptr) {
        return ESP_FAIL;
    }

    const uint64_t now_ms = hal_time_now_ms();
    if (!service::commissioning_window_is_active(*context->commissioning_window, now_ms)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kProvisioningNotActive);
    }

    char body[256]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    char pop_hex[service::kProvisioningSecretMaxBytes * 2U + 1U]{};
    char admin_password[128]{};
    if (!find_json_string_field(body, "proof_of_possession", pop_hex, sizeof(pop_hex)) || pop_hex[0] == '\0' ||
        !find_json_string_field(body, "admin_password", admin_password, sizeof(admin_password)) ||
        admin_password[0] == '\0') {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    uint8_t pop_bytes[service::kProvisioningSecretMaxBytes]{};
    uint32_t pop_len = 0U;
    if (!decode_hex(pop_hex, pop_bytes, sizeof(pop_bytes), &pop_len) ||
        !service::provisioning_secret_matches(*context->provisioning_secret, pop_bytes, pop_len)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kUnauthenticated);
    }

    // Enrollment happens before any session exists -- the grant this
    // route consumes is deliberately session-less (nullptr), matching
    // physical_presence_grant.hpp's own documented support for that case.
    if (!service::physical_presence_grant_consume(
            context->physical_presence, service::PhysicalPresenceActionClass::kProvisioningEnroll, nullptr,
            now_ms)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kPhysicalPresenceRequired);
    }

#if defined(CONFIG_ZGW_PRODUCTION_PROFILE) && CONFIG_ZGW_PRODUCTION_PROFILE
    // Plan FD-17 (Section 2.9): "duplicate/cloned GatewayId evidence
    // blocks production enrollment." Development skips this -- no
    // manufacturing record is ever populated there either, and
    // development already carries weaker trust guarantees throughout
    // (Section 2.7's own plain-HTTP posture).
    const service::GatewayIdVerificationResult identity_result =
        service::verify_gateway_id_against_manufacturing_record(context->runtime->gateway_id());
    if (!service::gateway_id_verification_allows_production_enrollment(identity_result)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }
#endif

    service::AdminVerifierRecord record{};
    if (!service::create_admin_verifier(admin_password, &record) ||
        service::set_stored_admin_verifier(record) != service::SecureStorageWriteResult::kWritten) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }

    // Plan #3's own text: "an administrator enrolls and the window
    // should close early."
    service::commissioning_window_stop(context->commissioning_window);

    return send_api_v1_ok(req);
}

// Plan #17/#22: "POST /api/v1/system/factory-reset/operations,
// registered with policy metadata in S6 but returning
// capability_unavailable until the S8 reset state machine is installed."
// #22: "Factory reset additionally requires a fresh manufacturing PoP
// challenge. S6 owns policy validation; S8 owns the reset journal and
// erase execution." -- so the POLICY half (physical presence + PoP) is
// real and enforced here; only the actual reset never runs, because S8
// does not exist. Wrapped mutation-grade (kFactoryReset).
esp_err_t factory_reset_operations_post_handler_v1(httpd_req_t* req) {
    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    if (context == nullptr || context->sessions == nullptr || context->physical_presence == nullptr ||
        context->provisioning_secret == nullptr) {
        return ESP_FAIL;
    }

    char body[256]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    char pop_hex[service::kProvisioningSecretMaxBytes * 2U + 1U]{};
    if (!find_json_string_field(body, "proof_of_possession", pop_hex, sizeof(pop_hex)) || pop_hex[0] == '\0') {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    uint8_t pop_bytes[service::kProvisioningSecretMaxBytes]{};
    uint32_t pop_len = 0U;
    if (!decode_hex(pop_hex, pop_bytes, sizeof(pop_bytes), &pop_len) ||
        !service::provisioning_secret_matches(*context->provisioning_secret, pop_bytes, pop_len)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kUnauthenticated);
    }

    char session_id_hex[service::kSessionIdHexChars + 1U]{};
    size_t session_id_capacity = sizeof(session_id_hex);
    if (httpd_req_get_cookie_val(req, service::kSessionCookieName, session_id_hex, &session_id_capacity) != ESP_OK) {
        return send_api_v1_error(req, ApiV1ErrorCode::kUnauthenticated);
    }

    const uint64_t now_ms = hal_time_now_ms();
    if (!service::physical_presence_grant_consume(
            context->physical_presence, service::PhysicalPresenceActionClass::kFactoryReset, session_id_hex,
            now_ms)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kPhysicalPresenceRequired);
    }

    // Policy validation passed -- the actual erase execution is S8's job
    // (not built), so this is the plan's own named stub outcome, not a
    // failure of anything checked above.
    return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
}

}  // namespace

bool register_auth_routes_v1(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr || context->sessions == nullptr ||
        context->physical_presence == nullptr || context->commissioning_window == nullptr ||
        context->provisioning_secret == nullptr) {
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
    if (!register_authenticated_uri_handler_v1(handle, session_uri, service::Capability::kReadStatus)) {
        return false;
    }

    httpd_uri_t password_uri{};
    password_uri.uri = "/api/v1/auth/password";
    password_uri.method = HTTP_POST;
    password_uri.handler = auth_password_post_handler_v1;
    password_uri.user_ctx = context;
    if (!register_authenticated_uri_handler_v1(handle, password_uri, service::Capability::kSecurityAdmin)) {
        return false;
    }

    // Enroll, like login, is deliberately NOT wrapped -- reached without
    // an existing session.
    httpd_uri_t enroll_uri{};
    enroll_uri.uri = "/api/v1/provisioning/enroll";
    enroll_uri.method = HTTP_POST;
    enroll_uri.handler = provisioning_enroll_post_handler_v1;
    enroll_uri.user_ctx = context;
    if (httpd_register_uri_handler(handle, &enroll_uri) != ESP_OK) {
        return false;
    }

    httpd_uri_t factory_reset_uri{};
    factory_reset_uri.uri = "/api/v1/system/factory-reset/operations";
    factory_reset_uri.method = HTTP_POST;
    factory_reset_uri.handler = factory_reset_operations_post_handler_v1;
    factory_reset_uri.user_ctx = context;
    return register_authenticated_uri_handler_v1(handle, factory_reset_uri, service::Capability::kFactoryReset);
}

}  // namespace web_ui
