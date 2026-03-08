/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "service_runtime.hpp"

#include <cstring>
#include <chrono>
#include <type_traits>

#include "hal_event_adapter_internal.hpp"
#include "hal_nvs.h"
#include "log_tags.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include "hal_zigbee.h"

namespace service {

namespace {

#ifdef ESP_PLATFORM
constexpr const char* kTag = LOG_TAG_SERVICE_RUNTIME;
#define SR_LOGI(...) ESP_LOGI(kTag, __VA_ARGS__)
#define SR_LOGW(...) ESP_LOGW(kTag, __VA_ARGS__)
#else
#define SR_LOGI(...) ((void)0)
#define SR_LOGW(...) ((void)0)
#endif

constexpr uint32_t kForceRemoveDefaultTimeoutMs = 5000U;
constexpr uint32_t kForceRemoveMinTimeoutMs = 500U;
constexpr uint32_t kForceRemoveMaxTimeoutMs = 30000U;
constexpr std::size_t kMaxProcessedEventsPerCycle = 64U;
constexpr const char* kPersistedCoreStateKey = "core_state_v1";
constexpr uint32_t kPersistedCoreStateMagic = 0x43535445U;  // "CSTE"
constexpr uint32_t kPersistedCoreStateVersion = 1U;
#ifdef ESP_PLATFORM
constexpr const char* kRuntimeTaskName = "service_runtime";
// NVS + network request processing can exceed 6KB on ESP32-C6 in AP provisioning flow.
constexpr uint32_t kRuntimeTaskStackSize = 9216U;
constexpr UBaseType_t kRuntimeTaskPriority = 6U;
constexpr TickType_t kRuntimeTaskPeriodTicks = pdMS_TO_TICKS(20);
constexpr const char* kScanWorkerTaskName = "wifi_scan_worker";
constexpr uint32_t kScanWorkerTaskStackSize = 4608U;
constexpr UBaseType_t kScanWorkerTaskPriority = 5U;
#endif

uint32_t clamp_force_remove_timeout(uint32_t timeout_ms) noexcept {
    if (timeout_ms < kForceRemoveMinTimeoutMs) {
        return kForceRemoveDefaultTimeoutMs;
    }
    if (timeout_ms > kForceRemoveMaxTimeoutMs) {
        return kForceRemoveMaxTimeoutMs;
    }
    return timeout_ms;
}

struct PersistedCoreStateV1 {
    uint32_t magic{kPersistedCoreStateMagic};
    uint32_t version{kPersistedCoreStateVersion};
    core::CoreState state{};
};

static_assert(std::is_trivially_copyable<PersistedCoreStateV1>::value, "PersistedCoreStateV1 must be POD-like");
static_assert(
    sizeof(PersistedCoreStateV1) == sizeof(core::CoreState) + sizeof(uint32_t) * 2U,
    "Persisted core-state storage size must match serialized payload");

bool sanitize_restored_core_state(core::CoreState* state) noexcept {
    if (state == nullptr) {
        return false;
    }

    uint16_t active_devices = 0U;
    for (std::size_t i = 0; i < state->devices.size(); ++i) {
        core::CoreDeviceRecord& device = state->devices[i];
        if (!device.online || device.short_addr == core::kUnknownDeviceShortAddr || device.short_addr == 0x0000U) {
            device = core::CoreDeviceRecord{};
            continue;
        }

        bool duplicate = false;
        for (std::size_t j = 0; j < i; ++j) {
            const core::CoreDeviceRecord& previous = state->devices[j];
            if (previous.online && previous.short_addr == device.short_addr) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            device = core::CoreDeviceRecord{};
            continue;
        }

        ++active_devices;
    }

    state->device_count = active_devices;
    state->network_connected = false;
    state->last_command_status = 0U;
    return true;
}

bool has_restorable_devices(const core::CoreState& state) noexcept {
    if (state.device_count != 0U) {
        return true;
    }

    for (const auto& device : state.devices) {
        if (device.short_addr != core::kUnknownDeviceShortAddr) {
            return true;
        }
    }
    return false;
}

bool decode_raw_u32_le(const ZigbeeRawAttributeReport& report, uint32_t* out_value) noexcept {
    if (out_value == nullptr) {
        return false;
    }

    *out_value = 0U;
    if (report.payload_len == 0U) {
        return true;
    }
    if (report.payload == nullptr || report.payload_len > 4U) {
        return false;
    }

    uint32_t value = 0U;
    for (uint8_t i = 0; i < report.payload_len; ++i) {
        value |= static_cast<uint32_t>(report.payload[i]) << (8U * i);
    }
    *out_value = value;
    return true;
}

bool decode_temperature_centi_c(
    const ZigbeeRawAttributeReport& report,
    int16_t* out_temp_centi_c,
    bool* out_valid) noexcept {
    if (out_temp_centi_c == nullptr || out_valid == nullptr) {
        return false;
    }
    if (report.payload == nullptr || report.payload_len != 2U) {
        return false;
    }

    const uint16_t raw = static_cast<uint16_t>(report.payload[0]) |
                         (static_cast<uint16_t>(report.payload[1]) << 8U);
    if (raw == 0x8000U) {
        *out_temp_centi_c = 0;
        *out_valid = false;
        return true;
    }

    *out_temp_centi_c = static_cast<int16_t>(raw);
    *out_valid = true;
    return true;
}

bool decode_ias_zone_status(
    const ZigbeeRawAttributeReport& report,
    uint32_t* out_normalized_status) noexcept {
    if (out_normalized_status == nullptr) {
        return false;
    }
    if (report.payload == nullptr || report.payload_len != 2U) {
        return false;
    }

    const uint16_t zone_status = static_cast<uint16_t>(report.payload[0]) |
                                 (static_cast<uint16_t>(report.payload[1]) << 8U);
    uint32_t normalized = 0U;
    // IAS Zone status mapping:
    // bit0 alarm1 -> contact open
    // bit2 tamper -> tamper true
    // bit3 battery -> battery low true
    if ((zone_status & (1U << 0U)) != 0U) {
        normalized |= 0x01U;
    }
    if ((zone_status & (1U << 2U)) != 0U) {
        normalized |= 0x02U;
    }
    if ((zone_status & (1U << 3U)) != 0U) {
        normalized |= 0x04U;
    }

    *out_normalized_status = normalized;
    return true;
}

bool decode_battery_percent(
    const ZigbeeRawAttributeReport& report,
    int32_t* out_percent,
    bool* out_valid) noexcept {
    if (out_percent == nullptr || out_valid == nullptr) {
        return false;
    }
    if (report.payload == nullptr || report.payload_len != 1U) {
        return false;
    }

    const uint8_t raw = report.payload[0];
    if (raw == 0xFFU) {
        *out_percent = 0;
        *out_valid = false;
        return true;
    }

    const uint32_t percent = static_cast<uint32_t>(raw) / 2U;
    *out_percent = static_cast<int32_t>(percent > 100U ? 100U : percent);
    *out_valid = true;
    return true;
}

bool decode_battery_voltage_mv(
    const ZigbeeRawAttributeReport& report,
    int32_t* out_mv,
    bool* out_valid) noexcept {
    if (out_mv == nullptr || out_valid == nullptr) {
        return false;
    }
    if (report.payload == nullptr || report.payload_len != 1U) {
        return false;
    }

    const uint8_t raw = report.payload[0];
    if (raw == 0xFFU) {
        *out_mv = 0;
        *out_valid = false;
        return true;
    }

    *out_mv = static_cast<int32_t>(static_cast<uint32_t>(raw) * 100U);
    *out_valid = true;
    return true;
}

bool reporting_profile_equal(
    const ConfigManager::ReportingProfile& lhs,
    const ConfigManager::ReportingProfile& rhs) noexcept {
    return lhs.in_use == rhs.in_use &&
           lhs.key.short_addr == rhs.key.short_addr &&
           lhs.key.endpoint == rhs.key.endpoint &&
           lhs.key.cluster_id == rhs.key.cluster_id &&
           lhs.min_interval_seconds == rhs.min_interval_seconds &&
           lhs.max_interval_seconds == rhs.max_interval_seconds &&
           lhs.reportable_change == rhs.reportable_change &&
           lhs.capability_flags == rhs.capability_flags;
}

}  // namespace

ServiceRuntime::ServiceRuntime(core::CoreRegistry& registry, EffectExecutor& effect_executor) noexcept
    : registry_(&registry), effect_executor_(&effect_executor) {
    (void)config_manager_.load();
    config_timeout_ms_cache_.store(config_manager_.command_timeout_ms(), std::memory_order_relaxed);
    config_max_retries_cache_.store(config_manager_.max_command_retries(), std::memory_order_relaxed);
    set_join_window_cache(false, 0U);
    sync_api_snapshots();
}

void ServiceRuntime::set_join_window_cache(bool open, uint16_t seconds_left) noexcept {
    join_window_open_cache_.store(open, std::memory_order_release);
    join_window_seconds_left_cache_.store(open ? static_cast<uint32_t>(seconds_left) : 0U, std::memory_order_release);
}

bool ServiceRuntime::persist_current_core_state() noexcept {
    auto* persisted = reinterpret_cast<PersistedCoreStateV1*>(persisted_core_state_storage_.bytes.data());
    *persisted = PersistedCoreStateV1{};
    persisted->state = state();
    return hal_nvs_set_blob(kPersistedCoreStateKey, persisted, sizeof(*persisted)) == HAL_NVS_STATUS_OK;
}

bool ServiceRuntime::restore_persisted_core_state() noexcept {
    auto* persisted = reinterpret_cast<PersistedCoreStateV1*>(persisted_core_state_storage_.bytes.data());
    *persisted = PersistedCoreStateV1{};
    uint32_t persisted_len = 0U;
    const hal_nvs_status_t status =
        hal_nvs_get_blob(kPersistedCoreStateKey, persisted, sizeof(*persisted), &persisted_len);
    if (status == HAL_NVS_STATUS_NOT_FOUND) {
        return false;
    }
    if (status != HAL_NVS_STATUS_OK || persisted_len != sizeof(*persisted) ||
        persisted->magic != kPersistedCoreStateMagic || persisted->version != kPersistedCoreStateVersion) {
        SR_LOGW("Persisted CoreState restore skipped: status=%d len=%u", static_cast<int>(status), persisted_len);
        return false;
    }

    core::CoreState restored = persisted->state;
    if (!sanitize_restored_core_state(&restored) || !has_restorable_devices(restored)) {
        return false;
    }

    if (!registry_->publish(restored)) {
        SR_LOGW("Persisted CoreState restore failed: registry publish rejected");
        return false;
    }

    SR_LOGI(
        "Restored persisted CoreState devices=%u revision=%lu",
        static_cast<unsigned>(restored.device_count),
        static_cast<unsigned long>(restored.revision));
    sync_api_snapshots();
    return true;
}

#ifdef ESP_PLATFORM
void ServiceRuntime::runtime_task_entry(void* arg) {
    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(arg);
    if (runtime == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    if (runtime->restore_core_state_pending_.exchange(false, std::memory_order_acq_rel)) {
        (void)runtime->restore_persisted_core_state();
    }

    const TickType_t delay_ticks = kRuntimeTaskPeriodTicks > 0 ? kRuntimeTaskPeriodTicks : 1U;
    for (;;) {
        const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        hal_zigbee_poll();
        (void)runtime->process_pending();
        (void)runtime->tick(now_ms);
        vTaskDelay(delay_ticks);
    }
}
#endif

bool ServiceRuntime::push_event(const core::CoreEvent& event) noexcept {
    RuntimeLockGuard guard(ingress_lock_);
    if (queue_count_ >= kEventQueueCapacity) {
        (void)dropped_ingress_events_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    event_queue_[queue_tail_] = event;
    queue_tail_ = (queue_tail_ + 1U) % kEventQueueCapacity;
    ++queue_count_;
    return true;
}

bool ServiceRuntime::pop_event(core::CoreEvent* out) noexcept {
    RuntimeLockGuard guard(ingress_lock_);
    if (out == nullptr || queue_count_ == 0) {
        return false;
    }

    *out = event_queue_[queue_head_];
    queue_head_ = (queue_head_ + 1U) % kEventQueueCapacity;
    --queue_count_;
    return true;
}

bool ServiceRuntime::post_event(const core::CoreEvent& event) noexcept {
    return push_event(event);
}

bool ServiceRuntime::queue_network_result(const NetworkResult& result) noexcept {
    RuntimeLockGuard guard(ingress_lock_);

    for (std::size_t i = 0; i < network_result_count_; ++i) {
        if (network_result_queue_[i].request_id == result.request_id && result.request_id != 0U) {
            network_result_queue_[i] = result;
            return true;
        }
    }

    if (network_result_count_ >= kNetworkResultQueueCapacity) {
        for (std::size_t i = 1; i < network_result_count_; ++i) {
            network_result_queue_[i - 1U] = network_result_queue_[i];
        }
        --network_result_count_;
    }

    network_result_queue_[network_result_count_] = result;
    ++network_result_count_;
    return true;
}

bool ServiceRuntime::take_network_result_locked(uint32_t request_id, NetworkResult* out) noexcept {
    if (out == nullptr || request_id == 0) {
        return false;
    }

    for (std::size_t i = 0; i < network_result_count_; ++i) {
        if (network_result_queue_[i].request_id != request_id) {
            continue;
        }

        *out = network_result_queue_[i];
        for (std::size_t j = i + 1U; j < network_result_count_; ++j) {
            network_result_queue_[j - 1U] = network_result_queue_[j];
        }
        --network_result_count_;
        return true;
    }

    return false;
}

core::CoreError ServiceRuntime::post_command(const core::CoreCommand& command) noexcept {
    if (command.type == core::CoreCommandType::kUpdateReportingProfile) {
        if (command.correlation_id == core::kNoCorrelationId) {
            return core::CoreError::kInvalidArgument;
        }

        if (command.device_short_addr == 0U ||
            command.device_short_addr == core::kUnknownDeviceShortAddr ||
            command.reporting_endpoint == 0U ||
            command.reporting_cluster_id == 0U) {
            return core::CoreError::kInvalidArgument;
        }

        if (command.reporting_max_interval_seconds == 0U ||
            command.reporting_min_interval_seconds > command.reporting_max_interval_seconds) {
            return core::CoreError::kInvalidArgument;
        }

        ConfigManager::ReportingProfile profile{};
        profile.in_use = true;
        profile.key.short_addr = command.device_short_addr;
        profile.key.endpoint = command.reporting_endpoint;
        profile.key.cluster_id = command.reporting_cluster_id;
        profile.min_interval_seconds = command.reporting_min_interval_seconds;
        profile.max_interval_seconds = command.reporting_max_interval_seconds;
        profile.reportable_change = command.reporting_reportable_change;
        profile.capability_flags = command.reporting_capability_flags;

        ConfigManager::ReportingProfile existing{};
        if (config_manager_.get_reporting_profile(profile.key, &existing) && reporting_profile_equal(existing, profile)) {
            return core::CoreError::kOk;
        }

        if (!post_reporting_profile_write(profile)) {
            return core::CoreError::kNoCapacity;
        }
        return core::CoreError::kOk;
    }

    return command_manager_.post_command(*this, command);
}

core::CoreError ServiceRuntime::submit_command(const core::CoreCommand& command) noexcept {
    // Backward-compatible API: command submission is now always ingress-queued.
    return post_command(command);
}

core::CoreError ServiceRuntime::handle_command_result(const core::CoreCommandResult& result) noexcept {
    return command_manager_.handle_command_result(*this, result);
}

bool ServiceRuntime::post_config_write(const ConfigWriteRequest& request) noexcept {
    return persistence_manager_.post_config_write(
        *this,
        request.set_timeout_ms,
        request.timeout_ms,
        request.set_max_retries,
        request.max_retries);
}

bool ServiceRuntime::post_reporting_profile_write(const ConfigManager::ReportingProfile& profile) noexcept {
    return persistence_manager_.post_reporting_profile_write(*this, profile);
}

bool ServiceRuntime::post_network_scan(uint32_t request_id) noexcept {
    if (request_id == 0) {
        return false;
    }

    NetworkRequest request{};
    request.request_id = request_id;
    request.operation = NetworkOperationType::kScan;
    return network_manager_.enqueue_request(*this, request);
}

bool ServiceRuntime::post_network_connect(
    uint32_t request_id,
    const char* ssid,
    const char* password,
    bool save_credentials) noexcept {
    if (request_id == 0 || ssid == nullptr || ssid[0] == '\0') {
        return false;
    }

    NetworkRequest request{};
    request.request_id = request_id;
    request.operation = NetworkOperationType::kConnect;
    request.save_credentials = save_credentials;
    std::strncpy(request.ssid, ssid, sizeof(request.ssid) - 1U);
    if (password != nullptr) {
        std::strncpy(request.password, password, sizeof(request.password) - 1U);
    }

    return network_manager_.enqueue_request(*this, request);
}

bool ServiceRuntime::post_network_credentials_status(uint32_t request_id) noexcept {
    if (request_id == 0) {
        return false;
    }

    NetworkRequest request{};
    request.request_id = request_id;
    request.operation = NetworkOperationType::kCredentialsStatus;
    return network_manager_.enqueue_request(*this, request);
}

bool ServiceRuntime::post_open_join_window(uint32_t request_id, uint16_t duration_seconds) noexcept {
    if (request_id == 0 || duration_seconds == 0U) {
        return false;
    }

    NetworkRequest request{};
    request.request_id = request_id;
    request.operation = NetworkOperationType::kOpenJoinWindow;
    request.join_window_seconds = duration_seconds;
    return network_manager_.enqueue_request(*this, request);
}

bool ServiceRuntime::post_zigbee_join_candidate(uint16_t short_addr) noexcept {
    if (short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U) {
        return false;
    }

    const uint32_t now_ms = monotonic_now_ms();
    if (is_duplicate_join_candidate(short_addr, now_ms)) {
        SR_LOGI(
            "Suppress duplicate join candidate short_addr=0x%04x window_ms=%lu",
            static_cast<unsigned>(short_addr),
            static_cast<unsigned long>(DeviceManager::kJoinDedupWindowMs));
        return true;
    }

    core::CoreEvent event{};
    event.type = core::CoreEventType::kDeviceJoined;
    event.device_short_addr = short_addr;
    if (!push_event(event)) {
        return false;
    }

    maybe_auto_close_join_window_after_first_join(short_addr);
    return true;
}

bool ServiceRuntime::post_zigbee_interview_result(
    uint32_t correlation_id,
    uint16_t short_addr,
    ZigbeeResult result) noexcept {
    (void)correlation_id;
    if (short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U || result != ZigbeeResult::kSuccess) {
        return false;
    }

    core::CoreEvent event{};
    event.type = core::CoreEventType::kDeviceInterviewCompleted;
    event.device_short_addr = short_addr;
    return push_event(event);
}

bool ServiceRuntime::post_zigbee_bind_result(
    uint32_t correlation_id,
    uint16_t short_addr,
    ZigbeeResult result) noexcept {
    (void)correlation_id;
    if (short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U || result != ZigbeeResult::kSuccess) {
        return false;
    }

    core::CoreEvent event{};
    event.type = core::CoreEventType::kDeviceBindingReady;
    event.device_short_addr = short_addr;
    return push_event(event);
}

bool ServiceRuntime::post_zigbee_configure_reporting_result(
    uint32_t correlation_id,
    uint16_t short_addr,
    ZigbeeResult result) noexcept {
    (void)correlation_id;
    if (short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U || result != ZigbeeResult::kSuccess) {
        return false;
    }

    core::CoreEvent event{};
    event.type = core::CoreEventType::kDeviceReportingConfigured;
    event.device_short_addr = short_addr;
    return push_event(event);
}

bool ServiceRuntime::post_zigbee_attribute_report_raw(const ZigbeeRawAttributeReport& report) noexcept {
    if (report.short_addr == core::kUnknownDeviceShortAddr || report.short_addr == 0x0000U) {
        return false;
    }
    const uint32_t now_ms = monotonic_now_ms();

    if (report.has_lqi) {
        core::CoreEvent lqi_event{};
        lqi_event.type = core::CoreEventType::kDeviceTelemetryUpdated;
        lqi_event.device_short_addr = report.short_addr;
        lqi_event.value_u32 = now_ms;
        lqi_event.telemetry_kind = core::CoreTelemetryKind::kLqi;
        lqi_event.telemetry_i32 = static_cast<int32_t>(report.lqi);
        lqi_event.telemetry_valid = true;
        if (!push_event(lqi_event)) {
            return false;
        }
    }

    if (report.has_rssi) {
        core::CoreEvent rssi_event{};
        rssi_event.type = core::CoreEventType::kDeviceTelemetryUpdated;
        rssi_event.device_short_addr = report.short_addr;
        rssi_event.value_u32 = now_ms;
        rssi_event.telemetry_kind = core::CoreTelemetryKind::kRssiDbm;
        rssi_event.telemetry_i32 = static_cast<int32_t>(report.rssi_dbm);
        rssi_event.telemetry_valid = true;
        if (!push_event(rssi_event)) {
            return false;
        }
    }

    if (report.cluster_id == 0x0402U && report.attribute_id == 0x0000U) {
        int16_t temperature_centi_c = 0;
        bool temperature_valid = false;
        if (!decode_temperature_centi_c(report, &temperature_centi_c, &temperature_valid)) {
            return false;
        }

        core::CoreEvent event{};
        event.type = core::CoreEventType::kDeviceTelemetryUpdated;
        event.device_short_addr = report.short_addr;
        event.value_u32 = now_ms;
        event.telemetry_kind = core::CoreTelemetryKind::kTemperatureCentiC;
        event.telemetry_i32 = static_cast<int32_t>(temperature_centi_c);
        event.telemetry_valid = temperature_valid;
        return push_event(event);
    }

    if (report.cluster_id == 0x0500U && report.attribute_id == 0x0002U) {
        uint32_t normalized_status = 0U;
        if (!decode_ias_zone_status(report, &normalized_status)) {
            return false;
        }

        core::CoreEvent event{};
        event.type = core::CoreEventType::kDeviceTelemetryUpdated;
        event.device_short_addr = report.short_addr;
        event.value_u32 = now_ms;
        event.telemetry_kind = core::CoreTelemetryKind::kContactIasZoneStatus;
        event.telemetry_i32 = static_cast<int32_t>(normalized_status);
        event.telemetry_valid = true;
        return push_event(event);
    }

    if (report.cluster_id == 0x0001U && report.attribute_id == 0x0021U) {
        int32_t battery_percent = 0;
        bool battery_valid = false;
        if (!decode_battery_percent(report, &battery_percent, &battery_valid)) {
            return false;
        }

        core::CoreEvent event{};
        event.type = core::CoreEventType::kDeviceTelemetryUpdated;
        event.device_short_addr = report.short_addr;
        event.value_u32 = now_ms;
        event.telemetry_kind = core::CoreTelemetryKind::kBatteryPercent;
        event.telemetry_i32 = battery_percent;
        event.telemetry_valid = battery_valid;
        return push_event(event);
    }

    if (report.cluster_id == 0x0001U && report.attribute_id == 0x0020U) {
        int32_t battery_mv = 0;
        bool battery_valid = false;
        if (!decode_battery_voltage_mv(report, &battery_mv, &battery_valid)) {
            return false;
        }

        core::CoreEvent event{};
        event.type = core::CoreEventType::kDeviceTelemetryUpdated;
        event.device_short_addr = report.short_addr;
        event.value_u32 = now_ms;
        event.telemetry_kind = core::CoreTelemetryKind::kBatteryVoltageMilliV;
        event.telemetry_i32 = battery_mv;
        event.telemetry_valid = battery_valid;
        return push_event(event);
    }

    uint32_t decoded_u32 = 0U;
    if (!decode_raw_u32_le(report, &decoded_u32)) {
        return false;
    }

    core::CoreEvent event{};
    event.type = core::CoreEventType::kAttributeReported;
    event.device_short_addr = report.short_addr;
    event.cluster_id = report.cluster_id;
    event.attribute_id = report.attribute_id;
    event.value_u32 = decoded_u32;
    event.value_bool = (decoded_u32 != 0U);
    return push_event(event);
}

bool ServiceRuntime::post_remove_device(
    uint32_t request_id,
    uint16_t short_addr,
    bool force_remove,
    uint32_t force_remove_timeout_ms) noexcept {
    if (request_id == 0 || short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U) {
        return false;
    }

    NetworkRequest request{};
    request.request_id = request_id;
    request.operation = NetworkOperationType::kRemoveDevice;
    request.device_short_addr = short_addr;
    request.force_remove = force_remove;
    request.force_remove_timeout_ms = clamp_force_remove_timeout(force_remove_timeout_ms);
    return network_manager_.enqueue_request(*this, request);
}

bool ServiceRuntime::get_join_window_status(uint16_t* seconds_left) const noexcept {
    if (seconds_left == nullptr) {
        return false;
    }

    const bool open = join_window_open_cache_.load(std::memory_order_acquire);
    const uint32_t cached_seconds = join_window_seconds_left_cache_.load(std::memory_order_acquire);
    const uint16_t local_seconds_left = cached_seconds > 0xFFFFU ? 0xFFFFU : static_cast<uint16_t>(cached_seconds);
    *seconds_left = open ? local_seconds_left : 0U;
    return open;
}

bool ServiceRuntime::build_devices_runtime_snapshot(
    const core::CoreState& state,
    uint32_t now_ms,
    DevicesRuntimeSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    const bool join_window_open = join_window_open_cache_.load(std::memory_order_acquire);
    const uint32_t cached_seconds = join_window_seconds_left_cache_.load(std::memory_order_acquire);
    const uint16_t join_window_seconds_left = cached_seconds > 0xFFFFU ? 0xFFFFU : static_cast<uint16_t>(cached_seconds);
    return device_manager_.build_runtime_snapshot(
        state,
        now_ms,
        join_window_open,
        join_window_seconds_left,
        out);
}

bool ServiceRuntime::build_devices_api_snapshot(uint32_t now_ms, DevicesApiSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    core::CoreRegistry::SnapshotRef snapshot{};
    if (!registry_->pin_current(&snapshot) || !snapshot.valid()) {
        return false;
    }

    DevicesRuntimeSnapshot runtime_snapshot{};
    const bool runtime_ok = build_devices_runtime_snapshot(*snapshot.state, now_ms, &runtime_snapshot);
    if (!runtime_ok) {
        registry_->release_snapshot(&snapshot);
        return false;
    }

    out->state = *snapshot.state;
    out->runtime = runtime_snapshot;
    registry_->release_snapshot(&snapshot);
    return true;
}

bool ServiceRuntime::capture_core_read_model(CoreReadModel* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    core::CoreRegistry::SnapshotRef snapshot{};
    if (!registry_->pin_current(&snapshot) || !snapshot.valid()) {
        return false;
    }

    out->revision = snapshot.state->revision;
    out->network_connected = snapshot.state->network_connected;
    out->last_command_status = snapshot.state->last_command_status;
    registry_->release_snapshot(&snapshot);
    return true;
}

void ServiceRuntime::publish_network_api_snapshot(const CoreReadModel& core_snapshot) noexcept {
    const uint32_t start_seq = network_api_snapshot_.seq.load(std::memory_order_relaxed);
    network_api_snapshot_.seq.store(start_seq + 1U, std::memory_order_release);
    network_api_snapshot_.revision.store(core_snapshot.revision, std::memory_order_relaxed);
    network_api_snapshot_.connected.store(core_snapshot.network_connected, std::memory_order_relaxed);
    network_api_snapshot_.refresh_requests.store(
        stats_.network_refresh_requests.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    network_api_snapshot_.current_backoff_ms.store(
        stats_.current_backoff_ms.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    network_api_snapshot_.seq.store(start_seq + 2U, std::memory_order_release);
}

void ServiceRuntime::publish_config_api_snapshot(const CoreReadModel& core_snapshot) noexcept {
    const uint32_t start_seq = config_api_snapshot_.seq.load(std::memory_order_relaxed);
    config_api_snapshot_.seq.store(start_seq + 1U, std::memory_order_release);
    config_api_snapshot_.revision.store(core_snapshot.revision, std::memory_order_relaxed);
    config_api_snapshot_.last_command_status.store(core_snapshot.last_command_status, std::memory_order_relaxed);
    config_api_snapshot_.command_timeout_ms.store(
        config_timeout_ms_cache_.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    config_api_snapshot_.max_command_retries.store(
        config_max_retries_cache_.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    config_api_snapshot_.autoconnect_failures.store(
        stats_.autoconnect_failures.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    config_api_snapshot_.seq.store(start_seq + 2U, std::memory_order_release);
}

void ServiceRuntime::sync_api_snapshots() noexcept {
    CoreReadModel core_snapshot{};
    if (!capture_core_read_model(&core_snapshot)) {
        return;
    }

    publish_network_api_snapshot(core_snapshot);
    publish_config_api_snapshot(core_snapshot);
}

bool ServiceRuntime::build_network_api_snapshot(NetworkApiSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    for (;;) {
        const uint32_t start_seq = network_api_snapshot_.seq.load(std::memory_order_acquire);
        if ((start_seq & 1U) != 0U) {
            continue;
        }

        NetworkApiSnapshot snapshot{};
        snapshot.revision = network_api_snapshot_.revision.load(std::memory_order_relaxed);
        snapshot.connected = network_api_snapshot_.connected.load(std::memory_order_relaxed);
        snapshot.refresh_requests = network_api_snapshot_.refresh_requests.load(std::memory_order_relaxed);
        snapshot.current_backoff_ms = network_api_snapshot_.current_backoff_ms.load(std::memory_order_relaxed);

        const uint32_t end_seq = network_api_snapshot_.seq.load(std::memory_order_acquire);
        if (start_seq == end_seq) {
            *out = snapshot;
            return true;
        }
    }
}

bool ServiceRuntime::build_config_api_snapshot(ConfigApiSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    for (;;) {
        const uint32_t start_seq = config_api_snapshot_.seq.load(std::memory_order_acquire);
        if ((start_seq & 1U) != 0U) {
            continue;
        }

        ConfigApiSnapshot snapshot{};
        snapshot.revision = config_api_snapshot_.revision.load(std::memory_order_relaxed);
        snapshot.last_command_status = static_cast<uint8_t>(
            config_api_snapshot_.last_command_status.load(std::memory_order_relaxed));
        snapshot.command_timeout_ms = config_api_snapshot_.command_timeout_ms.load(std::memory_order_relaxed);
        snapshot.max_command_retries = static_cast<uint8_t>(
            config_api_snapshot_.max_command_retries.load(std::memory_order_relaxed));
        snapshot.autoconnect_failures = config_api_snapshot_.autoconnect_failures.load(std::memory_order_relaxed);

        const uint32_t end_seq = config_api_snapshot_.seq.load(std::memory_order_acquire);
        if (start_seq == end_seq) {
            *out = snapshot;
            return true;
        }
    }
}

bool ServiceRuntime::get_force_remove_remaining_ms(
    uint16_t short_addr,
    uint32_t now_ms,
    uint32_t* remaining_ms) const noexcept {
    return device_manager_.get_force_remove_remaining_ms(short_addr, now_ms, remaining_ms);
}

bool ServiceRuntime::take_network_result(uint32_t request_id, NetworkResult* out) noexcept {
    RuntimeLockGuard guard(ingress_lock_);
    return take_network_result_locked(request_id, out);
}

bool ServiceRuntime::is_scan_request_queued(uint32_t request_id) const noexcept {
    if (request_id == 0) {
        return false;
    }
    return scan_manager_.is_request_queued(request_id) || network_manager_.is_scan_request_queued(request_id);
}

bool ServiceRuntime::is_scan_request_in_progress(uint32_t request_id) const noexcept {
    return scan_manager_.is_request_in_progress(request_id);
}

bool ServiceRuntime::initialize_hal_adapter() noexcept {
    if (!init_hal_event_adapter(*this)) {
        return false;
    }

    restore_core_state_pending_.store(true, std::memory_order_release);
    sync_api_snapshots();
    return true;
}

bool ServiceRuntime::ensure_wifi_mode_for_scan() noexcept {
    return connectivity_manager_.ensure_wifi_mode_for_scan();
}

bool ServiceRuntime::ensure_wifi_mode_for_sta_connect() noexcept {
    return connectivity_manager_.ensure_wifi_mode_for_sta_connect();
}

bool ServiceRuntime::start_provisioning_ap(const char* ssid, const char* password) noexcept {
    return connectivity_manager_.start_provisioning_ap(ssid, password);
}

ServiceRuntime::BootAutoconnectResult ServiceRuntime::autoconnect_from_saved_credentials() noexcept {
    const BootAutoconnectResult result = connectivity_manager_.autoconnect_from_saved_credentials(*this);
    sync_api_snapshots();
    return result;
}

bool ServiceRuntime::has_saved_wifi_credentials() noexcept {
    return connectivity_manager_.has_saved_wifi_credentials();
}

void ServiceRuntime::mark_wifi_credentials_available() noexcept {
    connectivity_manager_.mark_wifi_credentials_available();
}

bool ServiceRuntime::ensure_zigbee_started() noexcept {
    return connectivity_manager_.ensure_zigbee_started(*this);
}

bool ServiceRuntime::zigbee_started() const noexcept {
    return connectivity_manager_.zigbee_started();
}

bool ServiceRuntime::start() noexcept {
#ifdef ESP_PLATFORM
    if (runtime_task_handle_ != nullptr) {
        return true;
    }

    TaskHandle_t runtime_task = nullptr;
    const BaseType_t task_ok = xTaskCreate(
        &ServiceRuntime::runtime_task_entry,
        kRuntimeTaskName,
        kRuntimeTaskStackSize,
        this,
        kRuntimeTaskPriority,
        &runtime_task);
    if (task_ok != pdPASS) {
        runtime_task_handle_ = nullptr;
        SR_LOGI("Failed to create service runtime task");
        return false;
    }

    runtime_task_handle_ = runtime_task;

    TaskHandle_t scan_worker_task = nullptr;
    const BaseType_t scan_task_ok = xTaskCreate(
        &ScanManager::worker_task_entry,
        kScanWorkerTaskName,
        kScanWorkerTaskStackSize,
        this,
        kScanWorkerTaskPriority,
        &scan_worker_task);
    if (scan_task_ok != pdPASS) {
        vTaskDelete(runtime_task);
        runtime_task_handle_ = nullptr;
        SR_LOGI("Failed to create Wi-Fi scan worker task");
        return false;
    }

    scan_worker_task_handle_ = scan_worker_task;
    SR_LOGI("Service runtime task started");
    return true;
#else
    if (restore_core_state_pending_.exchange(false, std::memory_order_acq_rel)) {
        (void)restore_persisted_core_state();
    }
    return true;
#endif
}

void ServiceRuntime::apply_managers(const core::CoreEvent& event) noexcept {
    if (event.type == core::CoreEventType::kAttributeReported &&
        event.cluster_id == 0x0406U &&
        event.attribute_id == 0x0000U) {
        const bool occupied_raw = (event.value_u32 & 0x01U) != 0U;
        ReportingManager::OccupancyPolicy occupancy_policy{};
        occupancy_policy.debounce_ms = config_manager_.motion_occupancy_debounce_ms();
        occupancy_policy.hold_ms = config_manager_.motion_occupancy_hold_ms();

        core::CoreEvent domain_event{};
        if (reporting_manager_.normalize_occupancy_report(
                event.device_short_addr,
                occupied_raw,
                monotonic_now_ms(),
                occupancy_policy,
                &domain_event)) {
            if (!push_event(domain_event)) {
                (void)stats_.dropped_events.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    const ReportingManager::RuntimeActions reporting_actions = reporting_manager_.handle_event(event);
    if (reporting_actions.mark_degraded) {
        (void)stats_.reporting_failures.fetch_add(1, std::memory_order_relaxed);
    }
    if (reporting_actions.request_interview || reporting_actions.request_bind ||
        reporting_actions.request_configure_reporting) {
        // Retry/backoff orchestration is introduced in RPT-006.
        // For RPT-005 we keep the counter observable and deterministic.
        (void)stats_.reporting_retries.fetch_add(1, std::memory_order_relaxed);
    }
    stats_.stale_devices.store(reporting_manager_.degraded_count(), std::memory_order_relaxed);

    (void)device_manager_.handle_event(event);
    (void)network_manager_.handle_event(event);
    network_policy_manager_.maybe_request_auto_rejoin_window(
        *this,
        event.type,
        event.device_short_addr,
        monotonic_now_ms());
    stats_.network_refresh_requests.store(network_manager_.refresh_count(), std::memory_order_relaxed);
    event_bus_.publish(event);
}

void ServiceRuntime::execute_effects(const core::CoreEffectList& effects) noexcept {
    for (uint8_t i = 0; i < effects.count; ++i) {
        const core::CoreEffect& effect = effects.items[i];
        const bool ok = effect_executor_->execute(effect);
        (void)stats_.executed_effects.fetch_add(1, std::memory_order_relaxed);
        if (!ok) {
            (void)stats_.failed_effects.fetch_add(1, std::memory_order_relaxed);
        }

        if (effect.type == core::CoreEffectType::kPersistState) {
            (void)config_manager_.save();
            if (!persist_current_core_state()) {
                (void)stats_.failed_effects.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

std::size_t ServiceRuntime::process_pending() noexcept {
    std::size_t processed = 0;
    bool overall_progress = false;
    for (;;) {
        if (processed >= kMaxProcessedEventsPerCycle) {
            break;
        }

        bool made_progress = false;

        const uint32_t dropped_ingress = dropped_ingress_events_.exchange(0, std::memory_order_acq_rel);
        (void)stats_.dropped_events.fetch_add(dropped_ingress, std::memory_order_relaxed);
        overall_progress = overall_progress || (dropped_ingress != 0U);

        if (drain_nvs_writes()) {
            made_progress = true;
        }

        if (drain_config_writes()) {
            made_progress = true;
        }

        if (persistence_manager_.drain_reporting_profile_writes(*this)) {
            made_progress = true;
        }

        if (drain_network_requests()) {
            made_progress = true;
        }

        if (drain_command_requests()) {
            made_progress = true;
        }

        if (drain_command_results()) {
            made_progress = true;
        }

        core::CoreEvent event{};
        if (!pop_event(&event)) {
            if (!made_progress) {
                break;
            }
            overall_progress = true;
            continue;
        }

        ++processed;
        overall_progress = true;
        (void)stats_.processed_events.fetch_add(1, std::memory_order_relaxed);

        apply_managers(event);

        if (event.type == core::CoreEventType::kNetworkDown) {
            SR_LOGW("Network down detected, triggering automatic reconnection");
            (void)autoconnect_from_saved_credentials();
        }

        core::CoreRegistry::SnapshotRef prev_snapshot{};
        if (!registry_->pin_current(&prev_snapshot)) {
            (void)stats_.dropped_events.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        const core::CoreState prev = *prev_snapshot.state;
        registry_->release_snapshot(&prev_snapshot);
        const core::CoreReduceResult reduced = core::core_reduce(prev, event);

        if (reduced.next.revision != prev.revision) {
            if (!registry_->publish(reduced.next)) {
                (void)stats_.dropped_events.fetch_add(1, std::memory_order_relaxed);
            }

            if (event.type == core::CoreEventType::kDeviceJoined ||
                event.type == core::CoreEventType::kDeviceLeft) {
                SR_LOGI(
                    "Applied event=%u short_addr=0x%04x device_count=%u revision=%lu",
                    static_cast<unsigned>(event.type),
                    static_cast<unsigned>(event.device_short_addr),
                    static_cast<unsigned>(reduced.next.device_count),
                    static_cast<unsigned long>(reduced.next.revision));
            }
        }

        execute_effects(reduced.effects);
    }

    if (overall_progress) {
        sync_api_snapshots();
    }

    return processed;
}

std::size_t ServiceRuntime::tick(uint32_t now_ms) noexcept {
    stats_.stale_devices.store(reporting_manager_.degraded_count(), std::memory_order_relaxed);
    last_tick_ms_.store(now_ms, std::memory_order_release);
    process_zigbee_network_policy(now_ms);
    std::size_t queued = command_manager_.process_timeouts(*this, now_ms);

    std::array<uint16_t, core::kMaxDevices> stale_short_addrs{};
    const std::size_t stale_count =
        reporting_manager_.collect_stale_candidates(now_ms, ReportingManager::kDefaultMaxSilenceWindowMs, &stale_short_addrs);
    for (std::size_t i = 0; i < stale_count; ++i) {
        core::CoreEvent stale_event{};
        stale_event.type = core::CoreEventType::kDeviceStale;
        stale_event.device_short_addr = stale_short_addrs[i];
        if (push_event(stale_event)) {
            ++queued;
        } else {
            (void)stats_.dropped_events.fetch_add(1, std::memory_order_relaxed);
            (void)reporting_manager_.set_stale_pending(stale_short_addrs[i], false);
        }
    }

    queued += process_pending_sta_connect(now_ms);
    queued += process_force_remove_timeouts(now_ms);

    CoreReadModel core_snapshot{};
    const bool have_core_snapshot = capture_core_read_model(&core_snapshot);
    if (have_core_snapshot && !core_snapshot.network_connected &&
        !network_policy_manager_.has_pending_sta_connect() &&
        connectivity_manager_.can_attempt_autoconnect(now_ms, kMaxAutoconnectRetries)) {
        (void)autoconnect_from_saved_credentials();
    }

    sync_api_snapshots();
    return queued;
}

void ServiceRuntime::on_nvs_u32_written(const char* key, uint32_t value) noexcept {
    persistence_manager_.on_nvs_u32_written(*this, key, value);
}

ServiceRuntime::RuntimeStats ServiceRuntime::stats() const noexcept {
    RuntimeStats snapshot{};
    snapshot.processed_events = stats_.processed_events.load(std::memory_order_relaxed);
    snapshot.dropped_events = stats_.dropped_events.load(std::memory_order_relaxed);
    snapshot.executed_effects = stats_.executed_effects.load(std::memory_order_relaxed);
    snapshot.failed_effects = stats_.failed_effects.load(std::memory_order_relaxed);
    snapshot.command_retries = stats_.command_retries.load(std::memory_order_relaxed);
    snapshot.command_timeouts = stats_.command_timeouts.load(std::memory_order_relaxed);
    snapshot.reporting_retries = stats_.reporting_retries.load(std::memory_order_relaxed);
    snapshot.reporting_failures = stats_.reporting_failures.load(std::memory_order_relaxed);
    snapshot.stale_devices = stats_.stale_devices.load(std::memory_order_relaxed);
    snapshot.autoconnect_failures = stats_.autoconnect_failures.load(std::memory_order_relaxed);
    snapshot.current_backoff_ms = stats_.current_backoff_ms.load(std::memory_order_relaxed);
    snapshot.nvs_writes = stats_.nvs_writes.load(std::memory_order_relaxed);
    snapshot.last_nvs_revision = stats_.last_nvs_revision.load(std::memory_order_relaxed);
    snapshot.network_refresh_requests = stats_.network_refresh_requests.load(std::memory_order_relaxed);
    return snapshot;
}

ServiceRuntime::ConfigSnapshot ServiceRuntime::config_snapshot() const noexcept {
    ConfigSnapshot snapshot{};
    snapshot.command_timeout_ms = config_timeout_ms_cache_.load(std::memory_order_relaxed);
    snapshot.max_command_retries = static_cast<uint8_t>(config_max_retries_cache_.load(std::memory_order_relaxed));
    return snapshot;
}

core::CoreState ServiceRuntime::state() const noexcept {
    return registry_->snapshot_copy();
}

std::size_t ServiceRuntime::pending_events() const noexcept {
    std::size_t local_queue_count = 0;
    std::size_t local_network_result_count = 0;
    {
        RuntimeLockGuard guard(ingress_lock_);
        local_queue_count = queue_count_;
        local_network_result_count = network_result_count_;
    }

    return local_queue_count + command_manager_.pending_ingress_count() + persistence_manager_.pending_ingress_count() +
           network_manager_.pending_ingress_count() + local_network_result_count + scan_manager_.pending_ingress_count();
}

std::size_t ServiceRuntime::pending_commands() const noexcept {
    return command_manager_.pending_commands();
}

ConfigManager& ServiceRuntime::config_manager() noexcept {
    return config_manager_;
}

bool ServiceRuntime::drain_command_results() noexcept {
    return command_manager_.drain_command_results(*this);
}

bool ServiceRuntime::drain_command_requests() noexcept {
    return command_manager_.drain_command_requests(*this);
}

bool ServiceRuntime::drain_nvs_writes() noexcept {
    return persistence_manager_.drain_nvs_writes(*this);
}

bool ServiceRuntime::drain_config_writes() noexcept {
    return persistence_manager_.drain_config_writes(*this);
}

bool ServiceRuntime::drain_network_requests() noexcept {
    return network_manager_.drain_requests(*this);
}

uint32_t ServiceRuntime::monotonic_now_ms() const noexcept {
    const uint32_t last_tick = last_tick_ms_.load(std::memory_order_acquire);
    if (last_tick != 0U) {
        return last_tick;
    }
#ifdef ESP_PLATFORM
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#else
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
#endif
}

bool ServiceRuntime::is_duplicate_join_candidate(uint16_t short_addr, uint32_t now_ms) noexcept {
    return device_manager_.is_duplicate_join_candidate(short_addr, now_ms);
}

void ServiceRuntime::maybe_auto_close_join_window_after_first_join(uint16_t short_addr) noexcept {
#ifndef ESP_PLATFORM
    (void)short_addr;
#endif

    uint16_t join_window_seconds_left = 0U;
    if (!get_join_window_status(&join_window_seconds_left)) {
        return;
    }

    if (hal_zigbee_close_network() != HAL_ZIGBEE_STATUS_OK) {
        SR_LOGI("Failed to auto-close join window after join short_addr=0x%04x", static_cast<unsigned>(short_addr));
        return;
    }

    network_policy_manager_.on_join_window_force_closed(*this);

    SR_LOGI(
        "Auto-closed join window after first join short_addr=0x%04x seconds_left=%u",
        static_cast<unsigned>(short_addr),
        static_cast<unsigned>(join_window_seconds_left));
}

bool ServiceRuntime::request_join_window_open(uint16_t duration_seconds) noexcept {
    return network_policy_manager_.request_join_window_open(*this, duration_seconds, monotonic_now_ms());
}

void ServiceRuntime::process_zigbee_network_policy(uint32_t now_ms) noexcept {
    network_policy_manager_.process_zigbee_join_window_policy(*this, now_ms);
}

bool ServiceRuntime::schedule_force_remove(uint16_t short_addr, uint32_t deadline_ms) noexcept {
    return device_manager_.schedule_force_remove(short_addr, deadline_ms);
}

std::size_t ServiceRuntime::process_pending_sta_connect(uint32_t now_ms) noexcept {
    CoreReadModel core_snapshot{};
    if (!capture_core_read_model(&core_snapshot)) {
        return 0;
    }

    return network_policy_manager_.process_pending_sta_connect(*this, core_snapshot.network_connected, now_ms);
}

void ServiceRuntime::note_dropped_event() noexcept {
    (void)stats_.dropped_events.fetch_add(1, std::memory_order_relaxed);
}

std::size_t ServiceRuntime::process_force_remove_timeouts(uint32_t now_ms) noexcept {
    std::array<uint16_t, DeviceManager::kMaxPendingForceRemove> expired_short_addrs{};
    const std::size_t expired_count = device_manager_.collect_expired_force_remove(now_ms, &expired_short_addrs);

    std::size_t queued = 0;

    for (std::size_t i = 0; i < expired_count; ++i) {
        const uint16_t short_addr = expired_short_addrs[i];
        core::CoreEvent fallback_event{};
        fallback_event.type = core::CoreEventType::kDeviceLeft;
        fallback_event.device_short_addr = short_addr;
        if (push_event(fallback_event)) {
            ++queued;
            SR_LOGI(
                "Force-remove timeout reached, posted fallback kDeviceLeft short_addr=0x%04x",
                static_cast<unsigned>(short_addr));
        } else {
            (void)stats_.dropped_events.fetch_add(1, std::memory_order_relaxed);
            SR_LOGI(
                "Force-remove timeout reached but queue full, short_addr=0x%04x",
                static_cast<unsigned>(short_addr));
        }
    }

    return queued;
}

}  // namespace service
