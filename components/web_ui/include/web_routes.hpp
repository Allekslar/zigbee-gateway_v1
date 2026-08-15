/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstdint>

#include "session_store.hpp"

namespace service {
class ServiceRuntimeApi;
}

namespace web_ui {

struct WebRouteContext {
    service::ServiceRuntimeApi* runtime{nullptr};
    std::atomic<uint32_t>* next_correlation_id{nullptr};
    // Plan S6 "Authorization and physical presence" #17/#19: the one
    // live session store every auth-aware v1 handler (via
    // authorize_v1_request()) and the /api/v1/auth/* routes themselves
    // read/write. Owned by WebServer (session_store_ member), never null
    // once WebServer::start() has run.
    service::SessionStoreState* sessions{nullptr};
    // This gateway's own origin ("https://<production mDNS host>.local"),
    // built once at WebServer::start() time -- plan #15's same-origin
    // check needs it on every mutation request. Owned by WebServer as a
    // fixed buffer, never null once WebServer::start() has run.
    const char* expected_origin{nullptr};
};

bool register_web_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_static_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_device_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_network_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_config_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_ota_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_rcp_routes(void* server_handle, WebRouteContext* context) noexcept;

// Versioned /api/v1 contract (plan S4 HTTP #1-#5). Plan S4 required
// changes #28/#29 mandated these stay unregistered production contracts
// until "S6 owns the first production registration path" -- INV-H010
// enforced that by name-grepping main/app_main.cpp specifically (never
// components/web_ui/web_server.cpp, the real composition root). This is
// now that path: web_server.cpp's start_production_https() calls
// register_web_routes_v1() (plan #19's central authorization wired in via
// authorize_v1_request(), see web_v1_common.hpp) after certificate/trust
// validation and secure-storage readiness, exactly matching the plan
// text quoted above register_web_routes_v1()'s own definition. app_main.cpp
// itself still never calls any of these directly -- INV-H010's grep
// target -- so the invariant continues to hold unmodified. See
// docs/architecture/HTTP_API_V1.md.
bool register_web_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_capabilities_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_device_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_network_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_config_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_ota_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_rcp_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_operations_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
// Plan #17 (basic slice): POST /api/v1/auth/login, POST /api/v1/auth/logout,
// GET /api/v1/auth/session. The other #17 routes (provisioning/enroll,
// auth/password, certificate rotation, factory-reset) need the not-yet-
// built physical-presence grant (#20-22) and stay unregistered for now --
// see docs/security/CONTROL_PLANE_SECURITY.md's own section for this
// sub-slice.
bool register_auth_routes_v1(void* server_handle, WebRouteContext* context) noexcept;

}  // namespace web_ui
