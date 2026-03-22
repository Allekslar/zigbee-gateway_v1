/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "tuya_plugin.hpp"

namespace service {

inline constexpr std::size_t kTuyaInitMaxDevices = 16U;
inline constexpr uint8_t kTuyaInitMaxRetries = 3U;
inline constexpr uint32_t kTuyaInitStepTimeoutMs = 5000U;
inline constexpr uint32_t kTuyaInitCorrelationIdBase = 0x80000000U;

enum class TuyaInitStatus : uint8_t {
    kNotStarted = 0,
    kReady = 1,
    kInitPending = 2,
    kWaitingAck = 3,
    kDegraded = 4,
    kFailed = 5,
};

struct TuyaInitEntry {
    bool in_use{false};
    uint16_t short_addr{0xFFFFU};
    TuyaInitStatus status{TuyaInitStatus::kNotStarted};
    uint8_t current_step{0};
    uint8_t retries_left{kTuyaInitMaxRetries};
    uint32_t deadline_ms{0};
    uint32_t correlation_id{0};
    TuyaInitPlan plan{};
};

struct TuyaInitAction {
    bool pending{false};
    uint16_t short_addr{0xFFFFU};
    uint8_t endpoint{1};
    uint8_t dp_id{0};
    TuyaDpType dp_type{TuyaDpType::kRaw};
    uint8_t value[kTuyaDpCommandValueMaxLen]{};
    uint8_t value_len{0};
    uint32_t correlation_id{0};
};

class TuyaInitCoordinator {
public:
    void notify_device_resolved(uint16_t short_addr, const TuyaInitPlan& plan) noexcept;
    void notify_device_removed(uint16_t short_addr) noexcept;

    TuyaInitAction tick(uint32_t now_ms) noexcept;
    bool notify_ack(uint32_t correlation_id, bool success) noexcept;

    TuyaInitStatus status(uint16_t short_addr) const noexcept;
    void clear() noexcept;

private:
    TuyaInitEntry* find(uint16_t short_addr) noexcept;
    const TuyaInitEntry* find(uint16_t short_addr) const noexcept;
    TuyaInitEntry* allocate(uint16_t short_addr) noexcept;
    TuyaInitEntry* find_by_correlation_id(uint32_t correlation_id) noexcept;
    uint32_t next_correlation_id() noexcept;

    std::array<TuyaInitEntry, kTuyaInitMaxDevices> entries_{};
    uint32_t correlation_id_counter_{kTuyaInitCorrelationIdBase};
};

}  // namespace service
