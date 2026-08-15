/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_v1_common.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "hal_time.h"
#include "session_security_policy.hpp"

namespace web_ui {

namespace {

bool is_lowercase_hex(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

}  // namespace

const char* api_v1_error_token(ApiV1ErrorCode code) noexcept {
    switch (code) {
        case ApiV1ErrorCode::kDeviceNotFound:
            return "device_not_found";
        case ApiV1ErrorCode::kIdentityUnresolved:
            return "identity_unresolved";
        case ApiV1ErrorCode::kOperationNotFound:
            return "operation_not_found";
        case ApiV1ErrorCode::kDeviceOffline:
            return "device_offline";
        case ApiV1ErrorCode::kStaleLocator:
            return "stale_locator";
        case ApiV1ErrorCode::kCapabilityUnavailable:
            return "capability_unavailable";
        case ApiV1ErrorCode::kNoCapacity:
            return "no_capacity";
        case ApiV1ErrorCode::kConflict:
            return "conflict";
        case ApiV1ErrorCode::kInvalidRequest:
            return "invalid_request";
        case ApiV1ErrorCode::kLegacyMutationDisabled:
            return "legacy_mutation_disabled";
        case ApiV1ErrorCode::kUnauthenticated:
            return "unauthenticated";
        case ApiV1ErrorCode::kCsrfOrOriginInvalid:
            return "csrf_or_origin_invalid";
        default:
            return "internal_error";
    }
}

// Golden HTTP status/error matrix (plan S4 HTTP #9): every v1 error token
// resolves to exactly one status here, and only here -- handlers must not
// pick their own status string for an ApiV1ErrorCode.
const char* api_v1_error_status(ApiV1ErrorCode code) noexcept {
    switch (code) {
        case ApiV1ErrorCode::kDeviceNotFound:
        case ApiV1ErrorCode::kIdentityUnresolved:
        case ApiV1ErrorCode::kOperationNotFound:
            return "404 Not Found";
        case ApiV1ErrorCode::kDeviceOffline:
        case ApiV1ErrorCode::kStaleLocator:
        case ApiV1ErrorCode::kConflict:
            return "409 Conflict";
        case ApiV1ErrorCode::kCapabilityUnavailable:
        case ApiV1ErrorCode::kNoCapacity:
            return "503 Service Unavailable";
        case ApiV1ErrorCode::kInvalidRequest:
            return "400 Bad Request";
        case ApiV1ErrorCode::kLegacyMutationDisabled:
            return "410 Gone";
        case ApiV1ErrorCode::kUnauthenticated:
            return "401 Unauthorized";
        case ApiV1ErrorCode::kCsrfOrOriginInvalid:
            return "403 Forbidden";
        default:
            return "503 Service Unavailable";
    }
}

bool extract_uri_device_id_hex(
    const char* uri, const char* prefix, const char* suffix, char* out_hex, std::size_t out_hex_capacity) noexcept {
    if (uri == nullptr || prefix == nullptr || suffix == nullptr || out_hex == nullptr ||
        out_hex_capacity < kApiV1DeviceIdHexLength + 1U) {
        return false;
    }

    const std::size_t uri_len = std::strlen(uri);
    const std::size_t prefix_len = std::strlen(prefix);
    const std::size_t suffix_len = std::strlen(suffix);
    if (uri_len != prefix_len + kApiV1DeviceIdHexLength + suffix_len) {
        return false;
    }
    if (std::strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }
    if (suffix_len > 0U && std::strcmp(uri + prefix_len + kApiV1DeviceIdHexLength, suffix) != 0) {
        return false;
    }

    const char* hex = uri + prefix_len;
    for (std::size_t i = 0; i < kApiV1DeviceIdHexLength; ++i) {
        if (!is_lowercase_hex(hex[i])) {
            return false;
        }
    }

    std::memcpy(out_hex, hex, kApiV1DeviceIdHexLength);
    out_hex[kApiV1DeviceIdHexLength] = '\0';
    return true;
}

bool extract_uri_device_id_and_reporting_segments(
    const char* uri,
    const char* prefix,
    char* out_hex,
    std::size_t out_hex_capacity,
    uint32_t* out_endpoint,
    uint32_t* out_cluster_id) noexcept {
    if (uri == nullptr || prefix == nullptr || out_hex == nullptr || out_hex_capacity < kApiV1DeviceIdHexLength + 1U ||
        out_endpoint == nullptr || out_cluster_id == nullptr) {
        return false;
    }

    const std::size_t prefix_len = std::strlen(prefix);
    const std::size_t uri_len = std::strlen(uri);
    if (uri_len < prefix_len + kApiV1DeviceIdHexLength) {
        return false;
    }
    if (std::strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }

    const char* hex = uri + prefix_len;
    for (std::size_t i = 0; i < kApiV1DeviceIdHexLength; ++i) {
        if (!is_lowercase_hex(hex[i])) {
            return false;
        }
    }

    constexpr const char* kReportingInfix = "/reporting/";
    const char* tail = uri + prefix_len + kApiV1DeviceIdHexLength;
    const std::size_t infix_len = std::strlen(kReportingInfix);
    if (std::strncmp(tail, kReportingInfix, infix_len) != 0) {
        return false;
    }

    const char* endpoint_start = tail + infix_len;
    char* endpoint_end = nullptr;
    const unsigned long endpoint_value = std::strtoul(endpoint_start, &endpoint_end, 10);
    if (endpoint_end == endpoint_start || endpoint_end == nullptr || *endpoint_end != '/' ||
        endpoint_value > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    const char* cluster_start = endpoint_end + 1;
    char* cluster_end = nullptr;
    const unsigned long cluster_value = std::strtoul(cluster_start, &cluster_end, 10);
    if (cluster_end == cluster_start || cluster_end == nullptr || *cluster_end != '\0' ||
        cluster_value > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    std::memcpy(out_hex, hex, kApiV1DeviceIdHexLength);
    out_hex[kApiV1DeviceIdHexLength] = '\0';
    *out_endpoint = static_cast<uint32_t>(endpoint_value);
    *out_cluster_id = static_cast<uint32_t>(cluster_value);
    return true;
}

bool extract_uri_decimal_segment(const char* uri, const char* prefix, uint32_t* out_value) noexcept {
    if (uri == nullptr || prefix == nullptr || out_value == nullptr) {
        return false;
    }

    const std::size_t prefix_len = std::strlen(prefix);
    if (std::strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }

    const char* digits = uri + prefix_len;
    if (digits[0] == '\0') {
        return false;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(digits, &end, 10);
    if (end == digits || *end != '\0' || value > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    *out_value = static_cast<uint32_t>(value);
    return true;
}

esp_err_t send_api_v1_error(httpd_req_t* req, ApiV1ErrorCode code) noexcept {
    if (req == nullptr) {
        return ESP_FAIL;
    }

    char payload[192]{};
    const int length = std::snprintf(
        payload,
        sizeof(payload),
        "{\"schema_version\":%u,\"error\":\"%s\"}",
        static_cast<unsigned>(kApiV1SchemaVersion),
        api_v1_error_token(code));
    if (length <= 0 || length >= static_cast<int>(sizeof(payload))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_status(req, api_v1_error_status(code));
    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_api_v1_accepted(httpd_req_t* req, uint32_t request_id) noexcept {
    if (req == nullptr || request_id == 0U) {
        return ESP_FAIL;
    }

    char payload[128]{};
    const int length = std::snprintf(
        payload,
        sizeof(payload),
        "{\"schema_version\":%u,\"accepted\":true,\"request_id\":%u}",
        static_cast<unsigned>(kApiV1SchemaVersion),
        static_cast<unsigned>(request_id));
    if (length <= 0 || length >= static_cast<int>(sizeof(payload))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_status(req, "202 Accepted");
    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_api_v1_ok(httpd_req_t* req) noexcept {
    if (req == nullptr) {
        return ESP_FAIL;
    }

    char payload[64]{};
    const int length = std::snprintf(
        payload, sizeof(payload), "{\"schema_version\":%u,\"accepted\":true}", static_cast<unsigned>(kApiV1SchemaVersion));
    if (length <= 0 || length >= static_cast<int>(sizeof(payload))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

// `req` cannot be `const httpd_req_t*` despite never being written through
// here: on ESP_PLATFORM this is passed straight to the real
// httpd_req_get_cookie_val()/httpd_req_get_hdr_value_str() (esp_http_
// server.h), which both declare a non-const `httpd_req_t*` parameter --
// narrowing this function's own parameter would not compile against the
// real target. The host-only mock versions of those two functions (see
// web_handler_common.hpp/.cpp) take `const httpd_req_t*` precisely
// because they have no such real-API constraint.
service::RouteAuthResult authorize_v1_request(
    // cppcheck-suppress constParameterPointer
    httpd_req_t* req, WebRouteContext* context, service::Capability /*required*/) noexcept {
    if (req == nullptr || context == nullptr) {
        return service::RouteAuthResult::kUnauthenticated;
    }

    char session_id_hex[service::kSessionIdHexChars + 1U]{};
    size_t session_id_capacity = sizeof(session_id_hex);
    const bool has_session =
        httpd_req_get_cookie_val(req, service::kSessionCookieName, session_id_hex, &session_id_capacity) == ESP_OK;
    const char* session_id = has_session ? session_id_hex : nullptr;

    const uint64_t now_ms = hal_time_now_ms();

    if (req->method == HTTP_GET) {
        return service::authorize_read_request(context->sessions, session_id, now_ms);
    }

    char csrf_token_hex[service::kCsrfTokenHexChars + 1U]{};
    const bool has_csrf =
        httpd_req_get_hdr_value_str(req, "X-CSRF-Token", csrf_token_hex, sizeof(csrf_token_hex)) == ESP_OK;

    char origin[96]{};
    const bool has_origin = httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK;

    return service::authorize_mutation_request(
        context->sessions, session_id, has_csrf ? csrf_token_hex : nullptr, has_origin ? origin : nullptr,
        context->expected_origin, now_ms);
}

esp_err_t send_v1_auth_error(httpd_req_t* req, service::RouteAuthResult result) noexcept {
    switch (result) {
        case service::RouteAuthResult::kCsrfOrOriginInvalid:
            return send_api_v1_error(req, ApiV1ErrorCode::kCsrfOrOriginInvalid);
        case service::RouteAuthResult::kUnauthenticated:
        case service::RouteAuthResult::kAllowed:
        default:
            return send_api_v1_error(req, ApiV1ErrorCode::kUnauthenticated);
    }
}

}  // namespace web_ui
