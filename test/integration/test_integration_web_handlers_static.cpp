/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <string>

#include "web_handler_common.hpp"

extern "C" esp_err_t httpd_resp_set_hdr(httpd_req_t* req, const char* field, const char* value);

extern "C" {
asm(
    ".global _binary_index_html_gz_start\n"
    "_binary_index_html_gz_start:\n"
    ".ascii \"<html>ok</html>\"\n"
    ".global _binary_index_html_gz_end\n"
    "_binary_index_html_gz_end:\n"
    ".global _binary_style_css_gz_start\n"
    "_binary_style_css_gz_start:\n"
    ".ascii \"body{color:#000;}\"\n"
    ".global _binary_style_css_gz_end\n"
    "_binary_style_css_gz_end:\n"
    ".global _binary_app_js_gz_start\n"
    "_binary_app_js_gz_start:\n"
    ".ascii \"console.log('ok');\"\n"
    ".global _binary_app_js_gz_end\n"
    "_binary_app_js_gz_end:\n");
}

std::string g_last_type;
std::string g_last_response;
std::string g_last_cache_control;
std::string g_last_content_encoding;
std::string g_last_vary;
int g_set_hdr_calls = 0;
int g_register_call_count = 0;
int g_register_fail_at = 0;

extern "C" esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* type) {
    (void)req;
    g_last_type = type == nullptr ? "" : type;
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_set_hdr(httpd_req_t* req, const char* field, const char* value) {
    (void)req;
    const std::string field_value = field == nullptr ? "" : field;
    const std::string header_value = value == nullptr ? "" : value;
    if (field_value == "Cache-Control") {
        g_last_cache_control = header_value;
    } else if (field_value == "Content-Encoding") {
        g_last_content_encoding = header_value;
    } else if (field_value == "Vary") {
        g_last_vary = header_value;
    }
    ++g_set_hdr_calls;
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_send(httpd_req_t* req, const char* buf, ssize_t len) {
    (void)req;
    if (buf == nullptr || len == 0) {
        g_last_response.clear();
        return ESP_OK;
    }
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
    (void)status;
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
    if (buf == nullptr || len == 0) {
        return ESP_OK;
    }
    g_last_response.append(
        (len == HTTPD_RESP_USE_STRLEN) ? buf : std::string(buf, static_cast<std::size_t>(len)));
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
    g_last_type.clear();
    g_last_response.clear();
    g_last_cache_control.clear();
    g_last_content_encoding.clear();
    g_last_vary.clear();
    g_set_hdr_calls = 0;
}

#include "../../components/web_ui/web_handlers_static.cpp"

int main() {
    web_ui::WebRouteContext context{};
    std::atomic<uint32_t> next_correlation_id{1U};
    context.runtime = reinterpret_cast<service::ServiceRuntimeApi*>(0x1);
    context.next_correlation_id = &next_correlation_id;

    httpd_req_t req{};
    req.user_ctx = &context;

    clear_http_capture();
    assert(web_ui::root_get_handler(&req) == ESP_OK);
    assert(g_last_type == "text/html; charset=utf-8");
    assert(g_last_response.find("<html>ok</html>") != std::string::npos);
    assert(g_set_hdr_calls == 3);
    assert(g_last_cache_control == "no-store, max-age=0");
    assert(g_last_content_encoding == "gzip");
    assert(g_last_vary == "Accept-Encoding");

    clear_http_capture();
    assert(web_ui::style_css_get_handler(&req) == ESP_OK);
    assert(g_last_type == "text/css; charset=utf-8");
    assert(g_last_response.find("body{color:#000;}") != std::string::npos);
    assert(g_set_hdr_calls == 3);
    assert(g_last_cache_control == "public, max-age=31536000, immutable");
    assert(g_last_content_encoding == "gzip");
    assert(g_last_vary == "Accept-Encoding");

    clear_http_capture();
    assert(web_ui::app_js_get_handler(&req) == ESP_OK);
    assert(g_last_type == "application/javascript; charset=utf-8");
    assert(g_last_response.find("console.log('ok');") != std::string::npos);
    assert(g_set_hdr_calls == 3);
    assert(g_last_cache_control == "public, max-age=31536000, immutable");
    assert(g_last_content_encoding == "gzip");
    assert(g_last_vary == "Accept-Encoding");

    clear_http_capture();
    assert(web_ui::favicon_get_handler(&req) == ESP_OK);
    assert(g_last_type == "image/x-icon");
    assert(g_last_response.empty());
    assert(g_set_hdr_calls == 1);

    assert(!web_ui::register_static_routes(nullptr, &context));
    assert(!web_ui::register_static_routes(reinterpret_cast<void*>(1), nullptr));

    g_register_call_count = 0;
    g_register_fail_at = 0;
    assert(web_ui::register_static_routes(reinterpret_cast<void*>(1), &context));
    const int success_registration_count = g_register_call_count;
    assert(success_registration_count == 5);

    for (int fail_at = 1; fail_at <= success_registration_count; ++fail_at) {
        g_register_call_count = 0;
        g_register_fail_at = fail_at;
        assert(!web_ui::register_static_routes(reinterpret_cast<void*>(1), &context));
        assert(g_register_call_count == fail_at);
    }

    return 0;
}
