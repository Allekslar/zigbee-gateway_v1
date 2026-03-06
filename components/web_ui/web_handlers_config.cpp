/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_routes.hpp"

#include <inttypes.h>
#include <cstdio>

#include "core_events.hpp"
#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#endif
#include "service_runtime.hpp"
#include "web_handler_common.hpp"

namespace web_ui {

namespace {

bool parse_reporting_profile_request(
    const char* body,
    service::ConfigManager::ReportingProfile* out_profile,
    const char** out_error) noexcept {
    if (body == nullptr || out_profile == nullptr || out_error == nullptr) {
        return false;
    }

    *out_error = "invalid_payload";
    service::ConfigManager::ReportingProfile profile{};
    profile.in_use = true;

    uint32_t short_addr_raw = 0;
    uint32_t endpoint_raw = 0;
    uint32_t cluster_id_raw = 0;
    uint32_t min_interval_raw = 0;
    uint32_t max_interval_raw = 0;
    if (!find_json_u32_field(body, "short_addr", &short_addr_raw) ||
        !find_json_u32_field(body, "endpoint", &endpoint_raw) ||
        !find_json_u32_field(body, "cluster_id", &cluster_id_raw) ||
        !find_json_u32_field(body, "min_interval_seconds", &min_interval_raw) ||
        !find_json_u32_field(body, "max_interval_seconds", &max_interval_raw)) {
        return false;
    }

    if (short_addr_raw == 0U || short_addr_raw > 0xFFFFU ||
        short_addr_raw == static_cast<uint32_t>(core::kUnknownDeviceShortAddr) ||
        endpoint_raw == 0U || endpoint_raw > 0xFFU || cluster_id_raw == 0U ||
        cluster_id_raw > 0xFFFFU || min_interval_raw > 0xFFFFU ||
        max_interval_raw > 0xFFFFU) {
        *out_error = "invalid_profile_key";
        return false;
    }

    if (max_interval_raw == 0U || min_interval_raw > max_interval_raw) {
        *out_error = "invalid_profile_bounds";
        return false;
    }

    uint32_t reportable_change_raw = 0;
    (void)find_json_u32_field(body, "reportable_change", &reportable_change_raw);

    uint32_t capability_flags_raw = 0;
    if (find_json_u32_field(body, "capability_flags", &capability_flags_raw)) {
        if (capability_flags_raw > 0xFFU) {
            *out_error = "invalid_capability_flags";
            return false;
        }
        profile.capability_flags = static_cast<uint8_t>(capability_flags_raw);
    }

    profile.key.short_addr = static_cast<uint16_t>(short_addr_raw);
    profile.key.endpoint = static_cast<uint8_t>(endpoint_raw);
    profile.key.cluster_id = static_cast<uint16_t>(cluster_id_raw);
    profile.min_interval_seconds = static_cast<uint16_t>(min_interval_raw);
    profile.max_interval_seconds = static_cast<uint16_t>(max_interval_raw);
    profile.reportable_change = reportable_change_raw;

    *out_profile = profile;
    return true;
}

esp_err_t config_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    service::ServiceRuntime::ConfigApiSnapshot snapshot{};
    if (!context->runtime->build_config_api_snapshot(&snapshot)) {
        return send_json_error(req, "500 Internal Server Error", "snapshot_unavailable");
    }

    char response[224]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"revision\":%" PRIu32 ",\"last_command_status\":%u,\"command_timeout_ms\":%" PRIu32 ",\"max_command_retries\":%u,\"autoconnect_failures\":%" PRIu32 "}",
        snapshot.revision,
        static_cast<unsigned>(snapshot.last_command_status),
        snapshot.command_timeout_ms,
        static_cast<unsigned>(snapshot.max_command_retries),
        snapshot.autoconnect_failures);
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

esp_err_t config_reporting_post_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_json_error(req, "400 Bad Request", "invalid_body");
    }

    service::ConfigManager::ReportingProfile profile{};
    const char* parse_error = "invalid_payload";
    if (!parse_reporting_profile_request(body, &profile, &parse_error)) {
        return send_json_error(req, "400 Bad Request", parse_error);
    }

    if (!context->runtime->post_reporting_profile_write(profile)) {
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

    httpd_uri_t config_reporting_post_uri{};
    config_reporting_post_uri.uri = "/api/config/reporting";
    config_reporting_post_uri.method = HTTP_POST;
    config_reporting_post_uri.handler = config_reporting_post_handler;
    config_reporting_post_uri.user_ctx = context;

    auto handle = static_cast<httpd_handle_t>(server_handle);
    if (httpd_register_uri_handler(handle, &config_get_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &config_post_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &config_reporting_post_uri) != ESP_OK) {
        return false;
    }

    return true;
}

}  // namespace web_ui
