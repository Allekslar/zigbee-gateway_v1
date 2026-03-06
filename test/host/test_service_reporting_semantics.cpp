/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"

namespace {

const core::CoreDeviceRecord* find_device(const core::CoreState& state, uint16_t short_addr) {
    for (const auto& record : state.devices) {
        if (record.short_addr == short_addr && record.online) {
            return &record;
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x2201U;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1U);

    const uint8_t valid_payload[2] = {0x66U, 0x08U};  // 0x0866 => 2150 (21.50 C)
    service::ZigbeeRawAttributeReport valid_report{};
    valid_report.short_addr = 0x2201U;
    valid_report.endpoint = 1U;
    valid_report.cluster_id = 0x0402U;
    valid_report.attribute_id = 0x0000U;
    valid_report.payload = valid_payload;
    valid_report.payload_len = 2U;
    assert(runtime.post_zigbee_attribute_report_raw(valid_report));
    assert(runtime.process_pending() == 1U);

    core::CoreState state_after_valid = runtime.state();
    const core::CoreDeviceRecord* device = find_device(state_after_valid, 0x2201U);
    assert(device != nullptr);
    assert(device->has_temperature);
    assert(device->temperature_centi_c == 2150);
    assert(device->reporting_state == core::CoreReportingState::kReportingActive);
    assert(device->last_report_at_ms != 0U);
    const uint32_t last_report_after_valid = device->last_report_at_ms;

    const uint8_t invalid_payload[2] = {0x00U, 0x80U};  // invalid marker 0x8000
    service::ZigbeeRawAttributeReport invalid_report{};
    invalid_report.short_addr = 0x2201U;
    invalid_report.endpoint = 1U;
    invalid_report.cluster_id = 0x0402U;
    invalid_report.attribute_id = 0x0000U;
    invalid_report.payload = invalid_payload;
    invalid_report.payload_len = 2U;
    assert(runtime.post_zigbee_attribute_report_raw(invalid_report));
    assert(runtime.process_pending() == 1U);

    core::CoreState state_after_invalid = runtime.state();
    device = find_device(state_after_invalid, 0x2201U);
    assert(device != nullptr);
    assert(!device->has_temperature);
    assert(device->reporting_state == core::CoreReportingState::kReportingActive);
    assert(device->last_report_at_ms >= last_report_after_valid);

    service::ConfigManager::ReportingPolicyDefault motion_policy{};
    assert(runtime.config_manager().get_reporting_policy_default(
        service::ConfigManager::ReportingDeviceClass::kMotion,
        &motion_policy));
    motion_policy.occupancy_debounce_ms = 30U;
    motion_policy.occupancy_hold_ms = 80U;
    assert(runtime.config_manager().set_reporting_policy_default(
        service::ConfigManager::ReportingDeviceClass::kMotion,
        motion_policy));

    const uint8_t occ_payload[1] = {0x01U};
    service::ZigbeeRawAttributeReport occ_report{};
    occ_report.short_addr = 0x2201U;
    occ_report.endpoint = 1U;
    occ_report.cluster_id = 0x0406U;
    occ_report.attribute_id = 0x0000U;
    occ_report.payload = occ_payload;
    occ_report.payload_len = 1U;
    assert(runtime.post_zigbee_attribute_report_raw(occ_report));
    assert(runtime.process_pending() > 0U);
    device = find_device(runtime.state(), 0x2201U);
    assert(device != nullptr);
    assert(device->occupancy_state == core::CoreOccupancyState::kOccupied);

    const uint8_t clear_payload[1] = {0x00U};
    service::ZigbeeRawAttributeReport clear_report = occ_report;
    clear_report.payload = clear_payload;
    assert(runtime.post_zigbee_attribute_report_raw(clear_report));
    assert(runtime.process_pending() > 0U);
    device = find_device(runtime.state(), 0x2201U);
    assert(device != nullptr);
    assert(device->occupancy_state == core::CoreOccupancyState::kOccupied);

    std::this_thread::sleep_for(std::chrono::milliseconds(90));
    assert(runtime.post_zigbee_attribute_report_raw(clear_report));
    assert(runtime.process_pending() > 0U);
    device = find_device(runtime.state(), 0x2201U);
    assert(device != nullptr);
    assert(device->occupancy_state == core::CoreOccupancyState::kOccupied);

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(runtime.post_zigbee_attribute_report_raw(clear_report));
    assert(runtime.process_pending() > 0U);
    device = find_device(runtime.state(), 0x2201U);
    assert(device != nullptr);
    assert(device->occupancy_state == core::CoreOccupancyState::kNotOccupied);

    const uint8_t ias_open_tamper_low[2] = {0x0DU, 0x00U};  // bit0 open + bit2 tamper + bit3 battery_low
    service::ZigbeeRawAttributeReport ias_report{};
    ias_report.short_addr = 0x2201U;
    ias_report.endpoint = 1U;
    ias_report.cluster_id = 0x0500U;
    ias_report.attribute_id = 0x0002U;
    ias_report.payload = ias_open_tamper_low;
    ias_report.payload_len = 2U;
    assert(runtime.post_zigbee_attribute_report_raw(ias_report));
    assert(runtime.process_pending() > 0U);
    device = find_device(runtime.state(), 0x2201U);
    assert(device != nullptr);
    assert(device->contact_state == core::CoreContactState::kOpen);
    assert(device->contact_tamper);
    assert(device->contact_battery_low);

    const uint8_t ias_closed_clear[2] = {0x00U, 0x00U};
    ias_report.payload = ias_closed_clear;
    assert(runtime.post_zigbee_attribute_report_raw(ias_report));
    assert(runtime.process_pending() > 0U);
    device = find_device(runtime.state(), 0x2201U);
    assert(device != nullptr);
    assert(device->contact_state == core::CoreContactState::kClosed);
    assert(!device->contact_tamper);
    assert(!device->contact_battery_low);

    const uint8_t battery_pct_payload[1] = {0x96U};  // 150 half-percent => 75%
    service::ZigbeeRawAttributeReport battery_pct_report{};
    battery_pct_report.short_addr = 0x2201U;
    battery_pct_report.endpoint = 1U;
    battery_pct_report.cluster_id = 0x0001U;
    battery_pct_report.attribute_id = 0x0021U;
    battery_pct_report.payload = battery_pct_payload;
    battery_pct_report.payload_len = 1U;
    assert(runtime.post_zigbee_attribute_report_raw(battery_pct_report));
    assert(runtime.process_pending() > 0U);
    device = find_device(runtime.state(), 0x2201U);
    assert(device != nullptr);
    assert(device->has_battery);
    assert(device->battery_percent == 75U);

    const uint8_t battery_mv_payload[1] = {0x1EU};  // 30 * 100mV => 3000mV
    service::ZigbeeRawAttributeReport battery_mv_report = battery_pct_report;
    battery_mv_report.attribute_id = 0x0020U;
    battery_mv_report.payload = battery_mv_payload;
    assert(runtime.post_zigbee_attribute_report_raw(battery_mv_report));
    assert(runtime.process_pending() > 0U);
    device = find_device(runtime.state(), 0x2201U);
    assert(device != nullptr);
    assert(device->has_battery_voltage);
    assert(device->battery_voltage_mv == 3000U);

    const uint8_t lqi_rssi_payload[1] = {0x00U};
    service::ZigbeeRawAttributeReport lqi_rssi_report{};
    lqi_rssi_report.short_addr = 0x2201U;
    lqi_rssi_report.endpoint = 1U;
    lqi_rssi_report.cluster_id = 0x0006U;
    lqi_rssi_report.attribute_id = 0x0000U;
    lqi_rssi_report.payload = lqi_rssi_payload;
    lqi_rssi_report.payload_len = 1U;
    lqi_rssi_report.has_lqi = true;
    lqi_rssi_report.lqi = 181U;
    lqi_rssi_report.has_rssi = true;
    lqi_rssi_report.rssi_dbm = -59;
    assert(runtime.post_zigbee_attribute_report_raw(lqi_rssi_report));
    assert(runtime.process_pending() > 0U);
    device = find_device(runtime.state(), 0x2201U);
    assert(device != nullptr);
    assert(device->has_lqi);
    assert(device->lqi == 181U);
    assert(device->has_rssi);
    assert(device->rssi_dbm == -59);

    return 0;
}
