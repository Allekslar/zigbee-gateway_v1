/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// Host coverage for every /api/v1 route that does not depend on
// esp_timer_get_time() (all GET routes except devices; every mutation
// except device power, since power is the only mutation that stamps
// issued_at_ms from esp_timer). devices GET and device power POST
// additionally need a fuller ServiceRuntime fixture with a resolvable
// DeviceId; both are covered at the integration level
// (test_integration_web_handlers_v1.cpp), matching the precedent already
// set by the legacy /api/devices handler.

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "core_registry.hpp"
#include "device_id.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"
#include "web_handler_common.hpp"

#ifndef ESP_PLATFORM
extern "C" int64_t esp_timer_get_time() {
    return 1000000LL;
}
#endif

std::string g_last_response;
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
    (void)buf;
    (void)len;
    return ESP_OK;
}

void clear_http_capture() {
    g_last_response.clear();
    g_last_status.clear();
}

void set_request_body(httpd_req_t* req, const char* body) {
    g_request_body = body;
    req->content_len = static_cast<int>(g_request_body.size());
}

// Direct include for anonymous-namespace handlers, matching the established
// pattern (e.g. test_web_handlers_config_post.cpp).
#include "../../components/web_ui/web_handlers_v1.cpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    service::RuntimeCapabilities caps{};
    caps.zigbee_available = true;
    caps.mqtt_available = false;
    caps.matter_target_available = false;
    caps.ota_available = true;
    caps.rcp_update_available = false;
    runtime.set_capabilities(caps);
    // Join-window open (drained inside process_pending()) calls
    // ensure_zigbee_started(), which needs the HAL adapter initialized and
    // Zigbee start permission granted (normally gated on saved Wi-Fi
    // credentials).
    assert(runtime.initialize_hal_adapter());
    runtime.mark_wifi_credentials_available();

    std::atomic<uint32_t> next_id{1};
    web_ui::WebRouteContext route_ctx{};
    route_ctx.runtime = &runtime;
    route_ctx.next_correlation_id = &next_id;

    httpd_req_t req{};
    req.user_ctx = &route_ctx;

    // --- GET /api/v1/capabilities ---
    clear_http_capture();
    assert(web_ui::capabilities_get_handler_v1(&req) == ESP_OK);
    assert(g_last_status.empty());  // 200 OK: httpd_resp_set_status is not called for the success path.
    assert(g_last_response.find("\"schema_version\":1") != std::string::npos);
    assert(g_last_response.find("\"zigbee_available\":true") != std::string::npos);
    assert(g_last_response.find("\"mqtt_available\":false") != std::string::npos);
    assert(g_last_response.find("\"ota_available\":true") != std::string::npos);
    assert(g_last_response.find("\"rcp_update_available\":false") != std::string::npos);

    // --- GET /api/v1/network ---
    clear_http_capture();
    assert(web_ui::network_get_handler_v1(&req) == ESP_OK);
    assert(g_last_status.empty());
    assert(g_last_response.find("\"schema_version\":1") != std::string::npos);
    assert(g_last_response.find("\"connected\":false") != std::string::npos);
    assert(g_last_response.find("\"mqtt\":{") != std::string::npos);

    // --- GET /api/v1/config ---
    clear_http_capture();
    assert(web_ui::config_get_handler_v1(&req) == ESP_OK);
    assert(g_last_status.empty());
    assert(g_last_response.find("\"schema_version\":1") != std::string::npos);
    assert(g_last_response.find("\"command_timeout_ms\":") != std::string::npos);
    assert(g_last_response.find("\"max_command_retries\":") != std::string::npos);

    // --- Null user_ctx / null req are rejected without crashing. ---
    assert(web_ui::capabilities_get_handler_v1(nullptr) == ESP_FAIL);
    httpd_req_t bare_req{};
    bare_req.user_ctx = nullptr;
    assert(web_ui::network_get_handler_v1(&bare_req) == ESP_FAIL);
    assert(web_ui::config_get_handler_v1(&bare_req) == ESP_FAIL);

    // A device with a resolved identity and a live locator (for
    // device-scoped mutation success paths).
    core::DeviceId located_device_id{};
    assert(core::DeviceId::parse("00124b0001aa2201", 16, &located_device_id));
    core::CoreEvent located_joined{};
    located_joined.type = core::CoreEventType::kDeviceJoined;
    located_joined.device_id = located_device_id;
    located_joined.device_short_addr = 0x2201;
    assert(runtime.post_event(located_joined));
    assert(runtime.process_pending() >= 1);
    uint32_t located_locator_revision = 0U;
    assert(
        runtime.device_locator_registry().remap(located_device_id, 0x2201U, &located_locator_revision) ==
        service::DeviceLocatorRemapResult::kInserted);

    // A device with a resolved identity but NO current locator (for the
    // device_offline golden-matrix path).
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
    constexpr const char* kMalformedDeviceIdHex = "not-a-device-id!";

    // --- POST /api/v1/devices/join-window ---
    clear_http_capture();
    set_request_body(&req, "{\"duration_seconds\":0}");
    assert(web_ui::device_join_window_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request\"") != std::string::npos);

    clear_http_capture();
    set_request_body(&req, "{\"duration_seconds\":30}");
    assert(web_ui::device_join_window_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "202 Accepted");
    assert(g_last_response.find("\"schema_version\":1") != std::string::npos);
    assert(g_last_response.find("\"accepted\":true") != std::string::npos);
    // The join-window cache is only flipped once the queued NetworkRequest
    // is drained inside process_pending() -- not synchronously on submit.
    runtime.process_pending();

    // Window already open -> conflict.
    clear_http_capture();
    set_request_body(&req, "{\"duration_seconds\":30}");
    assert(web_ui::device_join_window_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "409 Conflict");
    assert(g_last_response.find("\"error\":\"conflict\"") != std::string::npos);

    // --- DELETE /api/v1/devices/{device_id} ---
    clear_http_capture();
    req.uri = "/api/v1/devices/not-16-hex";
    req.content_len = 0;
    g_request_body.clear();
    assert(web_ui::device_delete_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request\"") != std::string::npos);

    char unknown_delete_uri[64] = {};
    std::snprintf(unknown_delete_uri, sizeof(unknown_delete_uri), "/api/v1/devices/%s", kUnknownDeviceIdHex);
    clear_http_capture();
    req.uri = unknown_delete_uri;
    req.content_len = 0;
    assert(web_ui::device_delete_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "404 Not Found");
    assert(g_last_response.find("\"error\":\"device_not_found\"") != std::string::npos);

    char offline_delete_uri[64] = {};
    std::snprintf(offline_delete_uri, sizeof(offline_delete_uri), "/api/v1/devices/00124b0001aacccc");
    clear_http_capture();
    req.uri = offline_delete_uri;
    req.content_len = 0;
    assert(web_ui::device_delete_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "409 Conflict");
    assert(g_last_response.find("\"error\":\"device_offline\"") != std::string::npos);

    char located_delete_uri[64] = {};
    std::snprintf(located_delete_uri, sizeof(located_delete_uri), "/api/v1/devices/00124b0001aa2201");
    clear_http_capture();
    req.uri = located_delete_uri;
    req.content_len = 0;
    g_request_body.clear();
    // No Zigbee start permission yet in this fixture, so the underlying
    // network operation queue accepts the request but the eventual result
    // (polled separately, not part of this pass) would report failure;
    // what matters here is the v1 request-acceptance contract itself.
    assert(web_ui::device_delete_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "202 Accepted");

    // --- PUT /api/v1/devices/{device_id}/reporting/{endpoint}/{cluster_id} ---
    char reporting_uri[96] = {};
    std::snprintf(reporting_uri, sizeof(reporting_uri), "/api/v1/devices/00124b0001aa2201/reporting/1/1026");
    clear_http_capture();
    req.uri = reporting_uri;
    set_request_body(&req, "{\"min_interval_seconds\":301,\"max_interval_seconds\":300}");
    assert(web_ui::device_reporting_put_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request\"") != std::string::npos);

    clear_http_capture();
    req.uri = reporting_uri;
    set_request_body(
        &req,
        "{\"min_interval_seconds\":10,\"max_interval_seconds\":300,\"reportable_change\":25,\"capability_flags\":3}");
    assert(web_ui::device_reporting_put_handler_v1(&req) == ESP_OK);
    assert(g_last_status.empty());  // 200 OK
    assert(g_last_response.find("\"schema_version\":1") != std::string::npos);
    assert(g_last_response.find("\"accepted\":true") != std::string::npos);
    // post_reporting_profile_write() only enqueues; the write lands in
    // ConfigManager once process_pending() drains it.
    runtime.process_pending();

    service::ConfigManager::ReportingProfileKey reporting_key{};
    reporting_key.device_id = located_device_id;
    reporting_key.endpoint = 1U;
    reporting_key.cluster_id = 1026U;
    service::ConfigManager::ReportingProfile reporting_profile{};
    assert(runtime.config_manager().get_reporting_profile(reporting_key, &reporting_profile));
    assert(reporting_profile.min_interval_seconds == 10U);
    assert(reporting_profile.max_interval_seconds == 300U);

    char malformed_reporting_uri[96] = {};
    std::snprintf(
        malformed_reporting_uri, sizeof(malformed_reporting_uri), "/api/v1/devices/%s/reporting/1/1026",
        kMalformedDeviceIdHex);
    clear_http_capture();
    req.uri = malformed_reporting_uri;
    set_request_body(&req, "{\"min_interval_seconds\":10,\"max_interval_seconds\":300}");
    assert(web_ui::device_reporting_put_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");

    // --- POST /api/v1/network/scans ---
    clear_http_capture();
    req.content_len = 0;
    g_request_body.clear();
    assert(web_ui::network_scan_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "202 Accepted");
    assert(g_last_response.find("\"schema_version\":1") != std::string::npos);

    // --- POST /api/v1/network/connections ---
    clear_http_capture();
    set_request_body(&req, "{}");
    assert(web_ui::network_connect_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request\"") != std::string::npos);

    clear_http_capture();
    set_request_body(&req, "{\"ssid\":\"TestNet\",\"password\":\"hunter2\"}");
    assert(web_ui::network_connect_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "202 Accepted");

    // --- PATCH /api/v1/config ---
    clear_http_capture();
    set_request_body(&req, "{}");
    assert(web_ui::config_patch_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request\"") != std::string::npos);

    clear_http_capture();
    set_request_body(&req, "{\"command_timeout_ms\":7000,\"max_command_retries\":4}");
    assert(web_ui::config_patch_handler_v1(&req) == ESP_OK);
    assert(g_last_status.empty());  // 200 OK
    assert(g_last_response.find("\"schema_version\":1") != std::string::npos);
    runtime.process_pending();
    const service::ConfigSnapshot config_snapshot_after_patch = runtime.config_snapshot();
    assert(config_snapshot_after_patch.command_timeout_ms == 7000U);
    assert(config_snapshot_after_patch.max_command_retries == 4U);

    // --- POST /api/v1/ota/operations ---
    clear_http_capture();
    set_request_body(&req, "{}");
    assert(web_ui::ota_operations_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request\"") != std::string::npos);

    clear_http_capture();
    set_request_body(&req, "{\"url\":\"https://example.invalid/fw.bin\"}");
    assert(web_ui::ota_operations_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "202 Accepted");
    assert(g_last_response.find("\"schema_version\":1") != std::string::npos);

    // --- POST /api/v1/rcp-update/operations ---
    clear_http_capture();
    set_request_body(&req, "{}");
    assert(web_ui::rcp_update_operations_post_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request\"") != std::string::npos);

    // --- GET /api/v1/operations/{operation_id} ---
    clear_http_capture();
    req.uri = "/api/v1/operations/not-a-number";
    req.content_len = 0;
    g_request_body.clear();
    assert(web_ui::operations_get_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request\"") != std::string::npos);

    clear_http_capture();
    req.uri = "/api/v1/operations/0";
    assert(web_ui::operations_get_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");

    clear_http_capture();
    req.uri = "/api/v1/operations/999999999";
    assert(web_ui::operations_get_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "404 Not Found");
    assert(g_last_response.find("\"error\":\"operation_not_found\"") != std::string::npos);

    // A completed network scan: request_id resolves through
    // poll_operation_status() to domain=network, status=ready.
    const uint32_t scan_request_id = runtime.next_operation_request_id();
    assert(runtime.post_network_scan(scan_request_id));
    runtime.process_pending();
    char scan_op_uri[48] = {};
    std::snprintf(scan_op_uri, sizeof(scan_op_uri), "/api/v1/operations/%u", scan_request_id);
    clear_http_capture();
    req.uri = scan_op_uri;
    assert(web_ui::operations_get_handler_v1(&req) == ESP_OK);
    assert(g_last_status.empty());  // 200 OK
    assert(g_last_response.find("\"schema_version\":1") != std::string::npos);
    assert(g_last_response.find("\"domain\":\"network\"") != std::string::npos);
    assert(g_last_response.find("\"status\":\"ready\"") != std::string::npos);
    assert(g_last_response.find("\"ok\":true") != std::string::npos);

    // Re-polling the same (now-consumed) operation is 404 -- the store's
    // take_*_result() is destructive, matching the legacy per-domain GET
    // handlers' existing consume-on-read semantics.
    clear_http_capture();
    req.uri = scan_op_uri;
    assert(web_ui::operations_get_handler_v1(&req) == ESP_OK);
    assert(g_last_status == "404 Not Found");

    // A queued-but-not-yet-ready OTA operation reports status:"pending".
    const uint32_t ota_request_id = runtime.next_operation_request_id();
    runtime.mark_wifi_credentials_available();
    service::OtaStartRequest ota_request{};
    ota_request.request_id = ota_request_id;
    std::snprintf(ota_request.manifest.url.data(), ota_request.manifest.url.size(), "https://example.invalid/fw.bin");
    (void)runtime.post_ota_start(ota_request);
    char ota_op_uri[48] = {};
    std::snprintf(ota_op_uri, sizeof(ota_op_uri), "/api/v1/operations/%u", ota_request_id);
    clear_http_capture();
    req.uri = ota_op_uri;
    assert(web_ui::operations_get_handler_v1(&req) == ESP_OK);
    assert(g_last_status.empty());  // 200 OK
    assert(g_last_response.find("\"domain\":\"ota\"") != std::string::npos);
    assert(g_last_response.find("\"status\":\"pending\"") != std::string::npos);

    // Null user_ctx / null req are rejected without crashing.
    assert(web_ui::operations_get_handler_v1(nullptr) == ESP_FAIL);
    httpd_req_t bare_operations_req{};
    bare_operations_req.user_ctx = nullptr;
    assert(web_ui::operations_get_handler_v1(&bare_operations_req) == ESP_FAIL);

    // --- register_web_routes_v1 wires up all routes. ---
    assert(web_ui::register_web_routes_v1(reinterpret_cast<void*>(1), &route_ctx));
    assert(!web_ui::register_web_routes_v1(nullptr, &route_ctx));
    web_ui::WebRouteContext empty_ctx{};
    assert(!web_ui::register_web_routes_v1(reinterpret_cast<void*>(1), &empty_ctx));

    return 0;
}
