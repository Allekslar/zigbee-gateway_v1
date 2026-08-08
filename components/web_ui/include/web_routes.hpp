/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstdint>

namespace service {
class ServiceRuntimeApi;
}

namespace web_ui {

struct WebRouteContext {
    service::ServiceRuntimeApi* runtime{nullptr};
    std::atomic<uint32_t>* next_correlation_id{nullptr};
};

bool register_web_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_static_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_device_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_network_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_config_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_ota_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_rcp_routes(void* server_handle, WebRouteContext* context) noexcept;

// Versioned /api/v1 contract (plan S4 HTTP #1-#5). Deliberately NOT called
// from register_web_routes()/main/app_main.cpp -- plan S4 required changes
// #28/#29 mandate that S4 builds these as unregistered application
// contracts only (host/integration-testable), never a production route
// registration; S6 owns the first production registration path. INV-H010
// locks main/app_main.cpp's absence of any call to these functions. See
// docs/architecture/HTTP_API_V1.md.
bool register_web_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_capabilities_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_device_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_network_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_config_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_ota_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_rcp_routes_v1(void* server_handle, WebRouteContext* context) noexcept;
bool register_operations_routes_v1(void* server_handle, WebRouteContext* context) noexcept;

}  // namespace web_ui
