/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#else
#include <mutex>
#endif

namespace service {

class RuntimeLock {
public:
    RuntimeLock() noexcept {
#ifdef ESP_PLATFORM
        handle_ = xSemaphoreCreateMutexStatic(&storage_);
#endif
    }

    ~RuntimeLock() noexcept = default;

    RuntimeLock(const RuntimeLock&) = delete;
    RuntimeLock& operator=(const RuntimeLock&) = delete;

    void lock() noexcept {
#ifdef ESP_PLATFORM
        if (handle_ != nullptr) {
            (void)xSemaphoreTake(handle_, portMAX_DELAY);
        }
#else
        mutex_.lock();
#endif
    }

    void unlock() noexcept {
#ifdef ESP_PLATFORM
        if (handle_ != nullptr) {
            (void)xSemaphoreGive(handle_);
        }
#else
        mutex_.unlock();
#endif
    }

private:
#ifdef ESP_PLATFORM
    StaticSemaphore_t storage_{};
    SemaphoreHandle_t handle_{nullptr};
#else
    std::mutex mutex_{};
#endif
};

class RuntimeLockGuard {
public:
    explicit RuntimeLockGuard(RuntimeLock& lock) noexcept : lock_(lock) {
        lock_.lock();
    }

    ~RuntimeLockGuard() noexcept {
        lock_.unlock();
    }

    RuntimeLockGuard(const RuntimeLockGuard&) = delete;
    RuntimeLockGuard& operator=(const RuntimeLockGuard&) = delete;

private:
    RuntimeLock& lock_;
};

}  // namespace service
