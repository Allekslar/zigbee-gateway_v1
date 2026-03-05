/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace core {

inline constexpr uint32_t kNoCorrelationId = 0U;
inline constexpr uint16_t kUnknownDeviceShortAddr = 0xFFFFU;

enum class CoreEventType : uint8_t {
    kUnknown = 0,
    kDeviceJoined,
    kDeviceLeft,
    kNetworkUp,
    kNetworkDown,
    kAttributeReported,
    kCommandSetDevicePowerRequested,
    kCommandRefreshNetworkRequested,
    kCommandResultSuccess,
    kCommandResultTimeout,
    kCommandResultFailed,
    kDeviceInterviewCompleted,
    kDeviceBindingReady,
    kDeviceReportingConfigured,
    kDeviceTelemetryUpdated,
    kDeviceStale,
    kDeviceAdded = kDeviceJoined,
    kDeviceRemoved = kDeviceLeft,
};

enum class CoreTelemetryKind : uint8_t {
    kNone = 0,
    kTemperatureCentiC,
    kOccupancy,
    kContactIasZoneStatus,
    kBatteryPercent,
    kBatteryVoltageMilliV,
    kLqi,
    kRssiDbm,
};

struct CoreEvent {
    CoreEventType type{CoreEventType::kUnknown};
    uint32_t correlation_id{kNoCorrelationId};
    uint16_t device_short_addr{kUnknownDeviceShortAddr};
    uint16_t cluster_id{0};
    uint16_t attribute_id{0};
    uint32_t value_u32{0};
    bool value_bool{false};
    CoreTelemetryKind telemetry_kind{CoreTelemetryKind::kNone};
    int32_t telemetry_i32{0};
    bool telemetry_valid{false};
};

}  // namespace core
