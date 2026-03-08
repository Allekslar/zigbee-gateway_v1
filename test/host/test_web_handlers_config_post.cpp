/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>
#include <string>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"
#include "web_handler_common.hpp"

std::string g_last_response;
std::string g_last_status;
std::string g_request_body;

extern "C" esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* type) {
    (void)req;
    (void)type;
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_send(httpd_req_t* req, const char* buf, ssize_t len) {
    (void)req;
    g_last_response = (len == HTTPD_RESP_USE_STRLEN) ? buf : std::string(buf, static_cast<std::size_t>(len));
    return ESP_OK;
}

extern "C" esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t* uri_handler) {
    (void)handle;
    (void)uri_handler;
    return ESP_OK;
}

extern "C" esp_err_t httpd_resp_set_status(httpd_req_t* req, const char* status) {
    (void)req;
    g_last_status = status == nullptr ? "" : status;
    return ESP_OK;
}

extern "C" int httpd_req_recv(httpd_req_t* req, char* buf, size_t len) {
    (void)req;
    if (g_request_body.empty()) {
        return 0;
    }

    const std::size_t to_copy = g_request_body.size() < len ? g_request_body.size() : len;
    std::memcpy(buf, g_request_body.data(), to_copy);
    g_request_body.erase(0, to_copy);
    return static_cast<int>(to_copy);
}

extern "C" esp_err_t httpd_resp_send_chunk(httpd_req_t* req, const char* buf, ssize_t len) {
    (void)req;
    (void)buf;
    (void)len;
    return ESP_OK;
}

void clear_http_capture() {
    g_last_response.clear();
    g_last_status.clear();
}

// Direct include for anonymous-namespace handlers.
#include "../../components/web_ui/web_handlers_config.cpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    std::atomic<uint32_t> next_id{1};
    web_ui::WebRouteContext route_ctx{};
    route_ctx.runtime = &runtime;
    route_ctx.next_correlation_id = &next_id;

    httpd_req_t req{};
    req.user_ctx = &route_ctx;

    clear_http_capture();
    req.content_len = 0;
    g_request_body.clear();
    assert(web_ui::config_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_body\"") != std::string::npos);

    clear_http_capture();
    g_request_body = "{}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::config_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"empty_update\"") != std::string::npos);

    clear_http_capture();
    g_request_body = "{\"command_timeout_ms\":0}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::config_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_command_timeout_ms\"") != std::string::npos);

    clear_http_capture();
    g_request_body = "{\"max_command_retries\":6}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::config_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_max_command_retries\"") != std::string::npos);

    clear_http_capture();
    g_request_body = "{\"command_timeout_ms\":7000,\"max_command_retries\":4}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::config_post_handler(&req) == ESP_OK);
    assert(g_last_status.empty());
    assert(g_last_response.find("\"accepted\":true") != std::string::npos);

    runtime.process_pending();
    const service::ConfigSnapshot snapshot = runtime.config_snapshot();
    assert(snapshot.command_timeout_ms == 7000U);
    assert(snapshot.max_command_retries == 4U);

    clear_http_capture();
    g_request_body = "{\"short_addr\":8705,\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":301,\"max_interval_seconds\":300}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::config_reporting_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_profile_bounds\"") != std::string::npos);

    clear_http_capture();
    g_request_body = "{\"short_addr\":8705,\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300,\"capability_flags\":512}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::config_reporting_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");
    assert(g_last_response.find("\"error\":\"invalid_capability_flags\"") != std::string::npos);

    clear_http_capture();
    g_request_body = "{\"short_addr\":8705,\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300,\"reportable_change\":25,\"capability_flags\":3}";
    req.content_len = static_cast<int>(g_request_body.size());
    assert(web_ui::config_reporting_post_handler(&req) == ESP_OK);
    assert(g_last_status.empty());
    assert(g_last_response.find("\"accepted\":true") != std::string::npos);

    service::ConfigManager::ReportingProfileKey key{};
    key.short_addr = 0x2201U;
    key.endpoint = 1U;
    key.cluster_id = 0x0402U;
    service::ConfigManager::ReportingProfile before_apply{};
    assert(!runtime.config_manager().get_reporting_profile(key, &before_apply));

    runtime.process_pending();
    service::ConfigManager::ReportingProfile after_apply{};
    assert(runtime.config_manager().get_reporting_profile(key, &after_apply));
    assert(after_apply.in_use);
    assert(after_apply.min_interval_seconds == 10U);
    assert(after_apply.max_interval_seconds == 300U);
    assert(after_apply.reportable_change == 25U);
    assert(after_apply.capability_flags == 3U);

    assert(web_ui::register_config_routes(reinterpret_cast<void*>(1), &route_ctx));

    clear_http_capture();
    assert(web_ui::config_get_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"command_timeout_ms\":7000") != std::string::npos);
    assert(g_last_response.find("\"max_command_retries\":4") != std::string::npos);

    return 0;
}
