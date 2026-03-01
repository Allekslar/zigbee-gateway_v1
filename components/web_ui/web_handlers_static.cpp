/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_routes.hpp"

#include <cstddef>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#endif

namespace web_ui {

namespace {

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");

esp_err_t send_embedded_file(
    httpd_req_t* req,
    const char* content_type,
    const uint8_t* start,
    const uint8_t* end) noexcept {
    if (req == nullptr || content_type == nullptr || start == nullptr || end == nullptr || end < start) {
        return ESP_FAIL;
    }

    std::size_t file_size = static_cast<std::size_t>(end - start);
    if (file_size > 0U && start[file_size - 1U] == '\0') {
        --file_size;
    }
    (void)httpd_resp_set_type(req, content_type);
    (void)httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    (void)httpd_resp_set_hdr(req, "Pragma", "no-cache");
    (void)httpd_resp_set_hdr(req, "Expires", "0");
    return httpd_resp_send(req, reinterpret_cast<const char*>(start), static_cast<ssize_t>(file_size));
}

esp_err_t root_get_handler(httpd_req_t* req) {
    return send_embedded_file(req, "text/html; charset=utf-8", index_html_start, index_html_end);
}

esp_err_t index_html_get_handler(httpd_req_t* req) {
    return send_embedded_file(req, "text/html; charset=utf-8", index_html_start, index_html_end);
}

esp_err_t style_css_get_handler(httpd_req_t* req) {
    return send_embedded_file(req, "text/css; charset=utf-8", style_css_start, style_css_end);
}

esp_err_t app_js_get_handler(httpd_req_t* req) {
    return send_embedded_file(req, "application/javascript; charset=utf-8", app_js_start, app_js_end);
}

}  // namespace

bool register_static_routes(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t root_get_uri{};
    root_get_uri.uri = "/";
    root_get_uri.method = HTTP_GET;
    root_get_uri.handler = root_get_handler;
    root_get_uri.user_ctx = context;

    httpd_uri_t index_html_get_uri{};
    index_html_get_uri.uri = "/index.html";
    index_html_get_uri.method = HTTP_GET;
    index_html_get_uri.handler = index_html_get_handler;
    index_html_get_uri.user_ctx = context;

    httpd_uri_t style_css_get_uri{};
    style_css_get_uri.uri = "/style.css";
    style_css_get_uri.method = HTTP_GET;
    style_css_get_uri.handler = style_css_get_handler;
    style_css_get_uri.user_ctx = context;

    httpd_uri_t app_js_get_uri{};
    app_js_get_uri.uri = "/app.js";
    app_js_get_uri.method = HTTP_GET;
    app_js_get_uri.handler = app_js_get_handler;
    app_js_get_uri.user_ctx = context;

    auto handle = static_cast<httpd_handle_t>(server_handle);
    if (httpd_register_uri_handler(handle, &root_get_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &index_html_get_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &style_css_get_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &app_js_get_uri) != ESP_OK) {
        return false;
    }

    return true;
}

}  // namespace web_ui
