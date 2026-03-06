/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstdint>

namespace service {
class ServiceRuntime;
}

namespace web_ui {

struct WebRouteContext {
    service::ServiceRuntime* runtime{nullptr};
    std::atomic<uint32_t>* next_correlation_id{nullptr};
};

bool register_web_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_static_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_device_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_network_routes(void* server_handle, WebRouteContext* context) noexcept;
bool register_config_routes(void* server_handle, WebRouteContext* context) noexcept;

}  // namespace web_ui
