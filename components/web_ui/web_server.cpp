/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_server.hpp"

#include <chrono>

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#endif

#include "log_tags.h"
#include "web_routes.hpp"

namespace web_ui {

#ifdef ESP_PLATFORM
namespace {
constexpr const char* kTag = LOG_TAG_WEB_SERVER;
std::atomic<uint32_t> g_last_page_load_activity_ms{0U};
std::atomic<bool> g_page_load_seen{false};

uint32_t monotonic_now_ms() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}
}
#else
namespace {
std::atomic<uint32_t> g_last_page_load_activity_ms{0U};
std::atomic<bool> g_page_load_seen{false};

uint32_t monotonic_now_ms() noexcept {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
    return static_cast<uint32_t>(elapsed);
}
}
#endif

void note_page_load_activity() noexcept {
    g_page_load_seen.store(true, std::memory_order_relaxed);
    g_last_page_load_activity_ms.store(monotonic_now_ms(), std::memory_order_relaxed);
}

bool has_page_load_activity() noexcept {
    return g_page_load_seen.load(std::memory_order_relaxed);
}

uint32_t page_load_idle_ms() noexcept {
    if (!has_page_load_activity()) {
        return 0U;
    }
    const uint32_t last = g_last_page_load_activity_ms.load(std::memory_order_relaxed);
    return monotonic_now_ms() - last;
}

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
    // Leave socket headroom for MQTT reconnects and system traffic.
    config.max_open_sockets = 6;
    config.backlog_conn = 4;
#endif
    // Some handlers format multi-field JSON responses and can overflow
    // default 4KB HTTPD stack on ESP32-C6 under real traffic.
    // Increased to keep response streaming off the edge of the task stack.
    config.stack_size = 20480;

#ifdef ESP_PLATFORM
    config.lru_purge_enable = false;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 30;
    config.keep_alive_enable = false;
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
