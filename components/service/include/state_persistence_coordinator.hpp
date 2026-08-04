/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstdint>

#include "core_registry.hpp"
#include "persisted_state_store.hpp"

namespace service {

class StatePersistenceCoordinator {
public:
    enum class FlushResult : uint8_t {
        kNoop = 0,
        kFlushed,
        kFailed,
    };

    // Outcome of the one-time migration from the pre-S3 raw-blob schema,
    // consumed on the first restore after upgrading firmware. Not persisted
    // itself; recomputed each boot from whatever the legacy blob currently
    // contains (harmless since it is read-only and never deleted here --
    // plan Section 9 S3: "old keys are deleted only after one successful
    // release/canary window").
    struct MigrationReport {
        bool attempted{false};
        uint32_t legacy_devices_found{0};
        uint32_t migrated{0};
        uint32_t quarantined_no_identity{0};
    };

    explicit StatePersistenceCoordinator(core::CoreRegistry& registry) noexcept;

    void mark_restore_pending() noexcept;
    bool consume_restore_pending() noexcept;
    void note_persist_state_requested() noexcept;
    FlushResult flush_if_needed() noexcept;
    bool persist_current_core_state() noexcept;
    bool restore_persisted_core_state() noexcept;
    const MigrationReport& last_migration_report() const noexcept { return last_migration_report_; }

private:
    bool migrate_from_legacy_v1_blob() noexcept;

    core::CoreRegistry* registry_{nullptr};
    PersistedStateStore state_store_{};
    MigrationReport last_migration_report_{};
    std::atomic<bool> restore_core_state_pending_{false};
    std::atomic<bool> persist_core_state_pending_{false};
};

}  // namespace service
