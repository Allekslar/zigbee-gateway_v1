/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_handler_common.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace web_ui {

namespace {

const char* skip_json_whitespace(const char* value) noexcept {
    while (value != nullptr && *value != '\0' && std::isspace(static_cast<unsigned char>(*value)) != 0) {
        ++value;
    }
    return value;
}

bool locate_json_value(const char* body, const char* key, const char** value_out) noexcept {
    if (body == nullptr || key == nullptr || value_out == nullptr) {
        return false;
    }

    char key_token[64]{};
    const int token_len = std::snprintf(key_token, sizeof(key_token), "\"%s\"", key);
    if (token_len <= 0 || token_len >= static_cast<int>(sizeof(key_token))) {
        return false;
    }

    const char* key_pos = std::strstr(body, key_token);
    if (key_pos == nullptr) {
        return false;
    }

    const char* separator = std::strchr(key_pos + token_len, ':');
    if (separator == nullptr) {
        return false;
    }

    const char* value = skip_json_whitespace(separator + 1);
    if (value == nullptr || *value == '\0') {
        return false;
    }

    *value_out = value;
    return true;
}

}  // namespace

bool read_request_body(httpd_req_t* req, char* body, std::size_t body_capacity) noexcept {
    if (req == nullptr || body == nullptr || body_capacity == 0) {
        return false;
    }

    if (req->content_len <= 0 || static_cast<std::size_t>(req->content_len) >= body_capacity) {
        return false;
    }

    int received_total = 0;
    while (received_total < req->content_len) {
        const int received = httpd_req_recv(req, body + received_total, req->content_len - received_total);
        if (received <= 0) {
            return false;
        }
        received_total += received;
    }

    body[received_total] = '\0';
    return true;
}

bool find_json_u32_field(const char* body, const char* key, uint32_t* out) noexcept {
    if (out == nullptr) {
        return false;
    }

    const char* value = nullptr;
    if (!locate_json_value(body, key, &value)) {
        return false;
    }

    bool quoted = false;
    if (*value == '"') {
        quoted = true;
        ++value;
    }

    char* parse_end = nullptr;
    const unsigned long parsed = std::strtoul(value, &parse_end, 0);
    if (parse_end == value || parse_end == nullptr || parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    if (quoted && *parse_end != '"') {
        return false;
    }

    *out = static_cast<uint32_t>(parsed);
    return true;
}

bool find_json_bool_field(const char* body, const char* key, bool* out) noexcept {
    if (out == nullptr) {
        return false;
    }

    const char* value = nullptr;
    if (!locate_json_value(body, key, &value)) {
        return false;
    }

    if (std::strncmp(value, "true", 4) == 0) {
        *out = true;
        return true;
    }

    if (std::strncmp(value, "false", 5) == 0) {
        *out = false;
        return true;
    }

    return false;
}

bool find_json_string_field(const char* body, const char* key, char* out, std::size_t out_capacity) noexcept {
    if (out == nullptr || out_capacity == 0) {
        return false;
    }

    const char* value = nullptr;
    if (!locate_json_value(body, key, &value)) {
        return false;
    }

    if (*value != '"') {
        return false;
    }
    ++value;

    const char* end_quote = std::strchr(value, '"');
    if (end_quote == nullptr) {
        return false;
    }

    const std::size_t value_len = static_cast<std::size_t>(end_quote - value);
    if (value_len + 1 > out_capacity) {
        return false;
    }

    std::memcpy(out, value, value_len);
    out[value_len] = '\0';
    return true;
}

#ifndef ESP_PLATFORM
// Host-only mocks of the real esp_http_server cookie/header readers,
// shared by every host test that (transitively, via authorize_v1_request()
// in web_v1_common.cpp) needs them -- defined once here, unlike the
// per-test-file httpd_register_uri_handler()/httpd_resp_send() mocks,
// because there is exactly one sensible behavior for these two (read the
// simulated field straight off httpd_req_t), not a test-specific one.
extern "C" esp_err_t httpd_req_get_cookie_val(
    const httpd_req_t* req, const char* cookie_name, char* val, size_t* val_size) {
    if (req == nullptr || cookie_name == nullptr || val == nullptr || val_size == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->mock_cookie_header == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    const std::size_t name_len = std::strlen(cookie_name);
    const char* cursor = req->mock_cookie_header;
    while (cursor != nullptr && *cursor != '\0') {
        while (*cursor == ' ') {
            ++cursor;
        }
        if (std::strncmp(cursor, cookie_name, name_len) == 0 && cursor[name_len] == '=') {
            const char* value_start = cursor + name_len + 1;
            const char* value_end = std::strchr(value_start, ';');
            const std::size_t value_len =
                value_end != nullptr ? static_cast<std::size_t>(value_end - value_start) : std::strlen(value_start);
            if (value_len + 1 > *val_size) {
                return ESP_ERR_HTTPD_RESULT_TRUNC;
            }
            std::memcpy(val, value_start, value_len);
            val[value_len] = '\0';
            *val_size = value_len;
            return ESP_OK;
        }
        cursor = std::strchr(cursor, ';');
        if (cursor != nullptr) {
            ++cursor;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

extern "C" esp_err_t httpd_req_get_hdr_value_str(const httpd_req_t* r, const char* field, char* val, size_t val_size) {
    if (r == nullptr || field == nullptr || val == nullptr || val_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* source = nullptr;
    if (std::strcmp(field, "X-CSRF-Token") == 0) {
        source = r->mock_csrf_header;
    } else if (std::strcmp(field, "Origin") == 0) {
        source = r->mock_origin_header;
    }
    if (source == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    const std::size_t source_len = std::strlen(source);
    if (source_len + 1 > val_size) {
        return ESP_ERR_HTTPD_RESULT_TRUNC;
    }
    std::memcpy(val, source, source_len);
    val[source_len] = '\0';
    return ESP_OK;
}
#endif  // !ESP_PLATFORM

uint32_t allocate_correlation_id(WebRouteContext* context) noexcept {
    if (context == nullptr || context->next_correlation_id == nullptr) {
        return 1U;
    }

    uint32_t candidate = context->next_correlation_id->fetch_add(1U, std::memory_order_relaxed);
    if (candidate != 0U) {
        return candidate;
    }

    candidate = context->next_correlation_id->fetch_add(1U, std::memory_order_relaxed);
    if (candidate == 0U) {
        return 1U;
    }

    return candidate;
}

esp_err_t send_json_error(httpd_req_t* req, const char* status, const char* message) noexcept {
    if (req == nullptr || status == nullptr || message == nullptr) {
        return ESP_FAIL;
    }

    char payload[160]{};
    const int length = std::snprintf(payload, sizeof(payload), "{\"error\":\"%s\"}", message);
    if (length <= 0 || length >= static_cast<int>(sizeof(payload))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_status(req, status);
    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

}  // namespace web_ui
