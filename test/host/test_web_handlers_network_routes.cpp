/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "web_handler_common.hpp"

#ifndef ESP_PLATFORM
#define ESP_LOGI(tag, fmt, ...) std::printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) std::printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) std::printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)
extern "C" int64_t esp_timer_get_time() {
    return 2000000LL;
}
#endif

std::string g_last_response;
std::string g_last_status;
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
    (void)buf;
    (void)len;
    return 0;
}

extern "C" esp_err_t httpd_resp_send_chunk(httpd_req_t* req, const char* buf, ssize_t len) {
    (void)req;
    (void)buf;
    (void)len;
    return ESP_OK;
}

extern "C" esp_err_t httpd_req_get_url_query_str(httpd_req_t* req, char* buf, size_t len) {
    (void)req;
    (void)buf;
    (void)len;
    return ESP_FAIL;
}

extern "C" esp_err_t httpd_query_key_value(const char* qry, const char* key, char* val, size_t len) {
    (void)qry;
    (void)key;
    (void)val;
    (void)len;
    return ESP_FAIL;
}

void clear_http_capture() {
    g_last_response.clear();
    g_last_status.clear();
}

// Direct include for anonymous-namespace handlers.
#include "../../components/web_ui/web_handlers_network.cpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    std::atomic<uint32_t> next_id{700};
    web_ui::WebRouteContext route_ctx{};
    route_ctx.runtime = &runtime;
    route_ctx.next_correlation_id = &next_id;

    httpd_req_t req{};
    req.user_ctx = &route_ctx;

    assert(web_ui::network_get_handler(nullptr) == ESP_FAIL);
    httpd_req_t bad_req{};
    bad_req.user_ctx = nullptr;
    assert(web_ui::network_get_handler(&bad_req) == ESP_FAIL);

    core::CoreEvent network_up{};
    network_up.type = core::CoreEventType::kNetworkUp;
    assert(runtime.post_event(network_up));
    assert(runtime.process_pending() == 1U);

    clear_http_capture();
    assert(web_ui::network_get_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"revision\":1") != std::string::npos);
    assert(g_last_response.find("\"connected\":true") != std::string::npos);
    assert(g_last_response.find("\"refresh_requests\":0") != std::string::npos);
    assert(g_last_response.find("\"current_backoff_ms\":0") != std::string::npos);

    clear_http_capture();
    assert(runtime.stats().network_refresh_requests == 0U);
    assert(web_ui::network_refresh_post_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"accepted\":true") != std::string::npos);
    assert(g_last_response.find("\"correlation_id\":700") != std::string::npos);

    runtime.process_pending();
    assert(runtime.stats().network_refresh_requests == 1U);

    assert(!web_ui::register_network_routes(nullptr, &route_ctx));
    assert(!web_ui::register_network_routes(reinterpret_cast<void*>(1), nullptr));

    g_register_call_count = 0;
    g_register_fail_at = 0;
    assert(web_ui::register_network_routes(reinterpret_cast<void*>(1), &route_ctx));
    const int success_registration_count = g_register_call_count;
    assert(success_registration_count >= 6);

    for (int fail_at = 1; fail_at <= success_registration_count; ++fail_at) {
        g_register_call_count = 0;
        g_register_fail_at = fail_at;
        assert(!web_ui::register_network_routes(reinterpret_cast<void*>(1), &route_ctx));
        assert(g_register_call_count == fail_at);
    }

    g_register_call_count = 0;
    g_register_fail_at = 0;
    assert(web_ui::register_network_routes(reinterpret_cast<void*>(1), &route_ctx));
    assert(g_register_call_count == success_registration_count);

    return 0;
}
