/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "config_manager.hpp"
#include "command_manager.hpp"
#include "core_commands.hpp"
#include "core_event_bus.hpp"
#include "core_registry.hpp"
#include "device_manager.hpp"
#include "effect_executor.hpp"
#include "connectivity_manager.hpp"
#include "network_manager.hpp"
#include "network_policy_manager.hpp"
#include "persistence_manager.hpp"
#include "reporting_manager.hpp"
#include "scan_manager.hpp"
#include "hal_zigbee.h"

namespace service {

class ServiceRuntime {
public:
    static constexpr std::size_t kEventQueueCapacity = 32;
    static constexpr std::size_t kNetworkResultQueueCapacity = 8;
    static constexpr std::size_t kNetworkScanMaxRecords = service::kNetworkScanMaxRecords;
    static constexpr uint8_t kMaxAutoconnectRetries = 5U;

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

    using BootAutoconnectResult = ConnectivityAutoconnectResult;

    using NetworkOperationType = service::NetworkOperationType;
    using NetworkOperationStatus = service::NetworkOperationStatus;
    using NetworkScanRecord = service::NetworkScanRecord;
    using NetworkRequest = service::NetworkRequest;
    using NetworkResult = service::NetworkResult;

    using DevicesRuntimeSnapshot = DeviceRuntimeSnapshot;

    struct DevicesApiSnapshot {
        core::CoreState state{};
        DevicesRuntimeSnapshot runtime{};
    };

    ServiceRuntime(core::CoreRegistry& registry, EffectExecutor& effect_executor) noexcept;

    bool post_event(const core::CoreEvent& event) noexcept;
    core::CoreError post_command(const core::CoreCommand& command) noexcept;
    core::CoreError submit_command(const core::CoreCommand& command) noexcept;
    core::CoreError handle_command_result(const core::CoreCommandResult& result) noexcept;
    bool post_config_write(const ConfigWriteRequest& request) noexcept;
    bool post_network_scan(uint32_t request_id) noexcept;
    bool post_network_connect(
        uint32_t request_id,
        const char* ssid,
        const char* password,
        bool save_credentials) noexcept;
    bool post_network_credentials_status(uint32_t request_id) noexcept;
    bool post_network_credentials_raw_debug(uint32_t request_id) noexcept;
    bool post_open_join_window(uint32_t request_id, uint16_t duration_seconds) noexcept;
    bool post_zigbee_join_candidate(uint16_t short_addr) noexcept;
    bool post_zigbee_interview_result(
        uint32_t correlation_id,
        uint16_t short_addr,
        hal_zigbee_result_t result) noexcept;
    bool post_zigbee_bind_result(
        uint32_t correlation_id,
        uint16_t short_addr,
        hal_zigbee_result_t result) noexcept;
    bool post_zigbee_configure_reporting_result(
        uint32_t correlation_id,
        uint16_t short_addr,
        hal_zigbee_result_t result) noexcept;
    bool post_zigbee_attribute_report_raw(const hal_zigbee_raw_attribute_report_t& report) noexcept;
    bool post_remove_device(
        uint32_t request_id,
        uint16_t short_addr,
        bool force_remove,
        uint32_t force_remove_timeout_ms) noexcept;
    bool get_join_window_status(uint16_t* seconds_left) const noexcept;
    bool build_devices_runtime_snapshot(
        const core::CoreState& state,
        uint32_t now_ms,
        DevicesRuntimeSnapshot* out) const noexcept;
    bool build_devices_api_snapshot(uint32_t now_ms, DevicesApiSnapshot* out) const noexcept;
    bool get_force_remove_remaining_ms(uint16_t short_addr, uint32_t now_ms, uint32_t* remaining_ms) const noexcept;
    bool take_network_result(uint32_t request_id, NetworkResult* out) noexcept;
    bool is_scan_request_queued(uint32_t request_id) const noexcept;
    bool is_scan_request_in_progress(uint32_t request_id) const noexcept;
    bool initialize_hal_adapter() noexcept;
    bool start_provisioning_ap(const char* ssid, const char* password) noexcept;
    BootAutoconnectResult autoconnect_from_saved_credentials() noexcept;
    bool has_saved_wifi_credentials() noexcept;
    void mark_wifi_credentials_available() noexcept;
    bool ensure_zigbee_started() noexcept;
    bool zigbee_started() const noexcept;
    bool start() noexcept;
    void on_nvs_u32_written(const char* key, uint32_t value) noexcept;

    const RuntimeStats& stats() const noexcept;
    ConfigSnapshot config_snapshot() const noexcept;
    core::CoreState state() const noexcept;

    std::size_t pending_events() const noexcept;
    std::size_t pending_commands() const noexcept;

private:
    friend class CommandManager;
    friend class ConnectivityManager;
    friend class NetworkManager;
    friend class NetworkPolicyManager;
    friend class PersistenceManager;
    friend class ScanManager;
#if defined(SERVICE_RUNTIME_TEST_HOOKS)
public:
#else
private:
#endif
    std::size_t process_pending() noexcept;
    std::size_t tick(uint32_t now_ms) noexcept;

    ConfigManager& config_manager() noexcept;
#if defined(SERVICE_RUNTIME_TEST_HOOKS)
    bool pop_scan_worker_request_for_test(uint32_t* request_id) noexcept;
    void set_scan_request_in_progress_for_test(uint32_t request_id) noexcept;
    void clear_scan_request_in_progress_for_test() noexcept;
    bool push_network_result_for_test(const NetworkResult& result) noexcept;
#endif

private:
#ifdef ESP_PLATFORM
    static void runtime_task_entry(void* arg);
#endif

    bool push_event(const core::CoreEvent& event) noexcept;
    bool pop_event(core::CoreEvent* out) noexcept;
    bool queue_network_result(const NetworkResult& result) noexcept;
    bool take_network_result_locked(uint32_t request_id, NetworkResult* out) noexcept;
    void apply_managers(const core::CoreEvent& event) noexcept;
    void execute_effects(const core::CoreEffectList& effects) noexcept;
    bool drain_command_requests() noexcept;
    bool drain_command_results() noexcept;
    bool drain_nvs_writes() noexcept;
    bool drain_config_writes() noexcept;
    bool drain_network_requests() noexcept;
    uint32_t monotonic_now_ms() const noexcept;
    bool is_duplicate_join_candidate(uint16_t short_addr, uint32_t now_ms) noexcept;
    void maybe_auto_close_join_window_after_first_join(uint16_t short_addr) noexcept;
    bool ensure_wifi_mode_for_scan() noexcept;
    bool ensure_wifi_mode_for_sta_connect() noexcept;
    bool request_join_window_open(uint16_t duration_seconds) noexcept;
    void process_zigbee_network_policy(uint32_t now_ms) noexcept;
    void set_join_window_cache(bool open, uint16_t seconds_left) noexcept;
    bool schedule_force_remove(uint16_t short_addr, uint32_t deadline_ms) noexcept;
    std::size_t process_force_remove_timeouts(uint32_t now_ms) noexcept;
    std::size_t process_pending_sta_connect(uint32_t now_ms) noexcept;

    core::CoreRegistry* registry_{nullptr};
    EffectExecutor* effect_executor_{nullptr};
    ConfigManager config_manager_{};
    CommandManager command_manager_{};
    ConnectivityManager connectivity_manager_{};
    PersistenceManager persistence_manager_{};
    ReportingManager reporting_manager_{};
    DeviceManager device_manager_{};
    NetworkManager network_manager_{};
    NetworkPolicyManager network_policy_manager_{};
    ScanManager scan_manager_{};
    core::CoreEventBus event_bus_{};

    std::array<core::CoreEvent, kEventQueueCapacity> event_queue_{};
    std::size_t queue_head_{0};
    std::size_t queue_tail_{0};
    std::size_t queue_count_{0};

    std::array<NetworkResult, kNetworkResultQueueCapacity> network_result_queue_{};
    std::size_t network_result_count_{0};

    mutable std::atomic_flag ingress_lock_ = ATOMIC_FLAG_INIT;

    std::atomic<uint32_t> config_timeout_ms_cache_{5000};
    std::atomic<uint32_t> config_max_retries_cache_{1};
    std::atomic<bool> join_window_open_cache_{false};
    std::atomic<uint32_t> join_window_seconds_left_cache_{0};

    std::atomic<uint32_t> dropped_ingress_events_{0};

    RuntimeStats stats_{};
    std::atomic<uint32_t> last_tick_ms_{0};
#ifdef ESP_PLATFORM
    void* runtime_task_handle_{nullptr};
    void* scan_worker_task_handle_{nullptr};
#endif
};

}  // namespace service
