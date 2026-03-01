/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "core_state.hpp"

namespace core {

class CoreRegistry {
public:
    using SnapshotId = uint32_t;
    static constexpr std::size_t kSnapshotCount = 8;
    static constexpr uint32_t kInvalidSlotIndex = std::numeric_limits<uint32_t>::max();

    struct SnapshotRef {
        const CoreState* state{nullptr};
        SnapshotId id{0};
        uint32_t slot_index{kInvalidSlotIndex};

        bool valid() const noexcept {
            return state != nullptr && slot_index != kInvalidSlotIndex;
        }
    };

    CoreRegistry() noexcept;

    bool pin_current(SnapshotRef* out) const noexcept;
    void release_snapshot(SnapshotRef* snapshot) const noexcept;
    CoreState snapshot_copy() const noexcept;
    SnapshotId current_snapshot_id() const noexcept;
    uint32_t current_revision() const noexcept;
    bool publish(const CoreState& state) noexcept;

private:
    struct SnapshotSlot {
        SnapshotId id{0};
        CoreState state{};
    };

    std::array<SnapshotSlot, kSnapshotCount> slots_{};
    mutable std::array<std::atomic<uint32_t>, kSnapshotCount> slot_pin_count_{};
    std::atomic<uint32_t> active_index_{0};
    std::atomic<SnapshotId> active_snapshot_id_{0};
    uint32_t write_index_{0};
    SnapshotId next_snapshot_id_{1};
};

}  // namespace core
