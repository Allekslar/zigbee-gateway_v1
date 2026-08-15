/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#else
#include <sys/types.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_HTTPD_RESULT_TRUNC 0x8004  // real ESP_ERR_HTTPD_BASE(0x8000)+4
#define HTTPD_RESP_USE_STRLEN -1

typedef enum {
    HTTP_GET = 1,
    HTTP_POST = 2,
    HTTP_PUT = 3,
    HTTP_PATCH = 4,
    HTTP_DELETE = 5,
} httpd_method_t;

typedef struct {
    void* user_ctx;
    int content_len;
    // Mirrors real esp_http_server's httpd_req_t::uri, which every existing
    // (pre-v1) handler never needed (identifiers came from JSON bodies or
    // MQTT topic strings instead). v1 path-parameter routes (device_id,
    // reporting endpoint/cluster_id) need it; tests set it directly.
    const char* uri;
    // Mirrors real esp_http_server's httpd_req_t::method -- plan S6
    // "Authorization and physical presence" #19's authorize_v1_request()
    // needs it to tell a read (GET) from a state-changing request. Real
    // handlers never read this field directly (same as `uri`).
    httpd_method_t method;
    // Test-only header/cookie simulation. Real esp_http_server has no such
    // fields -- production code (authorize_v1_request()) only ever reads
    // this indirectly, through httpd_req_get_cookie_val()/
    // httpd_req_get_hdr_value_str() below, exactly as it would the real
    // API. A null field behaves as "header/cookie absent", matching the
    // real API's ESP_ERR_NOT_FOUND for a missing field.
    const char* mock_cookie_header;   // raw "Cookie:" value, e.g. "zgw_session=<hex>"
    const char* mock_csrf_header;     // raw "X-CSRF-Token:" value
    const char* mock_origin_header;   // raw "Origin:" value
} httpd_req_t;

typedef esp_err_t (*httpd_handler_t)(httpd_req_t *r);

typedef struct {
    const char* uri;
    httpd_method_t method;
    httpd_handler_t handler;
    void* user_ctx;
} httpd_uri_t;

typedef void* httpd_handle_t;

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t len);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t len);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t len);
esp_err_t httpd_query_key_value(const char *qry, const char *key, char *val, size_t len);
esp_err_t httpd_req_get_cookie_val(const httpd_req_t *req, const char *cookie_name, char *val, size_t *val_size);
esp_err_t httpd_req_get_hdr_value_str(const httpd_req_t *r, const char *field, char *val, size_t val_size);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
#ifdef __cplusplus
}
#endif
#endif

#include "web_routes.hpp"

namespace web_ui {

constexpr std::size_t kMaxRequestBodyBytes = 256;

bool read_request_body(httpd_req_t* req, char* body, std::size_t body_capacity) noexcept;
bool find_json_u32_field(const char* body, const char* key, uint32_t* out) noexcept;
bool find_json_bool_field(const char* body, const char* key, bool* out) noexcept;
bool find_json_string_field(const char* body, const char* key, char* out, std::size_t out_capacity) noexcept;
uint32_t allocate_correlation_id(WebRouteContext* context) noexcept;
esp_err_t send_json_error(httpd_req_t* req, const char* status, const char* message) noexcept;

}  // namespace web_ui
