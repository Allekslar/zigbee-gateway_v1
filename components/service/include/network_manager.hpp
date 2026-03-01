/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core_events.hpp"

namespace service {

class ServiceRuntime;

inline constexpr std::size_t kNetworkScanMaxRecords = 16;
inline constexpr uint32_t kStaConnectAsyncTimeoutMs = 12000U;

enum class NetworkOperationType : uint8_t {
    kUnknown = 0,
    kScan = 1,
    kConnect = 2,
    kCredentialsStatus = 3,
    kOpenJoinWindow = 4,
    kCredentialsRawDebug = 5,
    kRemoveDevice = 6,
};

enum class NetworkOperationStatus : uint8_t {
    kOk = 0,
    kInvalidArgument = 1,
    kNoCapacity = 2,
    kHalFailed = 3,
};

struct NetworkScanRecord {
    char ssid[33]{};
    int8_t rssi{0};
    bool is_open{false};
};

struct NetworkRequest {
    uint32_t request_id{0};
    NetworkOperationType operation{NetworkOperationType::kUnknown};
    bool force_remove{false};
    bool save_credentials{false};
    uint32_t force_remove_timeout_ms{0};
    uint16_t join_window_seconds{0};
    uint16_t device_short_addr{core::kUnknownDeviceShortAddr};
    char ssid[33]{};
    char password[65]{};
};

struct NetworkResult {
    uint32_t request_id{0};
    NetworkOperationType operation{NetworkOperationType::kUnknown};
    NetworkOperationStatus status{NetworkOperationStatus::kInvalidArgument};
    bool force_remove{false};
    bool saved{false};
    bool has_password{false};
    bool debug_ssid_present{false};
    bool debug_password_present{false};
    uint8_t debug_ssid_len{0};
    uint8_t debug_password_len{0};
    uint32_t force_remove_timeout_ms{0};
    uint16_t join_window_seconds{0};
    uint16_t device_short_addr{core::kUnknownDeviceShortAddr};
    uint8_t scan_count{0};
    std::array<NetworkScanRecord, kNetworkScanMaxRecords> scan_records{};
    char ssid[33]{};
    char debug_ssid[33]{};
};

struct PendingStaConnect {
    bool in_use{false};
    bool saved{false};
    uint32_t request_id{0};
    uint32_t deadline_ms{0};
    char ssid[33]{};
};

class NetworkManager {
public:
    static constexpr std::size_t kRequestQueueCapacity = 8;

    bool handle_event(const core::CoreEvent& event) noexcept;
    bool refresh_requested() const noexcept;
    void clear_refresh_requested() noexcept;
    uint32_t refresh_count() const noexcept;
    bool enqueue_request(ServiceRuntime& runtime, const NetworkRequest& request) noexcept;
    bool drain_requests(ServiceRuntime& runtime) noexcept;
    bool is_scan_request_queued(uint32_t request_id) const noexcept;
    std::size_t pending_ingress_count() const noexcept;

private:
    class SpinLockGuard {
    public:
        explicit SpinLockGuard(std::atomic_flag& lock) noexcept;
        ~SpinLockGuard() noexcept;

    private:
        std::atomic_flag& lock_;
    };

    bool pop_request(NetworkRequest* out) noexcept;
    bool handle_request(
        ServiceRuntime& runtime,
        const NetworkRequest& request,
        NetworkResult* result,
        bool* queue_result) noexcept;
    bool handle_scan(
        ServiceRuntime& runtime,
        const NetworkRequest& request,
        NetworkResult* result,
        bool* queue_result) noexcept;
    bool handle_connect(
        ServiceRuntime& runtime,
        const NetworkRequest& request,
        NetworkResult* result,
        bool* queue_result) noexcept;
    bool handle_credentials_status(
        const NetworkRequest& request,
        NetworkResult* result) noexcept;
    bool handle_credentials_raw_debug(
        const NetworkRequest& request,
        NetworkResult* result) noexcept;
    bool handle_open_join_window(
        ServiceRuntime& runtime,
        const NetworkRequest& request,
        NetworkResult* result) noexcept;
    bool handle_remove_device(
        ServiceRuntime& runtime,
        const NetworkRequest& request,
        NetworkResult* result) noexcept;

    bool refresh_requested_{false};
    uint32_t refresh_count_{0};

    mutable std::atomic_flag queue_lock_ = ATOMIC_FLAG_INIT;
    std::array<NetworkRequest, kRequestQueueCapacity> request_queue_{};
    std::size_t request_head_{0};
    std::size_t request_tail_{0};
    std::size_t request_count_{0};
};

}  // namespace service
