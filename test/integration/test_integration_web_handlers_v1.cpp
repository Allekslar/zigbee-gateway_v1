/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// Integration coverage for the two v1 routes that need
// esp_timer_get_time() and a fuller ServiceRuntime fixture: GET
// /api/v1/devices (plan S4 HTTP #1-#2) and POST
// /api/v1/devices/{device_id}/commands/power (the only mutation route that
// stamps issued_at_ms from esp_timer), matching the precedent of the
// legacy /api/devices integration test. Every other v1 route is
// host-tested in test_web_handlers_v1.cpp.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core_registry.hpp"
#include "device_id.hpp"
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

    const std::size_t to_copy = g_request_body.size() < len ? g_request_body.size() : len;
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

void set_request_body(httpd_req_t* req, const char* body) {
    g_request_body = body;
    req->content_len = static_cast<int>(g_request_body.size());
}

// Include implementation to access handlers in anonymous namespace.
#include "../../components/web_ui/web_handlers_v1.cpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    std::atomic<uint32_t> next_id{1};
    web_ui::WebRouteContext route_ctx{};
    route_ctx.runtime = &runtime;
    route_ctx.next_correlation_id = &next_id;

    httpd_req_t req{};
    req.user_ctx = &route_ctx;

    // No devices yet: an empty, well-formed envelope.
    clear_http_state();
    assert(web_ui::devices_get_handler_v1(&req) == ESP_OK);
    std::string empty_response;
    for (const std::string& chunk : g_chunks) {
        empty_response += chunk;
    }
    assert(empty_response.find("\"schema_version\":1") != std::string::npos);
    assert(empty_response.find("\"device_count\":0") != std::string::npos);
    assert(empty_response.find("\"devices\":[]") != std::string::npos);

    // A device with a resolved DeviceId but NO locator yet (e.g. restored
    // from persisted state, not yet rejoined): device_id present,
    // short_addr/locator_revision correctly null (plan S4 HTTP #2).
    core::DeviceId unlocated_device_id{};
    assert(core::DeviceId::parse("00124b0001aabbbb", 16, &unlocated_device_id));
    core::CoreEvent unlocated_joined{};
    unlocated_joined.type = core::CoreEventType::kDeviceJoined;
    unlocated_joined.device_id = unlocated_device_id;
    unlocated_joined.device_short_addr = 0x5501;
    assert(runtime.post_event(unlocated_joined));
    assert(runtime.process_pending() >= 1);
    // Note: intentionally NOT calling device_locator_registry().remap()
    // for this device -- it must still appear (identity-gated, not
    // locator-gated) but with short_addr:null.

    // A fully resolved device: device_id + a live locator.
    core::DeviceId located_device_id{};
    assert(core::DeviceId::parse("00124b0001aa2201", 16, &located_device_id));
    uint32_t locator_revision = 0U;
    assert(
        runtime.device_locator_registry().remap(located_device_id, 0x2201U, &locator_revision) ==
        service::DeviceLocatorRemapResult::kInserted);
    core::CoreEvent located_joined{};
    located_joined.type = core::CoreEventType::kDeviceJoined;
    located_joined.device_id = located_device_id;
    located_joined.device_short_addr = 0x2201;
    assert(runtime.post_event(located_joined));
    assert(runtime.process_pending() >= 1);

    clear_http_state();
    assert(web_ui::devices_get_handler_v1(&req) == ESP_OK);
    std::string response;
    for (const std::string& chunk : g_chunks) {
        response += chunk;
    }
    assert(response.find("\"schema_version\":1") != std::string::npos);
    assert(response.find("\"device_count\":2") != std::string::npos);

    char unlocated_hex[core::DeviceId::kHexLength + 1U] = {};
    assert(unlocated_device_id.format(unlocated_hex, sizeof(unlocated_hex)));
    char unlocated_needle[64] = {};
    std::snprintf(unlocated_needle, sizeof(unlocated_needle), "\"device_id\":\"%s\"", unlocated_hex);
    assert(response.find(unlocated_needle) != std::string::npos);
    // Exactly one of the two devices has no current locator (plan S4 HTTP
    // #2: short_addr null when there is no current locator); with only two
    // devices in this fixture and the other's short_addr pinned to the
    // specific value 8705 (asserted below), a single "short_addr":null
    // occurrence unambiguously belongs to the unlocated device.
    assert(response.find("\"short_addr\":null") != std::string::npos);
    assert(response.find("\"locator_revision\":null") != std::string::npos);

    char located_hex[core::DeviceId::kHexLength + 1U] = {};
    assert(located_device_id.format(located_hex, sizeof(located_hex)));
    char located_needle[64] = {};
    std::snprintf(located_needle, sizeof(located_needle), "\"device_id\":\"%s\"", located_hex);
    assert(response.find(located_needle) != std::string::npos);
    assert(response.find("\"short_addr\":8705") != std::string::npos);
    char locator_revision_needle[64] = {};
    std::snprintf(
        locator_revision_needle, sizeof(locator_revision_needle), "\"locator_revision\":%" PRIu32, locator_revision);
    assert(response.find(locator_revision_needle) != std::string::npos);

    // Null user_ctx / snapshot failure paths.
    assert(web_ui::devices_get_handler_v1(nullptr) == ESP_FAIL);

    // --- POST /api/v1/devices/{device_id}/commands/power ---
    // A device with a resolved identity but no current locator (offline
    // per the golden matrix), for the device_offline error path.
    core::DeviceId offline_device_id{};
    assert(core::DeviceId::parse("00124b0001aacccc", 16, &offline_device_id));
    core::CoreEvent offline_joined{};
    offline_joined.type = core::CoreEventType::kDeviceJoined;
    offline_joined.device_id = offline_device_id;
    offline_joined.device_short_addr = 0x9901;
    assert(runtime.post_event(offline_joined));
    assert(runtime.process_pending() >= 1);
    // Deliberately NOT calling device_locator_registry().remap() here.

    constexpr const char* kUnknownDeviceIdHex = "00124b0001aadddd";  // never joined at all.

    // Malformed URI (missing the /commands/power suffix) -> 400.
    clear_http_state();
    req.uri = "/api/v1/devices/00124b0001aa2201";
    set_request_body(&req, "{\"power_on\":true}");
    assert(web_ui::device_power_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request\"") != std::string::npos);

    // Missing power_on field in body -> 400.
    char power_uri[64] = {};
    std::snprintf(power_uri, sizeof(power_uri), "/api/v1/devices/00124b0001aa2201/commands/power");
    clear_http_state();
    req.uri = power_uri;
    set_request_body(&req, "{}");
    assert(web_ui::device_power_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request\"") != std::string::npos);

    // Unknown device_id -> 404 device_not_found.
    char unknown_power_uri[80] = {};
    std::snprintf(
        unknown_power_uri, sizeof(unknown_power_uri), "/api/v1/devices/%s/commands/power", kUnknownDeviceIdHex);
    clear_http_state();
    req.uri = unknown_power_uri;
    set_request_body(&req, "{\"power_on\":true}");
    assert(web_ui::device_power_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "404 Not Found");
    assert(g_last_response.find("\"error\":\"device_not_found\"") != std::string::npos);

    // Known device_id with no current locator -> 409 device_offline.
    char offline_power_uri[80] = {};
    std::snprintf(offline_power_uri, sizeof(offline_power_uri), "/api/v1/devices/00124b0001aacccc/commands/power");
    clear_http_state();
    req.uri = offline_power_uri;
    set_request_body(&req, "{\"power_on\":true}");
    assert(web_ui::device_power_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "409 Conflict");
    assert(g_last_response.find("\"error\":\"device_offline\"") != std::string::npos);

    // Located, resolvable device -> 202 Accepted.
    clear_http_state();
    req.uri = power_uri;
    set_request_body(&req, "{\"power_on\":true}");
    assert(web_ui::device_power_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "202 Accepted");
    assert(g_last_response.find("\"schema_version\":1") != std::string::npos);
    assert(g_last_response.find("\"accepted\":true") != std::string::npos);

    // Null user_ctx / null req are rejected without crashing.
    assert(web_ui::device_power_post_handler_v1(nullptr) == ESP_FAIL);

    return 0;
}
