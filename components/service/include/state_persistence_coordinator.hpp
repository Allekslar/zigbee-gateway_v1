/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "core_registry.hpp"

namespace service {

class StatePersistenceCoordinator {
public:
    enum class FlushResult : uint8_t {
        kNoop = 0,
        kFlushed,
        kFailed,
    };

    explicit StatePersistenceCoordinator(core::CoreRegistry& registry) noexcept;

    void mark_restore_pending() noexcept;
    bool consume_restore_pending() noexcept;
    void note_persist_state_requested() noexcept;
    FlushResult flush_if_needed() noexcept;
    bool persist_current_core_state() noexcept;
    bool restore_persisted_core_state() noexcept;

private:
    struct PersistedCoreStateStorage {
        alignas(core::CoreState) std::array<uint8_t, sizeof(core::CoreState) + sizeof(uint32_t) * 2U> bytes{};
    };

    core::CoreRegistry* registry_{nullptr};
    mutable PersistedCoreStateStorage persisted_core_state_storage_{};
    std::atomic<bool> restore_core_state_pending_{false};
    std::atomic<bool> persist_core_state_pending_{false};
};

}  // namespace service
