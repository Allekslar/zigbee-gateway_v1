/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

namespace service {

inline constexpr std::size_t kServiceMaxDevices = 64U;
inline constexpr uint16_t kUnknownShortAddr = 0xFFFFU;

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

}  // namespace service
