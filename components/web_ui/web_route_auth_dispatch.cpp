/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_route_auth_dispatch.hpp"

#include <algorithm>
#include <iterator>

#include "web_v1_common.hpp"

namespace web_ui {

namespace {

struct AuthenticatedRouteBinding {
    // `decltype(httpd_uri_t{}.handler)`, not the host-only `httpd_handler_t`
    // name: the real esp_http_server.h declares `httpd_uri_t::handler`
    // inline (`esp_err_t (*handler)(httpd_req_t *r)`), with no portable
    // named typedef -- `httpd_handler_t` only exists in this project's own
    // host mock (web_handler_common.hpp), so naming it here would not
    // compile against the real target.
    decltype(httpd_uri_t{}.handler) real_handler{nullptr};
    WebRouteContext* context{nullptr};
    service::Capability required{service::Capability::kReadStatus};
};

AuthenticatedRouteBinding g_bindings[kMaxAuthenticatedRoutes]{};
uint32_t g_binding_count = 0U;

esp_err_t authenticated_dispatch_trampoline(httpd_req_t* req) {
    if (req == nullptr) {
        return ESP_FAIL;
    }
    auto* binding = static_cast<AuthenticatedRouteBinding*>(req->user_ctx);
    if (binding == nullptr || binding->real_handler == nullptr || binding->context == nullptr) {
        return ESP_FAIL;
    }

    const service::RouteAuthResult result = authorize_v1_request(req, binding->context, binding->required);
    if (result != service::RouteAuthResult::kAllowed) {
        return send_v1_auth_error(req, result);
    }

    // Restore the user_ctx every real handler's own body expects
    // (WebRouteContext*, exactly as every pre-existing v1 registration
    // already sets it) -- the binding above was only ever a registration-
    // time/dispatch-time detail, invisible to the real handler.
    req->user_ctx = binding->context;
    return binding->real_handler(req);
}

}  // namespace

bool register_authenticated_uri_handler_v1(
    void* server_handle, const httpd_uri_t& uri_def, service::Capability required) noexcept {
    if (server_handle == nullptr || uri_def.handler == nullptr || uri_def.user_ctx == nullptr) {
        return false;
    }
    if (g_binding_count >= kMaxAuthenticatedRoutes) {
        return false;
    }

    AuthenticatedRouteBinding& binding = g_bindings[g_binding_count];
    binding.real_handler = uri_def.handler;
    binding.context = static_cast<WebRouteContext*>(uri_def.user_ctx);
    binding.required = required;
    ++g_binding_count;

    httpd_uri_t wrapped = uri_def;
    wrapped.handler = &authenticated_dispatch_trampoline;
    wrapped.user_ctx = &binding;

    return httpd_register_uri_handler(static_cast<httpd_handle_t>(server_handle), &wrapped) == ESP_OK;
}

void reset_authenticated_route_bindings_for_test() noexcept {
    g_binding_count = 0U;
    std::fill(std::begin(g_bindings), std::end(g_bindings), AuthenticatedRouteBinding{});
}

}  // namespace web_ui
