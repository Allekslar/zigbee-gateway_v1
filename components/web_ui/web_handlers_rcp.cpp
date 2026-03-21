/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_routes.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <limits>

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#endif

#include "service_runtime_api.hpp"
#include "web_handler_common.hpp"

namespace web_ui {

namespace {

constexpr std::size_t kMaxRcpRequestBodyBytes = 512U;

const char* rcp_stage_token(service::RcpUpdateStage stage) noexcept {
    switch (stage) {
        case service::RcpUpdateStage::kQueued:
            return "queued";
        case service::RcpUpdateStage::kApplying:
            return "applying";
        case service::RcpUpdateStage::kCompleted:
            return "completed";
        case service::RcpUpdateStage::kFailed:
            return "failed";
        case service::RcpUpdateStage::kIdle:
        default:
            return "idle";
    }
}

const char* rcp_status_token(service::RcpUpdateOperationStatus status) noexcept {
    switch (status) {
        case service::RcpUpdateOperationStatus::kOk:
            return "ok";
        case service::RcpUpdateOperationStatus::kInvalidArgument:
            return "invalid_argument";
        case service::RcpUpdateOperationStatus::kNoCapacity:
            return "no_capacity";
        case service::RcpUpdateOperationStatus::kConflict:
            return "conflict";
        case service::RcpUpdateOperationStatus::kBoardMismatch:
            return "board_mismatch";
        case service::RcpUpdateOperationStatus::kTransportMismatch:
            return "transport_mismatch";
        case service::RcpUpdateOperationStatus::kGatewayVersionMismatch:
            return "gateway_version_mismatch";
        case service::RcpUpdateOperationStatus::kTransportFailed:
            return "transport_failed";
        case service::RcpUpdateOperationStatus::kVerifyFailed:
            return "verify_failed";
        case service::RcpUpdateOperationStatus::kApplyFailed:
            return "apply_failed";
        case service::RcpUpdateOperationStatus::kProbeFailed:
            return "probe_failed";
        case service::RcpUpdateOperationStatus::kRecoveryFailed:
            return "recovery_failed";
        case service::RcpUpdateOperationStatus::kInternalError:
        default:
            return "internal_error";
    }
}

const char* rcp_poll_status_token(service::RcpUpdatePollStatus status) noexcept {
    switch (status) {
        case service::RcpUpdatePollStatus::kQueued:
            return "queued";
        case service::RcpUpdatePollStatus::kApplying:
            return "applying";
        case service::RcpUpdatePollStatus::kReady:
            return "ready";
        case service::RcpUpdatePollStatus::kNotReady:
        default:
            return "not_ready";
    }
}

const char* rcp_submit_error_message(service::RcpUpdateSubmitStatus status) noexcept {
    switch (status) {
        case service::RcpUpdateSubmitStatus::kBusy:
            return "rcp_update_busy";
        case service::RcpUpdateSubmitStatus::kConflict:
            return "ota_conflict";
        case service::RcpUpdateSubmitStatus::kBoardMismatch:
            return "board_mismatch";
        case service::RcpUpdateSubmitStatus::kTransportMismatch:
            return "transport_mismatch";
        case service::RcpUpdateSubmitStatus::kGatewayVersionMismatch:
            return "gateway_version_mismatch";
        case service::RcpUpdateSubmitStatus::kInvalidRequest:
        case service::RcpUpdateSubmitStatus::kAccepted:
        default:
            return "invalid_rcp_update_request";
    }
}

const char* rcp_submit_http_status(service::RcpUpdateSubmitStatus status) noexcept {
    switch (status) {
        case service::RcpUpdateSubmitStatus::kBusy:
            return "503 Service Unavailable";
        case service::RcpUpdateSubmitStatus::kConflict:
        case service::RcpUpdateSubmitStatus::kBoardMismatch:
        case service::RcpUpdateSubmitStatus::kTransportMismatch:
        case service::RcpUpdateSubmitStatus::kGatewayVersionMismatch:
            return "409 Conflict";
        case service::RcpUpdateSubmitStatus::kInvalidRequest:
        case service::RcpUpdateSubmitStatus::kAccepted:
        default:
            return "400 Bad Request";
    }
}

bool parse_query_request_id(httpd_req_t* req, uint32_t* request_id_out) noexcept {
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

bool escape_json_string(const char* input, char* output, std::size_t output_capacity) noexcept {
    if (input == nullptr || output == nullptr || output_capacity == 0U) {
        return false;
    }

    std::size_t out_index = 0U;
    for (std::size_t i = 0U; input[i] != '\0'; ++i) {
        const char ch = input[i];
        if (ch == '"' || ch == '\\') {
            if (out_index + 2U >= output_capacity) {
                return false;
            }
            output[out_index++] = '\\';
            output[out_index++] = ch;
            continue;
        }

        if (static_cast<unsigned char>(ch) < 0x20U) {
            return false;
        }

        if (out_index + 1U >= output_capacity) {
            return false;
        }
        output[out_index++] = ch;
    }

    output[out_index] = '\0';
    return true;
}

esp_err_t rcp_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    service::RcpUpdateApiSnapshot snapshot{};
    if (!context->runtime->build_rcp_update_api_snapshot(&snapshot)) {
        return send_json_error(req, "500 Internal Server Error", "snapshot_unavailable");
    }

    char current_version[service::RcpUpdateApiSnapshot::kVersionMaxLen * 2U]{};
    char target_version[service::RcpUpdateApiSnapshot::kVersionMaxLen * 2U]{};
    if (!escape_json_string(snapshot.current_version.data(), current_version, sizeof(current_version)) ||
        !escape_json_string(snapshot.target_version.data(), target_version, sizeof(target_version))) {
        return ESP_FAIL;
    }

    char response[384]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"active_request_id\":%" PRIu32 ",\"busy\":%s,\"stage\":\"%s\",\"last_error\":\"%s\","
        "\"written_bytes\":%" PRIu32 ",\"current_version\":\"%s\",\"target_version\":\"%s\"}",
        snapshot.active_request_id,
        snapshot.busy ? "true" : "false",
        rcp_stage_token(snapshot.stage),
        rcp_status_token(snapshot.last_error),
        snapshot.written_bytes,
        current_version,
        target_version);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t rcp_post_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    char body[kMaxRcpRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_json_error(req, "400 Bad Request", "invalid_body");
    }

    service::RcpUpdateRequest request{};
    if (!find_json_string_field(body, "url", request.url.data(), request.url.size()) || request.url[0] == '\0') {
        return send_json_error(req, "400 Bad Request", "invalid_url");
    }
    (void)find_json_string_field(body, "target_version", request.target_version.data(), request.target_version.size());
    (void)find_json_string_field(body, "sha256", request.sha256.data(), request.sha256.size());
    (void)find_json_string_field(body, "board", request.board.data(), request.board.size());
    (void)find_json_string_field(body, "transport", request.transport.data(), request.transport.size());
    (void)find_json_string_field(
        body, "gateway_min_version", request.gateway_min_version.data(), request.gateway_min_version.size());

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    request.request_id = context->runtime->next_operation_request_id();
    const service::RcpUpdateSubmitStatus submit_status = context->runtime->post_rcp_update_start(request);
    if (submit_status != service::RcpUpdateSubmitStatus::kAccepted) {
        return send_json_error(req, rcp_submit_http_status(submit_status), rcp_submit_error_message(submit_status));
    }

    char response[168]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"accepted\":true,\"request_id\":%" PRIu32 ",\"operation\":\"rcp_update\"}",
        request.request_id);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_status(req, "202 Accepted");
    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t rcp_result_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    uint32_t request_id = 0U;
    if (!parse_query_request_id(req, &request_id)) {
        return send_json_error(req, "400 Bad Request", "invalid_request_id");
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    service::RcpUpdateResult result{};
    if (!context->runtime->take_rcp_update_result(request_id, &result)) {
        const service::RcpUpdatePollStatus poll_status = context->runtime->get_rcp_update_poll_status(request_id);
        char response[96]{};
        const int written = std::snprintf(
            response,
            sizeof(response),
            "{\"ready\":false,\"status\":\"%s\"}",
            rcp_poll_status_token(poll_status));
        if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
            return ESP_FAIL;
        }
        (void)httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    char target_version[service::RcpUpdateResult::kVersionMaxLen * 2U]{};
    if (!escape_json_string(result.target_version.data(), target_version, sizeof(target_version))) {
        return ESP_FAIL;
    }

    char response[256]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"ready\":true,\"ok\":%s,\"request_id\":%" PRIu32 ",\"status\":%u,\"error\":\"%s\","
        "\"written_bytes\":%" PRIu32 ",\"target_version\":\"%s\"}",
        result.status == service::RcpUpdateOperationStatus::kOk ? "true" : "false",
        result.request_id,
        static_cast<unsigned>(result.status),
        rcp_status_token(result.status),
        result.written_bytes,
        target_version);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

}  // namespace

bool register_rcp_routes(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t get_uri{};
    get_uri.uri = "/api/rcp-update";
    get_uri.method = HTTP_GET;
    get_uri.handler = rcp_get_handler;
    get_uri.user_ctx = context;

    httpd_uri_t post_uri{};
    post_uri.uri = "/api/rcp-update";
    post_uri.method = HTTP_POST;
    post_uri.handler = rcp_post_handler;
    post_uri.user_ctx = context;

    httpd_uri_t result_uri{};
    result_uri.uri = "/api/rcp-update/result";
    result_uri.method = HTTP_GET;
    result_uri.handler = rcp_result_get_handler;
    result_uri.user_ctx = context;

    const httpd_handle_t handle = static_cast<httpd_handle_t>(server_handle);
    if (httpd_register_uri_handler(handle, &get_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &post_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &result_uri) != ESP_OK) {
        return false;
    }

    return true;
}

}  // namespace web_ui
