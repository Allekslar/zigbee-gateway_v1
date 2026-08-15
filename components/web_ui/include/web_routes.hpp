/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstdint>

#include "commissioning_window.hpp"
#include "physical_presence_grant.hpp"
#include "provisioning_secret_provider.hpp"
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
    // Plan #20/#21: the one live physical-presence grant state every
    // consuming route (auth/password, provisioning/enroll, certificate
    // rotation, factory-reset) reads/consumes. Owned by WebServer. Nothing
    // creates a grant in it yet (no button-debounce task exists) -- every
    // consuming route therefore fails closed today, honestly, matching
    // this project's own established "consumer built and wired against a
    // real primitive before the primitive's own upstream input exists"
    // precedent (e.g. Section 2.10's TLS validation before any real
    // certificate existed).
    service::PhysicalPresenceGrantState* physical_presence{nullptr};
    // Plan #3's commissioning window, auto-started by WebServer::start()
    // when commissioning_window_first_boot_policy_applies() (no admin
    // credential exists yet). `provisioning/enroll` is the one real
    // consumer of commissioning_window_is_active().
    service::CommissioningWindowState* commissioning_window{nullptr};
    // The manufacturing/development proof-of-possession value
    // `provisioning/enroll` and the factory-reset PoP challenge (#22)
    // both compare a caller-submitted candidate against, via
    // provisioning_secret_provider_matches(). Cached ONCE by
    // WebServer::start() (not re-fetched per request) specifically
    // because the development adapter regenerates a fresh value on every
    // call -- caching is what makes a real challenge/response actually
    // possible in development mode, where the value is logged once at
    // that same call site for an installer to read.
    const service::ProvisioningSecret* provisioning_secret{nullptr};
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
