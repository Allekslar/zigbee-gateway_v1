/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
    route_ctx.registry = &registry;
    route_ctx.runtime = &runtime;
    route_ctx.next_correlation_id = &next_id;

    httpd_req_t req{};
    req.user_ctx = &route_ctx;

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x2201;
    assert(runtime.post_event(joined));
    runtime.process_pending();

    clear_http_state();
    assert(web_ui::devices_get_handler(&req) == ESP_OK);
    std::string devices_response;
    for (const std::string& chunk : g_chunks) {
        devices_response += chunk;
    }
    assert(devices_response.find("\"device_count\":1") != std::string::npos);
    assert(devices_response.find("\"short_addr\":8705") != std::string::npos);

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
