/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_server.hpp"

#ifdef ESP_PLATFORM
#include <cstddef>
#include "esp_http_server.h"
#include "esp_log.h"
#endif

#include "log_tags.h"
#include "web_routes.hpp"

namespace web_ui {

#ifdef ESP_PLATFORM
namespace {
constexpr const char* kTag = LOG_TAG_WEB_SERVER;
}
#endif

WebServer::WebServer(service::ServiceRuntimeApi& runtime) noexcept
    : runtime_(&runtime) {
    route_context_.runtime = runtime_;
    route_context_.next_correlation_id = &next_correlation_id_;
}

bool WebServer::start() noexcept {
    if (started_) {
        return true;
    }

    if (runtime_ == nullptr) {
        return false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    // Keep headroom for future API additions.
    config.max_uri_handlers = 24;
#ifdef ESP_PLATFORM
    // Keep the HTTPD socket ceiling modest so MQTT and other system paths still
    // retain headroom in the tiny ESP32-C6 socket budget.
    config.max_open_sockets = 4;
    config.backlog_conn = 8;
#endif
    // Devices/network snapshots grew after Phase 3 (OTA + Tuya identity fields),
    // so the 12KB HTTPD stack is no longer sufficient under real UI polling.
    // Keep the runtime behavior close to the rollback candidate, but restore a
    // safer stack budget to avoid mixing stack faults with Zigbee regressions.
    config.stack_size = 20480;

#ifdef ESP_PLATFORM
    // Keep the HTTP socket lifecycle simple under mixed static-asset fetches and
    // API polling: prefer short-lived connections over long keep-alive reuse so
    // browser-held idle sockets do not consume most of the tiny ESP32-C6 budget.
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 15;
    config.enable_so_linger = false;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 5;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 2;
    ESP_LOGI(kTag, "Starting HTTP server stack_size=%u max_uri_handlers=%u", config.stack_size, config.max_uri_handlers);
#endif

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
