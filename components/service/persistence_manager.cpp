/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "persistence_manager.hpp"

#include <cstring>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <thread>
#endif

#include "service_runtime.hpp"

namespace service {

PersistenceManager::SpinLockGuard::SpinLockGuard(std::atomic_flag& lock) noexcept : lock_(lock) {
    uint32_t spin_count = 0U;
#ifndef ESP_PLATFORM
    (void)spin_count;
#endif
    while (lock_.test_and_set(std::memory_order_acquire)) {
#ifdef ESP_PLATFORM
        if ((++spin_count & 0x7U) == 0U) {
            vTaskDelay(1);
        } else {
            taskYIELD();
        }
#else
        std::this_thread::yield();
#endif
    }
}

PersistenceManager::SpinLockGuard::~SpinLockGuard() noexcept {
    lock_.clear(std::memory_order_release);
}

bool PersistenceManager::queue_nvs_write(const NvsWriteNotification& notification) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (nvs_write_count_ >= kNvsWriteQueueCapacity) {
        return false;
    }

    nvs_write_queue_[nvs_write_tail_] = notification;
    nvs_write_tail_ = (nvs_write_tail_ + 1U) % kNvsWriteQueueCapacity;
    ++nvs_write_count_;
    return true;
}

bool PersistenceManager::pop_nvs_write(NvsWriteNotification* out) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (out == nullptr || nvs_write_count_ == 0) {
        return false;
    }

    *out = nvs_write_queue_[nvs_write_head_];
    nvs_write_head_ = (nvs_write_head_ + 1U) % kNvsWriteQueueCapacity;
    --nvs_write_count_;
    return true;
}

bool PersistenceManager::queue_config_write(const ConfigWriteNotification& notification) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (config_write_count_ >= kConfigWriteQueueCapacity) {
        return false;
    }

    config_write_queue_[config_write_tail_] = notification;
    config_write_tail_ = (config_write_tail_ + 1U) % kConfigWriteQueueCapacity;
    ++config_write_count_;
    return true;
}

bool PersistenceManager::pop_config_write(ConfigWriteNotification* out) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (out == nullptr || config_write_count_ == 0) {
        return false;
    }

    *out = config_write_queue_[config_write_head_];
    config_write_head_ = (config_write_head_ + 1U) % kConfigWriteQueueCapacity;
    --config_write_count_;
    return true;
}

bool PersistenceManager::queue_reporting_profile_write(const ReportingProfileWriteNotification& notification) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (reporting_profile_write_count_ >= kConfigWriteQueueCapacity) {
        return false;
    }

    reporting_profile_write_queue_[reporting_profile_write_tail_] = notification;
    reporting_profile_write_tail_ = (reporting_profile_write_tail_ + 1U) % kConfigWriteQueueCapacity;
    ++reporting_profile_write_count_;
    return true;
}

bool PersistenceManager::pop_reporting_profile_write(ReportingProfileWriteNotification* out) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (out == nullptr || reporting_profile_write_count_ == 0) {
        return false;
    }

    *out = reporting_profile_write_queue_[reporting_profile_write_head_];
    reporting_profile_write_head_ = (reporting_profile_write_head_ + 1U) % kConfigWriteQueueCapacity;
    --reporting_profile_write_count_;
    return true;
}

bool PersistenceManager::post_config_write(
    ServiceRuntime& runtime,
    bool set_timeout_ms,
    uint32_t timeout_ms,
    bool set_max_retries,
    uint8_t max_retries) noexcept {
    if (!set_timeout_ms && !set_max_retries) {
        return false;
    }

    ConfigWriteNotification notification{};
    notification.set_timeout_ms = set_timeout_ms;
    notification.timeout_ms = timeout_ms;
    notification.set_max_retries = set_max_retries;
    notification.max_retries = max_retries;

    if (!queue_config_write(notification)) {
        (void)runtime.dropped_ingress_events_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    return true;
}

bool PersistenceManager::post_reporting_profile_write(
    ServiceRuntime& runtime,
    const ConfigManager::ReportingProfile& profile) noexcept {
    ReportingProfileWriteNotification notification{};
    notification.profile = profile;
    if (!queue_reporting_profile_write(notification)) {
        (void)runtime.dropped_ingress_events_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void PersistenceManager::on_nvs_u32_written(ServiceRuntime& runtime, const char* key, uint32_t value) noexcept {
    NvsWriteNotification notification{};
    if (key != nullptr && std::strcmp(key, "core_rev") == 0) {
        notification.is_core_revision = true;
        notification.value = value;
    }

    if (!queue_nvs_write(notification)) {
        (void)runtime.dropped_ingress_events_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool PersistenceManager::drain_nvs_writes(ServiceRuntime& runtime) noexcept {
    bool drained = false;
    NvsWriteNotification notification{};

    while (pop_nvs_write(&notification)) {
        drained = true;
        (void)runtime.stats_.nvs_writes.fetch_add(1, std::memory_order_relaxed);
        if (notification.is_core_revision) {
            runtime.stats_.last_nvs_revision.store(notification.value, std::memory_order_relaxed);
        }
    }

    return drained;
}

bool PersistenceManager::drain_config_writes(ServiceRuntime& runtime) noexcept {
    bool drained = false;
    ConfigWriteNotification notification{};

    while (pop_config_write(&notification)) {
        drained = true;

        bool changed = false;
        if (notification.set_timeout_ms) {
            changed = runtime.config_manager_.set_command_timeout_ms(notification.timeout_ms) || changed;
        }

        if (notification.set_max_retries) {
            changed = runtime.config_manager_.set_max_command_retries(notification.max_retries) || changed;
        }

        if (changed) {
            (void)runtime.config_manager_.save();
        } else {
            (void)runtime.stats_.dropped_events.fetch_add(1, std::memory_order_relaxed);
        }

        runtime.config_timeout_ms_cache_.store(
            runtime.config_manager_.command_timeout_ms(),
            std::memory_order_relaxed);
        runtime.config_max_retries_cache_.store(
            runtime.config_manager_.max_command_retries(),
            std::memory_order_relaxed);
    }

    return drained;
}

bool PersistenceManager::drain_reporting_profile_writes(ServiceRuntime& runtime) noexcept {
    bool drained = false;
    ReportingProfileWriteNotification notification{};

    while (pop_reporting_profile_write(&notification)) {
        drained = true;
        const bool changed = runtime.config_manager_.set_reporting_profile(notification.profile);
        if (changed) {
            (void)runtime.config_manager_.save();
        } else {
            (void)runtime.stats_.dropped_events.fetch_add(1, std::memory_order_relaxed);
        }
    }

    return drained;
}

std::size_t PersistenceManager::pending_ingress_count() const noexcept {
    SpinLockGuard guard(queue_lock_);
    return nvs_write_count_ + config_write_count_ + reporting_profile_write_count_;
}

}  // namespace service
