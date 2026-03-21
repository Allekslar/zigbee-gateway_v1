/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <atomic>
#include <string>

#include "web_handler_common.hpp"

namespace {

int g_register_call_count = 0;
int g_register_fail_at = 0;
std::string g_last_response;
std::string g_last_status;

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

extern "C" esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* type) {
    (void)req;
    (void)type;
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_send(httpd_req_t* req, const char* buf, ssize_t len) {
    (void)req;
    g_last_response = (len == HTTPD_RESP_USE_STRLEN) ? (buf == nullptr ? "" : buf) : std::string(buf, static_cast<std::size_t>(len));
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

}  // namespace

#include "../../components/web_ui/web_handlers_rcp.cpp"

int main() {
    std::atomic<uint32_t> next_correlation_id{1U};
    web_ui::WebRouteContext route_ctx{};
    route_ctx.runtime = reinterpret_cast<service::ServiceRuntimeApi*>(0x1);
    route_ctx.next_correlation_id = &next_correlation_id;

    assert(!web_ui::register_rcp_routes(nullptr, &route_ctx));
    assert(!web_ui::register_rcp_routes(reinterpret_cast<void*>(1), nullptr));

    g_register_call_count = 0;
    g_register_fail_at = 0;
    assert(web_ui::register_rcp_routes(reinterpret_cast<void*>(1), &route_ctx));
    assert(g_register_call_count == 3);

    for (int fail_at = 1; fail_at <= 3; ++fail_at) {
        g_register_call_count = 0;
        g_register_fail_at = fail_at;
        assert(!web_ui::register_rcp_routes(reinterpret_cast<void*>(1), &route_ctx));
        assert(g_register_call_count == fail_at);
    }

    httpd_req_t bad_req{};
    bad_req.user_ctx = nullptr;
    assert(web_ui::rcp_get_handler(nullptr) == ESP_FAIL);
    assert(web_ui::rcp_get_handler(&bad_req) == ESP_FAIL);
    assert(web_ui::rcp_post_handler(nullptr) == ESP_FAIL);
    assert(web_ui::rcp_post_handler(&bad_req) == ESP_FAIL);
    assert(web_ui::rcp_result_get_handler(nullptr) == ESP_FAIL);
    assert(web_ui::rcp_result_get_handler(&bad_req) == ESP_FAIL);

    return 0;
}
