/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_routes.hpp"

#include <cstddef>
#include <inttypes.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "core_commands.hpp"
#include "core_errors.hpp"
#include "core_events.hpp"
#include "core_state.hpp"
#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#endif
#include "log_tags.h"
#include "service_runtime_api.hpp"
#include "web_handler_common.hpp"

namespace web_ui {

namespace {

constexpr const char* kTag = LOG_TAG_WEB_NETWORK;

const char* operation_to_string(service::NetworkOperationType operation) {
    using Op = service::NetworkOperationType;
    switch (operation) {
        case Op::kScan:
            return "scan";
        case Op::kConnect:
            return "connect";
        case Op::kCredentialsStatus:
            return "credentials_status";
        case Op::kOpenJoinWindow:
            return "open_join_window";
        case Op::kRemoveDevice:
            return "remove_device";
        case Op::kUnknown:
        default:
            return "unknown";
    }
}

const char* operation_error_token(
    service::NetworkOperationType operation,
    service::NetworkOperationStatus status) {
    using Op = service::NetworkOperationType;
    using Status = service::NetworkOperationStatus;
    if (status == Status::kInvalidArgument) {
        return "invalid_argument";
    }
    if (status == Status::kNoCapacity) {
        return "no_capacity";
    }
    if (status != Status::kHalFailed) {
        return "failed";
    }

    switch (operation) {
        case Op::kScan:
            return "scan_failed";
        case Op::kConnect:
            return "connect_failed";
        case Op::kCredentialsStatus:
            return "credentials_failed";
        case Op::kOpenJoinWindow:
            return "join_failed";
        case Op::kRemoveDevice:
            return "remove_failed";
        case Op::kUnknown:
        default:
            return "failed";
    }
}

bool escape_json_string(const char* input, char* output, std::size_t output_capacity);

bool parse_query_request_id(httpd_req_t* req, uint32_t* request_id_out) {
    if (req == nullptr || request_id_out == nullptr) {
        return false;
    }

    char query[64]{};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }

    char value[24]{};
    if (httpd_query_key_value(query, "request_id", value, sizeof(value)) != ESP_OK) {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || end == nullptr || *end != '\0' || parsed == 0UL ||
        parsed > static_cast<unsigned long>(std::numeric_limits<uint32_t>::max())) {
        return false;
    }

    *request_id_out = static_cast<uint32_t>(parsed);
    return true;
}

esp_err_t send_async_accept(httpd_req_t* req, uint32_t request_id, const char* operation) {
    if (req == nullptr || request_id == 0 || operation == nullptr) {
        return ESP_FAIL;
    }

    char response[160]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"accepted\":true,\"request_id\":%" PRIu32 ",\"operation\":\"%s\"}",
        request_id,
        operation);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_status(req, "202 Accepted");
    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_network_result(httpd_req_t* req, const service::NetworkResult& result) {
    if (req == nullptr) {
        return ESP_FAIL;
    }

    const char* operation = operation_to_string(result.operation);
    if (result.status != service::NetworkOperationStatus::kOk) {
        char response[224]{};
        const int written = std::snprintf(
            response,
            sizeof(response),
            "{\"ready\":true,\"ok\":false,\"operation\":\"%s\",\"status\":%u,\"error\":\"%s\"}",
            operation,
            static_cast<unsigned>(result.status),
            operation_error_token(result.operation, result.status));
        if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
            return ESP_FAIL;
        }

        (void)httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    if (result.operation == service::NetworkOperationType::kScan) {
        const std::size_t found_count = result.scan_count;
        (void)httpd_resp_set_type(req, "application/json");

        char chunk[224]{};
        int written = std::snprintf(
            chunk,
            sizeof(chunk),
            "{\"ready\":true,\"ok\":true,\"operation\":\"scan\",\"count\":%u,\"networks\":[",
            static_cast<unsigned>(found_count));
        if (written <= 0 || written >= static_cast<int>(sizeof(chunk))) {
            return ESP_FAIL;
        }
        if (httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            return ESP_FAIL;
        }

        for (std::size_t i = 0; i < found_count; ++i) {
            char escaped_ssid[96]{};
            if (!escape_json_string(result.scan_records[i].ssid, escaped_ssid, sizeof(escaped_ssid))) {
                return ESP_FAIL;
            }

            written = std::snprintf(
                chunk,
                sizeof(chunk),
                "%s{\"ssid\":\"%s\",\"rssi\":%d,\"is_open\":%s}",
                i == 0 ? "" : ",",
                escaped_ssid,
                static_cast<int>(result.scan_records[i].rssi),
                result.scan_records[i].is_open ? "true" : "false");
            if (written <= 0 || written >= static_cast<int>(sizeof(chunk))) {
                return ESP_FAIL;
            }
            if (httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
                return ESP_FAIL;
            }
        }

        if (httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            return ESP_FAIL;
        }
        return httpd_resp_send_chunk(req, nullptr, 0);
    }

    if (result.operation == service::NetworkOperationType::kConnect) {
        char escaped_ssid[96]{};
        if (!escape_json_string(result.ssid, escaped_ssid, sizeof(escaped_ssid))) {
            return ESP_FAIL;
        }

        char response[224]{};
        const int written = std::snprintf(
            response,
            sizeof(response),
            "{\"ready\":true,\"ok\":true,\"operation\":\"connect\",\"saved\":%s,\"ssid\":\"%s\"}",
            result.saved ? "true" : "false",
            escaped_ssid);
        if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
            return ESP_FAIL;
        }
        (void)httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    if (result.operation == service::NetworkOperationType::kCredentialsStatus) {
        char escaped_ssid[96]{};
        if (result.saved && !escape_json_string(result.ssid, escaped_ssid, sizeof(escaped_ssid))) {
            return ESP_FAIL;
        }

        char response[256]{};
        int written = 0;
        if (result.saved) {
            written = std::snprintf(
                response,
                sizeof(response),
                "{\"ready\":true,\"ok\":true,\"operation\":\"credentials_status\","
                "\"saved\":%s,\"has_password\":%s,\"ssid\":\"%s\"}",
                "true",
                result.has_password ? "true" : "false",
                escaped_ssid);
        } else {
            written = std::snprintf(
                response,
                sizeof(response),
                "{\"ready\":true,\"ok\":true,\"operation\":\"credentials_status\","
                "\"saved\":%s,\"has_password\":%s}",
                "false",
                result.has_password ? "true" : "false");
        }
        if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
            return ESP_FAIL;
        }
        (void)httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    if (result.operation == service::NetworkOperationType::kOpenJoinWindow) {
        char response[224]{};
        const int written = std::snprintf(
            response,
            sizeof(response),
            "{\"ready\":true,\"ok\":true,\"operation\":\"open_join_window\",\"duration_seconds\":%u}",
            static_cast<unsigned>(result.join_window_seconds));
        if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
            return ESP_FAIL;
        }
        (void)httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    if (result.operation == service::NetworkOperationType::kRemoveDevice) {
        char response[256]{};
        const int written = std::snprintf(
            response,
            sizeof(response),
            "{\"ready\":true,\"ok\":true,\"operation\":\"remove_device\","
            "\"short_addr\":%u,\"force_remove\":%s,\"force_remove_timeout_ms\":%lu}",
            static_cast<unsigned>(result.device_short_addr),
            result.force_remove ? "true" : "false",
            static_cast<unsigned long>(result.force_remove_timeout_ms));
        if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
            return ESP_FAIL;
        }
        (void)httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    return send_json_error(req, "400 Bad Request", "invalid_operation");
}

bool escape_json_string(const char* input, char* output, std::size_t output_capacity) {
    if (input == nullptr || output == nullptr || output_capacity == 0) {
        return false;
    }

    std::size_t out_index = 0;
    auto append_escaped_hex = [&](unsigned char value) -> bool {
        static const char kHex[] = "0123456789ABCDEF";
        if (out_index + 6 >= output_capacity) {
            return false;
        }
        output[out_index++] = '\\';
        output[out_index++] = 'u';
        output[out_index++] = '0';
        output[out_index++] = '0';
        output[out_index++] = kHex[(value >> 4U) & 0x0FU];
        output[out_index++] = kHex[value & 0x0FU];
        return true;
    };

    for (std::size_t i = 0; input[i] != '\0'; ++i) {
        const unsigned char byte = static_cast<unsigned char>(input[i]);
        const char ch = static_cast<char>(byte);
        if (ch == '"' || ch == '\\') {
            if (out_index + 2 >= output_capacity) {
                return false;
            }
            output[out_index++] = '\\';
            output[out_index++] = ch;
            continue;
        }

        if (ch == '\b' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t') {
            if (out_index + 2 >= output_capacity) {
                return false;
            }
            output[out_index++] = '\\';
            output[out_index++] = ch == '\b' ? 'b' : ch == '\f' ? 'f' : ch == '\n' ? 'n' : ch == '\r' ? 'r' : 't';
            continue;
        }

        if (byte < 0x20U || byte >= 0x7FU) {
            if (!append_escaped_hex(byte)) {
                return false;
            }
            continue;
        }

        if (out_index + 1 >= output_capacity) {
            return false;
        }
        output[out_index++] = ch;
    }

    output[out_index] = '\0';
    return true;
}

esp_err_t network_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    service::NetworkApiSnapshot snapshot{};
    if (!context->runtime->build_network_api_snapshot(&snapshot)) {
        return send_json_error(req, "500 Internal Server Error", "snapshot_unavailable");
    }

    char response[192]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"revision\":%" PRIu32 ",\"connected\":%s,\"refresh_requests\":%" PRIu32 ",\"current_backoff_ms\":%" PRIu32 "}",
        snapshot.revision,
        snapshot.connected ? "true" : "false",
        snapshot.refresh_requests,
        snapshot.current_backoff_ms);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t network_refresh_post_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);

    core::CoreCommand command{};
    command.type = core::CoreCommandType::kRefreshNetwork;
    command.correlation_id = allocate_correlation_id(context);
    command.issued_at_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    const core::CoreError submit_result = context->runtime->post_command(command);
    if (submit_result != core::CoreError::kOk) {
        if (submit_result == core::CoreError::kInvalidArgument) {
            return send_json_error(req, "400 Bad Request", "invalid_command");
        }
        return send_json_error(req, "503 Service Unavailable", "command_rejected");
    }

    char response[128]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"accepted\":true,\"correlation_id\":%" PRIu32 "}",
        command.correlation_id);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t network_scan_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    ESP_LOGI(kTag, "HTTP GET /api/network/scan");

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    const uint32_t request_id = allocate_correlation_id(context);
    if (!context->runtime->post_network_scan(request_id)) {
        return send_json_error(req, "503 Service Unavailable", "scan_queue_full");
    }
    return send_async_accept(req, request_id, "scan");
}

esp_err_t network_credentials_status_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    ESP_LOGI(kTag, "HTTP GET /api/network/credentials/status");

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    const uint32_t request_id = allocate_correlation_id(context);
    if (!context->runtime->post_network_credentials_status(request_id)) {
        return send_json_error(req, "503 Service Unavailable", "credentials_queue_full");
    }
    return send_async_accept(req, request_id, "credentials_status");
}

esp_err_t network_connect_post_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_json_error(req, "400 Bad Request", "invalid_body");
    }

    char ssid[33]{};
    if (!find_json_string_field(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        return send_json_error(req, "400 Bad Request", "invalid_ssid");
    }

    char password[65]{};
    (void)find_json_string_field(body, "password", password, sizeof(password));

    bool save_credentials = true;
    (void)find_json_bool_field(body, "save_credentials", &save_credentials);

    ESP_LOGI(
        kTag,
        "HTTP POST /api/network/connect ssid='%s' save_credentials=%s",
        ssid,
        save_credentials ? "true" : "false");

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    const uint32_t request_id = allocate_correlation_id(context);
    if (!context->runtime->post_network_connect(request_id, ssid, password, save_credentials)) {
        return send_json_error(req, "503 Service Unavailable", "connect_queue_full");
    }
    return send_async_accept(req, request_id, "connect");
}

esp_err_t network_result_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    uint32_t request_id = 0;
    if (!parse_query_request_id(req, &request_id)) {
        return send_json_error(req, "400 Bad Request", "invalid_request_id");
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    service::NetworkResult result{};
    if (!context->runtime->take_network_result(request_id, &result)) {
        (void)httpd_resp_set_type(req, "application/json");
        if (context->runtime->is_scan_request_queued(request_id)) {
            ESP_LOGI(kTag, "HTTP GET /api/network/result request_id=%lu status=scan_queued", static_cast<unsigned long>(request_id));
            return httpd_resp_send(
                req,
                "{\"ready\":false,\"status\":\"scan_queued\"}",
                HTTPD_RESP_USE_STRLEN);
        }
        if (context->runtime->is_scan_request_in_progress(request_id)) {
            ESP_LOGI(
                kTag,
                "HTTP GET /api/network/result request_id=%lu status=scan_in_progress",
                static_cast<unsigned long>(request_id));
            return httpd_resp_send(
                req,
                "{\"ready\":false,\"status\":\"scan_in_progress\"}",
                HTTPD_RESP_USE_STRLEN);
        }
        ESP_LOGI(kTag, "HTTP GET /api/network/result request_id=%lu status=not_ready", static_cast<unsigned long>(request_id));
        return httpd_resp_send(req, "{\"ready\":false}", HTTPD_RESP_USE_STRLEN);
    }

    ESP_LOGI(
        kTag,
        "HTTP GET /api/network/result request_id=%lu ready=true operation=%s status=%u",
        static_cast<unsigned long>(request_id),
        operation_to_string(result.operation),
        static_cast<unsigned>(result.status));
    return send_network_result(req, result);
}

}  // namespace

bool register_network_routes(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t network_get_uri{};
    network_get_uri.uri = "/api/network";
    network_get_uri.method = HTTP_GET;
    network_get_uri.handler = network_get_handler;
    network_get_uri.user_ctx = context;

    httpd_uri_t refresh_post_uri{};
    refresh_post_uri.uri = "/api/network/refresh";
    refresh_post_uri.method = HTTP_POST;
    refresh_post_uri.handler = network_refresh_post_handler;
    refresh_post_uri.user_ctx = context;

    httpd_uri_t scan_get_uri{};
    scan_get_uri.uri = "/api/network/scan";
    scan_get_uri.method = HTTP_GET;
    scan_get_uri.handler = network_scan_get_handler;
    scan_get_uri.user_ctx = context;

    httpd_uri_t result_get_uri{};
    result_get_uri.uri = "/api/network/result";
    result_get_uri.method = HTTP_GET;
    result_get_uri.handler = network_result_get_handler;
    result_get_uri.user_ctx = context;

    httpd_uri_t credentials_status_get_uri{};
    credentials_status_get_uri.uri = "/api/network/credentials/status";
    credentials_status_get_uri.method = HTTP_GET;
    credentials_status_get_uri.handler = network_credentials_status_get_handler;
    credentials_status_get_uri.user_ctx = context;

    httpd_uri_t connect_post_uri{};
    connect_post_uri.uri = "/api/network/connect";
    connect_post_uri.method = HTTP_POST;
    connect_post_uri.handler = network_connect_post_handler;
    connect_post_uri.user_ctx = context;

    auto handle = static_cast<httpd_handle_t>(server_handle);
    if (httpd_register_uri_handler(handle, &network_get_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &refresh_post_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &scan_get_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &result_get_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &credentials_status_get_uri) != ESP_OK) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &connect_post_uri) != ESP_OK) {
        return false;
    }

    return true;
}

}  // namespace web_ui
