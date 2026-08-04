/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// Covers the one-time migration path from the pre-S3 raw-blob schema
// (StatePersistenceCoordinator::migrate_from_legacy_v1_blob), in a process
// of its own so the legacy "core_state_v1" key is the only thing present --
// exercising the genuine kNotFound-on-new-schema branch that triggers it.

#include <cassert>
#include <cstdint>
#include <type_traits>

#include "core_registry.hpp"
#include "core_state.hpp"
#include "device_id.hpp"
#include "hal_nvs.h"
#include "state_persistence_coordinator.hpp"

namespace {

// Mirrors the private wire layout StatePersistenceCoordinator reads for
// migration (magic 0x43535445 "CSTE", version 1, raw core::CoreState). This
// is deliberately re-declared here rather than shared via a header: the test
// is pinning the historical ON-DISK format as an external contract, not the
// implementation's internal type.
constexpr uint32_t kLegacyMagic = 0x43535445U;
constexpr uint32_t kLegacyVersion = 1U;

struct LegacyPersistedCoreStateV1 {
    uint32_t magic{kLegacyMagic};
    uint32_t version{kLegacyVersion};
    core::CoreState state{};
};

static_assert(std::is_trivially_copyable<LegacyPersistedCoreStateV1>::value, "must be POD-like");

core::DeviceId make_id(const char* hex) {
    core::DeviceId id{};
    const bool ok = core::DeviceId::parse(hex, 16, &id);
    assert(ok);
    return id;
}

}  // namespace

int main() {
    const core::DeviceId device_with_identity = make_id("00124b0001aaaaaa");

    LegacyPersistedCoreStateV1 legacy{};
    legacy.state.revision = 5U;
    legacy.state.device_count = 2U;

    // One record already has a resolved DeviceId (as any S2-or-later
    // firmware would have written) -- this one must migrate.
    legacy.state.devices[0].device_id = device_with_identity;
    legacy.state.devices[0].short_addr = 0x1111U;
    legacy.state.devices[0].online = true;
    legacy.state.devices[0].power_on = true;

    // The other is a genuine pre-S2 legacy record: a real locator, no
    // resolved identity. FD-01/FD-03: this must be quarantined by omission,
    // never guessed or carried forward under the new schema.
    legacy.state.devices[1].short_addr = 0x2222U;
    legacy.state.devices[1].online = true;

    assert(hal_nvs_set_blob("core_state_v1", &legacy, sizeof(legacy)) == HAL_NVS_STATUS_OK);

    core::CoreRegistry registry;
    service::StatePersistenceCoordinator persistence(registry);
    assert(persistence.restore_persisted_core_state());

    const service::StatePersistenceCoordinator::MigrationReport& report = persistence.last_migration_report();
    assert(report.attempted);
    assert(report.legacy_devices_found == 2U);
    assert(report.migrated == 1U);
    assert(report.quarantined_no_identity == 1U);

    const core::CoreState restored = registry.snapshot_copy();
    assert(restored.device_count == 1U);
    assert(restored.devices[0].device_id == device_with_identity);
    assert(restored.devices[0].short_addr == 0x1111U);
    assert(!restored.devices[0].online);

    // Migration commits the explicit-schema state, so a second restore
    // (fresh coordinator, same process/NVS) now takes the normal load path
    // and does not need to re-read/re-attempt the legacy blob.
    core::CoreRegistry registry2;
    service::StatePersistenceCoordinator persistence2(registry2);
    assert(persistence2.restore_persisted_core_state());
    assert(!persistence2.last_migration_report().attempted);
    const core::CoreState restored2 = registry2.snapshot_copy();
    assert(restored2.device_count == 1U);
    assert(restored2.devices[0].device_id == device_with_identity);

    return 0;
}
