/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include <algorithm>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"
#include "web_handler_common.hpp"

// Mocks for ESP-IDF components not available on host
#ifndef ESP_PLATFORM
#define ESP_LOGI(tag, fmt, ...) printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)

extern "C" int64_t esp_timer_get_time() {
    return 1000000LL; // 1 second in microseconds
}
#endif

std::string g_last_response;
std::vector<std::string> g_chunks;
std::string g_query_string;
std::string g_request_body;
std::string g_last_status;

extern "C" esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) {
    (void)r; (void)type;
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len) {
    (void)r;
    g_last_response = (len == HTTPD_RESP_USE_STRLEN) ? buf : std::string(buf, len);
    return ESP_OK;
}

extern "C" esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler) {
    (void)handle; (void)uri_handler;
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status) {
    (void)r; (void)status;
    g_last_status = status ? status : "";
    return ESP_OK;
}

extern "C" int httpd_req_recv(httpd_req_t *r, char *buf, size_t len) {
    (void)r;
    if (g_request_body.empty()) return 0;
    size_t to_copy = std::min(len, g_request_body.length());
    std::memcpy(buf, g_request_body.c_str(), to_copy);
    g_request_body.erase(0, to_copy);
    return (int)to_copy;
}

extern "C" esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t len) {
    (void)r;
    if (buf == nullptr) return ESP_OK;
    g_chunks.push_back((len == HTTPD_RESP_USE_STRLEN) ? buf : std::string(buf, len));
    return ESP_OK;
}

extern "C" esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t len) {
    (void)r;
    if (g_query_string.length() >= len) return ESP_FAIL;
    std::strncpy(buf, g_query_string.c_str(), len);
    return ESP_OK;
}

extern "C" esp_err_t httpd_query_key_value(const char *qry, const char *key, char *val, size_t len) {
    std::string q(qry);
    std::string k(key);
    size_t pos = q.find(k + "=");
    if (pos == std::string::npos) return ESP_FAIL;
    size_t start = pos + k.length() + 1;
    size_t end = q.find('&', start);
    std::string value = q.substr(start, end - start);
    if (value.length() >= len) return ESP_FAIL;
    std::strncpy(val, value.c_str(), len);
    return ESP_OK;
}

// Include implementation to access anonymous namespace
#include "../../components/web_ui/web_handlers_network.cpp"
#include "hal_wifi.h"
#include "hal_nvs.h"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    
    uint32_t current_time = 1000;
    runtime.tick(current_time);
    assert(runtime.initialize_hal_adapter());

    std::atomic<uint32_t> next_id{100};
    web_ui::WebRouteContext route_ctx;
    route_ctx.runtime = &runtime;
    route_ctx.next_correlation_id = &next_id;

    httpd_req_t req;
    req.user_ctx = &route_ctx;

    
    g_last_response.clear();
    esp_err_t err = web_ui::network_scan_get_handler(&req);
    assert(err == ESP_OK);
    assert(g_last_response.find("\"request_id\":100") != std::string::npos);
    assert(g_last_response.find("\"operation\":\"scan\"") != std::string::npos);

    
    runtime.process_pending();

    
    g_query_string = "request_id=100";
    g_chunks.clear();
    err = web_ui::network_result_get_handler(&req);
    assert(err == ESP_OK);

    std::string full_response;
    for (const auto& chunk : g_chunks) full_response += chunk;
    
    assert(full_response.find("\"ready\":true") != std::string::npos);
    assert(full_response.find("\"ok\":true") != std::string::npos);
    assert(full_response.find("HomeWiFi") != std::string::npos);
    assert(full_response.find("OfficeWiFi") != std::string::npos);

    
    g_request_body = "{\"ssid\": \"\"}";
    assert(g_request_body.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
    req.content_len = static_cast<int>(g_request_body.size());
    g_last_response.clear();
    g_last_status.clear();
    
    err = web_ui::network_connect_post_handler(&req);
    assert(err == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_ssid\"") != std::string::npos);

    
    const char* test_ssid = "TestWiFi";
    g_request_body = "{\"ssid\": \"TestWiFi\", \"password\": \"pass123\"}";
    assert(g_request_body.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
    req.content_len = static_cast<int>(g_request_body.size());
    g_last_response.clear();
    g_last_status.clear();
    
    
    next_id.store(200);

    err = web_ui::network_connect_post_handler(&req);
    assert(err == ESP_OK);
    assert(g_last_status == "202 Accepted");
    assert(g_last_response.find("\"accepted\":true") != std::string::npos);
    assert(g_last_response.find("\"request_id\":200") != std::string::npos);

    
    runtime.process_pending();
    runtime.tick(current_time + 1000);
    
    service::NetworkResult connect_result{};
    assert(runtime.take_network_result(200, &connect_result));
    assert(connect_result.operation == service::NetworkOperationType::kConnect);
    assert(connect_result.status == service::NetworkOperationStatus::kOk);
    assert(std::strcmp(connect_result.ssid, test_ssid) == 0);

    
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK); 
    
    (void)hal_nvs_set_str("wifi_ssid", "");
    (void)hal_nvs_set_str("wifi_password", "");
    next_id.store(300);
    g_last_response.clear();
    err = web_ui::network_credentials_status_get_handler(&req);
    assert(err == ESP_OK);
    assert(g_last_response.find("\"request_id\":300") != std::string::npos);

    runtime.process_pending();

    g_query_string = "request_id=300";
    g_last_response.clear();
    err = web_ui::network_result_get_handler(&req);
    assert(err == ESP_OK);
    
    assert(g_last_response.find("\"operation\":\"credentials_status\"") != std::string::npos);
    assert(g_last_response.find("\"saved\":false") != std::string::npos);
    assert(g_last_response.find("\"has_password\":false") != std::string::npos);

    
    assert(hal_nvs_set_str("wifi_ssid", "SavedWiFi") == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_str("wifi_password", "secret123") == HAL_NVS_STATUS_OK);
    
    next_id.store(400);
    err = web_ui::network_credentials_status_get_handler(&req);
    assert(err == ESP_OK);

    runtime.process_pending();

    g_query_string = "request_id=400";
    g_last_response.clear();
    err = web_ui::network_result_get_handler(&req);
    assert(err == ESP_OK);
    
    assert(g_last_response.find("\"saved\":true") != std::string::npos);
    assert(g_last_response.find("\"has_password\":true") != std::string::npos);
    assert(g_last_response.find("\"ssid\":\"SavedWiFi\"") != std::string::npos);

    
    g_query_string = "request_id=abc"; 
    g_last_response.clear();
    g_last_status.clear();
    err = web_ui::network_result_get_handler(&req);
    assert(err == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request_id\"") != std::string::npos);

    g_query_string = ""; 
    g_last_response.clear();
    g_last_status.clear();
    err = web_ui::network_result_get_handler(&req);
    assert(err == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_request_id\"") != std::string::npos);

    printf("All network handler tests passed!\n");
    return 0;
}
