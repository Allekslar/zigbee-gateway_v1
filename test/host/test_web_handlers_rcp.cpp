/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"
#include "web_handler_common.hpp"

namespace {

std::string g_last_response;
std::string g_last_status;
std::string g_query_string;
std::string g_request_body;
int g_register_call_count = 0;

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
    ++g_register_call_count;
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
    const std::size_t to_copy = std::min(len, g_request_body.size());
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

extern "C" esp_err_t httpd_req_get_url_query_str(httpd_req_t* req, char* buf, size_t len) {
    (void)req;
    if (g_query_string.size() >= len) {
        return ESP_FAIL;
    }
    std::strncpy(buf, g_query_string.c_str(), len);
    return ESP_OK;
}

extern "C" esp_err_t httpd_query_key_value(const char* qry, const char* key, char* val, size_t len) {
    const std::string query(qry == nullptr ? "" : qry);
    const std::string token = std::string(key == nullptr ? "" : key) + "=";
    const std::size_t pos = query.find(token);
    if (pos == std::string::npos) {
        return ESP_FAIL;
    }
    const std::size_t start = pos + token.size();
    const std::size_t end = query.find('&', start);
    const std::string value = query.substr(start, end - start);
    if (value.size() >= len) {
        return ESP_FAIL;
    }
    std::strncpy(val, value.c_str(), len);
    return ESP_OK;
}

extern "C" int hal_rcp_stack_update_begin(void) {
    return 0;
}

extern "C" bool hal_rcp_stack_get_running_version(char* out, size_t out_len) {
    assert(out != nullptr);
    assert(out_len > 12U);
    std::strncpy(out, "rcp-1.0.0", out_len - 1U);
    out[out_len - 1U] = '\0';
    return true;
}

extern "C" int hal_rcp_stack_prepare_for_update(void) {
    return 0;
}

extern "C" int hal_rcp_stack_update_write(const uint8_t* data, uint32_t len) {
    assert(data != nullptr);
    assert(len != 0U);
    return 0;
}

extern "C" int hal_rcp_stack_update_end(void) {
    return 0;
}

extern "C" int hal_rcp_stack_recover_after_update(bool update_applied) {
    (void)update_applied;
    return 0;
}

uint32_t extract_request_id(const std::string& response) {
    const std::string token = "\"request_id\":";
    const std::size_t pos = response.find(token);
    assert(pos != std::string::npos);
    const std::size_t start = pos + token.size();
    const std::size_t end = response.find_first_not_of("0123456789", start);
    return static_cast<uint32_t>(std::stoul(response.substr(start, end - start)));
}

}  // namespace

#include "../../components/web_ui/web_handlers_rcp.cpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    std::atomic<uint32_t> next_id{1U};
    web_ui::WebRouteContext route_ctx{};
    route_ctx.runtime = &runtime;
    route_ctx.next_correlation_id = &next_id;

    httpd_req_t req{};
    req.user_ctx = &route_ctx;

    g_last_response.clear();
    assert(web_ui::rcp_get_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"stage\":\"idle\"") != std::string::npos);
    assert(g_last_response.find("\"current_version\":\"rcp-1.0.0\"") != std::string::npos);

    g_request_body = "{\"target_version\":\"rcp-1.0.0\"}";
    req.content_len = static_cast<int>(g_request_body.size());
    g_last_status.clear();
    assert(web_ui::rcp_post_handler(&req) == ESP_OK);
    assert(g_last_status == "400 Bad Request");

    g_request_body =
        "{\"url\":\"https://updates.local/rcp.bin\",\"target_version\":\"rcp-1.0.0\",\"transport\":\"uart\"}";
    req.content_len = static_cast<int>(g_request_body.size());
    g_last_status.clear();
    g_last_response.clear();
    assert(web_ui::rcp_post_handler(&req) == ESP_OK);
    assert(g_last_status == "202 Accepted");
    const uint32_t request_id = extract_request_id(g_last_response);

    g_query_string = "request_id=" + std::to_string(request_id);
    g_last_response.clear();
    assert(web_ui::rcp_result_get_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"ready\":false") != std::string::npos);
    assert(g_last_response.find("\"status\":\"queued\"") != std::string::npos);

    assert(runtime.process_pending() == 0U);

    g_last_response.clear();
    assert(web_ui::rcp_get_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"stage\":\"completed\"") != std::string::npos);
    assert(g_last_response.find("\"target_version\":\"rcp-1.0.0\"") != std::string::npos);

    g_last_response.clear();
    assert(web_ui::rcp_result_get_handler(&req) == ESP_OK);
    assert(g_last_response.find("\"ready\":true") != std::string::npos);
    assert(g_last_response.find("\"ok\":true") != std::string::npos);
    assert(g_last_response.find("\"written_bytes\":") != std::string::npos);

    g_request_body =
        "{\"url\":\"https://updates.local/rcp.bin\",\"target_version\":\"rcp-1.0.0\",\"board\":\"other-board\"}";
    req.content_len = static_cast<int>(g_request_body.size());
    g_last_status.clear();
    g_last_response.clear();
    assert(web_ui::rcp_post_handler(&req) == ESP_OK);
    assert(g_last_status == "409 Conflict");
    assert(g_last_response.find("\"error\":\"board_mismatch\"") != std::string::npos);

    g_register_call_count = 0;
    assert(web_ui::register_rcp_routes(reinterpret_cast<void*>(1), &route_ctx));
    assert(g_register_call_count == 3);

    return 0;
}
