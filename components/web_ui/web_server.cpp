/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_server.hpp"

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#endif

#include "web_routes.hpp"

namespace web_ui {

WebServer::WebServer(core::CoreRegistry& registry, service::ServiceRuntime& runtime) noexcept
    : registry_(&registry), runtime_(&runtime) {
    route_context_.registry = registry_;
    route_context_.runtime = runtime_;
    route_context_.next_correlation_id = &next_correlation_id_;
}

bool WebServer::start() noexcept {
    if (started_) {
        return true;
    }

    if (registry_ == nullptr || runtime_ == nullptr) {
        return false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    // Route count can exceed 16 in debug builds (extra raw-debug endpoint).
    // Keep headroom for future API additions.
    config.max_uri_handlers = 24;
    // Some handlers format multi-field JSON responses and can overflow
    // default 4KB HTTPD stack on ESP32-C6 under test load.
    config.stack_size = 8192;

    httpd_handle_t handle = nullptr;
    if (httpd_start(&handle, &config) != ESP_OK) {
        return false;
    }

    if (!register_web_routes(static_cast<void*>(handle), &route_context_)) {
        (void)httpd_stop(handle);
        return false;
    }

    server_handle_ = static_cast<void*>(handle);
    started_ = true;
    return started_;
}

void WebServer::stop() noexcept {
    if (server_handle_ != nullptr) {
        (void)httpd_stop(static_cast<httpd_handle_t>(server_handle_));
        server_handle_ = nullptr;
    }

    started_ = false;
}

bool WebServer::started() const noexcept {
    return started_;
}

}  // namespace web_ui
