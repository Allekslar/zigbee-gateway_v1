/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <array>
#include <cassert>
#include <cstdint>

#include "core_registry.hpp"

int main() {
    core::CoreRegistry registry;

    core::CoreRegistry::SnapshotRef initial_ref{};
    assert(registry.pin_current(&initial_ref));
    assert(initial_ref.valid());
    assert(initial_ref.id == 0);
    assert(initial_ref.state->revision == 0);
    assert(initial_ref.state->device_count == 0);
    assert(!initial_ref.state->network_connected);
    registry.release_snapshot(&initial_ref);
    assert(!initial_ref.valid());

    assert(registry.current_snapshot_id() == 0);
    assert(registry.current_revision() == 0);

    core::CoreState state = registry.snapshot_copy();
    state.revision = 0;
    state.device_count = 3;
    state.network_connected = true;
    const auto snapshot_before_first = registry.current_snapshot_id();
    assert(registry.publish(state));

    assert(registry.current_snapshot_id() > snapshot_before_first);
    assert(registry.current_revision() == 1);
    core::CoreState current_copy = registry.snapshot_copy();
    assert(current_copy.device_count == 3);
    assert(current_copy.network_connected);

    core::CoreState state_with_higher_revision = registry.snapshot_copy();
    state_with_higher_revision.revision = 10;
    state_with_higher_revision.device_count = 5;
    assert(registry.publish(state_with_higher_revision));

    assert(registry.current_revision() == 10);
    current_copy = registry.snapshot_copy();
    assert(current_copy.device_count == 5);

    // Hold previous snapshot pinned while publishing a newer state.
    core::CoreRegistry::SnapshotRef pinned_old{};
    assert(registry.pin_current(&pinned_old));
    assert(pinned_old.state != nullptr);
    assert(pinned_old.state->device_count == 5);

    core::CoreState pinned_publish = registry.snapshot_copy();
    pinned_publish.revision = 0;
    pinned_publish.device_count = 42;
    assert(registry.publish(pinned_publish));
    assert(pinned_old.state->device_count == 5);
    registry.release_snapshot(&pinned_old);

    uint32_t expected_revision = registry.current_revision();
    for (uint16_t i = 1; i <= 20; ++i) {
        core::CoreState rolling_state = registry.snapshot_copy();
        rolling_state.revision = 0;  // force auto-increment branch
        rolling_state.device_count = i;
        const auto prev_snapshot_id = registry.current_snapshot_id();
        assert(registry.publish(rolling_state));
        ++expected_revision;

        assert(registry.current_snapshot_id() > prev_snapshot_id);
        assert(registry.current_revision() == expected_revision);
        assert(registry.snapshot_copy().device_count == i);
    }

    std::array<core::CoreRegistry::SnapshotRef, core::CoreRegistry::kSnapshotCount> pinned_slots{};
    assert(registry.pin_current(&pinned_slots[0]));
    for (std::size_t i = 1; i < pinned_slots.size(); ++i) {
        core::CoreState next = registry.snapshot_copy();
        next.revision = 0;
        next.device_count = static_cast<uint16_t>(100 + i);
        assert(registry.publish(next));
        assert(registry.pin_current(&pinned_slots[i]));
    }

    core::CoreState no_slot_state = registry.snapshot_copy();
    no_slot_state.revision = 0;
    no_slot_state.device_count = 777;
    assert(!registry.publish(no_slot_state));

    for (std::size_t i = 0; i < pinned_slots.size(); ++i) {
        registry.release_snapshot(&pinned_slots[i]);
    }

    return 0;
}
