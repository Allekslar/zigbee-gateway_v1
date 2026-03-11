/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "zigbee_lifecycle_coordinator.hpp"

#include "device_manager.hpp"
#include "hal_zigbee.h"
#include "log_tags.h"
#include "network_policy_manager.hpp"
#include "service_runtime.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

namespace service {

namespace {

#ifdef ESP_PLATFORM
constexpr const char* kTag = LOG_TAG_SERVICE_RUNTIME;
#define ZLC_LOGI(...) ESP_LOGI(kTag, __VA_ARGS__)
#else
#define ZLC_LOGI(...) ((void)0)
#endif

}  // namespace

ZigbeeLifecycleCoordinator::ZigbeeLifecycleCoordinator(
    NetworkPolicyManager& network_policy_manager,
    DeviceManager& device_manager) noexcept
    : device_manager_(&device_manager), network_policy_manager_(&network_policy_manager) {}

void ZigbeeLifecycleCoordinator::set_join_window_cache(bool open, uint16_t seconds_left) noexcept {
    join_window_open_cache_.store(open, std::memory_order_release);
    join_window_seconds_left_cache_.store(open ? static_cast<uint32_t>(seconds_left) : 0U, std::memory_order_release);
}

bool ZigbeeLifecycleCoordinator::get_join_window_status(uint16_t* seconds_left) const noexcept {
    if (seconds_left == nullptr) {
        return false;
    }

    const bool open = join_window_open_cache_.load(std::memory_order_acquire);
    const uint32_t cached_seconds = join_window_seconds_left_cache_.load(std::memory_order_acquire);
    const uint16_t local_seconds_left = cached_seconds > 0xFFFFU ? 0xFFFFU : static_cast<uint16_t>(cached_seconds);
    *seconds_left = open ? local_seconds_left : 0U;
    return open;
}

bool ZigbeeLifecycleCoordinator::request_join_window_open(
    ServiceRuntime& runtime,
    uint16_t duration_seconds,
    uint32_t now_ms) noexcept {
    return network_policy_manager_->request_join_window_open(runtime, duration_seconds, now_ms);
}

void ZigbeeLifecycleCoordinator::process_join_window_policy(ServiceRuntime& runtime, uint32_t now_ms) noexcept {
    network_policy_manager_->process_zigbee_join_window_policy(runtime, now_ms);
}

bool ZigbeeLifecycleCoordinator::handle_join_candidate(
    ServiceRuntime& runtime,
    uint16_t short_addr,
    uint32_t now_ms) noexcept {
    if (short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U) {
        return false;
    }

    if (device_manager_->is_duplicate_join_candidate(short_addr, now_ms)) {
        ZLC_LOGI(
            "Suppress duplicate join candidate short_addr=0x%04x window_ms=%lu",
            static_cast<unsigned>(short_addr),
            static_cast<unsigned long>(DeviceManager::kJoinDedupWindowMs));
        return true;
    }

    core::CoreEvent event{};
    event.type = core::CoreEventType::kDeviceJoined;
    event.device_short_addr = short_addr;
    if (!runtime.push_event(event)) {
        return false;
    }

    maybe_auto_close_join_window_after_first_join(runtime, short_addr);
    return true;
}

void ZigbeeLifecycleCoordinator::maybe_auto_close_join_window_after_first_join(
    ServiceRuntime& runtime,
    uint16_t short_addr) noexcept {
#ifndef ESP_PLATFORM
    (void)short_addr;
#endif

    uint16_t join_window_seconds_left = 0U;
    if (!get_join_window_status(&join_window_seconds_left)) {
        return;
    }

    if (hal_zigbee_close_network() != HAL_ZIGBEE_STATUS_OK) {
        ZLC_LOGI("Failed to auto-close join window after join short_addr=0x%04x", static_cast<unsigned>(short_addr));
        return;
    }

    network_policy_manager_->on_join_window_force_closed(runtime);

    ZLC_LOGI(
        "Auto-closed join window after first join short_addr=0x%04x seconds_left=%u",
        static_cast<unsigned>(short_addr),
        static_cast<unsigned>(join_window_seconds_left));
}

}  // namespace service
