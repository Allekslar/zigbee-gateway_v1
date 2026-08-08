/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "application_requests.hpp"
#include "config_manager.hpp"
#include "connectivity_manager.hpp"
#include "device_descriptor_store.hpp"
#include "gateway_id.hpp"
#include "matter_runtime_api.hpp"
#include "network_manager.hpp"
#include "ota_manifest.hpp"
#include "service_public_types.hpp"

namespace service {

enum class ZigbeeResult : uint8_t {
    kSuccess = 0,
    kTimeout = 1,
    kFailed = 2,
};

struct ZigbeeRawAttributeReport {
    uint16_t short_addr{kUnknownShortAddr};
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

struct ZigbeeReadAttributeResult {
    uint16_t short_addr{kUnknownShortAddr};
    uint8_t endpoint{0};
    uint16_t cluster_id{0};
    uint16_t attribute_id{0};
    bool success{false};
    const uint8_t* value{nullptr};
    uint8_t value_len{0};
};

// Service-owned, host-testable projection of which optional subsystems are
// actually usable. Values are computed once at the composition root (app_main,
// which has access to Kconfig and app_hal weak-symbol probes) and injected via
// ServiceRuntimeApi::set_capabilities() before start(). Core/Service never read
// Kconfig or HAL directly (INV-ARCH-01); adapters must consult this projection
// instead of querying HAL/weak symbols on their own.
struct RuntimeCapabilities {
    bool zigbee_available{false};
    bool mqtt_available{false};
    bool matter_target_available{false};
    bool ota_available{false};
    bool rcp_update_available{false};
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

enum class NetworkOperationPollStatus : uint8_t {
    kNotReady = 0,
    kScanQueued = 1,
    kScanInProgress = 2,
    kReady = 3,
};

// Unified operation-result poll (plan S4 HTTP #1: GET /api/v1/operations/
// {operation_id}; a distinct unit of work called out explicitly in
// docs/architecture/HTTP_API_V1.md Section 4.1). OperationResultStore
// tracks network/config/OTA/RCP results as four separate typed queues,
// each keyed by a request_id drawn from one shared global counter
// (next_operation_request_id()) -- request_ids across domains can never
// collide, so a poll can safely dispatch by trying each domain in turn
// and reporting which one claimed the id.
enum class OperationDomain : uint8_t {
    kUnknown = 0,
    kNetwork = 1,
    kConfig = 2,
    kOta = 3,
    kRcpUpdate = 4,
};

// Collapses each domain's own finer-grained mid-flight states (scan
// queued/in-progress, OTA downloading, RCP applying) into one generic
// kInProgress for this domain-agnostic poll surface; the full
// domain-specific detail is still available by fetching the result once
// kReady (take_network_result()/take_ota_result()/etc.).
enum class OperationPollStatus : uint8_t {
    kUnknown = 0,     // request_id not tracked in any domain
    kInProgress = 1,
    kReady = 2,
};

struct OperationStatusSnapshot {
    OperationDomain domain{OperationDomain::kUnknown};
    OperationPollStatus status{OperationPollStatus::kUnknown};
};

enum class OtaStage : uint8_t {
    kIdle = 0,
    kQueued = 1,
    kDownloading = 2,
    kSwitchPending = 3,
    kRebootPending = 4,
    kFailed = 5,
};

enum class OtaPollStatus : uint8_t {
    kNotReady = 0,
    kQueued = 1,
    kDownloading = 2,
    kReady = 3,
};

enum class OtaSubmitStatus : uint8_t {
    kAccepted = 0,
    kBusy = 1,
    kInvalidRequest = 2,
    kInvalidManifest = 3,
    kProjectMismatch = 4,
    kBoardMismatch = 5,
    kChipTargetMismatch = 6,
    kSchemaMismatch = 7,
    kDowngradeRejected = 8,
    kMissingSignature = 9,
    kInvalidSignature = 10,
};

enum class OtaOperationStatus : uint8_t {
    kOk = 0,
    kInvalidArgument = 1,
    kNoCapacity = 2,
    kDownloadFailed = 3,
    kVerifyFailed = 4,
    kApplyFailed = 5,
    kInternalError = 6,
    kManifestInvalid = 7,
    kProjectMismatch = 8,
    kBoardMismatch = 9,
    kChipTargetMismatch = 10,
    kSchemaMismatch = 11,
    kDowngradeRejected = 12,
    kMissingSignature = 13,
    kInvalidSignature = 14,
};

struct OtaStartRequest {
    uint32_t request_id{0};
    OtaManifest manifest{};
};

struct OtaApiSnapshot {
    static constexpr std::size_t kVersionMaxLen = kOtaManifestVersionMaxLen;
    static constexpr std::size_t kDebugBreadcrumbMaxLen = 48U;

    uint32_t active_request_id{0};
    bool busy{false};
    OtaStage stage{OtaStage::kIdle};
    OtaOperationStatus last_error{OtaOperationStatus::kOk};
    uint32_t downloaded_bytes{0};
    uint32_t image_size{0};
    bool image_size_known{false};
    uint32_t transport_last_esp_err{0};
    uint32_t transport_last_tls_error{0};
    int32_t transport_tls_error_code{0};
    int32_t transport_tls_flags{0};
    int32_t transport_socket_errno{0};
    int32_t transport_http_status_code{0};
    uint8_t transport_failure_stage{0};
    uint32_t debug_request_id{0};
    std::array<char, kDebugBreadcrumbMaxLen> debug_breadcrumb{};
    std::array<char, kVersionMaxLen> current_version{};
    std::array<char, kVersionMaxLen> target_version{};
};

constexpr std::size_t kRcpUpdateUrlMaxLen = kOtaManifestUrlMaxLen;
constexpr std::size_t kRcpUpdateVersionMaxLen = kOtaManifestVersionMaxLen;
constexpr std::size_t kRcpUpdateSha256MaxLen = kOtaManifestSha256MaxLen;
constexpr std::size_t kRcpUpdateBoardMaxLen = kOtaManifestBoardMaxLen;
constexpr std::size_t kRcpUpdateTransportMaxLen = 32U;

enum class RcpUpdateStage : uint8_t {
    kIdle = 0,
    kQueued = 1,
    kApplying = 2,
    kCompleted = 3,
    kFailed = 4,
};

enum class RcpUpdatePollStatus : uint8_t {
    kNotReady = 0,
    kQueued = 1,
    kApplying = 2,
    kReady = 3,
};

enum class RcpUpdateSubmitStatus : uint8_t {
    kAccepted = 0,
    kBusy = 1,
    kInvalidRequest = 2,
    kConflict = 3,
    kBoardMismatch = 4,
    kTransportMismatch = 5,
    kGatewayVersionMismatch = 6,
    kUnsupportedBackend = 7,
};

enum class RcpUpdateOperationStatus : uint8_t {
    kOk = 0,
    kInvalidArgument = 1,
    kNoCapacity = 2,
    kConflict = 3,
    kBoardMismatch = 4,
    kTransportMismatch = 5,
    kGatewayVersionMismatch = 6,
    kTransportFailed = 7,
    kVerifyFailed = 8,
    kApplyFailed = 9,
    kProbeFailed = 10,
    kRecoveryFailed = 11,
    kInternalError = 12,
    kUnsupportedBackend = 13,
};

struct RcpUpdateRequest {
    uint32_t request_id{0};
    std::array<char, kRcpUpdateUrlMaxLen> url{};
    std::array<char, kRcpUpdateVersionMaxLen> target_version{};
    std::array<char, kRcpUpdateSha256MaxLen> sha256{};
    std::array<char, kRcpUpdateBoardMaxLen> board{};
    std::array<char, kRcpUpdateTransportMaxLen> transport{};
    std::array<char, kRcpUpdateVersionMaxLen> gateway_min_version{};
};

struct RcpUpdateApiSnapshot {
    static constexpr std::size_t kVersionMaxLen = kRcpUpdateVersionMaxLen;
    static constexpr std::size_t kBackendNameMaxLen = 32U;

    uint32_t active_request_id{0};
    bool busy{false};
    bool backend_available{false};
    RcpUpdateStage stage{RcpUpdateStage::kIdle};
    RcpUpdateOperationStatus last_error{RcpUpdateOperationStatus::kOk};
    uint32_t written_bytes{0};
    std::array<char, kBackendNameMaxLen> backend_name{};
    std::array<char, kVersionMaxLen> current_version{};
    std::array<char, kVersionMaxLen> target_version{};
};

struct RcpUpdateResult {
    static constexpr std::size_t kVersionMaxLen = kRcpUpdateVersionMaxLen;

    uint32_t request_id{0};
    RcpUpdateOperationStatus status{RcpUpdateOperationStatus::kInternalError};
    uint32_t written_bytes{0};
    std::array<char, kVersionMaxLen> target_version{};
};

struct OtaResult {
    static constexpr std::size_t kVersionMaxLen = kOtaManifestVersionMaxLen;

    uint32_t request_id{0};
    OtaOperationStatus status{OtaOperationStatus::kInternalError};
    bool reboot_required{false};
    uint32_t downloaded_bytes{0};
    uint32_t image_size{0};
    bool image_size_known{false};
    uint32_t transport_last_esp_err{0};
    uint32_t transport_last_tls_error{0};
    int32_t transport_tls_error_code{0};
    int32_t transport_tls_flags{0};
    int32_t transport_socket_errno{0};
    int32_t transport_http_status_code{0};
    uint8_t transport_failure_stage{0};
    std::array<char, kVersionMaxLen> target_version{};
};

struct DevicesApiDeviceSnapshot {
    // Canonical durable identity (FD-01), pre-formatted as exactly 16
    // lowercase hex characters + a null terminator so adapters (web_ui) can
    // copy it into JSON without ever naming core::DeviceId directly
    // (INV-M030). Always populated: a device only appears in this snapshot
    // at all when its DeviceId is valid (see DevicesApiSnapshotBuilder).
    std::array<char, core::DeviceId::kHexLength + 1U> device_id_hex{};
    // short_addr is a diagnostic locator only (FD-01), never identity. It is
    // meaningful only when has_locator is true -- callers must serialize it
    // as JSON null otherwise, per plan S4 HTTP #2 ("short_addr always
    // present as a nullable diagnostic field").
    bool has_locator{false};
    uint16_t short_addr{kUnknownShortAddr};
    // Monotonically increasing per DeviceLocatorRegistry::remap() call;
    // meaningful only when has_locator is true.
    uint32_t locator_revision{0};
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
    DeviceDescriptorStatus identity_status{DeviceDescriptorStatus::kUnknown};
    std::array<char, kDeviceDescriptorManufacturerMaxLen> manufacturer{};
    std::array<char, kDeviceDescriptorModelMaxLen> model{};
};

struct DevicesApiSnapshot {
    uint32_t revision{0};
    uint16_t device_count{0};
    bool join_window_open{false};
    uint16_t join_window_seconds_left{0};
    std::array<DevicesApiDeviceSnapshot, kServiceMaxDevices> devices{};
};

struct MqttBridgeDeviceSnapshot {
    uint16_t short_addr{kUnknownShortAddr};
    // Empty (device_id_hex[0] == '\0') when the device's identity has not
    // resolved to a valid DeviceId yet (plan S4 MQTT #10-#12: v1 topics are
    // DeviceId-keyed, so a device without one cannot be published under a
    // v1 topic and is skipped by v1 publishing until it resolves).
    std::array<char, core::DeviceId::kHexLength + 1U> device_id_hex{};
    bool online{false};
    bool power_on{false};
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
    bool stale{false};
    uint32_t last_report_at_ms{0};
};

struct MqttBridgeSnapshot {
    uint32_t revision{0};
    uint16_t device_count{0};
    std::array<MqttBridgeDeviceSnapshot, kServiceMaxDevices> devices{};
};

class ServiceRuntimeApi : public MatterRuntimeApi {
public:
    virtual ~ServiceRuntimeApi() = default;

    virtual void set_capabilities(const RuntimeCapabilities& capabilities) noexcept = 0;
    virtual RuntimeCapabilities capabilities() const noexcept = 0;

    // Resolves a canonical 16-lowercase-hex-character DeviceId string (as
    // used in /api/v1 device-scoped URL paths) to a current short_addr for
    // command dispatch, without ever exposing core::DeviceId to callers
    // (INV-M030: web_ui adapters must not name core:: symbols). String-in,
    // string-free-out by design.
    enum class DeviceIdResolveStatus : uint8_t {
        kResolved = 0,          // known identity with a current locator; *out_short_addr is valid.
        kMalformedHex = 1,      // not exactly 16 lowercase hex characters.
        kUnknownDevice = 2,     // well-formed hex, but no device with that identity is known to Core.
        kNoCurrentLocator = 3,  // known identity, but it has no current locator (offline/not yet joined).
    };
    virtual DeviceIdResolveStatus resolve_short_addr_for_device_id_hex(
        const char* device_id_hex, uint16_t* out_short_addr) noexcept = 0;

    // Canonical gateway identity (plan FD-17, S4 slice): the factory base
    // MAC, resolved once at construction and stable for the process
    // lifetime. Never derived from mutable network configuration.
    virtual common::GatewayId gateway_id() const noexcept = 0;

    virtual uint32_t next_operation_request_id() noexcept override = 0;
    virtual CommandSubmitStatus post_device_power_request(
        const DevicePowerCommandRequest& request) noexcept override = 0;
    virtual CommandSubmitStatus post_network_refresh_request(
        uint32_t correlation_id,
        uint32_t issued_at_ms) noexcept = 0;
    virtual bool post_config_write(const ConfigWriteRequest& request) noexcept = 0;
    virtual bool post_reporting_profile_write(const ConfigManager::ReportingProfile& profile) noexcept = 0;
    // Looks up the DeviceId currently mapped to short_addr in the locator
    // registry; returns an invalid (default) DeviceId when unresolved.
    virtual core::DeviceId resolve_device_id_for_short_addr(uint16_t short_addr) noexcept = 0;
    virtual bool post_network_scan(uint32_t request_id) noexcept = 0;
    virtual bool post_network_connect(
        uint32_t request_id,
        const char* ssid,
        const char* password,
        bool save_credentials) noexcept = 0;
    virtual bool post_network_credentials_status(uint32_t request_id) noexcept = 0;
    virtual bool post_mqtt_status(const NetworkApiSnapshot::MqttStatusSnapshot& snapshot) noexcept = 0;
    virtual OtaSubmitStatus post_ota_start(const OtaStartRequest& request) noexcept = 0;
    virtual RcpUpdateSubmitStatus post_rcp_update_start(const RcpUpdateRequest& request) noexcept = 0;
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
    virtual bool build_ota_api_snapshot(OtaApiSnapshot* out) const noexcept = 0;
    virtual bool build_rcp_update_api_snapshot(RcpUpdateApiSnapshot* out) const noexcept = 0;
    virtual bool build_mqtt_bridge_snapshot(MqttBridgeSnapshot* out) const noexcept = 0;
    virtual bool build_matter_bridge_snapshot(MatterBridgeSnapshot* out) const noexcept override = 0;
    virtual bool take_config_result(uint32_t request_id, ConfigResult* out) noexcept = 0;
    virtual bool take_network_result(uint32_t request_id, NetworkResult* out) noexcept = 0;
    virtual bool take_ota_result(uint32_t request_id, OtaResult* out) noexcept = 0;
    virtual bool take_rcp_update_result(uint32_t request_id, RcpUpdateResult* out) noexcept = 0;
    virtual NetworkOperationPollStatus get_network_operation_poll_status(uint32_t request_id) const noexcept = 0;
    virtual OtaPollStatus get_ota_poll_status(uint32_t request_id) const noexcept = 0;
    virtual RcpUpdatePollStatus get_rcp_update_poll_status(uint32_t request_id) const noexcept = 0;
    virtual OperationStatusSnapshot poll_operation_status(uint32_t request_id) const noexcept = 0;
    virtual bool is_scan_request_queued(uint32_t request_id) const noexcept = 0;
    virtual bool is_scan_request_in_progress(uint32_t request_id) const noexcept = 0;
    virtual bool initialize_hal_adapter() noexcept = 0;
    virtual bool start_provisioning_ap(const char* ssid, const char* password) noexcept = 0;
    virtual BootAutoconnectResult autoconnect_from_saved_credentials() noexcept = 0;
    virtual bool start() noexcept = 0;
};

}  // namespace service
