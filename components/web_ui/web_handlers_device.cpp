/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_routes.hpp"

#include <inttypes.h>
#include <cstddef>
#include <cstdio>

#include "core_commands.hpp"
#include "core_events.hpp"
#include "core_errors.hpp"
#include "core_registry.hpp"
#include "core_state.hpp"
#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#endif
#include "log_tags.h"
#include "service_runtime.hpp"
#include "web_handler_common.hpp"

namespace web_ui {

namespace {

constexpr const char* kTag = LOG_TAG_WEB_DEVICE;
constexpr uint32_t kDefaultForceRemoveTimeoutMs = 5000U;

esp_err_t send_async_accept(httpd_req_t* req, uint32_t request_id, const char* operation) {
    if (req == nullptr || request_id == 0 || operation == nullptr) {
        return ESP_FAIL;
    }

    char response[192]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"accepted\":true,\"request_id\":%" PRIu32 ",\"operation\":\"%s\"}",
        request_id,
        operation);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_status(req, "202 Accepted");
    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t devices_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    service::ServiceRuntime::DevicesApiSnapshot devices_snapshot{};
    if (!context->runtime->build_devices_api_snapshot(now_ms, &devices_snapshot)) {
        return send_json_error(req, "500 Internal Server Error", "snapshot_unavailable");
    }
    const core::CoreState& state = devices_snapshot.state;
    const service::ServiceRuntime::DevicesRuntimeSnapshot& runtime_snapshot = devices_snapshot.runtime;

    (void)httpd_resp_set_type(req, "application/json");

    char chunk[320]{};
    int written = std::snprintf(
        chunk,
        sizeof(chunk),
        "{\"revision\":%" PRIu32 ",\"device_count\":%u,\"join_window_open\":%s,\"join_window_seconds_left\":%u,\"devices\":[",
        state.revision,
        static_cast<unsigned>(state.device_count),
        runtime_snapshot.join_window_open ? "true" : "false",
        static_cast<unsigned>(runtime_snapshot.join_window_seconds_left));
    if (written <= 0 || written >= static_cast<int>(sizeof(chunk))) {
        return ESP_FAIL;
    }
    if (httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }

    bool first = true;
    for (std::size_t i = 0; i < state.devices.size(); ++i) {
        const auto& device = state.devices[i];
        if (device.short_addr == core::kUnknownDeviceShortAddr) {
            continue;
        }

        const uint32_t force_remove_ms_left = runtime_snapshot.force_remove_ms_left[i];
        const bool force_remove_armed = force_remove_ms_left > 0U;

        written = std::snprintf(
            chunk,
            sizeof(chunk),
            "%s{\"short_addr\":%u,\"online\":%s,\"power_on\":%s,\"force_remove_armed\":%s,\"force_remove_ms_left\":%lu}",
            first ? "" : ",",
            static_cast<unsigned>(device.short_addr),
            device.online ? "true" : "false",
            device.power_on ? "true" : "false",
            force_remove_armed ? "true" : "false",
            static_cast<unsigned long>(force_remove_ms_left));
        if (written <= 0 || written >= static_cast<int>(sizeof(chunk))) {
            return ESP_FAIL;
        }
        if (httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            return ESP_FAIL;
        }
        first = false;
    }

    if (httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }

    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t device_power_post_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_json_error(req, "400 Bad Request", "invalid_body");
    }

    uint32_t short_addr_raw = 0;
    bool desired_power = false;
    if (!find_json_u32_field(body, "short_addr", &short_addr_raw) ||
        !find_json_bool_field(body, "power_on", &desired_power) ||
        short_addr_raw > 0xFFFFU) {
        return send_json_error(req, "400 Bad Request", "invalid_payload");
    }

    core::CoreCommand command{};
    command.type = core::CoreCommandType::kSetDevicePower;
    command.correlation_id = allocate_correlation_id(context);
    command.device_short_addr = static_cast<uint16_t>(short_addr_raw);
    command.desired_power_on = desired_power;
    command.issued_at_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    const core::CoreError submit_result = context->runtime->post_command(command);
    if (submit_result != core::CoreError::kOk) {
        if (submit_result == core::CoreError::kInvalidArgument) {
            return send_json_error(req, "400 Bad Request", "invalid_command");
        }
        return send_json_error(req, "503 Service Unavailable", "command_rejected");
    }

    char response[128]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"accepted\":true,\"correlation_id\":%" PRIu32 "}",
        command.correlation_id);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t device_join_post_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_json_error(req, "400 Bad Request", "invalid_body");
    }

    uint16_t join_window_seconds_left = 0U;
    if (context->runtime->get_join_window_status(&join_window_seconds_left)) {
        char response[128]{};
        const int written = std::snprintf(
            response,
            sizeof(response),
            "{\"error\":\"join_window_already_open\",\"seconds_left\":%u}",
            static_cast<unsigned>(join_window_seconds_left));
        if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
            return ESP_FAIL;
        }

        (void)httpd_resp_set_status(req, "409 Conflict");
        (void)httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    uint32_t duration_seconds_raw = 30;
    (void)find_json_u32_field(body, "duration_seconds", &duration_seconds_raw);
    if (duration_seconds_raw == 0U || duration_seconds_raw > 255U) {
        ESP_LOGW(kTag, "HTTP POST /api/devices/join invalid duration_seconds=%lu", static_cast<unsigned long>(duration_seconds_raw));
        return send_json_error(req, "400 Bad Request", "invalid_duration_seconds");
    }

    const uint32_t request_id = allocate_correlation_id(context);
    ESP_LOGI(
        kTag,
        "HTTP POST /api/devices/join request_id=%lu duration_seconds=%lu",
        static_cast<unsigned long>(request_id),
        static_cast<unsigned long>(duration_seconds_raw));
    if (!context->runtime->post_open_join_window(request_id, static_cast<uint16_t>(duration_seconds_raw))) {
        ESP_LOGW(kTag, "Join request queue full request_id=%lu", static_cast<unsigned long>(request_id));
        return send_json_error(req, "503 Service Unavailable", "join_queue_full");
    }
    return send_async_accept(req, request_id, "open_join_window");
}

esp_err_t device_remove_post_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_json_error(req, "400 Bad Request", "invalid_body");
    }

    uint32_t short_addr_raw = 0;
    if (!find_json_u32_field(body, "short_addr", &short_addr_raw) || short_addr_raw > 0xFFFFU ||
        short_addr_raw == core::kUnknownDeviceShortAddr) {
        return send_json_error(req, "400 Bad Request", "invalid_payload");
    }

    bool force_remove = false;
    (void)find_json_bool_field(body, "force_remove", &force_remove);
    uint32_t force_remove_timeout_ms = kDefaultForceRemoveTimeoutMs;
    uint32_t force_remove_timeout_raw = 0;
    if (force_remove &&
        find_json_u32_field(body, "force_remove_timeout_ms", &force_remove_timeout_raw)) {
        force_remove_timeout_ms = force_remove_timeout_raw;
    }

    const uint32_t request_id = allocate_correlation_id(context);
    if (!context->runtime->post_remove_device(
            request_id,
            static_cast<uint16_t>(short_addr_raw),
            force_remove,
            force_remove_timeout_ms)) {
        return send_json_error(req, "503 Service Unavailable", "remove_queue_full");
    }
    return send_async_accept(req, request_id, "remove_device");
}

}  // namespace

bool register_device_routes(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t devices_get_uri{};
    devices_get_uri.uri = "/api/devices";
    devices_get_uri.method = HTTP_GET;
    devices_get_uri.handler = devices_get_handler;
    devices_get_uri.user_ctx = context;

    httpd_uri_t device_power_post_uri{};
    device_power_post_uri.uri = "/api/devices/power";
    device_power_post_uri.method = HTTP_POST;
    device_power_post_uri.handler = device_power_post_handler;
    device_power_post_uri.user_ctx = context;

    httpd_uri_t device_join_post_uri{};
    device_join_post_uri.uri = "/api/devices/join";
    device_join_post_uri.method = HTTP_POST;
    device_join_post_uri.handler = device_join_post_handler;
    device_join_post_uri.user_ctx = context;

    httpd_uri_t device_remove_post_uri{};
    device_remove_post_uri.uri = "/api/devices/remove";
    device_remove_post_uri.method = HTTP_POST;
    device_remove_post_uri.handler = device_remove_post_handler;
    device_remove_post_uri.user_ctx = context;

    auto handle = static_cast<httpd_handle_t>(server_handle);
    if (httpd_register_uri_handler(handle, &devices_get_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &device_power_post_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &device_join_post_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &device_remove_post_uri) != ESP_OK) {
        return false;
    }

    return true;
}

}  // namespace web_ui
