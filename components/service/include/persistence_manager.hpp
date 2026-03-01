/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace service {

class ServiceRuntime;

class PersistenceManager {
public:
    static constexpr std::size_t kNvsWriteQueueCapacity = 16;
    static constexpr std::size_t kConfigWriteQueueCapacity = 8;

    bool post_config_write(
        ServiceRuntime& runtime,
        bool set_timeout_ms,
        uint32_t timeout_ms,
        bool set_max_retries,
        uint8_t max_retries) noexcept;
    void on_nvs_u32_written(ServiceRuntime& runtime, const char* key, uint32_t value) noexcept;

    bool drain_nvs_writes(ServiceRuntime& runtime) noexcept;
    bool drain_config_writes(ServiceRuntime& runtime) noexcept;
    std::size_t pending_ingress_count() const noexcept;

private:
    struct NvsWriteNotification {
        bool is_core_revision{false};
        uint32_t value{0};
    };

    struct ConfigWriteNotification {
        bool set_timeout_ms{false};
        uint32_t timeout_ms{0};
        bool set_max_retries{false};
        uint8_t max_retries{0};
    };

    class SpinLockGuard {
    public:
        explicit SpinLockGuard(std::atomic_flag& lock) noexcept;
        ~SpinLockGuard() noexcept;

    private:
        std::atomic_flag& lock_;
    };

    bool queue_nvs_write(const NvsWriteNotification& notification) noexcept;
    bool pop_nvs_write(NvsWriteNotification* out) noexcept;
    bool queue_config_write(const ConfigWriteNotification& notification) noexcept;
    bool pop_config_write(ConfigWriteNotification* out) noexcept;

    mutable std::atomic_flag queue_lock_ = ATOMIC_FLAG_INIT;

    std::array<NvsWriteNotification, kNvsWriteQueueCapacity> nvs_write_queue_{};
    std::size_t nvs_write_head_{0};
    std::size_t nvs_write_tail_{0};
    std::size_t nvs_write_count_{0};

    std::array<ConfigWriteNotification, kConfigWriteQueueCapacity> config_write_queue_{};
    std::size_t config_write_head_{0};
    std::size_t config_write_tail_{0};
    std::size_t config_write_count_{0};
};

}  // namespace service
