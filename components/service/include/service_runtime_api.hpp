/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

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
    bool set_timeout_ms{false};
    uint32_t timeout_ms{0};
    bool set_max_retries{false};
    uint8_t max_retries{0};
};

struct ConfigSnapshot {
    uint32_t command_timeout_ms{5000};
    uint8_t max_command_retries{1};
};

struct NetworkApiSnapshot {
    uint32_t revision{0};
    bool connected{false};
    uint32_t refresh_requests{0};
    uint32_t current_backoff_ms{0};
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

struct DevicesApiSnapshot {
    core::CoreState state{};
    DevicesRuntimeSnapshot runtime{};
};

class ServiceRuntimeApi {
public:
    virtual ~ServiceRuntimeApi() = default;

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
    virtual core::CoreState state() const noexcept = 0;
    virtual bool take_network_result(uint32_t request_id, NetworkResult* out) noexcept = 0;
    virtual bool is_scan_request_queued(uint32_t request_id) const noexcept = 0;
    virtual bool is_scan_request_in_progress(uint32_t request_id) const noexcept = 0;
    virtual bool initialize_hal_adapter() noexcept = 0;
    virtual bool start_provisioning_ap(const char* ssid, const char* password) noexcept = 0;
    virtual BootAutoconnectResult autoconnect_from_saved_credentials() noexcept = 0;
    virtual bool start() noexcept = 0;
};

}  // namespace service
