/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_routes.hpp"

#include <inttypes.h>
#include <cstdio>

#include "core_registry.hpp"
#include "core_state.hpp"
#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#endif
#include "service_runtime.hpp"
#include "web_handler_common.hpp"

namespace web_ui {

namespace {

esp_err_t config_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    core::CoreRegistry::SnapshotRef snapshot{};
    if (!context->registry->pin_current(&snapshot) || !snapshot.valid()) {
        return send_json_error(req, "500 Internal Server Error", "snapshot_unavailable");
    }

    const core::CoreState state = *snapshot.state;
    context->registry->release_snapshot(&snapshot);

    const service::ServiceRuntime::ConfigSnapshot config = context->runtime->config_snapshot();
    const auto& stats = context->runtime->stats();

    char response[224]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"revision\":%" PRIu32 ",\"last_command_status\":%u,\"command_timeout_ms\":%" PRIu32 ",\"max_command_retries\":%u,\"autoconnect_failures\":%" PRIu32 "}",
        state.revision,
        static_cast<unsigned>(state.last_command_status),
        config.command_timeout_ms,
        static_cast<unsigned>(config.max_command_retries),
        stats.autoconnect_failures);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t config_post_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_json_error(req, "400 Bad Request", "invalid_body");
    }

    service::ServiceRuntime::ConfigWriteRequest request{};
    uint32_t timeout_ms = 0;
    if (find_json_u32_field(body, "command_timeout_ms", &timeout_ms)) {
        if (timeout_ms == 0U) {
            return send_json_error(req, "400 Bad Request", "invalid_command_timeout_ms");
        }
        request.set_timeout_ms = true;
        request.timeout_ms = timeout_ms;
    }

    uint32_t max_retries_raw = 0;
    if (find_json_u32_field(body, "max_command_retries", &max_retries_raw)) {
        if (max_retries_raw > 5U) {
            return send_json_error(req, "400 Bad Request", "invalid_max_command_retries");
        }
        request.set_max_retries = true;
        request.max_retries = static_cast<uint8_t>(max_retries_raw);
    }

    if (!request.set_timeout_ms && !request.set_max_retries) {
        return send_json_error(req, "400 Bad Request", "empty_update");
    }

    if (!context->runtime->post_config_write(request)) {
        return send_json_error(req, "503 Service Unavailable", "config_queue_full");
    }

    char response[64]{};
    const int written = std::snprintf(response, sizeof(response), "{\"accepted\":true}");
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

}  // namespace

bool register_config_routes(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t config_get_uri{};
    config_get_uri.uri = "/api/config";
    config_get_uri.method = HTTP_GET;
    config_get_uri.handler = config_get_handler;
    config_get_uri.user_ctx = context;

    httpd_uri_t config_post_uri{};
    config_post_uri.uri = "/api/config";
    config_post_uri.method = HTTP_POST;
    config_post_uri.handler = config_post_handler;
    config_post_uri.user_ctx = context;

    auto handle = static_cast<httpd_handle_t>(server_handle);
    if (httpd_register_uri_handler(handle, &config_get_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &config_post_uri) != ESP_OK) {
        return false;
    }

    return true;
}

}  // namespace web_ui
