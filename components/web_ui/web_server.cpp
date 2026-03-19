/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_server.hpp"

#ifdef ESP_PLATFORM
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
    // Reserve socket headroom for OTA HTTPS, MQTT reconnects, and system traffic.
    // A smaller HTTPD ceiling works better than letting UI polling consume most of
    // the global lwIP socket budget on ESP32-C6.
    config.max_open_sockets = 4;
    config.backlog_conn = 8;
#endif
    // Some handlers format multi-field JSON responses and can overflow
    // default 4KB HTTPD stack on ESP32-C6 under real traffic.
    config.stack_size = 12288;

#ifdef ESP_PLATFORM
    // Under repeated UI polling, opening a new TCP connection per request can
    // quickly exhaust the small lwIP socket budget. Keep-alive reduces socket
    // churn, while LRU purge and short wait timeouts still reclaim stale peers.
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 15;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 8;
    config.keep_alive_interval = 4;
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
