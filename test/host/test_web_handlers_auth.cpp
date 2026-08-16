/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// Host coverage for plan S6 "Authorization and physical presence" #17
// (basic slice: login/logout/session) and the #19 registration-level auth
// wrapper (web_route_auth_dispatch.cpp, already linked via web_ui_host).
// Every route is exercised through a *captured real registration* --
// httpd_register_uri_handler() below stores exactly what
// register_auth_routes_v1()/register_authenticated_uri_handler_v1()
// actually register, and each test case invokes the captured handler
// directly (`captured.handler(&req)`) -- the same call a real
// esp_http_server dispatch would make, including through the
// authenticated_dispatch_trampoline for the wrapped routes (logout,
// session). This is deliberately NOT a call to the underlying
// auth_*_handler_v1 functions directly (they are anonymous-namespace,
// unreachable from here) -- going through the captured registration is
// what actually proves the wrapper is wired in for these routes at all.

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "admin_verifier.hpp"
#include "hal_security_state_test.h"
#include "web_handler_common.hpp"

std::string g_last_response;
std::string g_last_status;
std::string g_request_body;
std::vector<std::pair<std::string, std::string>> g_last_response_headers;  // {field, value}

struct CapturedRoute {
    std::string uri;
    httpd_method_t method{HTTP_GET};
    httpd_handler_t handler{nullptr};
    void* user_ctx{nullptr};
};
std::vector<CapturedRoute> g_captured_routes;

extern "C" esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* type) {
    (void)req;
    (void)type;
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_send(httpd_req_t* req, const char* buf, ssize_t len) {
    (void)req;
    g_last_response = (len == HTTPD_RESP_USE_STRLEN) ? buf : std::string(buf, static_cast<std::size_t>(len));
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_send_chunk(httpd_req_t* req, const char* buf, ssize_t len) {
    (void)req;
    (void)buf;
    (void)len;
    return ESP_OK;
}

extern "C" esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t* uri_handler) {
    (void)handle;
    CapturedRoute route;
    route.uri = uri_handler->uri;
    route.method = uri_handler->method;
    route.handler = uri_handler->handler;
    route.user_ctx = uri_handler->user_ctx;
    g_captured_routes.push_back(route);
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_set_status(httpd_req_t* req, const char* status) {
    (void)req;
    g_last_status = status == nullptr ? "" : status;
    return ESP_OK;
}

extern "C" int httpd_req_recv(httpd_req_t* req, char* buf, size_t len) {
    (void)req;
    if (g_request_body.empty()) {
        return 0;
    }
    const std::size_t to_copy = g_request_body.size() < len ? g_request_body.size() : len;
    std::memcpy(buf, g_request_body.data(), to_copy);
    g_request_body.erase(0, to_copy);
    return static_cast<int>(to_copy);
}

extern "C" esp_err_t httpd_resp_set_hdr(httpd_req_t* req, const char* field, const char* value) {
    (void)req;
    g_last_response_headers.emplace_back(field, value);
    return ESP_OK;
}

void clear_http_capture() {
    g_last_response.clear();
    g_last_status.clear();
    g_last_response_headers.clear();
}

const CapturedRoute* find_captured(const char* uri, httpd_method_t method) {
    for (const auto& route : g_captured_routes) {
        if (route.uri == uri && route.method == method) {
            return &route;
        }
    }
    return nullptr;
}

const std::string* find_response_header(const char* field) {
    for (const auto& header : g_last_response_headers) {
        if (header.first == field) {
            return &header.second;
        }
    }
    return nullptr;
}

#include "../../components/web_ui/web_handlers_auth.cpp"

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"

namespace {

void provision_admin_credential(const char* password) {
    hal_security_state_set_mock_flash_encryption_enabled(true);
    service::AdminVerifierRecord record{};
    assert(service::create_admin_verifier(password, &record));
    assert(service::set_stored_admin_verifier(record) == service::SecureStorageWriteResult::kWritten);
}

}  // namespace

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    std::atomic<uint32_t> next_id{1};
    service::SessionStoreState sessions{};
    service::PhysicalPresenceGrantState physical_presence{};
    service::CommissioningWindowState commissioning_window{};
    service::ProvisioningSecret provisioning_secret{};
    web_ui::WebRouteContext context{};
    context.runtime = &runtime;
    context.next_correlation_id = &next_id;
    context.sessions = &sessions;
    context.expected_origin = "https://zigbee-gateway-test.local";
    context.physical_presence = &physical_presence;
    context.commissioning_window = &commissioning_window;
    context.provisioning_secret = &provisioning_secret;

    assert(web_ui::register_auth_routes_v1(reinterpret_cast<void*>(1), &context));
    const CapturedRoute* login = find_captured("/api/v1/auth/login", HTTP_POST);
    const CapturedRoute* logout = find_captured("/api/v1/auth/logout", HTTP_POST);
    const CapturedRoute* session_get = find_captured("/api/v1/auth/session", HTTP_GET);
    const CapturedRoute* password = find_captured("/api/v1/auth/password", HTTP_POST);
    const CapturedRoute* enroll = find_captured("/api/v1/provisioning/enroll", HTTP_POST);
    const CapturedRoute* factory_reset = find_captured("/api/v1/system/factory-reset/operations", HTTP_POST);
    const CapturedRoute* certificates = find_captured("/api/v1/security/certificates/operations", HTTP_POST);
    assert(login != nullptr && login->handler != nullptr);
    assert(logout != nullptr && logout->handler != nullptr);
    assert(session_get != nullptr && session_get->handler != nullptr);
    assert(password != nullptr && password->handler != nullptr);
    assert(enroll != nullptr && enroll->handler != nullptr);
    assert(factory_reset != nullptr && factory_reset->handler != nullptr);
    assert(certificates != nullptr && certificates->handler != nullptr);

    // --- Bad-argument rejection at registration time. ---
    assert(!web_ui::register_auth_routes_v1(nullptr, &context));
    web_ui::WebRouteContext no_sessions_ctx{};
    no_sessions_ctx.runtime = &runtime;
    assert(!web_ui::register_auth_routes_v1(reinterpret_cast<void*>(1), &no_sessions_ctx));

    // --- No admin credential enrolled yet: login fails closed. ---
    {
        httpd_req_t req{};
        req.user_ctx = login->user_ctx;
        req.method = HTTP_POST;
        g_request_body = "{\"password\":\"whatever\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(login->handler(&req) == ESP_OK);
        assert(g_last_status == "503 Service Unavailable");
        assert(g_last_response.find("capability_unavailable") != std::string::npos);
    }

    provision_admin_credential("correct horse battery staple");

    // --- Malformed body: 400. ---
    {
        httpd_req_t req{};
        req.user_ctx = login->user_ctx;
        req.method = HTTP_POST;
        g_request_body = "not json";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(login->handler(&req) == ESP_OK);
        assert(g_last_status == "400 Bad Request");
    }

    // --- Wrong password: 401, no session created. ---
    {
        httpd_req_t req{};
        req.user_ctx = login->user_ctx;
        req.method = HTTP_POST;
        g_request_body = "{\"password\":\"nope\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(login->handler(&req) == ESP_OK);
        assert(g_last_status == "401 Unauthorized");
        assert(find_response_header("Set-Cookie") == nullptr);
    }

    // --- Correct password: 200, Set-Cookie issued, session created. ---
    std::string session_id_hex;
    {
        httpd_req_t req{};
        req.user_ctx = login->user_ctx;
        req.method = HTTP_POST;
        g_request_body = "{\"password\":\"correct horse battery staple\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(login->handler(&req) == ESP_OK);
        assert(g_last_status.empty());  // 200 OK (no explicit set_status call)
        assert(g_last_response.find("\"logged_in\":true") != std::string::npos);
        // login's own response must never include the CSRF token (plan
        // #14: only the session endpoint returns it).
        assert(g_last_response.find("csrf_token") == std::string::npos);

        const std::string* set_cookie = find_response_header("Set-Cookie");
        assert(set_cookie != nullptr);
        assert(set_cookie->find("zgw_session=") == 0U);
        assert(set_cookie->find("Secure") != std::string::npos);
        assert(set_cookie->find("HttpOnly") != std::string::npos);
        assert(set_cookie->find("SameSite=Strict") != std::string::npos);

        const std::size_t value_start = std::strlen("zgw_session=");
        const std::size_t value_end = set_cookie->find(';');
        session_id_hex = set_cookie->substr(value_start, value_end - value_start);
        assert(session_id_hex.size() == service::kSessionIdHexChars);
    }

    // --- GET /api/v1/auth/session: wrapped, needs the cookie AND (being
    // a GET) no CSRF/Origin. ---
    std::string csrf_token_hex;
    {
        httpd_req_t req{};
        // The captured user_ctx (session_get->user_ctx) is the wrapper's
        // own AuthenticatedRouteBinding*, not WebRouteContext* -- exactly
        // what a real esp_http_server dispatch would place in
        // req->user_ctx after register_authenticated_uri_handler_v1()
        // wrapped this route. The trampoline reads it as a binding first,
        // then (only once authorized) restores it to the real
        // WebRouteContext* before calling the real handler.
        req.user_ctx = session_get->user_ctx;
        req.method = HTTP_GET;
        clear_http_capture();

        // No cookie at all -> the wrapper itself rejects with 401 before
        // the real handler ever runs.
        req.mock_cookie_header = nullptr;
        assert(session_get->handler(&req) == ESP_OK);
        assert(g_last_status == "401 Unauthorized");

        // With the real session cookie -> allowed, real handler runs.
        const std::string cookie_header = "zgw_session=" + session_id_hex;
        req.mock_cookie_header = cookie_header.c_str();
        clear_http_capture();
        assert(session_get->handler(&req) == ESP_OK);
        assert(g_last_status.empty());
        assert(g_last_response.find("\"read_status\"") != std::string::npos);
        assert(g_last_response.find("\"security_admin\"") != std::string::npos);
        assert(g_last_response.find("\"firmware_admin\"") == std::string::npos);  // ota unavailable by default
        const std::size_t csrf_pos = g_last_response.find("\"csrf_token\":\"");
        assert(csrf_pos != std::string::npos);
        const std::size_t value_start = csrf_pos + std::strlen("\"csrf_token\":\"");
        const std::size_t value_end = g_last_response.find('"', value_start);
        csrf_token_hex = g_last_response.substr(value_start, value_end - value_start);
        assert(csrf_token_hex.size() == service::kCsrfTokenHexChars);
    }

    // --- POST /api/v1/auth/password: wrapped mutation-grade, plus
    // current-credential and physical-presence checks of its own. ---
    {
        const std::string cookie_header = "zgw_session=" + session_id_hex;
        httpd_req_t req{};
        req.user_ctx = password->user_ctx;
        req.method = HTTP_POST;
        req.mock_cookie_header = cookie_header.c_str();
        req.mock_csrf_header = csrf_token_hex.c_str();
        req.mock_origin_header = context.expected_origin;

        // Wrong current password -> 401, and the (still nonexistent)
        // physical-presence grant is never even consulted -- next
        // assertion confirms a *correct* attempt still needs a grant.
        g_request_body = "{\"current_password\":\"nope\",\"new_password\":\"new password here\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(password->handler(&req) == ESP_OK);
        assert(g_last_status == "401 Unauthorized");

        // Correct current password, but no physical-presence grant yet
        // (nothing in this repository creates one -- see
        // web_handlers_auth.cpp's own top-of-file comment) -> 403. The
        // wrapper's own trampoline overwrites req.user_ctx with the real
        // WebRouteContext* on every ALLOWED pass (see
        // web_route_auth_dispatch.cpp) regardless of what the real
        // handler itself decides afterward -- req.user_ctx must be reset
        // to the wrapper's own binding (password->user_ctx) before every
        // subsequent call through this same wrapped route reusing `req`.
        req.user_ctx = password->user_ctx;
        g_request_body =
            "{\"current_password\":\"correct horse battery staple\",\"new_password\":\"new password here\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(password->handler(&req) == ESP_OK);
        assert(g_last_status == "403 Forbidden");
        assert(g_last_response.find("physical_presence_required") != std::string::npos);

        // Simulate what a future button-press wiring would do: create a
        // grant for exactly this session/action-class, then retry. Must
        // use hal_time_now_ms() -- the same real monotonic clock basis
        // the handler itself checks against, not an arbitrary fixed
        // value (which would appear either already-expired or, if the
        // real clock reads less than it, "created in the future" and
        // rejected by the clock-goes-backward guard).
        service::physical_presence_grant_create(
            &physical_presence, service::PhysicalPresenceActionClass::kAdminPasswordChange, session_id_hex.c_str(),
            hal_time_now_ms());
        req.user_ctx = password->user_ctx;
        g_request_body =
            "{\"current_password\":\"correct horse battery staple\",\"new_password\":\"new password here\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(password->handler(&req) == ESP_OK);
        assert(g_last_status.empty());

        // The new password actually took effect, and the grant was
        // consumed (a second, otherwise-identical attempt is rejected
        // again for lack of a grant).
        service::AdminVerifierRecord updated_record{};
        assert(service::get_stored_admin_verifier(&updated_record) == service::SecureStorageStatus::kAvailable);
        assert(service::verify_admin_password("new password here", updated_record));
        assert(!service::verify_admin_password("correct horse battery staple", updated_record));

        req.user_ctx = password->user_ctx;
        g_request_body = "{\"current_password\":\"new password here\",\"new_password\":\"yet another one\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(password->handler(&req) == ESP_OK);
        assert(g_last_status == "403 Forbidden");
    }

    // --- POST /api/v1/provisioning/enroll: NOT wrapped (no session
    // exists yet) -- gated on commissioning-mode-active, proof of
    // possession, and physical presence, all checked directly. ---
    {
        httpd_req_t req{};
        req.user_ctx = enroll->user_ctx;
        req.method = HTTP_POST;
        g_request_body = "{\"proof_of_possession\":\"deadbeef\",\"admin_password\":\"a whole new admin\"}";
        req.content_len = static_cast<int>(g_request_body.size());

        // Commissioning window not active -> 409, before the body is
        // even inspected for content correctness.
        clear_http_capture();
        assert(enroll->handler(&req) == ESP_OK);
        assert(g_last_status == "409 Conflict");
        assert(g_last_response.find("provisioning_not_active") != std::string::npos);

        service::commissioning_window_start(
            &commissioning_window, service::CommissioningWindowTrigger::kFirstBootPolicy, hal_time_now_ms());

        // Commissioning active, but the real provisioning secret ("")
        // never matches the submitted "deadbeef" -> 401. read_request_
        // body()'s host mock DESTRUCTIVELY drains g_request_body as it
        // reads (see httpd_req_recv's own mock above) -- it must be
        // reassigned before every call that reaches the body-read step,
        // not just once at the top of this block.
        g_request_body = "{\"proof_of_possession\":\"deadbeef\",\"admin_password\":\"a whole new admin\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(enroll->handler(&req) == ESP_OK);
        assert(g_last_status == "401 Unauthorized");

        // Set the real expected secret to match what the request submits.
        provisioning_secret.bytes[0] = 0xDE;
        provisioning_secret.bytes[1] = 0xAD;
        provisioning_secret.bytes[2] = 0xBE;
        provisioning_secret.bytes[3] = 0xEF;
        provisioning_secret.len = 4U;

        // Correct PoP now, but no physical-presence grant -> 403.
        g_request_body = "{\"proof_of_possession\":\"deadbeef\",\"admin_password\":\"a whole new admin\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(enroll->handler(&req) == ESP_OK);
        assert(g_last_status == "403 Forbidden");

        // A real button-press grant, session-less (enroll has no session
        // yet) -> success. The commissioning window closes early (plan
        // #3's own text) and the admin credential is really overwritten.
        service::physical_presence_grant_create(
            &physical_presence, service::PhysicalPresenceActionClass::kProvisioningEnroll, nullptr,
            hal_time_now_ms());
        g_request_body = "{\"proof_of_possession\":\"deadbeef\",\"admin_password\":\"a whole new admin\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(enroll->handler(&req) == ESP_OK);
        assert(g_last_status.empty());
        assert(!service::commissioning_window_is_active(commissioning_window, hal_time_now_ms()));

        service::AdminVerifierRecord enrolled_record{};
        assert(service::get_stored_admin_verifier(&enrolled_record) == service::SecureStorageStatus::kAvailable);
        assert(service::verify_admin_password("a whole new admin", enrolled_record));
    }

    // --- POST /api/v1/system/factory-reset/operations: wrapped
    // mutation-grade, plus PoP and physical-presence checks of its own.
    // Always ends in capability_unavailable (S8 not built) once policy
    // checks pass -- the plan's own named stub outcome, not a failure. ---
    {
        const std::string cookie_header = "zgw_session=" + session_id_hex;
        httpd_req_t req{};
        req.user_ctx = factory_reset->user_ctx;
        req.method = HTTP_POST;
        req.mock_cookie_header = cookie_header.c_str();
        req.mock_csrf_header = csrf_token_hex.c_str();
        req.mock_origin_header = context.expected_origin;

        g_request_body = "{\"proof_of_possession\":\"00\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(factory_reset->handler(&req) == ESP_OK);
        assert(g_last_status == "401 Unauthorized");

        // Reset req.user_ctx to the wrapper's own binding before every
        // subsequent call -- see the auth/password test block's own
        // comment for why this is load-bearing, not defensive noise.
        req.user_ctx = factory_reset->user_ctx;
        g_request_body = "{\"proof_of_possession\":\"deadbeef\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(factory_reset->handler(&req) == ESP_OK);
        assert(g_last_status == "403 Forbidden");

        service::physical_presence_grant_create(
            &physical_presence, service::PhysicalPresenceActionClass::kFactoryReset, session_id_hex.c_str(),
            hal_time_now_ms());
        req.user_ctx = factory_reset->user_ctx;
        g_request_body = "{\"proof_of_possession\":\"deadbeef\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(factory_reset->handler(&req) == ESP_OK);
        assert(g_last_status == "503 Service Unavailable");
        assert(g_last_response.find("capability_unavailable") != std::string::npos);
    }

    // --- POST /api/v1/security/certificates/operations: wrapped
    // mutation-grade (kSecurityAdmin). hal_tls_validate_certificate()
    // always fails closed on host (no real mbedtls -- see
    // hal_tls_certificate_validator.h's own header comment), so the
    // "candidate actually validates, grant gets consumed, staging slot
    // gets written" success path can only be exercised via a real
    // idf.py build -- exactly the same host-testability boundary
    // cert_rotation_state.hpp's own tests already document. What IS
    // host-testable: every rejection this handler applies BEFORE ever
    // reaching the real validator call. ---
    {
        const std::string cookie_header = "zgw_session=" + session_id_hex;
        httpd_req_t req{};
        req.user_ctx = certificates->user_ctx;
        req.method = HTTP_POST;
        req.mock_cookie_header = cookie_header.c_str();
        req.mock_csrf_header = csrf_token_hex.c_str();
        req.mock_origin_header = context.expected_origin;

        // Missing fields -> 400, before any decoding is attempted.
        g_request_body = "{\"certificate_pem_hex\":\"deadbeef00\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(certificates->handler(&req) == ESP_OK);
        assert(g_last_status == "400 Bad Request");

        // Malformed hex (odd length) -> 400.
        req.user_ctx = certificates->user_ctx;
        g_request_body = "{\"certificate_pem_hex\":\"abc\",\"private_key_pem_hex\":\"deadbeef00\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(certificates->handler(&req) == ESP_OK);
        assert(g_last_status == "400 Bad Request");

        // Well-formed hex, but missing the trailing-NUL byte mbedtls's
        // own PEM convention requires ("deadbeef" decodes to bytes with
        // no trailing 0x00) -> 400, caught by this handler's own
        // explicit check before ever calling the real validator.
        req.user_ctx = certificates->user_ctx;
        g_request_body = "{\"certificate_pem_hex\":\"deadbeef\",\"private_key_pem_hex\":\"deadbeef00\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(certificates->handler(&req) == ESP_OK);
        assert(g_last_status == "400 Bad Request");

        // Well-formed candidate (both fields end in a NUL byte), but no
        // product CA has ever been provisioned in this test's storage --
        // 503, and this happens BEFORE the real validator is ever
        // called (CA is read first).
        req.user_ctx = certificates->user_ctx;
        g_request_body = "{\"certificate_pem_hex\":\"deadbeef00\",\"private_key_pem_hex\":\"deadbeef00\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(certificates->handler(&req) == ESP_OK);
        assert(g_last_status == "503 Service Unavailable");
        assert(g_last_response.find("capability_unavailable") != std::string::npos);

        // Provision a placeholder CA (synthetic bytes, never anything
        // resembling real key material -- same convention every other
        // S5/S6 test in this repository already follows) so the request
        // reaches the real validator this time. hal_tls_validate_
        // certificate() always fails closed on host regardless of input
        // -> 400 "candidate failed validation", and (unlike auth/
        // password's own test, which CAN reach and prove its grant-
        // ordering) no physical-presence grant is ever consulted here --
        // the real validator call is an unconditional gate on host, the
        // one honest limit this sub-slice's own evidence file names.
        assert(
            service::tls_identity_set_product_ca(
                reinterpret_cast<const uint8_t*>("placeholder-ca\0"), 15U) ==
            service::SecureStorageWriteResult::kWritten);
        req.user_ctx = certificates->user_ctx;
        g_request_body = "{\"certificate_pem_hex\":\"deadbeef00\",\"private_key_pem_hex\":\"deadbeef00\"}";
        req.content_len = static_cast<int>(g_request_body.size());
        clear_http_capture();
        assert(certificates->handler(&req) == ESP_OK);
        assert(g_last_status == "400 Bad Request");
        // No grant was consumed by any of the above -- confirmed by
        // factory-reset's own already-created kFactoryReset grant (from
        // the block above) still being irrelevant here (different action
        // class), and no kCertificateRotation grant was ever created in
        // this test at all, yet nothing here ever asked for one (the
        // validator gate rejected every attempt first).
    }

    // --- POST /api/v1/auth/logout: wrapped mutation-grade -- needs
    // cookie + matching CSRF + matching Origin. ---
    {
        const std::string cookie_header = "zgw_session=" + session_id_hex;

        // Missing CSRF header -> rejected by the wrapper, session still
        // alive (revoke inside the real handler never runs).
        httpd_req_t req{};
        req.user_ctx = logout->user_ctx;  // the wrapper's own binding, see the session-route comment above
        req.method = HTTP_POST;
        req.mock_cookie_header = cookie_header.c_str();
        req.mock_csrf_header = nullptr;
        req.mock_origin_header = context.expected_origin;
        clear_http_capture();
        assert(logout->handler(&req) == ESP_OK);
        assert(g_last_status == "403 Forbidden");

        // Correct CSRF + Origin -> allowed, session actually revoked, and
        // the clear-cookie header is sent.
        req.mock_csrf_header = csrf_token_hex.c_str();
        clear_http_capture();
        assert(logout->handler(&req) == ESP_OK);
        assert(g_last_status.empty());
        const std::string* set_cookie = find_response_header("Set-Cookie");
        assert(set_cookie != nullptr);
        assert(set_cookie->find("Max-Age=0") != std::string::npos);

        // The session is gone: GET /auth/session with the same (now
        // revoked) cookie is rejected again.
        clear_http_capture();
        httpd_req_t session_req{};
        session_req.user_ctx = session_get->user_ctx;
        session_req.method = HTTP_GET;
        session_req.mock_cookie_header = cookie_header.c_str();
        assert(session_get->handler(&session_req) == ESP_OK);
        assert(g_last_status == "401 Unauthorized");
    }

    return 0;
}
