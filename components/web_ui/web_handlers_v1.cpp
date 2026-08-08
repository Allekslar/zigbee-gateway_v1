/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// Versioned /api/v1 HTTP contract (plan S4 HTTP #1-#5, #9). Covers every
// route in the plan's canonical table except the unified
// GET /api/v1/operations/{operation_id} poll (needs an OperationResultStore
// unification that is a distinct piece of work) and the legacy read-alias/
// 410-Gone-legacy-mutation contract (plan #29 keeps S4 out of the
// production route-registration path entirely) -- see
// docs/architecture/HTTP_API_V1.md. These handlers are built and tested
// but never registered from main/app_main.cpp (plan S4 #28/#29; enforced
// by INV-H010).

#include "web_routes.hpp"

#include <inttypes.h>
#include <cstdio>

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#include "esp_timer.h"
#endif
#include "service_runtime_api.hpp"
#include "web_handler_common.hpp"
#include "web_v1_common.hpp"

namespace web_ui {

namespace {

const char* v1_reporting_state_to_string(service::DeviceReportingState state) noexcept {
    switch (state) {
        case service::DeviceReportingState::kInterviewCompleted:
            return "interview_completed";
        case service::DeviceReportingState::kBindingReady:
            return "binding_ready";
        case service::DeviceReportingState::kReportingConfigured:
            return "reporting_configured";
        case service::DeviceReportingState::kReportingActive:
            return "reporting_active";
        case service::DeviceReportingState::kStale:
            return "stale";
        case service::DeviceReportingState::kUnknown:
        default:
            return "unknown";
    }
}

const char* v1_occupancy_state_to_string(service::DeviceOccupancyState state) noexcept {
    switch (state) {
        case service::DeviceOccupancyState::kNotOccupied:
            return "not_occupied";
        case service::DeviceOccupancyState::kOccupied:
            return "occupied";
        case service::DeviceOccupancyState::kUnknown:
        default:
            return "unknown";
    }
}

const char* v1_contact_state_to_string(service::DeviceContactState state) noexcept {
    switch (state) {
        case service::DeviceContactState::kClosed:
            return "closed";
        case service::DeviceContactState::kOpen:
            return "open";
        case service::DeviceContactState::kUnknown:
        default:
            return "unknown";
    }
}

const char* v1_mqtt_connect_error_token(service::NetworkApiSnapshot::MqttConnectionError error) noexcept {
    using Error = service::NetworkApiSnapshot::MqttConnectionError;
    switch (error) {
        case Error::kDisabled:
            return "disabled";
        case Error::kInitFailed:
            return "init_failed";
        case Error::kStartFailed:
            return "start_failed";
        case Error::kSubscribeFailed:
            return "subscribe_failed";
        case Error::kNone:
        default:
            return "none";
    }
}

esp_err_t capabilities_get_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    const service::RuntimeCapabilities capabilities = context->runtime->capabilities();

    char response[224]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"schema_version\":%u,\"zigbee_available\":%s,\"mqtt_available\":%s,"
        "\"matter_target_available\":%s,\"ota_available\":%s,\"rcp_update_available\":%s}",
        static_cast<unsigned>(kApiV1SchemaVersion),
        capabilities.zigbee_available ? "true" : "false",
        capabilities.mqtt_available ? "true" : "false",
        capabilities.matter_target_available ? "true" : "false",
        capabilities.ota_available ? "true" : "false",
        capabilities.rcp_update_available ? "true" : "false");
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t devices_get_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    service::DevicesApiSnapshot snapshot{};
    if (!context->runtime->build_devices_api_snapshot(now_ms, &snapshot)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }

    (void)httpd_resp_set_type(req, "application/json");

    char chunk[1024]{};
    int written = std::snprintf(
        chunk,
        sizeof(chunk),
        "{\"schema_version\":%u,\"revision\":%" PRIu32 ",\"device_count\":%u,"
        "\"join_window_open\":%s,\"join_window_seconds_left\":%u,\"devices\":[",
        static_cast<unsigned>(kApiV1SchemaVersion),
        snapshot.revision,
        static_cast<unsigned>(snapshot.device_count),
        snapshot.join_window_open ? "true" : "false",
        static_cast<unsigned>(snapshot.join_window_seconds_left));
    if (written <= 0 || written >= static_cast<int>(sizeof(chunk))) {
        return ESP_FAIL;
    }
    if (httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }

    for (std::size_t i = 0; i < snapshot.device_count; ++i) {
        const auto& device = snapshot.devices[i];

        char short_addr_json[16]{};
        char locator_revision_json[16]{};
        if (device.has_locator) {
            std::snprintf(short_addr_json, sizeof(short_addr_json), "%u", static_cast<unsigned>(device.short_addr));
            std::snprintf(
                locator_revision_json, sizeof(locator_revision_json), "%" PRIu32, device.locator_revision);
        } else {
            std::snprintf(short_addr_json, sizeof(short_addr_json), "null");
            std::snprintf(locator_revision_json, sizeof(locator_revision_json), "null");
        }

        char temperature_c[24] = "null";
        char battery_percent[16] = "null";
        char battery_voltage_mv[16] = "null";
        char lqi[16] = "null";
        char rssi[16] = "null";
        if (device.has_temperature) {
            std::snprintf(
                temperature_c, sizeof(temperature_c), "%.2f",
                static_cast<double>(device.temperature_centi_c) / 100.0);
        }
        if (device.has_battery) {
            std::snprintf(battery_percent, sizeof(battery_percent), "%u", static_cast<unsigned>(device.battery_percent));
        }
        if (device.has_battery_voltage) {
            std::snprintf(
                battery_voltage_mv, sizeof(battery_voltage_mv), "%u", static_cast<unsigned>(device.battery_voltage_mv));
        }
        if (device.has_lqi) {
            std::snprintf(lqi, sizeof(lqi), "%u", static_cast<unsigned>(device.lqi));
        }
        if (device.has_rssi) {
            std::snprintf(rssi, sizeof(rssi), "%d", static_cast<int>(device.rssi_dbm));
        }

        written = std::snprintf(
            chunk,
            sizeof(chunk),
            "%s{\"device_id\":\"%s\",\"short_addr\":%s,\"locator_revision\":%s,"
            "\"online\":%s,\"power_on\":%s,\"reporting_state\":\"%s\",\"last_report_at\":%lu,\"stale\":%s,"
            "\"temperature_c\":%s,\"occupancy\":\"%s\",\"contact\":{\"state\":\"%s\",\"tamper\":%s,\"battery_low\":%s},"
            "\"battery\":{\"percent\":%s,\"voltage_mv\":%s},\"lqi\":%s,\"rssi\":%s}",
            i == 0 ? "" : ",",
            device.device_id_hex.data(),
            short_addr_json,
            locator_revision_json,
            device.online ? "true" : "false",
            device.power_on ? "true" : "false",
            v1_reporting_state_to_string(device.reporting_state),
            static_cast<unsigned long>(device.last_report_at_ms),
            device.stale ? "true" : "false",
            temperature_c,
            v1_occupancy_state_to_string(device.occupancy_state),
            v1_contact_state_to_string(device.contact_state),
            device.contact_tamper ? "true" : "false",
            device.contact_battery_low ? "true" : "false",
            battery_percent,
            battery_voltage_mv,
            lqi,
            rssi);
        if (written <= 0 || written >= static_cast<int>(sizeof(chunk))) {
            return ESP_FAIL;
        }
        if (httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    if (httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t network_get_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    service::NetworkApiSnapshot snapshot{};
    if (!context->runtime->build_network_api_snapshot(&snapshot)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }

    char response[416]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"schema_version\":%u,\"revision\":%" PRIu32 ",\"connected\":%s,\"refresh_requests\":%" PRIu32
        ",\"current_backoff_ms\":%" PRIu32 ",\"mqtt\":{\"enabled\":%s,\"connected\":%s,\"last_connect_error\":\"%s\"}}",
        static_cast<unsigned>(kApiV1SchemaVersion),
        snapshot.revision,
        snapshot.connected ? "true" : "false",
        snapshot.refresh_requests,
        snapshot.current_backoff_ms,
        snapshot.mqtt.enabled ? "true" : "false",
        snapshot.mqtt.connected ? "true" : "false",
        v1_mqtt_connect_error_token(snapshot.mqtt.last_connect_error));
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t config_get_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    service::ConfigApiSnapshot snapshot{};
    if (!context->runtime->build_config_api_snapshot(&snapshot)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kCapabilityUnavailable);
    }

    char response[256]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"schema_version\":%u,\"revision\":%" PRIu32 ",\"last_command_status\":%u,"
        "\"command_timeout_ms\":%" PRIu32 ",\"max_command_retries\":%u,\"autoconnect_failures\":%" PRIu32 "}",
        static_cast<unsigned>(kApiV1SchemaVersion),
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

constexpr uint32_t kDefaultForceRemoveTimeoutMsV1 = 5000U;

// Resolves a URL-path device_id to a short_addr and, on any failure, sends
// the golden-matrix-mapped error response itself and returns false --
// callers just need to check the return value.
bool resolve_device_id_or_send_error_v1(
    WebRouteContext* context, httpd_req_t* req, const char* device_id_hex, uint16_t* out_short_addr) noexcept {
    const service::ServiceRuntimeApi::DeviceIdResolveStatus status =
        context->runtime->resolve_short_addr_for_device_id_hex(device_id_hex, out_short_addr);
    switch (status) {
        case service::ServiceRuntimeApi::DeviceIdResolveStatus::kResolved:
            return true;
        case service::ServiceRuntimeApi::DeviceIdResolveStatus::kMalformedHex:
            (void)send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
            return false;
        case service::ServiceRuntimeApi::DeviceIdResolveStatus::kUnknownDevice:
            (void)send_api_v1_error(req, ApiV1ErrorCode::kDeviceNotFound);
            return false;
        case service::ServiceRuntimeApi::DeviceIdResolveStatus::kNoCurrentLocator:
        default:
            (void)send_api_v1_error(req, ApiV1ErrorCode::kDeviceOffline);
            return false;
    }
}

// OTA/RCP submit-status enums enumerate many domain-specific validation
// failure reasons (manifest/board/project/schema/signature mismatches).
// The golden matrix (plan S4 HTTP #9) deliberately keeps a small, stable
// vocabulary rather than exposing every internal validation nuance as a
// distinct top-level v1 error code, so every non-kBusy failure maps to the
// same 400 invalid_request; kBusy (another operation already in flight)
// maps to 409 conflict.
ApiV1ErrorCode map_ota_submit_error_v1(service::OtaSubmitStatus status) noexcept {
    if (status == service::OtaSubmitStatus::kBusy) {
        return ApiV1ErrorCode::kConflict;
    }
    return ApiV1ErrorCode::kInvalidRequest;
}

ApiV1ErrorCode map_rcp_submit_error_v1(service::RcpUpdateSubmitStatus status) noexcept {
    if (status == service::RcpUpdateSubmitStatus::kBusy || status == service::RcpUpdateSubmitStatus::kConflict) {
        return ApiV1ErrorCode::kConflict;
    }
    return ApiV1ErrorCode::kInvalidRequest;
}

esp_err_t device_join_window_post_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    uint16_t join_window_seconds_left = 0U;
    if (context->runtime->get_join_window_status(&join_window_seconds_left)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kConflict);
    }

    uint32_t duration_seconds_raw = 30U;
    (void)find_json_u32_field(body, "duration_seconds", &duration_seconds_raw);
    if (duration_seconds_raw == 0U || duration_seconds_raw > 255U) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    const uint32_t request_id = context->runtime->next_operation_request_id();
    if (!context->runtime->post_open_join_window(request_id, static_cast<uint16_t>(duration_seconds_raw))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kNoCapacity);
    }
    return send_api_v1_accepted(req, request_id);
}

esp_err_t device_power_post_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    char device_id_hex[kApiV1DeviceIdHexLength + 1U]{};
    if (!extract_uri_device_id_hex(
            req->uri, "/api/v1/devices/", "/commands/power", device_id_hex, sizeof(device_id_hex))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }
    bool desired_power_on = false;
    if (!find_json_bool_field(body, "power_on", &desired_power_on)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    uint16_t short_addr = 0U;
    if (!resolve_device_id_or_send_error_v1(context, req, device_id_hex, &short_addr)) {
        return ESP_OK;
    }

    const uint32_t correlation_id = allocate_correlation_id(context);
    service::DevicePowerCommandRequest request{};
    request.correlation_id = correlation_id;
    request.short_addr = short_addr;
    request.desired_power_on = desired_power_on;
    request.issued_at_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    const service::CommandSubmitStatus submit_result = context->runtime->post_device_power_request(request);
    if (submit_result != service::CommandSubmitStatus::kAccepted) {
        if (submit_result == service::CommandSubmitStatus::kInvalidArgument) {
            return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
        }
        return send_api_v1_error(req, ApiV1ErrorCode::kNoCapacity);
    }
    return send_api_v1_accepted(req, correlation_id);
}

esp_err_t device_delete_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    char device_id_hex[kApiV1DeviceIdHexLength + 1U]{};
    if (!extract_uri_device_id_hex(req->uri, "/api/v1/devices/", "", device_id_hex, sizeof(device_id_hex))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    uint16_t short_addr = 0U;
    if (!resolve_device_id_or_send_error_v1(context, req, device_id_hex, &short_addr)) {
        return ESP_OK;
    }

    // Body is optional for DELETE: {"force_remove":bool,
    // "force_remove_timeout_ms":u32}. No body (content_len == 0, so
    // read_request_body() fails) is not an error here -- force_remove just
    // defaults to false.
    char body[kMaxRequestBodyBytes]{};
    bool force_remove = false;
    uint32_t force_remove_timeout_ms = kDefaultForceRemoveTimeoutMsV1;
    if (read_request_body(req, body, sizeof(body))) {
        (void)find_json_bool_field(body, "force_remove", &force_remove);
        uint32_t force_remove_timeout_raw = 0U;
        if (force_remove && find_json_u32_field(body, "force_remove_timeout_ms", &force_remove_timeout_raw)) {
            force_remove_timeout_ms = force_remove_timeout_raw;
        }
    }

    const uint32_t request_id = context->runtime->next_operation_request_id();
    if (!context->runtime->post_remove_device(request_id, short_addr, force_remove, force_remove_timeout_ms)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kNoCapacity);
    }
    return send_api_v1_accepted(req, request_id);
}

esp_err_t device_reporting_put_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    char device_id_hex[kApiV1DeviceIdHexLength + 1U]{};
    uint32_t endpoint_raw = 0U;
    uint32_t cluster_id_raw = 0U;
    if (!extract_uri_device_id_and_reporting_segments(
            req->uri, "/api/v1/devices/", device_id_hex, sizeof(device_id_hex), &endpoint_raw, &cluster_id_raw) ||
        endpoint_raw == 0U || endpoint_raw > 0xFFU || cluster_id_raw == 0U || cluster_id_raw > 0xFFFFU) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    uint32_t min_interval = 0U;
    uint32_t max_interval = 0U;
    if (!find_json_u32_field(body, "min_interval_seconds", &min_interval) ||
        !find_json_u32_field(body, "max_interval_seconds", &max_interval) || min_interval > 0xFFFFU ||
        max_interval > 0xFFFFU) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }
    if (max_interval == 0U || min_interval > max_interval) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    uint32_t capability_flags_raw = 0U;
    if (find_json_u32_field(body, "capability_flags", &capability_flags_raw) && capability_flags_raw > 0xFFU) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    uint16_t short_addr = 0U;
    if (!resolve_device_id_or_send_error_v1(context, req, device_id_hex, &short_addr)) {
        return ESP_OK;
    }

    service::ConfigManager::ReportingProfile profile{};
    profile.in_use = true;
    // Deliberately `auto`, not a spelled-out Core-namespaced type: web_ui
    // adapters must not name Core symbols directly (INV-M030).
    const auto device_id = context->runtime->resolve_device_id_for_short_addr(short_addr);
    profile.key.device_id = device_id;
    profile.key.endpoint = static_cast<uint8_t>(endpoint_raw);
    profile.key.cluster_id = static_cast<uint16_t>(cluster_id_raw);
    profile.min_interval_seconds = static_cast<uint16_t>(min_interval);
    profile.max_interval_seconds = static_cast<uint16_t>(max_interval);
    uint32_t reportable_change = 0U;
    (void)find_json_u32_field(body, "reportable_change", &reportable_change);
    profile.reportable_change = reportable_change;
    profile.capability_flags = static_cast<uint8_t>(capability_flags_raw);

    if (!context->runtime->post_reporting_profile_write(profile)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kNoCapacity);
    }
    return send_api_v1_ok(req);
}

esp_err_t network_scan_post_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    const uint32_t request_id = context->runtime->next_operation_request_id();
    if (!context->runtime->post_network_scan(request_id)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kNoCapacity);
    }
    return send_api_v1_accepted(req, request_id);
}

esp_err_t network_connect_post_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    char ssid[33]{};
    if (!find_json_string_field(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }
    char password[65]{};
    (void)find_json_string_field(body, "password", password, sizeof(password));
    bool save_credentials = true;
    (void)find_json_bool_field(body, "save_credentials", &save_credentials);

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    const uint32_t request_id = context->runtime->next_operation_request_id();
    if (!context->runtime->post_network_connect(request_id, ssid, password, save_credentials)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kNoCapacity);
    }
    return send_api_v1_accepted(req, request_id);
}

esp_err_t config_patch_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    service::ConfigWriteRequest request{};
    uint32_t timeout_ms = 0U;
    if (find_json_u32_field(body, "command_timeout_ms", &timeout_ms)) {
        if (timeout_ms == 0U) {
            return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
        }
        request.set_timeout_ms = true;
        request.timeout_ms = timeout_ms;
    }
    uint32_t max_retries_raw = 0U;
    if (find_json_u32_field(body, "max_command_retries", &max_retries_raw)) {
        if (max_retries_raw > 5U) {
            return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
        }
        request.set_max_retries = true;
        request.max_retries = static_cast<uint8_t>(max_retries_raw);
    }
    if (!request.set_timeout_ms && !request.set_max_retries) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    if (!context->runtime->post_config_write(request)) {
        return send_api_v1_error(req, ApiV1ErrorCode::kNoCapacity);
    }
    return send_api_v1_ok(req);
}

esp_err_t ota_operations_post_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    char body[1024U]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    service::OtaStartRequest request{};
    if (!find_json_string_field(body, "url", request.manifest.url.data(), request.manifest.url.size()) ||
        request.manifest.url[0] == '\0') {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }
    (void)find_json_string_field(
        body, "target_version", request.manifest.version.data(), request.manifest.version.size());
    (void)find_json_string_field(body, "project", request.manifest.project.data(), request.manifest.project.size());
    (void)find_json_string_field(body, "board", request.manifest.board.data(), request.manifest.board.size());
    (void)find_json_string_field(
        body, "chip_target", request.manifest.chip_target.data(), request.manifest.chip_target.size());
    (void)find_json_string_field(body, "sha256", request.manifest.sha256.data(), request.manifest.sha256.size());
    (void)find_json_string_field(
        body, "signature_algo", request.manifest.signature_algo.data(), request.manifest.signature_algo.size());
    (void)find_json_string_field(
        body, "signature_key_id", request.manifest.signature_key_id.data(), request.manifest.signature_key_id.size());
    (void)find_json_string_field(body, "signature", request.manifest.signature.data(), request.manifest.signature.size());
    (void)find_json_u32_field(body, "min_schema", &request.manifest.min_schema);
    (void)find_json_bool_field(body, "allow_downgrade", &request.manifest.allow_downgrade);

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    request.request_id = context->runtime->next_operation_request_id();
    const service::OtaSubmitStatus submit_status = context->runtime->post_ota_start(request);
    if (submit_status != service::OtaSubmitStatus::kAccepted) {
        return send_api_v1_error(req, map_ota_submit_error_v1(submit_status));
    }
    return send_api_v1_accepted(req, request.request_id);
}

esp_err_t rcp_update_operations_post_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    char body[512U]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    service::RcpUpdateRequest request{};
    if (!find_json_string_field(body, "url", request.url.data(), request.url.size()) || request.url[0] == '\0') {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }
    (void)find_json_string_field(body, "target_version", request.target_version.data(), request.target_version.size());
    (void)find_json_string_field(body, "sha256", request.sha256.data(), request.sha256.size());
    (void)find_json_string_field(body, "board", request.board.data(), request.board.size());
    (void)find_json_string_field(body, "transport", request.transport.data(), request.transport.size());
    (void)find_json_string_field(
        body, "gateway_min_version", request.gateway_min_version.data(), request.gateway_min_version.size());

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    request.request_id = context->runtime->next_operation_request_id();
    const service::RcpUpdateSubmitStatus submit_status = context->runtime->post_rcp_update_start(request);
    if (submit_status != service::RcpUpdateSubmitStatus::kAccepted) {
        return send_api_v1_error(req, map_rcp_submit_error_v1(submit_status));
    }
    return send_api_v1_accepted(req, request.request_id);
}

const char* operation_domain_token_v1(service::OperationDomain domain) noexcept {
    switch (domain) {
        case service::OperationDomain::kNetwork:
            return "network";
        case service::OperationDomain::kConfig:
            return "config";
        case service::OperationDomain::kOta:
            return "ota";
        case service::OperationDomain::kRcpUpdate:
            return "rcp_update";
        case service::OperationDomain::kUnknown:
        default:
            return "unknown";
    }
}

// Deliberately coarse (mirrors the OTA/RCP submit-error-mapping precedent
// in this same file): every domain's *Result::status enum reserves 0 for
// "ok" (NetworkOperationStatus::kOk, OtaOperationStatus::kOk,
// RcpUpdateOperationStatus::kOk all equal 0), so "ok" is derived generically
// from the raw numeric code rather than switching on each domain's own
// ~15-value enum; the raw status_code is still exposed for full detail.
// Domain-specific diagnostic fields (e.g. OTA transport error details, full
// network scan records) are intentionally not surfaced here -- this is the
// unified poll's stable summary contract, not a replacement for the
// legacy per-domain result GETs.
esp_err_t operations_get_handler_v1(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    uint32_t operation_id = 0U;
    if (!extract_uri_decimal_segment(req->uri, "/api/v1/operations/", &operation_id) || operation_id == 0U) {
        return send_api_v1_error(req, ApiV1ErrorCode::kInvalidRequest);
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    const service::OperationStatusSnapshot snapshot = context->runtime->poll_operation_status(operation_id);
    if (snapshot.domain == service::OperationDomain::kUnknown) {
        return send_api_v1_error(req, ApiV1ErrorCode::kOperationNotFound);
    }

    (void)httpd_resp_set_type(req, "application/json");

    if (snapshot.status != service::OperationPollStatus::kReady) {
        char pending_response[128]{};
        const int written = std::snprintf(
            pending_response,
            sizeof(pending_response),
            "{\"schema_version\":%u,\"request_id\":%" PRIu32 ",\"domain\":\"%s\",\"status\":\"pending\"}",
            static_cast<unsigned>(kApiV1SchemaVersion),
            operation_id,
            operation_domain_token_v1(snapshot.domain));
        if (written <= 0 || written >= static_cast<int>(sizeof(pending_response))) {
            return ESP_FAIL;
        }
        return httpd_resp_send(req, pending_response, HTTPD_RESP_USE_STRLEN);
    }

    char response[512]{};
    int written = 0;
    switch (snapshot.domain) {
        case service::OperationDomain::kNetwork: {
            service::NetworkResult result{};
            if (!context->runtime->take_network_result(operation_id, &result)) {
                return send_api_v1_error(req, ApiV1ErrorCode::kOperationNotFound);
            }
            char short_addr_json[16]{};
            if (result.device_short_addr == service::kUnknownShortAddr) {
                std::snprintf(short_addr_json, sizeof(short_addr_json), "null");
            } else {
                std::snprintf(
                    short_addr_json, sizeof(short_addr_json), "%u", static_cast<unsigned>(result.device_short_addr));
            }
            written = std::snprintf(
                response,
                sizeof(response),
                "{\"schema_version\":%u,\"request_id\":%" PRIu32
                ",\"domain\":\"network\",\"status\":\"ready\",\"ok\":%s,\"status_code\":%u,"
                "\"operation_type\":%u,\"short_addr\":%s,\"ssid\":\"%s\",\"saved\":%s,"
                "\"join_window_seconds\":%u,\"scan_count\":%u}",
                static_cast<unsigned>(kApiV1SchemaVersion),
                operation_id,
                result.status == service::NetworkOperationStatus::kOk ? "true" : "false",
                static_cast<unsigned>(result.status),
                static_cast<unsigned>(result.operation),
                short_addr_json,
                result.ssid,
                result.saved ? "true" : "false",
                static_cast<unsigned>(result.join_window_seconds),
                static_cast<unsigned>(result.scan_count));
            break;
        }
        case service::OperationDomain::kOta: {
            service::OtaResult result{};
            if (!context->runtime->take_ota_result(operation_id, &result)) {
                return send_api_v1_error(req, ApiV1ErrorCode::kOperationNotFound);
            }
            written = std::snprintf(
                response,
                sizeof(response),
                "{\"schema_version\":%u,\"request_id\":%" PRIu32
                ",\"domain\":\"ota\",\"status\":\"ready\",\"ok\":%s,\"status_code\":%u,"
                "\"reboot_required\":%s,\"downloaded_bytes\":%" PRIu32 ",\"image_size\":%" PRIu32
                ",\"image_size_known\":%s,\"target_version\":\"%s\"}",
                static_cast<unsigned>(kApiV1SchemaVersion),
                operation_id,
                result.status == service::OtaOperationStatus::kOk ? "true" : "false",
                static_cast<unsigned>(result.status),
                result.reboot_required ? "true" : "false",
                result.downloaded_bytes,
                result.image_size,
                result.image_size_known ? "true" : "false",
                result.target_version.data());
            break;
        }
        case service::OperationDomain::kRcpUpdate: {
            service::RcpUpdateResult result{};
            if (!context->runtime->take_rcp_update_result(operation_id, &result)) {
                return send_api_v1_error(req, ApiV1ErrorCode::kOperationNotFound);
            }
            written = std::snprintf(
                response,
                sizeof(response),
                "{\"schema_version\":%u,\"request_id\":%" PRIu32
                ",\"domain\":\"rcp_update\",\"status\":\"ready\",\"ok\":%s,\"status_code\":%u,"
                "\"written_bytes\":%" PRIu32 ",\"target_version\":\"%s\"}",
                static_cast<unsigned>(kApiV1SchemaVersion),
                operation_id,
                result.status == service::RcpUpdateOperationStatus::kOk ? "true" : "false",
                static_cast<unsigned>(result.status),
                result.written_bytes,
                result.target_version.data());
            break;
        }
        case service::OperationDomain::kConfig: {
            service::ConfigResult result{};
            if (!context->runtime->take_config_result(operation_id, &result)) {
                return send_api_v1_error(req, ApiV1ErrorCode::kOperationNotFound);
            }
            written = std::snprintf(
                response,
                sizeof(response),
                "{\"schema_version\":%u,\"request_id\":%" PRIu32
                ",\"domain\":\"config\",\"status\":\"ready\",\"ok\":true,\"status_code\":%u}",
                static_cast<unsigned>(kApiV1SchemaVersion),
                operation_id,
                static_cast<unsigned>(result.last_command_status));
            break;
        }
        case service::OperationDomain::kUnknown:
        default:
            return send_api_v1_error(req, ApiV1ErrorCode::kOperationNotFound);
    }

    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

}  // namespace

bool register_capabilities_routes_v1(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t capabilities_get_uri{};
    capabilities_get_uri.uri = "/api/v1/capabilities";
    capabilities_get_uri.method = HTTP_GET;
    capabilities_get_uri.handler = capabilities_get_handler_v1;
    capabilities_get_uri.user_ctx = context;

    return httpd_register_uri_handler(static_cast<httpd_handle_t>(server_handle), &capabilities_get_uri) == ESP_OK;
}

bool register_device_routes_v1(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }
    const auto handle = static_cast<httpd_handle_t>(server_handle);

    httpd_uri_t devices_get_uri{};
    devices_get_uri.uri = "/api/v1/devices";
    devices_get_uri.method = HTTP_GET;
    devices_get_uri.handler = devices_get_handler_v1;
    devices_get_uri.user_ctx = context;
    if (httpd_register_uri_handler(handle, &devices_get_uri) != ESP_OK) {
        return false;
    }

    // Registered before the wildcard mutation routes below: esp_http_
    // server tries registered patterns in registration order under
    // httpd_uri_match_wildcard, so this exact match must be tried first --
    // real target dispatch ordering is unverified in this sandbox (no
    // ESP-IDF), see docs/architecture/HTTP_API_V1.md.
    httpd_uri_t join_window_uri{};
    join_window_uri.uri = "/api/v1/devices/join-window";
    join_window_uri.method = HTTP_POST;
    join_window_uri.handler = device_join_window_post_handler_v1;
    join_window_uri.user_ctx = context;
    if (httpd_register_uri_handler(handle, &join_window_uri) != ESP_OK) {
        return false;
    }

    httpd_uri_t power_uri{};
    power_uri.uri = "/api/v1/devices/*";
    power_uri.method = HTTP_POST;
    power_uri.handler = device_power_post_handler_v1;
    power_uri.user_ctx = context;
    if (httpd_register_uri_handler(handle, &power_uri) != ESP_OK) {
        return false;
    }

    httpd_uri_t delete_uri{};
    delete_uri.uri = "/api/v1/devices/*";
    delete_uri.method = HTTP_DELETE;
    delete_uri.handler = device_delete_handler_v1;
    delete_uri.user_ctx = context;
    if (httpd_register_uri_handler(handle, &delete_uri) != ESP_OK) {
        return false;
    }

    httpd_uri_t reporting_put_uri{};
    reporting_put_uri.uri = "/api/v1/devices/*";
    reporting_put_uri.method = HTTP_PUT;
    reporting_put_uri.handler = device_reporting_put_handler_v1;
    reporting_put_uri.user_ctx = context;
    return httpd_register_uri_handler(handle, &reporting_put_uri) == ESP_OK;
}

bool register_network_routes_v1(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }
    const auto handle = static_cast<httpd_handle_t>(server_handle);

    httpd_uri_t network_get_uri{};
    network_get_uri.uri = "/api/v1/network";
    network_get_uri.method = HTTP_GET;
    network_get_uri.handler = network_get_handler_v1;
    network_get_uri.user_ctx = context;
    if (httpd_register_uri_handler(handle, &network_get_uri) != ESP_OK) {
        return false;
    }

    httpd_uri_t scans_uri{};
    scans_uri.uri = "/api/v1/network/scans";
    scans_uri.method = HTTP_POST;
    scans_uri.handler = network_scan_post_handler_v1;
    scans_uri.user_ctx = context;
    if (httpd_register_uri_handler(handle, &scans_uri) != ESP_OK) {
        return false;
    }

    httpd_uri_t connections_uri{};
    connections_uri.uri = "/api/v1/network/connections";
    connections_uri.method = HTTP_POST;
    connections_uri.handler = network_connect_post_handler_v1;
    connections_uri.user_ctx = context;
    return httpd_register_uri_handler(handle, &connections_uri) == ESP_OK;
}

bool register_config_routes_v1(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }
    const auto handle = static_cast<httpd_handle_t>(server_handle);

    httpd_uri_t config_get_uri{};
    config_get_uri.uri = "/api/v1/config";
    config_get_uri.method = HTTP_GET;
    config_get_uri.handler = config_get_handler_v1;
    config_get_uri.user_ctx = context;
    if (httpd_register_uri_handler(handle, &config_get_uri) != ESP_OK) {
        return false;
    }

    httpd_uri_t config_patch_uri{};
    config_patch_uri.uri = "/api/v1/config";
    config_patch_uri.method = HTTP_PATCH;
    config_patch_uri.handler = config_patch_handler_v1;
    config_patch_uri.user_ctx = context;
    return httpd_register_uri_handler(handle, &config_patch_uri) == ESP_OK;
}

bool register_ota_routes_v1(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t ota_operations_uri{};
    ota_operations_uri.uri = "/api/v1/ota/operations";
    ota_operations_uri.method = HTTP_POST;
    ota_operations_uri.handler = ota_operations_post_handler_v1;
    ota_operations_uri.user_ctx = context;
    return httpd_register_uri_handler(static_cast<httpd_handle_t>(server_handle), &ota_operations_uri) == ESP_OK;
}

bool register_rcp_routes_v1(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t rcp_operations_uri{};
    rcp_operations_uri.uri = "/api/v1/rcp-update/operations";
    rcp_operations_uri.method = HTTP_POST;
    rcp_operations_uri.handler = rcp_update_operations_post_handler_v1;
    rcp_operations_uri.user_ctx = context;
    return httpd_register_uri_handler(static_cast<httpd_handle_t>(server_handle), &rcp_operations_uri) == ESP_OK;
}

bool register_operations_routes_v1(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t operations_get_uri{};
    operations_get_uri.uri = "/api/v1/operations/*";
    operations_get_uri.method = HTTP_GET;
    operations_get_uri.handler = operations_get_handler_v1;
    operations_get_uri.user_ctx = context;
    return httpd_register_uri_handler(static_cast<httpd_handle_t>(server_handle), &operations_get_uri) == ESP_OK;
}

bool register_web_routes_v1(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr || context->runtime == nullptr) {
        return false;
    }

    if (!register_capabilities_routes_v1(server_handle, context)) {
        return false;
    }
    if (!register_device_routes_v1(server_handle, context)) {
        return false;
    }
    if (!register_network_routes_v1(server_handle, context)) {
        return false;
    }
    if (!register_config_routes_v1(server_handle, context)) {
        return false;
    }
    if (!register_ota_routes_v1(server_handle, context)) {
        return false;
    }
    if (!register_rcp_routes_v1(server_handle, context)) {
        return false;
    }
    if (!register_operations_routes_v1(server_handle, context)) {
        return false;
    }

    return true;
}

}  // namespace web_ui
