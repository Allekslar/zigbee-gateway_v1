/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>

#include "esp_http_client.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_wifi.h"
#include "hal_zigbee.h"
#include "service_runtime.hpp"
#include "unity.h"
#include "web_server.hpp"

namespace {

constexpr int kHttpTimeoutMs = 2000;
constexpr int kHttpRetryCount = 30;
constexpr int kHttpRetryDelayMs = 100;

struct HttpResponseBuffer {
    char* data{nullptr};
    std::size_t capacity{0};
    std::size_t length{0};
};

esp_err_t http_event_handler(esp_http_client_event_t* event) {
    if (event == nullptr) {
        return ESP_FAIL;
    }

    if (event->event_id != HTTP_EVENT_ON_DATA || event->user_data == nullptr || event->data == nullptr ||
        event->data_len <= 0) {
        return ESP_OK;
    }

    auto* response = static_cast<HttpResponseBuffer*>(event->user_data);
    if (response->data == nullptr || response->capacity == 0U) {
        return ESP_OK;
    }

    const std::size_t writable =
        response->length < (response->capacity - 1U) ? (response->capacity - 1U - response->length) : 0U;
    if (writable == 0U) {
        return ESP_OK;
    }

    std::size_t to_copy = static_cast<std::size_t>(event->data_len);
    if (to_copy > writable) {
        to_copy = writable;
    }
    std::memcpy(response->data + response->length, event->data, to_copy);
    response->length += to_copy;
    response->data[response->length] = '\0';
    return ESP_OK;
}

bool http_request(
    const char* url,
    esp_http_client_method_t method,
    const char* body,
    char* response_out,
    std::size_t response_capacity,
    int* status_code_out) {
    if (url == nullptr || response_out == nullptr || response_capacity == 0 || status_code_out == nullptr) {
        return false;
    }

    response_out[0] = '\0';
    *status_code_out = 0;

    HttpResponseBuffer response{};
    response.data = response_out;
    response.capacity = response_capacity;
    response.length = 0U;

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = kHttpTimeoutMs;
    config.event_handler = http_event_handler;
    config.user_data = &response;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    esp_http_client_set_method(client, method);
    if (body != nullptr) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, static_cast<int>(std::strlen(body)));
    }

    const esp_err_t perform_err = esp_http_client_perform(client);
    if (perform_err != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }

    *status_code_out = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    return true;
}

bool http_request_with_retry(
    const char* url,
    esp_http_client_method_t method,
    const char* body,
    char* response_out,
    std::size_t response_capacity,
    int* status_code_out) {
    for (int attempt = 0; attempt < kHttpRetryCount; ++attempt) {
        if (http_request(url, method, body, response_out, response_capacity, status_code_out)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(kHttpRetryDelayMs));
    }
    return false;
}

bool extract_u32_field(const char* json, const char* field_name, uint32_t* value_out) {
    if (json == nullptr || field_name == nullptr || value_out == nullptr) {
        return false;
    }

    char pattern[64] = {};
    const int pattern_len = std::snprintf(pattern, sizeof(pattern), "\"%s\":", field_name);
    if (pattern_len <= 0 || pattern_len >= static_cast<int>(sizeof(pattern))) {
        return false;
    }

    const char* field = std::strstr(json, pattern);
    if (field == nullptr) {
        return false;
    }

    field += pattern_len;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(field, &end, 10);
    if (end == field) {
        return false;
    }

    *value_out = static_cast<uint32_t>(parsed);
    return true;
}

bool extract_request_id(const char* json, uint32_t* request_id_out) {
    return extract_u32_field(json, "request_id", request_id_out);
}

bool response_has(const char* response, const char* token) {
    return response != nullptr && token != nullptr && std::strstr(response, token) != nullptr;
}

std::size_t count_token_occurrences(const char* response, const char* token) {
    if (response == nullptr || token == nullptr || token[0] == '\0') {
        return 0U;
    }

    std::size_t count = 0U;
    const std::size_t token_len = std::strlen(token);
    const char* cursor = response;
    while (true) {
        const char* found = std::strstr(cursor, token);
        if (found == nullptr) {
            break;
        }
        ++count;
        cursor = found + token_len;
    }
    return count;
}

struct RuntimeFixture {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime;
    web_ui::WebServer web_server;

    RuntimeFixture() : runtime(registry, effect_executor), web_server(registry, runtime) {}
};

}  // namespace

extern "C" void test_web_api_http_command_result_updates_snapshot(void) {
    const char* failure = nullptr;

    RuntimeFixture* fixture = new RuntimeFixture();
    service::ServiceRuntime& runtime = fixture->runtime;
    web_ui::WebServer& web_server = fixture->web_server;

    char response[256] = {};
    int status_code = 0;
    uint32_t correlation_id = 0;
    uint32_t revision = 0;

    if (!runtime.initialize_hal_adapter()) {
        failure = "runtime.initialize_hal_adapter failed";
        goto cleanup;
    }

    if (!web_server.start()) {
        failure = "web_server.start failed";
        goto cleanup;
    }

    (void)runtime.process_pending();

    if (!http_request_with_retry(
            "http://127.0.0.1/api/devices/power",
            HTTP_METHOD_POST,
            "{\"short_addr\":4660,\"power_on\":true}",
            response,
            sizeof(response),
            &status_code)) {
        failure = "POST /api/devices/power failed";
        goto cleanup;
    }

    if (status_code != 200) {
        failure = "POST /api/devices/power status != 200";
        goto cleanup;
    }

    if (!extract_u32_field(response, "correlation_id", &correlation_id) || correlation_id == 0U) {
        failure = "missing correlation_id in POST response";
        goto cleanup;
    }

    if (runtime.pending_events() == 0U) {
        failure = "command event was not queued by HTTP route";
        goto cleanup;
    }

    (void)runtime.process_pending();

    hal_zigbee_notify_command_result(correlation_id, HAL_ZIGBEE_RESULT_SUCCESS);
    (void)runtime.process_pending();

    if (!http_request_with_retry(
            "http://127.0.0.1/api/config",
            HTTP_METHOD_GET,
            nullptr,
            response,
            sizeof(response),
            &status_code)) {
        failure = "GET /api/config failed";
        goto cleanup;
    }

    if (status_code != 200) {
        failure = "GET /api/config status != 200";
        goto cleanup;
    }

    if (std::strstr(response, "\"last_command_status\":1") == nullptr) {
        failure = "snapshot does not contain successful command status";
        goto cleanup;
    }

    if (!extract_u32_field(response, "revision", &revision) || revision == 0U) {
        failure = "snapshot revision was not incremented";
        goto cleanup;
    }

cleanup:
    web_server.stop();
    delete fixture;
    if (failure != nullptr) {
        TEST_FAIL_MESSAGE(failure);
    }
}

extern "C" void test_web_api_config_reporting_update_validation_and_queue_apply(void) {
    const char* failure = nullptr;

    RuntimeFixture* fixture = new RuntimeFixture();
    service::ServiceRuntime& runtime = fixture->runtime;
    web_ui::WebServer& web_server = fixture->web_server;

    char response[256] = {};
    int status_code = 0;
    service::ConfigManager::ReportingProfileKey key{};
    service::ConfigManager::ReportingProfile profile{};
    const uint16_t test_cluster_id = static_cast<uint16_t>(0x7000U | (esp_random() & 0x0FFFU));
    char invalid_payload[192] = {};
    char valid_payload[256] = {};

    std::snprintf(
        invalid_payload,
        sizeof(invalid_payload),
        "{\"short_addr\":8705,\"endpoint\":1,\"cluster_id\":%u,\"min_interval_seconds\":301,\"max_interval_seconds\":300}",
        static_cast<unsigned>(test_cluster_id));
    std::snprintf(
        valid_payload,
        sizeof(valid_payload),
        "{\"short_addr\":8705,\"endpoint\":1,\"cluster_id\":%u,\"min_interval_seconds\":10,\"max_interval_seconds\":300,\"reportable_change\":42,\"capability_flags\":3}",
        static_cast<unsigned>(test_cluster_id));

    if (!runtime.initialize_hal_adapter()) {
        failure = "runtime.initialize_hal_adapter failed";
        goto cleanup;
    }

    if (!web_server.start()) {
        failure = "web_server.start failed";
        goto cleanup;
    }

    if (!http_request_with_retry(
            "http://127.0.0.1/api/config/reporting",
            HTTP_METHOD_POST,
            invalid_payload,
            response,
            sizeof(response),
            &status_code)) {
        failure = "POST /api/config/reporting failed for invalid bounds";
        goto cleanup;
    }

    if (status_code != 400 || !response_has(response, "\"error\":\"invalid_profile_bounds\"")) {
        failure = "invalid bounds were not rejected with 400";
        goto cleanup;
    }

    if (!http_request_with_retry(
            "http://127.0.0.1/api/config/reporting",
            HTTP_METHOD_POST,
            valid_payload,
            response,
            sizeof(response),
            &status_code)) {
        failure = "POST /api/config/reporting failed for valid payload";
        goto cleanup;
    }

    if (status_code != 200 || !response_has(response, "\"accepted\":true")) {
        failure = "valid reporting profile update was not accepted";
        goto cleanup;
    }

    key.short_addr = 0x2201U;
    key.endpoint = 1U;
    key.cluster_id = test_cluster_id;
    if (runtime.config_manager().get_reporting_profile(key, &profile)) {
        failure = "reporting profile applied before queue drain";
        goto cleanup;
    }

    (void)runtime.process_pending();

    if (!runtime.config_manager().get_reporting_profile(key, &profile)) {
        failure = "reporting profile was not applied after queue drain";
        goto cleanup;
    }

    if (profile.min_interval_seconds != 10U || profile.max_interval_seconds != 300U ||
        profile.reportable_change != 42U || profile.capability_flags != 3U) {
        failure = "applied reporting profile values mismatch";
        goto cleanup;
    }

cleanup:
    web_server.stop();
    delete fixture;
    if (failure != nullptr) {
        TEST_FAIL_MESSAGE(failure);
    }
}

extern "C" void test_web_api_end_to_end_zigbee_core_effects_http_flow(void) {
    const char* failure = nullptr;

    RuntimeFixture* fixture = new RuntimeFixture();
    service::ServiceRuntime& runtime = fixture->runtime;
    web_ui::WebServer& web_server = fixture->web_server;

    char response[1024] = {};
    int status_code = 0;
    uint32_t correlation_id = 0;

    static constexpr uint16_t kDeviceShortAddr = 0x3344;
    static constexpr uint16_t kOnOffClusterId = 0x0006;
    static constexpr uint16_t kOnOffAttributeId = 0x0000;

    if (!runtime.initialize_hal_adapter()) {
        failure = "runtime.initialize_hal_adapter failed";
        goto cleanup;
    }

    if (!web_server.start()) {
        failure = "web_server.start failed";
        goto cleanup;
    }

    // Zigbee -> HAL callback -> Service ingress -> Core state.
    hal_zigbee_notify_device_joined(kDeviceShortAddr);
    (void)runtime.process_pending();

    if (!http_request_with_retry(
            "http://127.0.0.1/api/devices",
            HTTP_METHOD_GET,
            nullptr,
            response,
            sizeof(response),
            &status_code)) {
        failure = "GET /api/devices failed after join";
        goto cleanup;
    }

    if (status_code != 200) {
        failure = "GET /api/devices status != 200 after join";
        goto cleanup;
    }

    if (!response_has(response, "\"device_count\":1") || !response_has(response, "\"short_addr\":13124")) {
        failure = "joined device is missing in /api/devices";
        goto cleanup;
    }

    // Zigbee attribute report should update Core state and be visible via Web API.
    hal_zigbee_notify_attribute_report(kDeviceShortAddr, kOnOffClusterId, kOnOffAttributeId, true, 1U);
    (void)runtime.process_pending();

    if (!http_request_with_retry(
            "http://127.0.0.1/api/devices",
            HTTP_METHOD_GET,
            nullptr,
            response,
            sizeof(response),
            &status_code)) {
        failure = "GET /api/devices failed after ON attribute report";
        goto cleanup;
    }

    if (status_code != 200 || !response_has(response, "\"short_addr\":13124") || !response_has(response, "\"power_on\":true")) {
        failure = "ON attribute report is not reflected in /api/devices";
        goto cleanup;
    }

    // Web API -> Core command -> Effects executor -> HAL command path.
    if (!http_request_with_retry(
            "http://127.0.0.1/api/devices/power",
            HTTP_METHOD_POST,
            "{\"short_addr\":13124,\"power_on\":false}",
            response,
            sizeof(response),
            &status_code)) {
        failure = "POST /api/devices/power failed";
        goto cleanup;
    }

    if (status_code != 200) {
        failure = "POST /api/devices/power status != 200";
        goto cleanup;
    }

    if (!extract_u32_field(response, "correlation_id", &correlation_id) || correlation_id == 0U) {
        failure = "missing correlation_id in power command response";
        goto cleanup;
    }

    if (runtime.pending_commands() == 0U) {
        failure = "power command did not enter pending command pipeline";
        goto cleanup;
    }

    (void)runtime.process_pending();
    if (runtime.pending_commands() == 0U) {
        failure = "pending command disappeared before command result callback";
        goto cleanup;
    }

    // HAL callback -> Core command completion -> Config snapshot update.
    hal_zigbee_notify_command_result(correlation_id, HAL_ZIGBEE_RESULT_SUCCESS);
    hal_zigbee_notify_attribute_report(kDeviceShortAddr, kOnOffClusterId, kOnOffAttributeId, false, 0U);
    (void)runtime.process_pending();

    if (runtime.pending_commands() != 0U) {
        failure = "pending command was not resolved after command result callback";
        goto cleanup;
    }

    if (!http_request_with_retry(
            "http://127.0.0.1/api/config",
            HTTP_METHOD_GET,
            nullptr,
            response,
            sizeof(response),
            &status_code)) {
        failure = "GET /api/config failed after command result";
        goto cleanup;
    }

    if (status_code != 200 || !response_has(response, "\"last_command_status\":1")) {
        failure = "command result success is not reflected in /api/config";
        goto cleanup;
    }

    if (!http_request_with_retry(
            "http://127.0.0.1/api/devices",
            HTTP_METHOD_GET,
            nullptr,
            response,
            sizeof(response),
            &status_code)) {
        failure = "GET /api/devices failed after OFF attribute report";
        goto cleanup;
    }

    if (status_code != 200 || !response_has(response, "\"short_addr\":13124") || !response_has(response, "\"power_on\":false")) {
        failure = "OFF state is not reflected in /api/devices after full flow";
        goto cleanup;
    }

cleanup:
    web_server.stop();
    delete fixture;
    if (failure != nullptr) {
        TEST_FAIL_MESSAGE(failure);
    }
}

extern "C" void test_web_api_network_scan_result_status_transitions(void) {
    const char* failure = nullptr;

    RuntimeFixture* fixture = new RuntimeFixture();
    service::ServiceRuntime& runtime = fixture->runtime;
    web_ui::WebServer& web_server = fixture->web_server;

    char response[256] = {};
    char url[96] = {};
    int status_code = 0;
    uint32_t request_id = 0;
    uint32_t scan_worker_request_id = 0;
    service::ServiceRuntime::NetworkResult ready{};

    if (!runtime.initialize_hal_adapter()) {
        failure = "runtime.initialize_hal_adapter failed";
        goto cleanup;
    }

    // Keep STA capability available for scan paths in test environment.
    (void)hal_wifi_set_mode(HAL_WIFI_MODE_APSTA);

    if (!web_server.start()) {
        failure = "web_server.start failed";
        goto cleanup;
    }

    if (!http_request_with_retry(
            "http://127.0.0.1/api/network/scan",
            HTTP_METHOD_GET,
            nullptr,
            response,
            sizeof(response),
            &status_code)) {
        failure = "GET /api/network/scan failed";
        goto cleanup;
    }

    if (status_code != 202) {
        failure = "GET /api/network/scan status != 202";
        goto cleanup;
    }

    if (!extract_request_id(response, &request_id) || request_id == 0U) {
        failure = "missing request_id in /api/network/scan response";
        goto cleanup;
    }

    std::snprintf(url, sizeof(url), "http://127.0.0.1/api/network/result?request_id=%" PRIu32, request_id);
    if (!http_request_with_retry(url, HTTP_METHOD_GET, nullptr, response, sizeof(response), &status_code)) {
        failure = "GET /api/network/result (queued pre-drain) failed";
        goto cleanup;
    }
    if (status_code != 200 || !response_has(response, "\"status\":\"scan_queued\"")) {
        failure = "network result did not report scan_queued pre-drain";
        goto cleanup;
    }

    (void)runtime.process_pending();
    if (!runtime.pop_scan_worker_request_for_test(&scan_worker_request_id) || scan_worker_request_id != request_id) {
        failure = "failed to pop scan worker request in test hook";
        goto cleanup;
    }
    runtime.set_scan_request_in_progress_for_test(request_id);

    if (!http_request_with_retry(url, HTTP_METHOD_GET, nullptr, response, sizeof(response), &status_code)) {
        failure = "GET /api/network/result (in_progress) failed";
        goto cleanup;
    }
    if (status_code != 200 || !response_has(response, "\"status\":\"scan_in_progress\"")) {
        failure = "network result did not report scan_in_progress";
        goto cleanup;
    }

    ready.request_id = request_id;
    ready.operation = service::ServiceRuntime::NetworkOperationType::kScan;
    ready.status = service::ServiceRuntime::NetworkOperationStatus::kOk;
    ready.scan_count = 0U;
    runtime.clear_scan_request_in_progress_for_test();
    if (!runtime.push_network_result_for_test(ready)) {
        failure = "failed to queue ready network result via test hook";
        goto cleanup;
    }

    if (!http_request_with_retry(url, HTTP_METHOD_GET, nullptr, response, sizeof(response), &status_code)) {
        failure = "GET /api/network/result (ready) failed";
        goto cleanup;
    }
    if (status_code != 200 || !response_has(response, "\"ready\":true") || !response_has(response, "\"operation\":\"scan\"")) {
        failure = "network result did not transition to ready scan response";
        goto cleanup;
    }

cleanup:
    web_server.stop();
    delete fixture;
    if (failure != nullptr) {
        TEST_FAIL_MESSAGE(failure);
    }
}

extern "C" void test_web_api_network_scan_result_failure_transition(void) {
    const char* failure = nullptr;

    RuntimeFixture* fixture = new RuntimeFixture();
    service::ServiceRuntime& runtime = fixture->runtime;
    web_ui::WebServer& web_server = fixture->web_server;

    char response[256] = {};
    char url[96] = {};
    int status_code = 0;
    uint32_t request_id = 0;
    uint32_t scan_worker_request_id = 0;
    service::ServiceRuntime::NetworkResult failed{};

    if (!runtime.initialize_hal_adapter()) {
        failure = "runtime.initialize_hal_adapter failed";
        goto cleanup;
    }

    (void)hal_wifi_set_mode(HAL_WIFI_MODE_APSTA);

    if (!web_server.start()) {
        failure = "web_server.start failed";
        goto cleanup;
    }

    if (!http_request_with_retry(
            "http://127.0.0.1/api/network/scan",
            HTTP_METHOD_GET,
            nullptr,
            response,
            sizeof(response),
            &status_code)) {
        failure = "GET /api/network/scan failed";
        goto cleanup;
    }

    if (status_code != 202) {
        failure = "GET /api/network/scan status != 202";
        goto cleanup;
    }

    if (!extract_request_id(response, &request_id) || request_id == 0U) {
        failure = "missing request_id in /api/network/scan response";
        goto cleanup;
    }

    std::snprintf(url, sizeof(url), "http://127.0.0.1/api/network/result?request_id=%" PRIu32, request_id);
    if (!http_request_with_retry(url, HTTP_METHOD_GET, nullptr, response, sizeof(response), &status_code)) {
        failure = "GET /api/network/result (queued pre-drain) failed";
        goto cleanup;
    }
    if (status_code != 200 || !response_has(response, "\"status\":\"scan_queued\"")) {
        failure = "network result did not report scan_queued pre-drain";
        goto cleanup;
    }

    (void)runtime.process_pending();
    if (!runtime.pop_scan_worker_request_for_test(&scan_worker_request_id) || scan_worker_request_id != request_id) {
        failure = "failed to pop scan worker request in test hook";
        goto cleanup;
    }
    runtime.set_scan_request_in_progress_for_test(request_id);

    if (!http_request_with_retry(url, HTTP_METHOD_GET, nullptr, response, sizeof(response), &status_code)) {
        failure = "GET /api/network/result (in_progress) failed";
        goto cleanup;
    }
    if (status_code != 200 || !response_has(response, "\"status\":\"scan_in_progress\"")) {
        failure = "network result did not report scan_in_progress";
        goto cleanup;
    }

    failed.request_id = request_id;
    failed.operation = service::ServiceRuntime::NetworkOperationType::kScan;
    failed.status = service::ServiceRuntime::NetworkOperationStatus::kHalFailed;
    runtime.clear_scan_request_in_progress_for_test();
    if (!runtime.push_network_result_for_test(failed)) {
        failure = "failed to queue failed network result via test hook";
        goto cleanup;
    }

    if (!http_request_with_retry(url, HTTP_METHOD_GET, nullptr, response, sizeof(response), &status_code)) {
        failure = "GET /api/network/result (failed) failed";
        goto cleanup;
    }
    if (status_code != 200 || !response_has(response, "\"ready\":true") || !response_has(response, "\"ok\":false") ||
        !response_has(response, "\"error\":\"scan_failed\"")) {
        failure = "network result did not transition to failed scan response";
        goto cleanup;
    }

cleanup:
    web_server.stop();
    delete fixture;
    if (failure != nullptr) {
        TEST_FAIL_MESSAGE(failure);
    }
}

extern "C" void test_web_api_devices_snapshot_consistency_under_runtime_updates(void) {
    const char* failure = nullptr;

    RuntimeFixture* fixture = new RuntimeFixture();
    service::ServiceRuntime& runtime = fixture->runtime;
    web_ui::WebServer& web_server = fixture->web_server;

    char response[1024] = {};
    int status_code = 0;
    uint32_t device_count = 0;
    bool saw_force_remove_armed = false;

    if (!runtime.initialize_hal_adapter()) {
        failure = "runtime.initialize_hal_adapter failed";
        goto cleanup;
    }

    if (!web_server.start()) {
        failure = "web_server.start failed";
        goto cleanup;
    }

    hal_zigbee_notify_device_joined(0x2201);
    hal_zigbee_notify_device_joined(0x2202);
    (void)runtime.process_pending();

    if (!http_request_with_retry(
            "http://127.0.0.1/api/devices/remove",
            HTTP_METHOD_POST,
            "{\"short_addr\":8706,\"force_remove\":true,\"force_remove_timeout_ms\":15000}",
            response,
            sizeof(response),
            &status_code)) {
        failure = "POST /api/devices/remove failed";
        goto cleanup;
    }

    if (status_code != 202) {
        failure = "POST /api/devices/remove status != 202";
        goto cleanup;
    }

    (void)runtime.process_pending();

    for (uint16_t i = 0; i < 8U; ++i) {
        const uint16_t transient_short_addr = static_cast<uint16_t>(0x2300U + i);
        if ((i % 2U) == 0U) {
            hal_zigbee_notify_device_joined(transient_short_addr);
        } else {
            hal_zigbee_notify_device_left(static_cast<uint16_t>(transient_short_addr - 1U));
        }
        (void)runtime.process_pending();

        if (!http_request_with_retry(
                "http://127.0.0.1/api/devices",
                HTTP_METHOD_GET,
                nullptr,
                response,
                sizeof(response),
                &status_code)) {
            failure = "GET /api/devices failed";
            goto cleanup;
        }

        if (status_code != 200) {
            failure = "GET /api/devices status != 200";
            goto cleanup;
        }

        if (!extract_u32_field(response, "device_count", &device_count)) {
            failure = "missing device_count in /api/devices response";
            goto cleanup;
        }

        const std::size_t listed_devices = count_token_occurrences(response, "\"short_addr\":");
        if (listed_devices != static_cast<std::size_t>(device_count)) {
            failure = "device_count does not match listed devices";
            goto cleanup;
        }

        if (response_has(response, "\"join_window_open\":false") &&
            !response_has(response, "\"join_window_seconds_left\":0")) {
            failure = "join_window_open=false with non-zero seconds_left";
            goto cleanup;
        }

        if (response_has(response, "\"force_remove_armed\":true,\"force_remove_ms_left\":0")) {
            failure = "force_remove_armed=true with zero ms_left";
            goto cleanup;
        }

        if (response_has(response, "\"force_remove_armed\":true")) {
            saw_force_remove_armed = true;
        }
    }

    if (!saw_force_remove_armed) {
        failure = "force remove state was not visible in /api/devices snapshots";
        goto cleanup;
    }

cleanup:
    web_server.stop();
    delete fixture;
    if (failure != nullptr) {
        TEST_FAIL_MESSAGE(failure);
    }
}
