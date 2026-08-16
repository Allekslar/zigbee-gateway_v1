/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// Plan S6 "Authorization and physical presence" #17: POST
// /api/v1/auth/login, POST /api/v1/auth/logout, GET /api/v1/auth/session,
// POST /api/v1/auth/password, POST /api/v1/provisioning/enroll,
// POST /api/v1/system/factory-reset/operations, and (this sub-slice's own
// addition) POST /api/v1/security/certificates/operations -- plan #12's
// FD-17 atomic-slot-rotation route. The route's own POLICY half (decode,
// offline X.509 validation, physical presence) is real; the ACTIVATION
// half (bounded local handshake self-test + the atomic active-slot
// switch it gates) is not yet wired here -- see this file's own
// certificates_operations_post_handler_v1() comment for the exact line
// drawn and why. cert_rotation_state.hpp (components/service) is the
// separate primitive that half depends on.
//
// Every route below that needs a physical-presence grant
// (auth/password, provisioning/enroll, factory-reset/operations,
// certificates/operations) now has a real, HIL-confirmed path to obtain
// one: a genuine BOOT-button press on the connected ESP32-C6 was
// captured creating a grant over real serial output (see
// docs/security/CONTROL_PLANE_SECURITY.md Section 2.14).

#include "web_routes.hpp"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#endif
#include "admin_verifier.hpp"
#include "cert_rotation_self_test.hpp"
#include "cert_rotation_state.hpp"
#include "commissioning_window.hpp"
#include "gateway_id.hpp"
#include "gateway_identity_verification.hpp"
#include "hal_time.h"
#include "hal_tls_certificate_validator.h"
#include "physical_presence_grant.hpp"
#include "provisioning_secret_provider.hpp"
#include "provisioning_secrets.hpp"
#include "route_authorization.hpp"
#include "service_runtime_api.hpp"
#include "session_security_policy.hpp"
#include "tls_provisioning_storage_port.hpp"
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

// Plan #12/FD-17: "POST /api/v1/security/certificates/operations for
// FD-17 rotation." Wrapped mutation-grade (kSecurityAdmin, same grade as
// auth/password -- both are "authenticated" per plan text).
//
// The full FD-17 chain is now real, in order: decode a candidate
// certificate/private key (hex-encoded PEM -- see decode_hex() above;
// JSON string fields carry the hex text, not raw PEM bytes, avoiding any
// need for this project's own ad-hoc JSON reader to understand embedded-
// newline escaping, matching provisioning/enroll's own reuse of
// decode_hex() for opaque byte payloads) -> offline validation
// (hal_tls_validate_certificate -- chain-to-CA, expiry, both SAN values,
// key/certificate pairing) -> a bounded local live handshake self-test
// (web_ui::cert_rotation_bounded_self_test(), see that module's own
// header) -> only THEN consume the physical-presence grant (a bad
// candidate -- offline OR live -- must never burn the grant, the same
// principle auth_password_post_handler_v1 already applies to a wrong
// current password) -> write the candidate into the staging slot for
// real (encrypted, S5's write gate) -> atomically activate it
// (cert_rotation_activate(), pending confirmation) -> respond success
// -> schedule a short-delayed reboot. The device reboots into the newly
// active slot; web_server.cpp's start_production_https() re-validates it
// on that next boot (cert_rotation_confirm_pending()) and rolls back to
// the previously confirmed slot if anything regressed -- this is plan
// #12's own "one successful reboot/post-activation check."
esp_err_t certificates_operations_post_handler_v1(httpd_req_t* req) {
    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    if (context == nullptr || context->sessions == nullptr || context->physical_presence == nullptr ||
        context->runtime == nullptr) {
        return ESP_FAIL;
    }

    // `static`, not stack-local: matches web_server.cpp's own
    // start_production_https() precedent exactly -- these buffers (2x
    // hex text + 2x decoded bytes, up to kTlsCertOrKeyMaxBytes*2+1 and
    // kTlsCertOrKeyMaxBytes each) are far too large for the httpd worker
    // task's own stack budget as ordinary locals, the same real "Guru
    // Meditation Error" class of bug that precedent's own comment
    // documents. Safe as `static` here for the same reason: this
    // project's single httpd worker task dispatches every registered
    // handler serially, never concurrently (session_store.hpp's own
    // established assumption).
    static char body[2U * (service::kTlsCertOrKeyMaxBytes * 2U + 1U) + 128U]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    static char cert_pem_hex[service::kTlsCertOrKeyMaxBytes * 2U + 1U]{};
    static char key_pem_hex[service::kTlsCertOrKeyMaxBytes * 2U + 1U]{};
    if (!find_json_string_field(body, "certificate_pem_hex", cert_pem_hex, sizeof(cert_pem_hex)) ||
        cert_pem_hex[0] == '\0' ||
        !find_json_string_field(body, "private_key_pem_hex", key_pem_hex, sizeof(key_pem_hex)) ||
        key_pem_hex[0] == '\0') {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    static uint8_t cert_pem[service::kTlsCertOrKeyMaxBytes]{};
    static uint8_t key_pem[service::kTlsCertOrKeyMaxBytes]{};
    uint32_t cert_len = 0U;
    uint32_t key_len = 0U;
    if (!decode_hex(cert_pem_hex, cert_pem, sizeof(cert_pem), &cert_len) ||
        !decode_hex(key_pem_hex, key_pem, sizeof(key_pem), &key_len)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    // mbedtls's own PEM-vs-DER convention (hal_tls_certificate_
    // validator.h's own doc comment): `_len` must include a trailing NUL
    // byte for PEM text to be recognized as such. The client is
    // responsible for encoding that NUL into the hex candidate; its
    // absence is rejected here directly rather than left to produce a
    // confusing PARSE_FAILED from mbedtls itself.
    if (cert_len == 0U || cert_pem[cert_len - 1U] != 0U || key_len == 0U || key_pem[key_len - 1U] != 0U) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    static uint8_t ca_pem[service::kTlsCertOrKeyMaxBytes]{};
    uint32_t ca_len = 0U;
    if (service::tls_identity_get_product_ca(ca_pem, sizeof(ca_pem), &ca_len) !=
        service::SecureStorageStatus::kAvailable) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }

    char mdns_host[32]{};
    char dns_san[40]{};
    char uri_san[32]{};
    if (!service::build_gateway_mdns_host(context->runtime->gateway_id(), mdns_host, sizeof(mdns_host)) ||
        std::snprintf(dns_san, sizeof(dns_san), "%s.local", mdns_host) <= 0 ||
        !service::build_gateway_uri_san(context->runtime->gateway_id(), uri_san, sizeof(uri_san))) {
        return ESP_FAIL;
    }

    const hal_tls_cert_validation_result_t validation_result = hal_tls_validate_certificate(
        cert_pem, cert_len, key_pem, key_len, ca_pem, ca_len, dns_san, uri_san);
    if (validation_result != HAL_TLS_CERT_VALIDATION_VALID) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    // Plan #12/FD-17: "starts a bounded local verification using `next`,
    // atomically switches the active reference only after validation" --
    // read as validation meaning BOTH the offline X.509 check above AND
    // this live handshake self-test, not just the first. Runs against
    // the in-memory candidate bytes directly (no need to read the
    // staging slot back from storage). Deliberately runs BEFORE
    // consuming the physical-presence grant, same "a bad candidate must
    // never burn the grant" principle this handler already applies to
    // the offline check above -- a candidate that parses/chains/matches
    // SAN offline but still fails a REAL handshake is just as much "not
    // a usable candidate" as one that fails the static check.
    if (!web_ui::cert_rotation_bounded_self_test(cert_pem, cert_len, key_pem, key_len, ca_pem, ca_len, dns_san)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    char session_id_hex[service::kSessionIdHexChars + 1U]{};
    size_t session_id_capacity = sizeof(session_id_hex);
    if (httpd_req_get_cookie_val(req, service::kSessionCookieName, session_id_hex, &session_id_capacity) != ESP_OK) {
        return send_api_v1_error(req, ApiV1ErrorCode::kUnauthenticated);
    }

    const uint64_t now_ms = hal_time_now_ms();
    if (!service::physical_presence_grant_consume(
            context->physical_presence, service::PhysicalPresenceActionClass::kCertificateRotation, session_id_hex,
            now_ms)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kPhysicalPresenceRequired);
    }

    const service::TlsCertificateSlot staging_slot = service::cert_rotation_staging_slot();
    if (service::tls_identity_set_certificate(staging_slot, cert_pem, cert_len) !=
            service::SecureStorageWriteResult::kWritten ||
        service::tls_identity_set_private_key(staging_slot, key_pem, key_len) !=
            service::SecureStorageWriteResult::kWritten) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }

    // The atomic active-slot switch itself (a single NVS write -- see
    // cert_rotation_state.hpp's own header comment on why this can never
    // tear across a power loss the way copying cert/key bytes between
    // slots could). Sets confirmation = pending; the boot-time
    // cert_rotation_confirm_pending() call web_server.cpp's
    // start_production_https() already makes will confirm or roll this
    // back on the very next boot -- which the reboot scheduled below
    // triggers immediately.
    if (service::cert_rotation_activate(staging_slot) != service::CertRotationActivateResult::kActivated) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }

    // Respond to the client BEFORE rebooting -- schedule_cert_rotation_
    // reboot() defers the actual esp_restart() by a short delay
    // (see cert_rotation_self_test.cpp's own comment) specifically so
    // httpd_resp_send() below has time to actually flush this response
    // over the socket first. An immediate synchronous esp_restart()
    // here would tear the connection down before the client ever saw
    // a response.
    const esp_err_t response_result = send_api_v1_ok(req);
    web_ui::schedule_cert_rotation_reboot();
    return response_result;
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
    if (!register_authenticated_uri_handler_v1(handle, factory_reset_uri, service::Capability::kFactoryReset)) {
        return false;
    }

    httpd_uri_t certificates_uri{};
    certificates_uri.uri = "/api/v1/security/certificates/operations";
    certificates_uri.method = HTTP_POST;
    certificates_uri.handler = certificates_operations_post_handler_v1;
    certificates_uri.user_ctx = context;
    return register_authenticated_uri_handler_v1(handle, certificates_uri, service::Capability::kSecurityAdmin);
}

}  // namespace web_ui
