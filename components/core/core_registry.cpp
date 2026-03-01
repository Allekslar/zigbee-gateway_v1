/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "core_registry.hpp"

namespace core {

CoreRegistry::CoreRegistry() noexcept {
    slots_[0].id = 0;
    slots_[0].state = CoreState{};
}

bool CoreRegistry::pin_current(SnapshotRef* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    for (;;) {
        const uint32_t pinned_index = active_index_.load(std::memory_order_acquire);
        slot_pin_count_[pinned_index].fetch_add(1, std::memory_order_acq_rel);

        const uint32_t observed_index = active_index_.load(std::memory_order_acquire);
        if (observed_index == pinned_index) {
            out->state = &slots_[pinned_index].state;
            out->id = slots_[pinned_index].id;
            out->slot_index = pinned_index;
            return true;
        }

        (void)slot_pin_count_[pinned_index].fetch_sub(1, std::memory_order_acq_rel);
    }
}

void CoreRegistry::release_snapshot(SnapshotRef* snapshot) const noexcept {
    if (snapshot == nullptr || !snapshot->valid()) {
        return;
    }

    const uint32_t slot_index = snapshot->slot_index;
    if (slot_index < kSnapshotCount) {
        const uint32_t prev = slot_pin_count_[slot_index].fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 0) {
            slot_pin_count_[slot_index].store(0, std::memory_order_release);
        }
    }

    *snapshot = SnapshotRef{};
}

CoreState CoreRegistry::snapshot_copy() const noexcept {
    SnapshotRef ref{};
    if (!pin_current(&ref)) {
        return CoreState{};
    }

    const CoreState copy = *ref.state;
    release_snapshot(&ref);
    return copy;
}

CoreRegistry::SnapshotId CoreRegistry::current_snapshot_id() const noexcept {
    return active_snapshot_id_.load(std::memory_order_acquire);
}

uint32_t CoreRegistry::current_revision() const noexcept {
    SnapshotRef ref{};
    if (!pin_current(&ref)) {
        return 0;
    }

    const uint32_t revision = ref.state->revision;
    release_snapshot(&ref);
    return revision;
}

bool CoreRegistry::publish(const CoreState& state) noexcept {
    const uint32_t current_index = active_index_.load(std::memory_order_acquire);
    const CoreState& current_state = slots_[current_index].state;

    CoreState next_state = state;
    if (next_state.revision <= current_state.revision) {
        next_state.revision = current_state.revision + 1U;
    }

    uint32_t next_index = current_index;
    bool found_slot = false;
    for (std::size_t i = 0; i < kSnapshotCount; ++i) {
        const uint32_t candidate = (write_index_ + 1U + static_cast<uint32_t>(i)) % kSnapshotCount;
        if (candidate == current_index) {
            continue;
        }

        if (slot_pin_count_[candidate].load(std::memory_order_acquire) != 0) {
            continue;
        }

        next_index = candidate;
        found_slot = true;
        break;
    }

    if (!found_slot) {
        return false;
    }

    SnapshotSlot& slot = slots_[next_index];
    slot.state = next_state;
    slot.id = next_snapshot_id_++;

    write_index_ = next_index;
    active_snapshot_id_.store(slot.id, std::memory_order_release);
    active_index_.store(next_index, std::memory_order_release);
    return true;
}

}  // namespace core
