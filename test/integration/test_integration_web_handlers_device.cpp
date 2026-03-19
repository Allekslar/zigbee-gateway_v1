/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"
#include "web_handler_common.hpp"

#ifndef ESP_PLATFORM
#define ESP_LOGI(tag, fmt, ...) std::printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) std::printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) std::printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)

extern "C" int64_t esp_timer_get_time() {
    return 1000000LL;
}
#endif

std::string g_last_response;
std::vector<std::string> g_chunks;
std::string g_last_status;
std::string g_request_body;

int g_register_call_count = 0;
int g_register_fail_at = 0;

extern "C" esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* type) {
    (void)req;
    (void)type;
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_send(httpd_req_t* req, const char* buf, ssize_t len) {
    (void)req;
    g_last_response = (len == HTTPD_RESP_USE_STRLEN) ? buf : std::string(buf, static_cast<std::size_t>(len));
    return ESP_OK;
}

extern "C" esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t* uri_handler) {
    (void)handle;
    (void)uri_handler;
    ++g_register_call_count;
    if (g_register_fail_at > 0 && g_register_call_count == g_register_fail_at) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_set_status(httpd_req_t* req, const char* status) {
    (void)req;
    g_last_status = status == nullptr ? "" : status;
    return ESP_OK;
}

extern "C" int httpd_req_recv(httpd_req_t* req, char* buf, size_t len) {
    (void)req;
    if (g_request_body.empty()) {
        return 0;
    }

    const std::size_t to_copy = std::min(len, g_request_body.size());
    std::memcpy(buf, g_request_body.data(), to_copy);
    g_request_body.erase(0, to_copy);
    return static_cast<int>(to_copy);
}

extern "C" esp_err_t httpd_resp_send_chunk(httpd_req_t* req, const char* buf, ssize_t len) {
    (void)req;
    if (buf != nullptr) {
        g_chunks.push_back((len == HTTPD_RESP_USE_STRLEN) ? buf : std::string(buf, static_cast<std::size_t>(len)));
    }
    return ESP_OK;
}

void clear_http_state() {
    g_last_response.clear();
    g_chunks.clear();
    g_last_status.clear();
}

// Include implementation to access handlers in anonymous namespace.
#include "../../components/web_ui/web_handlers_device.cpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    std::atomic<uint32_t> next_id{100};
    web_ui::WebRouteContext route_ctx{};
    route_ctx.runtime = &runtime;
    route_ctx.next_correlation_id = &next_id;

    httpd_req_t req{};
    req.user_ctx = &route_ctx;

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x2201;
    assert(runtime.post_event(joined));
    core::CoreEvent reporting_configured{};
    reporting_configured.type = core::CoreEventType::kDeviceReportingConfigured;
    reporting_configured.device_short_addr = 0x2201;
    assert(runtime.post_event(reporting_configured));
    core::CoreEvent telemetry_temperature{};
    telemetry_temperature.type = core::CoreEventType::kDeviceTelemetryUpdated;
    telemetry_temperature.device_short_addr = 0x2201;
    telemetry_temperature.value_u32 = 4242U;
    telemetry_temperature.telemetry_kind = core::CoreTelemetryKind::kTemperatureCentiC;
    telemetry_temperature.telemetry_i32 = 2150;
    telemetry_temperature.telemetry_valid = true;
    assert(runtime.post_event(telemetry_temperature));
    core::CoreEvent telemetry_occupancy{};
    telemetry_occupancy.type = core::CoreEventType::kDeviceTelemetryUpdated;
    telemetry_occupancy.device_short_addr = 0x2201;
    telemetry_occupancy.value_u32 = 4242U;
    telemetry_occupancy.telemetry_kind = core::CoreTelemetryKind::kOccupancy;
    telemetry_occupancy.telemetry_i32 = 1;
    telemetry_occupancy.telemetry_valid = true;
    assert(runtime.post_event(telemetry_occupancy));
    core::CoreEvent telemetry_contact{};
    telemetry_contact.type = core::CoreEventType::kDeviceTelemetryUpdated;
    telemetry_contact.device_short_addr = 0x2201;
    telemetry_contact.value_u32 = 4242U;
    telemetry_contact.telemetry_kind = core::CoreTelemetryKind::kContactIasZoneStatus;
    telemetry_contact.telemetry_i32 = 7;  // open + tamper + battery_low
    telemetry_contact.telemetry_valid = true;
    assert(runtime.post_event(telemetry_contact));
    core::CoreEvent telemetry_battery{};
    telemetry_battery.type = core::CoreEventType::kDeviceTelemetryUpdated;
    telemetry_battery.device_short_addr = 0x2201;
    telemetry_battery.value_u32 = 4242U;
    telemetry_battery.telemetry_kind = core::CoreTelemetryKind::kBatteryPercent;
    telemetry_battery.telemetry_i32 = 74;
    telemetry_battery.telemetry_valid = true;
    assert(runtime.post_event(telemetry_battery));
    core::CoreEvent telemetry_battery_mv{};
    telemetry_battery_mv.type = core::CoreEventType::kDeviceTelemetryUpdated;
    telemetry_battery_mv.device_short_addr = 0x2201;
    telemetry_battery_mv.value_u32 = 4242U;
    telemetry_battery_mv.telemetry_kind = core::CoreTelemetryKind::kBatteryVoltageMilliV;
    telemetry_battery_mv.telemetry_i32 = 3000;
    telemetry_battery_mv.telemetry_valid = true;
    assert(runtime.post_event(telemetry_battery_mv));
    core::CoreEvent telemetry_lqi{};
    telemetry_lqi.type = core::CoreEventType::kDeviceTelemetryUpdated;
    telemetry_lqi.device_short_addr = 0x2201;
    telemetry_lqi.value_u32 = 4242U;
    telemetry_lqi.telemetry_kind = core::CoreTelemetryKind::kLqi;
    telemetry_lqi.telemetry_i32 = 201;
    telemetry_lqi.telemetry_valid = true;
    assert(runtime.post_event(telemetry_lqi));
    core::CoreEvent telemetry_rssi{};
    telemetry_rssi.type = core::CoreEventType::kDeviceTelemetryUpdated;
    telemetry_rssi.device_short_addr = 0x2201;
    telemetry_rssi.value_u32 = 4242U;
    telemetry_rssi.telemetry_kind = core::CoreTelemetryKind::kRssiDbm;
    telemetry_rssi.telemetry_i32 = -63;
    telemetry_rssi.telemetry_valid = true;
    assert(runtime.post_event(telemetry_rssi));
    runtime.process_pending();

    clear_http_state();
    assert(web_ui::devices_get_handler(&req) == ESP_OK);
    std::string devices_response;
    for (const std::string& chunk : g_chunks) {
        devices_response += chunk;
    }
    assert(devices_response.find("\"device_count\":1") != std::string::npos);
    assert(devices_response.find("\"short_addr\":8705") != std::string::npos);
    assert(devices_response.find("\"reporting_state\":\"reporting_active\"") != std::string::npos);
    assert(devices_response.find("\"last_report_at\":4242") != std::string::npos);
    assert(devices_response.find("\"stale\":false") != std::string::npos);
    assert(devices_response.find("\"temperature_c\":21.50") != std::string::npos);
    assert(devices_response.find("\"occupancy\":\"occupied\"") != std::string::npos);
    assert(devices_response.find("\"contact\":{\"state\":\"open\",\"tamper\":true,\"battery_low\":true}") != std::string::npos);
    assert(devices_response.find("\"battery\":{\"percent\":74,\"voltage_mv\":3000}") != std::string::npos);
    assert(devices_response.find("\"lqi\":201") != std::string::npos);
    assert(devices_response.find("\"rssi\":-63") != std::string::npos);

    clear_http_state();
    req.content_len = 0;
    g_request_body.clear();
    assert(web_ui::device_power_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_body\"") != std::string::npos);

    clear_http_state();
    g_request_body = "{\"short_addr\":8705}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::device_power_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_payload\"") != std::string::npos);

    clear_http_state();
    next_id.store(600);
    g_request_body = "{\"short_addr\":8705,\"power_on\":false}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::device_power_post_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"accepted\":true") != std::string::npos);
    assert(g_last_response.find("\"correlation_id\":600") != std::string::npos);

    clear_http_state();
    g_request_body = "{\"duration_seconds\":0}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::device_join_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_duration_seconds\"") != std::string::npos);

    clear_http_state();
    g_request_body = "{\"short_addr\":65535}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::device_remove_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_payload\"") != std::string::npos);

    assert(!web_ui::register_device_routes(nullptr, &route_ctx));
    assert(!web_ui::register_device_routes(reinterpret_cast<void*>(1), nullptr));

    g_register_call_count = 0;
    g_register_fail_at = 0;
    assert(web_ui::register_device_routes(reinterpret_cast<void*>(1), &route_ctx));
    const int success_registration_count = g_register_call_count;
    assert(success_registration_count == 4);

    for (int fail_at = 1; fail_at <= success_registration_count; ++fail_at) {
        g_register_call_count = 0;
        g_register_fail_at = fail_at;
        assert(!web_ui::register_device_routes(reinterpret_cast<void*>(1), &route_ctx));
        assert(g_register_call_count == fail_at);
    }

    g_register_call_count = 0;
    g_register_fail_at = 0;
    assert(web_ui::register_device_routes(reinterpret_cast<void*>(1), &route_ctx));

    return 0;
}
