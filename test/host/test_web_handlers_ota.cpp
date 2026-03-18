/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_ota.h"
#include "service_runtime.hpp"
#include "web_handler_common.hpp"

#ifndef ESP_PLATFORM
#define ESP_LOGI(tag, fmt, ...) std::printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) std::printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) std::printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#endif

namespace {

std::string g_last_response;
std::string g_last_status;
std::string g_query_string;
std::string g_request_body;
int g_register_call_count = 0;

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
    (void)buf;
    (void)len;
    return ESP_OK;
}

extern "C" esp_err_t httpd_req_get_url_query_str(httpd_req_t* req, char* buf, size_t len) {
    (void)req;
    if (g_query_string.size() >= len) {
        return ESP_FAIL;
    }
    std::strncpy(buf, g_query_string.c_str(), len);
    return ESP_OK;
}

extern "C" esp_err_t httpd_query_key_value(const char* qry, const char* key, char* val, size_t len) {
    const std::string query(qry == nullptr ? "" : qry);
    const std::string token = std::string(key == nullptr ? "" : key) + "=";
    const std::size_t pos = query.find(token);
    if (pos == std::string::npos) {
        return ESP_FAIL;
    }
    const std::size_t start = pos + token.size();
    const std::size_t end = query.find('&', start);
    const std::string value = query.substr(start, end - start);
    if (value.size() >= len) {
        return ESP_FAIL;
    }
    std::strncpy(val, value.c_str(), len);
    return ESP_OK;
}

extern "C" int hal_ota_platform_perform_https_update(
    const hal_ota_https_request_t* request,
    hal_ota_progress_cb_t progress_cb,
    void* user_ctx,
    hal_ota_https_result_t* out_result) {
    assert(request != nullptr);
    assert(out_result != nullptr);
    std::memset(out_result, 0, sizeof(*out_result));
    out_result->status = HAL_OTA_HTTPS_STATUS_OK;
    out_result->reboot_required = true;
    out_result->bytes_read = 2048U;
    out_result->image_size = 4096U;
    out_result->image_size_known = true;
    std::strncpy(out_result->discovered_version, "9.9.9", sizeof(out_result->discovered_version) - 1U);
    if (progress_cb != nullptr) {
        progress_cb(1024U, 4096U, true, user_ctx);
        progress_cb(2048U, 4096U, true, user_ctx);
    }
    return 0;
}

uint32_t extract_request_id(const std::string& response) {
    const std::string token = "\"request_id\":";
    const std::size_t pos = response.find(token);
    assert(pos != std::string::npos);
    const std::size_t start = pos + token.size();
    const std::size_t end = response.find_first_not_of("0123456789", start);
    return static_cast<uint32_t>(std::stoul(response.substr(start, end - start)));
}

}  // namespace

#include "../../components/web_ui/web_handlers_ota.cpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    std::atomic<uint32_t> next_id{1U};
    web_ui::WebRouteContext route_ctx{};
    route_ctx.runtime = &runtime;
    route_ctx.next_correlation_id = &next_id;

    httpd_req_t req{};
    req.user_ctx = &route_ctx;

    g_last_response.clear();
    g_last_status.clear();
    assert(web_ui::ota_get_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"stage\":\"idle\"") != std::string::npos);
    assert(g_last_response.find("\"current_version\":\"host-test\"") != std::string::npos);

    g_request_body = "{\"target_version\":\"1.2.3\"}";
    assert(g_request_body.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
    req.content_len = static_cast<int>(g_request_body.size());
    g_last_response.clear();
    g_last_status.clear();
    assert(web_ui::ota_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_url\"") != std::string::npos);

    g_request_body = "{\"url\":\"https://updates.local/gateway.bin\",\"target_version\":\"1.2.3\"}";
    assert(g_request_body.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
    req.content_len = static_cast<int>(g_request_body.size());
    g_last_response.clear();
    g_last_status.clear();
    assert(web_ui::ota_post_handler(&req) == ESP_OK);
    assert(g_last_status == "202 Accepted");
    assert(g_last_response.find("\"operation\":\"ota\"") != std::string::npos);
    const uint32_t request_id = extract_request_id(g_last_response);

    g_query_string = "request_id=" + std::to_string(request_id);
    g_last_response.clear();
    assert(web_ui::ota_result_get_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"ready\":false") != std::string::npos);
    assert(g_last_response.find("\"status\":\"queued\"") != std::string::npos);

    assert(runtime.process_pending() == 0U);

    g_last_response.clear();
    assert(web_ui::ota_get_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"stage\":\"switch_pending\"") != std::string::npos);
    assert(g_last_response.find("\"target_version\":\"1.2.3\"") != std::string::npos);
    assert(g_last_response.find("\"progress_percent\":50") != std::string::npos);

    g_last_response.clear();
    assert(web_ui::ota_result_get_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"ready\":true") != std::string::npos);
    assert(g_last_response.find("\"ok\":true") != std::string::npos);
    assert(g_last_response.find("\"reboot_required\":true") != std::string::npos);
    assert(g_last_response.find("\"target_version\":\"1.2.3\"") != std::string::npos);

    g_register_call_count = 0;
    assert(web_ui::register_ota_routes(reinterpret_cast<void*>(1), &route_ctx));
    assert(g_register_call_count == 3);

    return 0;
}
