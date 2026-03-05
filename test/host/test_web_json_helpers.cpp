/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "core_state.hpp"
#include "web_dto.hpp"
#include "web_handler_common.hpp"

// Mocks for linker (web_handler_common.cpp uses these)
extern "C" esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) {
    (void)r; (void)type;
    return ESP_OK;
}
extern "C" esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len) {
    (void)r; (void)buf; (void)len;
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

int main() {
    const char* valid_json = "{\"cmd_id\": 123, \"enabled\": true, \"ssid\": \"MyHome\"}";
    
    
    uint32_t u32_val = 0;
    assert(web_ui::find_json_u32_field(valid_json, "cmd_id", &u32_val));
    assert(u32_val == 123);

    
    bool bool_val = false;
    assert(web_ui::find_json_bool_field(valid_json, "enabled", &bool_val));
    assert(bool_val == true);

    
    char str_val[32];
    assert(web_ui::find_json_string_field(valid_json, "ssid", str_val, sizeof(str_val)));
    assert(std::strcmp(str_val, "MyHome") == 0);

    
    assert(!web_ui::find_json_u32_field(valid_json, "missing", &u32_val));
    
    const char* invalid_json = "{\"val\": \"not_a_number\"}";
    assert(!web_ui::find_json_u32_field(invalid_json, "val", &u32_val));

    web_ui::DeviceDto dto{};
    assert(dto.short_addr == 0U);
    assert(!dto.online);
    assert(!dto.power_on);
    assert(dto.reporting_state == core::CoreReportingState::kUnknown);
    assert(dto.last_report_at_ms == 0U);
    assert(!dto.stale);
    assert(!dto.has_temperature);
    assert(dto.occupancy_state == core::CoreOccupancyState::kUnknown);
    assert(dto.contact_state == core::CoreContactState::kUnknown);
    assert(!dto.contact_tamper);
    assert(!dto.contact_battery_low);
    assert(!dto.has_battery);
    assert(!dto.has_battery_voltage);
    assert(!dto.has_lqi);
    assert(!dto.has_rssi);
    assert(!dto.force_remove_armed);
    assert(dto.force_remove_ms_left == 0U);

    return 0;
}
