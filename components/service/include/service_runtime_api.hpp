/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "config_manager.hpp"
#include "connectivity_manager.hpp"
#include "core_commands.hpp"
#include "core_errors.hpp"
#include "core_events.hpp"
#include "core_state.hpp"
#include "device_manager.hpp"
#include "network_manager.hpp"

namespace service {

enum class ZigbeeResult : uint8_t {
    kSuccess = 0,
    kTimeout = 1,
    kFailed = 2,
};

struct ZigbeeRawAttributeReport {
    uint16_t short_addr{core::kUnknownDeviceShortAddr};
    uint8_t endpoint{0};
    uint16_t cluster_id{0};
    uint16_t attribute_id{0};
    uint8_t zcl_data_type{0};
    bool has_lqi{false};
    uint8_t lqi{0};
    bool has_rssi{false};
    int8_t rssi_dbm{0};
    const uint8_t* payload{nullptr};
    uint8_t payload_len{0};
};

struct RuntimeStats {
    uint32_t processed_events{0};
    uint32_t dropped_events{0};
    uint32_t executed_effects{0};
    uint32_t failed_effects{0};
    uint32_t command_retries{0};
    uint32_t command_timeouts{0};
    uint32_t reporting_retries{0};
    uint32_t reporting_failures{0};
    uint32_t stale_devices{0};
    uint32_t autoconnect_failures{0};
    uint32_t current_backoff_ms{0};
    uint32_t nvs_writes{0};
    uint32_t last_nvs_revision{0};
    uint32_t network_refresh_requests{0};
};

struct ConfigWriteRequest {
    uint32_t request_id{0};
    bool set_timeout_ms{false};
    uint32_t timeout_ms{0};
    bool set_max_retries{false};
    uint8_t max_retries{0};
};

struct ConfigResult {
    uint32_t request_id{0};
    uint8_t last_command_status{0};
};

struct ConfigSnapshot {
    uint32_t command_timeout_ms{5000};
    uint8_t max_command_retries{1};
};

struct NetworkApiSnapshot {
    enum class MqttConnectionError : uint8_t {
        kNone = 0,
        kDisabled = 1,
        kInitFailed = 2,
        kStartFailed = 3,
        kSubscribeFailed = 4,
    };

    struct MqttStatusSnapshot {
        static constexpr std::size_t kBrokerEndpointSummaryMaxLen = 96U;

        bool enabled{false};
        bool connected{false};
        MqttConnectionError last_connect_error{MqttConnectionError::kNone};
        std::array<char, kBrokerEndpointSummaryMaxLen> broker_endpoint_summary{};
    };

    uint32_t revision{0};
    bool connected{false};
    uint32_t refresh_requests{0};
    uint32_t current_backoff_ms{0};
    MqttStatusSnapshot mqtt{};
};

struct ConfigApiSnapshot {
    uint32_t revision{0};
    uint8_t last_command_status{0};
    uint32_t command_timeout_ms{5000};
    uint8_t max_command_retries{1};
    uint32_t autoconnect_failures{0};
};

using BootAutoconnectResult = ConnectivityAutoconnectResult;
using DevicesRuntimeSnapshot = DeviceRuntimeSnapshot;

enum class DeviceReportingState : uint8_t {
    kUnknown = 0,
    kInterviewCompleted = 1,
    kBindingReady = 2,
    kReportingConfigured = 3,
    kReportingActive = 4,
    kStale = 5,
};

enum class DeviceOccupancyState : uint8_t {
    kUnknown = 0,
    kNotOccupied = 1,
    kOccupied = 2,
};

enum class DeviceContactState : uint8_t {
    kUnknown = 0,
    kClosed = 1,
    kOpen = 2,
};

enum class NetworkOperationPollStatus : uint8_t {
    kNotReady = 0,
    kScanQueued = 1,
    kScanInProgress = 2,
    kReady = 3,
};

struct DevicesApiDeviceSnapshot {
    uint16_t short_addr{core::kUnknownDeviceShortAddr};
    bool online{false};
    bool power_on{false};
    DeviceReportingState reporting_state{DeviceReportingState::kUnknown};
    uint32_t last_report_at_ms{0};
    bool stale{false};
    bool has_temperature{false};
    int16_t temperature_centi_c{0};
    DeviceOccupancyState occupancy_state{DeviceOccupancyState::kUnknown};
    DeviceContactState contact_state{DeviceContactState::kUnknown};
    bool contact_tamper{false};
    bool contact_battery_low{false};
    bool has_battery{false};
    uint8_t battery_percent{0};
    bool has_battery_voltage{false};
    uint16_t battery_voltage_mv{0};
    bool has_lqi{false};
    uint8_t lqi{0};
    bool has_rssi{false};
    int8_t rssi_dbm{0};
    bool force_remove_armed{false};
    uint32_t force_remove_ms_left{0};
};

struct DevicesApiSnapshot {
    uint32_t revision{0};
    uint16_t device_count{0};
    bool join_window_open{false};
    uint16_t join_window_seconds_left{0};
    std::array<DevicesApiDeviceSnapshot, core::kMaxDevices> devices{};
};

struct MqttBridgeDeviceSnapshot {
    uint16_t short_addr{core::kUnknownDeviceShortAddr};
    bool online{false};
    bool power_on{false};
    bool has_temperature{false};
    int16_t temperature_centi_c{0};
    core::CoreOccupancyState occupancy_state{core::CoreOccupancyState::kUnknown};
    core::CoreContactState contact_state{core::CoreContactState::kUnknown};
    bool contact_tamper{false};
    bool contact_battery_low{false};
    bool has_battery{false};
    uint8_t battery_percent{0};
    bool has_battery_voltage{false};
    uint16_t battery_voltage_mv{0};
    bool has_lqi{false};
    uint8_t lqi{0};
    bool has_rssi{false};
    int8_t rssi_dbm{0};
    bool stale{false};
    uint32_t last_report_at_ms{0};
};

struct MqttBridgeSnapshot {
    uint32_t revision{0};
    uint16_t device_count{0};
    std::array<MqttBridgeDeviceSnapshot, core::kMaxDevices> devices{};
};

enum class MatterBridgeDeviceClass : uint8_t {
    kUnknown = 0,
    kTemperature = 1,
    kOccupancy = 2,
    kContact = 3,
};

struct MatterBridgeDeviceSnapshot {
    uint16_t short_addr{core::kUnknownDeviceShortAddr};
    bool online{false};
    bool stale{false};
    MatterBridgeDeviceClass primary_class{MatterBridgeDeviceClass::kUnknown};
    bool has_temperature{false};
    int16_t temperature_centi_c{0};
    bool has_occupancy{false};
    bool occupied{false};
    bool has_contact{false};
    bool contact_open{false};
};

struct MatterBridgeSnapshot {
    uint32_t revision{0};
    uint16_t device_count{0};
    std::array<MatterBridgeDeviceSnapshot, core::kMaxDevices> devices{};
};

class ServiceRuntimeApi {
public:
    virtual ~ServiceRuntimeApi() = default;

    virtual uint32_t next_operation_request_id() noexcept = 0;
    virtual core::CoreError post_command(const core::CoreCommand& command) noexcept = 0;
    virtual bool post_config_write(const ConfigWriteRequest& request) noexcept = 0;
    virtual bool post_reporting_profile_write(const ConfigManager::ReportingProfile& profile) noexcept = 0;
    virtual bool post_network_scan(uint32_t request_id) noexcept = 0;
    virtual bool post_network_connect(
        uint32_t request_id,
        const char* ssid,
        const char* password,
        bool save_credentials) noexcept = 0;
    virtual bool post_network_credentials_status(uint32_t request_id) noexcept = 0;
    virtual bool post_mqtt_status(const NetworkApiSnapshot::MqttStatusSnapshot& snapshot) noexcept = 0;
    virtual bool post_open_join_window(uint32_t request_id, uint16_t duration_seconds) noexcept = 0;
    virtual bool post_remove_device(
        uint32_t request_id,
        uint16_t short_addr,
        bool force_remove,
        uint32_t force_remove_timeout_ms) noexcept = 0;
    virtual bool get_join_window_status(uint16_t* seconds_left) const noexcept = 0;
    virtual bool build_devices_api_snapshot(uint32_t now_ms, DevicesApiSnapshot* out) const noexcept = 0;
    virtual bool build_network_api_snapshot(NetworkApiSnapshot* out) const noexcept = 0;
    virtual bool build_config_api_snapshot(ConfigApiSnapshot* out) const noexcept = 0;
    virtual bool build_mqtt_bridge_snapshot(MqttBridgeSnapshot* out) const noexcept = 0;
    virtual bool build_matter_bridge_snapshot(MatterBridgeSnapshot* out) const noexcept = 0;
    virtual bool take_config_result(uint32_t request_id, ConfigResult* out) noexcept = 0;
    virtual bool take_network_result(uint32_t request_id, NetworkResult* out) noexcept = 0;
    virtual NetworkOperationPollStatus get_network_operation_poll_status(uint32_t request_id) const noexcept = 0;
    virtual bool is_scan_request_queued(uint32_t request_id) const noexcept = 0;
    virtual bool is_scan_request_in_progress(uint32_t request_id) const noexcept = 0;
    virtual bool initialize_hal_adapter() noexcept = 0;
    virtual bool start_provisioning_ap(const char* ssid, const char* password) noexcept = 0;
    virtual BootAutoconnectResult autoconnect_from_saved_credentials() noexcept = 0;
    virtual bool start() noexcept = 0;
};

}  // namespace service
