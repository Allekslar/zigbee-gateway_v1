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
    web_ui::WebRouteContext context{};
    context.runtime = &runtime;
    context.next_correlation_id = &next_id;
    context.sessions = &sessions;
    context.expected_origin = "https://zigbee-gateway-test.local";

    assert(web_ui::register_auth_routes_v1(reinterpret_cast<void*>(1), &context));
    const CapturedRoute* login = find_captured("/api/v1/auth/login", HTTP_POST);
    const CapturedRoute* logout = find_captured("/api/v1/auth/logout", HTTP_POST);
    const CapturedRoute* session_get = find_captured("/api/v1/auth/session", HTTP_GET);
    assert(login != nullptr && login->handler != nullptr);
    assert(logout != nullptr && logout->handler != nullptr);
    assert(session_get != nullptr && session_get->handler != nullptr);

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
