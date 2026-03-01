/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>

#include "web_routes.hpp"

namespace core {
class CoreRegistry;
}

namespace service {
class ServiceRuntime;
}

namespace web_ui {

class WebServer {
public:
    WebServer(core::CoreRegistry& registry, service::ServiceRuntime& runtime) noexcept;

    bool start() noexcept;
    void stop() noexcept;
    bool started() const noexcept;

private:
    core::CoreRegistry* registry_{nullptr};
    service::ServiceRuntime* runtime_{nullptr};
    void* server_handle_{nullptr};
    std::atomic<uint32_t> next_correlation_id_{1};
    WebRouteContext route_context_{};
    bool started_{false};
};

}  // namespace web_ui
