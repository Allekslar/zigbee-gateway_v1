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
#define HTTPD_RESP_USE_STRLEN -1

typedef enum {
    HTTP_GET = 1,
    HTTP_POST = 2,
} httpd_method_t;

typedef struct {
    void* user_ctx;
    int content_len;
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
