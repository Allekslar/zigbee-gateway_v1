/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>

#include "commissioning_window.hpp"
#include "physical_presence_grant.hpp"
#include "provisioning_secret_provider.hpp"
#include "session_store.hpp"
#include "web_routes.hpp"

namespace service {
class ServiceRuntimeApi;
}

namespace web_ui {

class WebServer {
public:
    explicit WebServer(service::ServiceRuntimeApi& runtime) noexcept;

    bool start() noexcept;
    void stop() noexcept;
    bool started() const noexcept;

private:
    service::ServiceRuntimeApi* runtime_{nullptr};
    void* server_handle_{nullptr};
    std::atomic<uint32_t> next_correlation_id_{1};
    // Plan S6 "Authorization and physical presence" #17/#19: the one
    // live session store, owned here (RAM-only by design -- see
    // session_store.hpp) and reached by every v1 handler via
    // route_context_.sessions.
    service::SessionStoreState session_store_{};
    // "https://<production mDNS host>.local", built once in start()
    // (plan #9's mDNS host, prefixed with the scheme the production
    // listener actually serves -- see web_server.cpp) -- plan #15's
    // same-origin check needs it on every mutation request.
    char expected_origin_[64]{};
    // Plan #20/#21: RAM-only, boot-fresh -- see WebRouteContext's own
    // comment for why nothing populates this yet.
    service::PhysicalPresenceGrantState physical_presence_{};
    // Plan #3: auto-started in start() when
    // commissioning_window_first_boot_policy_applies() (no admin
    // credential exists yet).
    service::CommissioningWindowState commissioning_window_{};
    // Plan #2/#17/#22: fetched exactly once in start() -- see
    // WebRouteContext's own comment for why "exactly once" (not
    // per-request) is load-bearing, not just an optimization.
    service::ProvisioningSecret provisioning_secret_{};
    WebRouteContext route_context_{};
    bool started_{false};
    // Plan S6 "HTTPS and sessions" #7: which start function created
    // server_handle_ determines which stop function correctly tears it
    // down (httpd_ssl_stop() vs httpd_stop()) -- see web_server.cpp.
    bool using_https_{false};
};

}  // namespace web_ui
