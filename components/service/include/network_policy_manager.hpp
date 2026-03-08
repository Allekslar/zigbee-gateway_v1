/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

#include "core_events.hpp"
#include "network_manager.hpp"

namespace service {

class ServiceRuntime;

class NetworkPolicyManager {
public:
    bool has_pending_sta_connect() const noexcept;
    void arm_pending_sta_connect(uint32_t request_id, bool saved, const char* ssid, uint32_t deadline_ms) noexcept;
    std::size_t process_pending_sta_connect(ServiceRuntime& runtime, bool connected, uint32_t now_ms) noexcept;

    bool request_join_window_open(ServiceRuntime& runtime, uint16_t duration_seconds, uint32_t now_ms) noexcept;
    void maybe_request_auto_rejoin_window(
        ServiceRuntime& runtime,
        core::CoreEventType event_type,
        uint16_t short_addr,
        uint32_t now_ms) noexcept;
    void process_zigbee_join_window_policy(ServiceRuntime& runtime, uint32_t now_ms) noexcept;
    void on_zigbee_started(ServiceRuntime& runtime, uint32_t now_ms) noexcept;
    void on_join_window_force_closed(ServiceRuntime& runtime) noexcept;

private:
    static constexpr uint32_t kZigbeeFormationRetryMs = 1000U;
    static constexpr uint16_t kAutoRejoinWindowSeconds = 30U;
    static constexpr uint32_t kAutoRejoinCooldownMs = 15000U;

    static bool is_deadline_reached(uint32_t now_ms, uint32_t deadline_ms) noexcept;

    PendingStaConnect pending_sta_connect_{};
    bool join_window_explicit_expected_{false};
    uint16_t pending_join_window_seconds_{0};
    uint32_t zigbee_next_formation_retry_ms_{0};
    uint16_t zigbee_formation_retry_count_{0};
    bool zigbee_join_window_was_open_{false};
    uint32_t auto_rejoin_next_open_ms_{0};
};

}  // namespace service
