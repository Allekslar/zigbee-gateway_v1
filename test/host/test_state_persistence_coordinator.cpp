/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_registry.hpp"
#include "state_persistence_coordinator.hpp"

int main() {
    core::CoreRegistry registry;
    service::StatePersistenceCoordinator persistence(registry);

    persistence.mark_restore_pending();
    assert(persistence.consume_restore_pending());
    assert(!persistence.consume_restore_pending());

    core::CoreState state{};
    state.device_count = 1U;
    state.revision = 1U;
    state.devices[0].online = true;
    state.devices[0].short_addr = 0x4411;
    assert(registry.publish(state));
    persistence.note_persist_state_requested();
    assert(persistence.flush_if_needed() == service::StatePersistenceCoordinator::FlushResult::kFlushed);
    assert(persistence.flush_if_needed() == service::StatePersistenceCoordinator::FlushResult::kNoop);

    core::CoreRegistry restored_registry;
    service::StatePersistenceCoordinator restored_persistence(restored_registry);
    assert(restored_persistence.restore_persisted_core_state());
    const core::CoreState restored = restored_registry.snapshot_copy();
    assert(restored.device_count == 1U);
    assert(restored.devices[0].short_addr == 0x4411);
    assert(!restored.network_connected);
    return 0;
}
