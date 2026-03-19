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
#include "esp_log.h"
#endif

#include "log_tags.h"
#include "service_runtime_api.hpp"
#include "web_handler_common.hpp"

namespace web_ui {

namespace {

constexpr const char* kTag = LOG_TAG_WEB_NETWORK;

const char* ota_stage_token(service::OtaStage stage) noexcept {
    switch (stage) {
        case service::OtaStage::kQueued:
            return "queued";
        case service::OtaStage::kDownloading:
            return "downloading";
        case service::OtaStage::kSwitchPending:
            return "switch_pending";
        case service::OtaStage::kRebootPending:
            return "reboot_pending";
        case service::OtaStage::kFailed:
            return "failed";
        case service::OtaStage::kIdle:
        default:
            return "idle";
    }
}

const char* ota_status_token(service::OtaOperationStatus status) noexcept {
    switch (status) {
        case service::OtaOperationStatus::kOk:
            return "ok";
        case service::OtaOperationStatus::kInvalidArgument:
            return "invalid_argument";
        case service::OtaOperationStatus::kNoCapacity:
            return "no_capacity";
        case service::OtaOperationStatus::kDownloadFailed:
            return "download_failed";
        case service::OtaOperationStatus::kVerifyFailed:
            return "verify_failed";
        case service::OtaOperationStatus::kApplyFailed:
            return "apply_failed";
        case service::OtaOperationStatus::kManifestInvalid:
            return "manifest_invalid";
        case service::OtaOperationStatus::kProjectMismatch:
            return "project_mismatch";
        case service::OtaOperationStatus::kBoardMismatch:
            return "board_mismatch";
        case service::OtaOperationStatus::kChipTargetMismatch:
            return "chip_target_mismatch";
        case service::OtaOperationStatus::kSchemaMismatch:
            return "schema_mismatch";
        case service::OtaOperationStatus::kDowngradeRejected:
            return "downgrade_rejected";
        case service::OtaOperationStatus::kInternalError:
        default:
            return "internal_error";
    }
}

const char* ota_poll_status_token(service::OtaPollStatus status) noexcept {
    switch (status) {
        case service::OtaPollStatus::kQueued:
            return "queued";
        case service::OtaPollStatus::kDownloading:
            return "downloading";
        case service::OtaPollStatus::kReady:
            return "ready";
        case service::OtaPollStatus::kNotReady:
        default:
            return "not_ready";
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

const char* ota_submit_error_message(service::OtaSubmitStatus status) noexcept {
    switch (status) {
        case service::OtaSubmitStatus::kBusy:
            return "ota_queue_full";
        case service::OtaSubmitStatus::kInvalidRequest:
            return "invalid_ota_request";
        case service::OtaSubmitStatus::kInvalidManifest:
            return "invalid_ota_manifest";
        case service::OtaSubmitStatus::kProjectMismatch:
            return "project_mismatch";
        case service::OtaSubmitStatus::kBoardMismatch:
            return "board_mismatch";
        case service::OtaSubmitStatus::kChipTargetMismatch:
            return "chip_target_mismatch";
        case service::OtaSubmitStatus::kSchemaMismatch:
            return "schema_mismatch";
        case service::OtaSubmitStatus::kDowngradeRejected:
            return "downgrade_rejected";
        case service::OtaSubmitStatus::kAccepted:
        default:
            return "ota_error";
    }
}

const char* ota_submit_http_status(service::OtaSubmitStatus status) noexcept {
    switch (status) {
        case service::OtaSubmitStatus::kBusy:
            return "503 Service Unavailable";
        case service::OtaSubmitStatus::kProjectMismatch:
        case service::OtaSubmitStatus::kBoardMismatch:
        case service::OtaSubmitStatus::kChipTargetMismatch:
        case service::OtaSubmitStatus::kSchemaMismatch:
        case service::OtaSubmitStatus::kDowngradeRejected:
            return "409 Conflict";
        case service::OtaSubmitStatus::kInvalidRequest:
        case service::OtaSubmitStatus::kInvalidManifest:
        case service::OtaSubmitStatus::kAccepted:
        default:
            return "400 Bad Request";
    }
}

esp_err_t ota_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    service::OtaApiSnapshot snapshot{};
    if (!context->runtime->build_ota_api_snapshot(&snapshot)) {
        return send_json_error(req, "500 Internal Server Error", "snapshot_unavailable");
    }

    char current_version[service::OtaApiSnapshot::kVersionMaxLen * 2U]{};
    char target_version[service::OtaApiSnapshot::kVersionMaxLen * 2U]{};
    if (!escape_json_string(snapshot.current_version.data(), current_version, sizeof(current_version)) ||
        !escape_json_string(snapshot.target_version.data(), target_version, sizeof(target_version))) {
        return ESP_FAIL;
    }

    unsigned long progress_percent = 0UL;
    if (snapshot.image_size_known && snapshot.image_size != 0U) {
        const uint32_t percent = static_cast<uint32_t>(
            (static_cast<uint64_t>(snapshot.downloaded_bytes) * 100ULL) / snapshot.image_size);
        progress_percent = static_cast<unsigned long>(percent > 100U ? 100U : percent);
    }

    char response[512]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"active_request_id\":%" PRIu32 ",\"busy\":%s,\"stage\":\"%s\",\"last_error\":\"%s\","
        "\"downloaded_bytes\":%" PRIu32 ",\"image_size\":%" PRIu32 ",\"image_size_known\":%s,"
        "\"progress_percent\":%lu,\"current_version\":\"%s\",\"target_version\":\"%s\"}",
        snapshot.active_request_id,
        snapshot.busy ? "true" : "false",
        ota_stage_token(snapshot.stage),
        ota_status_token(snapshot.last_error),
        snapshot.downloaded_bytes,
        snapshot.image_size,
        snapshot.image_size_known ? "true" : "false",
        progress_percent,
        current_version,
        target_version);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t ota_post_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    char body[kMaxRequestBodyBytes]{};
    if (!read_request_body(req, body, sizeof(body))) {
        return send_json_error(req, "400 Bad Request", "invalid_body");
    }

    service::OtaStartRequest request{};
    if (!find_json_string_field(body, "url", request.manifest.url.data(), request.manifest.url.size()) ||
        request.manifest.url[0] == '\0') {
        return send_json_error(req, "400 Bad Request", "invalid_url");
    }
    (void)find_json_string_field(body, "target_version", request.manifest.version.data(), request.manifest.version.size());
    (void)find_json_string_field(body, "project", request.manifest.project.data(), request.manifest.project.size());
    (void)find_json_string_field(body, "board", request.manifest.board.data(), request.manifest.board.size());
    (void)find_json_string_field(body, "chip_target", request.manifest.chip_target.data(), request.manifest.chip_target.size());
    (void)find_json_string_field(body, "sha256", request.manifest.sha256.data(), request.manifest.sha256.size());
    (void)find_json_u32_field(body, "min_schema", &request.manifest.min_schema);
    (void)find_json_bool_field(body, "allow_downgrade", &request.manifest.allow_downgrade);

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    request.request_id = context->runtime->next_operation_request_id();
    const service::OtaSubmitStatus submit_status = context->runtime->post_ota_start(request);
    if (submit_status != service::OtaSubmitStatus::kAccepted) {
        return send_json_error(req, ota_submit_http_status(submit_status), ota_submit_error_message(submit_status));
    }

    char response[160]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"accepted\":true,\"request_id\":%" PRIu32 ",\"operation\":\"ota\"}",
        request.request_id);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_status(req, "202 Accepted");
    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t ota_result_get_handler(httpd_req_t* req) {
    if (req == nullptr || req->user_ctx == nullptr) {
        return ESP_FAIL;
    }

    uint32_t request_id = 0U;
    if (!parse_query_request_id(req, &request_id)) {
        return send_json_error(req, "400 Bad Request", "invalid_request_id");
    }

    auto* context = static_cast<WebRouteContext*>(req->user_ctx);
    service::OtaResult result{};
    if (!context->runtime->take_ota_result(request_id, &result)) {
        const service::OtaPollStatus poll_status = context->runtime->get_ota_poll_status(request_id);
        char response[96]{};
        const int written = std::snprintf(
            response,
            sizeof(response),
            "{\"ready\":false,\"status\":\"%s\"}",
            ota_poll_status_token(poll_status));
        if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
            return ESP_FAIL;
        }
        (void)httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    char target_version[service::OtaResult::kVersionMaxLen * 2U]{};
    if (!escape_json_string(result.target_version.data(), target_version, sizeof(target_version))) {
        return ESP_FAIL;
    }

    char response[320]{};
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"ready\":true,\"ok\":%s,\"request_id\":%" PRIu32 ",\"status\":%u,\"error\":\"%s\","
        "\"reboot_required\":%s,\"downloaded_bytes\":%" PRIu32 ",\"image_size\":%" PRIu32 ","
        "\"image_size_known\":%s,\"target_version\":\"%s\"}",
        result.status == service::OtaOperationStatus::kOk ? "true" : "false",
        result.request_id,
        static_cast<unsigned>(result.status),
        ota_status_token(result.status),
        result.reboot_required ? "true" : "false",
        result.downloaded_bytes,
        result.image_size,
        result.image_size_known ? "true" : "false",
        target_version);
    if (written <= 0 || written >= static_cast<int>(sizeof(response))) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

}  // namespace

bool register_ota_routes(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t ota_get_uri{};
    ota_get_uri.uri = "/api/ota";
    ota_get_uri.method = HTTP_GET;
    ota_get_uri.handler = ota_get_handler;
    ota_get_uri.user_ctx = context;

    httpd_uri_t ota_post_uri{};
    ota_post_uri.uri = "/api/ota";
    ota_post_uri.method = HTTP_POST;
    ota_post_uri.handler = ota_post_handler;
    ota_post_uri.user_ctx = context;

    httpd_uri_t ota_result_uri{};
    ota_result_uri.uri = "/api/ota/result";
    ota_result_uri.method = HTTP_GET;
    ota_result_uri.handler = ota_result_get_handler;
    ota_result_uri.user_ctx = context;

    const httpd_handle_t handle = static_cast<httpd_handle_t>(server_handle);
    if (httpd_register_uri_handler(handle, &ota_get_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &ota_post_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &ota_result_uri) != ESP_OK) {
        return false;
    }

    return true;
}

}  // namespace web_ui
