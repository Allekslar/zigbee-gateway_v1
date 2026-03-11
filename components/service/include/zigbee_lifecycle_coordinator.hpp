/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstdint>

namespace service {

class DeviceManager;
class NetworkPolicyManager;
class ServiceRuntime;

class ZigbeeLifecycleCoordinator {
public:
    ZigbeeLifecycleCoordinator(NetworkPolicyManager& network_policy_manager, DeviceManager& device_manager) noexcept;

    void set_join_window_cache(bool open, uint16_t seconds_left) noexcept;
    bool get_join_window_status(uint16_t* seconds_left) const noexcept;
    bool request_join_window_open(ServiceRuntime& runtime, uint16_t duration_seconds, uint32_t now_ms) noexcept;
    void process_join_window_policy(ServiceRuntime& runtime, uint32_t now_ms) noexcept;
    bool handle_join_candidate(ServiceRuntime& runtime, uint16_t short_addr, uint32_t now_ms) noexcept;
    void maybe_auto_close_join_window_after_first_join(ServiceRuntime& runtime, uint16_t short_addr) noexcept;

private:
    DeviceManager* device_manager_{nullptr};
    NetworkPolicyManager* network_policy_manager_{nullptr};
    std::atomic<bool> join_window_open_cache_{false};
    std::atomic<uint32_t> join_window_seconds_left_cache_{0};
};

}  // namespace service
