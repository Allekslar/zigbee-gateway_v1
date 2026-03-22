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
#include "device_identity_store.hpp"
#include "device_manager.hpp"
#include "tuya_init_coordinator.hpp"
#include "tuya_translator.hpp"
#include "effect_executor.hpp"
#include "connectivity_manager.hpp"
#include "network_manager.hpp"
#include "network_policy_manager.hpp"
#include "ota_manager.hpp"
#include "operation_result_store.hpp"
#include "persistence_manager.hpp"
#include "rcp_update_manager.hpp"
#include "read_model_coordinator.hpp"
#include "reporting_manager.hpp"
#include "scan_manager.hpp"
#include "service_public_types.hpp"
#include "state_persistence_coordinator.hpp"
#include "runtime_lock.hpp"
#include "service_runtime_api.hpp"
#include "zigbee_lifecycle_coordinator.hpp"

namespace service {

class ServiceRuntimeTestAccess;

// External app/web/bridge code should depend on ServiceRuntimeApi. This concrete
// runtime stays visible for bootstrap, internal managers, and tests.
class ServiceRuntime : public ServiceRuntimeApi {
public:
    static constexpr std::size_t kEventQueueCapacity = 32;
    static constexpr std::size_t kNetworkResultQueueCapacity = OperationResultStore::kNetworkResultQueueCapacity;
    static constexpr std::size_t kNetworkScanMaxRecords = service::kNetworkScanMaxRecords;
    static constexpr uint8_t kMaxAutoconnectRetries = 5U;

    using RuntimeStats = service::RuntimeStats;
    using ConfigWriteRequest = service::ConfigWriteRequest;
    using ConfigSnapshot = service::ConfigSnapshot;
    using NetworkApiSnapshot = service::NetworkApiSnapshot;
    using MqttStatusSnapshot = service::NetworkApiSnapshot::MqttStatusSnapshot;
    using ConfigApiSnapshot = service::ConfigApiSnapshot;
    using BootAutoconnectResult = service::BootAutoconnectResult;

    using NetworkOperationType = service::NetworkOperationType;
    using NetworkOperationStatus = service::NetworkOperationStatus;
    using NetworkScanRecord = service::NetworkScanRecord;
    using NetworkRequest = service::NetworkRequest;
    using NetworkResult = service::NetworkResult;

    using DevicesRuntimeSnapshot = service::DeviceRuntimeSnapshot;
    using DevicesApiSnapshot = service::DevicesApiSnapshot;
    using MqttBridgeDeviceSnapshot = service::MqttBridgeDeviceSnapshot;
    using MqttBridgeSnapshot = service::MqttBridgeSnapshot;
    using MatterBridgeDeviceClass = service::MatterBridgeDeviceClass;
    using MatterBridgeDeviceSnapshot = service::MatterBridgeDeviceSnapshot;
    using MatterBridgeSnapshot = service::MatterBridgeSnapshot;

    ServiceRuntime(core::CoreRegistry& registry, EffectExecutor& effect_executor) noexcept;

    uint32_t next_operation_request_id() noexcept override;
    bool post_event(const core::CoreEvent& event) noexcept;
    CommandSubmitStatus post_device_power_request(const DevicePowerCommandRequest& request) noexcept override;
    CommandSubmitStatus post_network_refresh_request(
        uint32_t correlation_id,
        uint32_t issued_at_ms) noexcept override;
    core::CoreError post_command(const core::CoreCommand& command) noexcept;
    core::CoreError submit_command(const core::CoreCommand& command) noexcept;
    core::CoreError handle_command_result(const core::CoreCommandResult& result) noexcept;
    bool post_config_write(const ConfigWriteRequest& request) noexcept override;
    bool post_reporting_profile_write(const ConfigManager::ReportingProfile& profile) noexcept override;
    bool post_network_scan(uint32_t request_id) noexcept override;
    bool post_network_connect(
        uint32_t request_id,
        const char* ssid,
        const char* password,
        bool save_credentials) noexcept override;
    bool post_network_credentials_status(uint32_t request_id) noexcept override;
    bool post_mqtt_status(const MqttStatusSnapshot& snapshot) noexcept override;
    OtaSubmitStatus post_ota_start(const OtaStartRequest& request) noexcept override;
    RcpUpdateSubmitStatus post_rcp_update_start(const RcpUpdateRequest& request) noexcept override;
    bool post_open_join_window(uint32_t request_id, uint16_t duration_seconds) noexcept override;
    bool post_zigbee_join_candidate(uint16_t short_addr) noexcept;
    bool post_zigbee_interview_result(
        uint32_t correlation_id,
        uint16_t short_addr,
        ZigbeeResult result) noexcept;
    bool post_zigbee_bind_result(
        uint32_t correlation_id,
        uint16_t short_addr,
        ZigbeeResult result) noexcept;
    bool post_zigbee_configure_reporting_result(
        uint32_t correlation_id,
        uint16_t short_addr,
        ZigbeeResult result) noexcept;
    bool post_zigbee_read_attribute_result(
        uint32_t correlation_id,
        const ZigbeeReadAttributeResult& result) noexcept;
    bool post_zigbee_attribute_report_raw(const ZigbeeRawAttributeReport& report) noexcept;
    bool post_remove_device(
        uint32_t request_id,
        uint16_t short_addr,
        bool force_remove,
        uint32_t force_remove_timeout_ms) noexcept override;
    bool get_join_window_status(uint16_t* seconds_left) const noexcept override;
    bool build_devices_runtime_snapshot(
        const core::CoreState& state,
        uint32_t now_ms,
        DevicesRuntimeSnapshot* out) const noexcept;
    bool build_devices_api_snapshot(uint32_t now_ms, DevicesApiSnapshot* out) const noexcept override;
    bool build_network_api_snapshot(NetworkApiSnapshot* out) const noexcept override;
    bool build_config_api_snapshot(ConfigApiSnapshot* out) const noexcept override;
    bool build_ota_api_snapshot(OtaApiSnapshot* out) const noexcept override;
    bool build_rcp_update_api_snapshot(RcpUpdateApiSnapshot* out) const noexcept override;
    bool build_mqtt_bridge_snapshot(MqttBridgeSnapshot* out) const noexcept override;
    bool build_matter_bridge_snapshot(MatterBridgeSnapshot* out) const noexcept override;
    bool take_config_result(uint32_t request_id, ConfigResult* out) noexcept override;
    bool get_force_remove_remaining_ms(uint16_t short_addr, uint32_t now_ms, uint32_t* remaining_ms) const noexcept;
    bool take_network_result(uint32_t request_id, NetworkResult* out) noexcept override;
    bool take_ota_result(uint32_t request_id, OtaResult* out) noexcept override;
    bool take_rcp_update_result(uint32_t request_id, RcpUpdateResult* out) noexcept override;
    NetworkOperationPollStatus get_network_operation_poll_status(uint32_t request_id) const noexcept override;
    OtaPollStatus get_ota_poll_status(uint32_t request_id) const noexcept override;
    RcpUpdatePollStatus get_rcp_update_poll_status(uint32_t request_id) const noexcept override;
    bool is_scan_request_queued(uint32_t request_id) const noexcept override;
    bool is_scan_request_in_progress(uint32_t request_id) const noexcept override;
    bool initialize_hal_adapter() noexcept override;
    bool start_provisioning_ap(const char* ssid, const char* password) noexcept override;
    BootAutoconnectResult autoconnect_from_saved_credentials() noexcept override;
    bool has_saved_wifi_credentials() noexcept;
    void mark_wifi_credentials_available() noexcept;
    bool ensure_zigbee_started() noexcept;
    bool zigbee_started() const noexcept;
    bool start() noexcept override;
    void on_nvs_u32_written(const char* key, uint32_t value) noexcept;

    RuntimeStats stats() const noexcept;
    ConfigSnapshot config_snapshot() const noexcept;
    core::CoreState state() const noexcept;

    std::size_t pending_events() const noexcept;
    std::size_t pending_commands() const noexcept;
    std::size_t process_pending() noexcept;
    std::size_t tick(uint32_t now_ms) noexcept;

    ConfigManager& config_manager() noexcept;
    const ConfigManager::LoadReport& config_load_report() const noexcept;
    bool config_bootstrap_ok() const noexcept;

private:
    friend class CommandManager;
    friend class ConnectivityManager;
    friend class NetworkManager;
    friend class NetworkPolicyManager;
    friend class OtaManager;
    friend class PersistenceManager;
    friend class RcpUpdateManager;
    friend class ScanManager;
    friend class ServiceRuntimeTestAccess;
    friend class ZigbeeLifecycleCoordinator;

private:
#ifdef ESP_PLATFORM
    static void runtime_task_entry(void* arg);
#endif

    bool push_event(const core::CoreEvent& event) noexcept;
    bool pop_event(core::CoreEvent* out) noexcept;
    bool queue_network_result(const NetworkResult& result) noexcept;
    bool queue_ota_result(const OtaResult& result) noexcept;
    bool queue_rcp_update_result(const RcpUpdateResult& result) noexcept;
    void note_network_operation_poll_status(uint32_t request_id, NetworkOperationPollStatus status) noexcept;
    void note_ota_poll_status(uint32_t request_id, OtaPollStatus status) noexcept;
    void note_rcp_update_poll_status(uint32_t request_id, RcpUpdatePollStatus status) noexcept;
    void apply_managers(const core::CoreEvent& event) noexcept;
    void execute_effects(const core::CoreEffectList& effects) noexcept;
    bool drain_command_requests() noexcept;
    bool drain_command_results() noexcept;
    bool drain_nvs_writes() noexcept;
    bool drain_config_writes() noexcept;
    bool drain_network_requests() noexcept;
    bool drain_ota_requests() noexcept;
    bool drain_ota_status_update() noexcept;
    bool drain_rcp_update_requests() noexcept;
    bool drain_rcp_update_status_update() noexcept;
    uint32_t monotonic_now_ms() const noexcept;
    bool ensure_wifi_mode_for_scan() noexcept;
    bool ensure_wifi_mode_for_sta_connect() noexcept;
    bool request_join_window_open(uint16_t duration_seconds) noexcept;
    void process_zigbee_network_policy(uint32_t now_ms) noexcept;
    void set_join_window_cache(bool open, uint16_t seconds_left) noexcept;
    void request_device_identity_read(uint16_t short_addr) noexcept;
    bool try_tuya_translate(const ZigbeeRawAttributeReport& report, uint32_t now_ms) noexcept;
    bool try_execute_tuya_on_off(const core::CoreEffect& effect, bool* routed) noexcept;
    void maybe_start_tuya_init(uint16_t short_addr) noexcept;
    void note_dropped_event() noexcept;
    bool persist_current_core_state() noexcept;
    bool restore_persisted_core_state() noexcept;
    std::size_t process_pending_sta_connect(uint32_t now_ms) noexcept;

    struct RuntimeStatsStorage {
        std::atomic<uint32_t> processed_events{0};
        std::atomic<uint32_t> dropped_events{0};
        std::atomic<uint32_t> executed_effects{0};
        std::atomic<uint32_t> failed_effects{0};
        std::atomic<uint32_t> command_retries{0};
        std::atomic<uint32_t> command_timeouts{0};
        std::atomic<uint32_t> reporting_retries{0};
        std::atomic<uint32_t> reporting_failures{0};
        std::atomic<uint32_t> stale_devices{0};
        std::atomic<uint32_t> autoconnect_failures{0};
        std::atomic<uint32_t> current_backoff_ms{0};
        std::atomic<uint32_t> nvs_writes{0};
        std::atomic<uint32_t> last_nvs_revision{0};
        std::atomic<uint32_t> network_refresh_requests{0};
    };

    struct CoreReadModel {
        uint32_t revision{0};
        bool network_connected{false};
        uint8_t last_command_status{0};
    };

    struct PendingMqttStatusUpdate {
        bool present{false};
        MqttStatusSnapshot snapshot{};
    };

    bool capture_core_read_model(CoreReadModel* out) const noexcept;
    void notify_read_models_from_core_snapshot() noexcept;
    void notify_read_models_from_runtime_stats() noexcept;
    void notify_read_models_from_config_cache() noexcept;
    void reload_config_bootstrap_state() noexcept;
    bool drain_mqtt_status_update() noexcept;

    // CoreRegistry is owned by ServiceRuntime; managers must consume prepared
    // runtime state fragments instead of reading snapshots directly.
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
    OtaManager ota_manager_{};
    RcpUpdateManager rcp_update_manager_{};
    ScanManager scan_manager_{};
    core::CoreEventBus event_bus_{};

    std::array<core::CoreEvent, kEventQueueCapacity> event_queue_{};
    std::size_t queue_head_{0};
    std::size_t queue_tail_{0};
    std::size_t queue_count_{0};

    mutable RuntimeLock ingress_lock_{};
    OperationResultStore operation_result_store_{};
    ReadModelCoordinator read_model_coordinator_;
    StatePersistenceCoordinator state_persistence_coordinator_;
    ZigbeeLifecycleCoordinator zigbee_lifecycle_coordinator_;
    DeviceIdentityStore device_identity_store_{};
    TuyaTranslator tuya_translator_{};
    TuyaInitCoordinator tuya_init_coordinator_{};

    std::atomic<uint32_t> config_timeout_ms_cache_{5000};
    std::atomic<uint32_t> config_max_retries_cache_{1};
    std::atomic<uint32_t> dropped_ingress_events_{0};

    RuntimeStatsStorage stats_{};
    PendingMqttStatusUpdate pending_mqtt_status_update_{};
    MqttStatusSnapshot mqtt_status_cache_{};
    std::atomic<uint32_t> last_tick_ms_{0};
#ifdef ESP_PLATFORM
    void* runtime_task_handle_{nullptr};
    void* scan_worker_task_handle_{nullptr};
    void* ota_worker_task_handle_{nullptr};
    void* rcp_update_worker_task_handle_{nullptr};
#endif
};

}  // namespace service
