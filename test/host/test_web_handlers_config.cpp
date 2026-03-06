/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "web_handler_common.hpp"

std::string g_last_response;
extern "C" esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) {
    (void)r; (void)type;
    return ESP_OK;
}
extern "C" esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len) {
    (void)r;
    g_last_response = (len == HTTPD_RESP_USE_STRLEN) ? buf : std::string(buf, len);
    return ESP_OK;
}
extern "C" esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler) {
    (void)handle; (void)uri_handler;
    return ESP_OK;
}
extern "C" esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status) {
    (void)r; (void)status;
    return ESP_OK;
}
extern "C" int httpd_req_recv(httpd_req_t *r, char *buf, size_t len) {
    (void)r; (void)buf; (void)len;
    return 0;
}
extern "C" esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t len) {
    (void)r; (void)buf; (void)len;
    return ESP_OK;
}


#include "../../components/web_ui/web_handlers_config.cpp"
#include "hal_wifi.h"
#include "hal_nvs.h"

/**
 * 
 * 
 * 
 * 
 */
int main() {
    // 1. Setup environment
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_str("wifi_ssid", "TestAP") == HAL_NVS_STATUS_OK);
    
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    
    uint32_t current_time = 1000;
    runtime.tick(current_time);
    assert(runtime.initialize_hal_adapter());

    
    hal_wifi_simulate_connect_failure(true);
    for (int i = 0; i < 3; ++i) {
        runtime.tick(current_time);
        runtime.autoconnect_from_saved_credentials();
        current_time += 10000; 
    }
    assert(runtime.stats().autoconnect_failures == 3);

    
    core::CoreState state = registry.snapshot_copy();
    state.revision = 42;
    state.last_command_status = 1; // Success
    registry.publish(state);

    
    std::atomic<uint32_t> next_id{1};
    web_ui::WebRouteContext route_ctx;
    route_ctx.runtime = &runtime;
    route_ctx.next_correlation_id = &next_id;

    httpd_req_t req;
    req.user_ctx = &route_ctx;

    
    
    bool registered = web_ui::register_config_routes((void*)1, &route_ctx);
    assert(registered);

    
    esp_err_t err = web_ui::config_get_handler(&req);
    assert(err == ESP_OK);

    
    
    
    printf("Response: %s\n", g_last_response.c_str());

    assert(g_last_response.find("\"revision\":42") != std::string::npos);
    assert(g_last_response.find("\"last_command_status\":1") != std::string::npos);
    assert(g_last_response.find("\"autoconnect_failures\":3") != std::string::npos);
    assert(g_last_response.find("\"command_timeout_ms\":5000") != std::string::npos);
    assert(g_last_response.find("\"max_command_retries\":1") != std::string::npos);

    
    assert(g_last_response.front() == '{');
    assert(g_last_response.back() == '}');

    return 0;
}
