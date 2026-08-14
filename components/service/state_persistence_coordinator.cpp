/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "state_persistence_coordinator.hpp"

#include <type_traits>

#include "hal_nvs.h"
#include "log_tags.h"
#include "persisted_device_state.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

namespace service {

namespace {

#ifdef ESP_PLATFORM
constexpr const char* kTag = LOG_TAG_SERVICE_RUNTIME;
#define SPC_LOGI(...) ESP_LOGI(kTag, __VA_ARGS__)
#define SPC_LOGW(...) ESP_LOGW(kTag, __VA_ARGS__)
#else
#define SPC_LOGI(...) ((void)0)
#define SPC_LOGW(...) ((void)0)
#endif

// Pre-S3 raw-blob schema, kept ONLY as a one-time migration source (plan
// Section 9 S3: legacy keys are read-only and are never deleted here). This
// is intentionally the same layout StatePersistenceCoordinator used to write
// directly to NVS before the explicit versioned schema replaced it.
constexpr const char* kLegacyPersistedCoreStateKey = "core_state_v1";
constexpr uint32_t kLegacyPersistedCoreStateMagic = 0x43535445U;  // "CSTE"
constexpr uint32_t kLegacyPersistedCoreStateVersion = 1U;

struct LegacyPersistedCoreStateV1 {
    uint32_t magic{kLegacyPersistedCoreStateMagic};
    uint32_t version{kLegacyPersistedCoreStateVersion};
    core::CoreState state{};
};

static_assert(
    std::is_trivially_copyable<LegacyPersistedCoreStateV1>::value,
    "LegacyPersistedCoreStateV1 must be POD-like");

bool has_restorable_devices(const core::CoreState& state) noexcept {
    return state.device_count != 0U;
}

}  // namespace

StatePersistenceCoordinator::StatePersistenceCoordinator(core::CoreRegistry& registry) noexcept : registry_(&registry) {}

void StatePersistenceCoordinator::mark_restore_pending() noexcept {
    restore_core_state_pending_.store(true, std::memory_order_release);
}

bool StatePersistenceCoordinator::consume_restore_pending() noexcept {
    return restore_core_state_pending_.exchange(false, std::memory_order_acq_rel);
}

void StatePersistenceCoordinator::note_persist_state_requested() noexcept {
    persist_core_state_pending_.store(true, std::memory_order_release);
}

StatePersistenceCoordinator::FlushResult StatePersistenceCoordinator::flush_if_needed() noexcept {
    if (!persist_core_state_pending_.exchange(false, std::memory_order_acq_rel)) {
        return FlushResult::kNoop;
    }
    return persist_current_core_state() ? FlushResult::kFlushed : FlushResult::kFailed;
}

bool StatePersistenceCoordinator::persist_current_core_state() noexcept {
    // `static`, not stack-local -- same real stack-overflow pattern (and
    // fix) documented in restore_persisted_core_state()/
    // migrate_from_legacy_v1_blob() below. registry_->snapshot_copy()
    // still returns core::CoreState by value (CoreRegistry's own public
    // API, unchanged -- it has many other, host-test-only callers where
    // this is not a concern), but assigning its result into a `static`
    // destination here at least ensures it never overlaps in scope with
    // `s_payload`.
    static core::CoreState s_snapshot;
    s_snapshot = registry_->snapshot_copy();
    static PersistedStatePayload s_payload;
    to_payload(s_snapshot, &s_payload);
    return state_store_.save(s_payload);
}

bool StatePersistenceCoordinator::restore_persisted_core_state() noexcept {
    // `static`, not stack-local -- see migrate_from_legacy_v1_blob()'s own
    // comment below for the real "Guru Meditation Error: Stack protection
    // fault" this exact pattern (PersistedStatePayload ~2.2KB +
    // core::CoreState ~2.6KB simultaneously in scope) caused on real
    // ESP32-C6 hardware, and why `static` is safe here (single-task,
    // single-caller, never concurrent).
    static PersistedStatePayload s_payload;
    s_payload = PersistedStatePayload{};
    const PersistedStateStore::LoadResult result = state_store_.load(&s_payload);

    if (result == PersistedStateStore::LoadResult::kCorrupt) {
        SPC_LOGW("Persisted state restore skipped: both generations failed validation");
        return false;
    }

    if (result == PersistedStateStore::LoadResult::kNotFound) {
        return migrate_from_legacy_v1_blob();
    }

    static core::CoreState s_restored;
    apply_payload(s_payload, &s_restored);
    if (!has_restorable_devices(s_restored)) {
        return false;
    }
    if (!registry_->publish(s_restored)) {
        SPC_LOGW("Persisted CoreState restore failed: registry publish rejected");
        return false;
    }

    SPC_LOGI(
        "Restored persisted CoreState devices=%u",
        static_cast<unsigned>(s_restored.device_count));
    return true;
}

bool StatePersistenceCoordinator::migrate_from_legacy_v1_blob() noexcept {
    last_migration_report_ = MigrationReport{};
    last_migration_report_.attempted = true;

    // `static`, not stack-local: LegacyPersistedCoreStateV1 (~2.6KB,
    // dominated by core::CoreState), PersistedStatePayload (~2.2KB) and a
    // second core::CoreState (~2.6KB) as ordinary stack locals here
    // overlap in scope for the rest of this function and together total
    // ~7.3KB -- nearly the entire 9216-byte service_runtime task stack
    // budget, and the real, confirmed root cause of a "Guru Meditation
    // Error: Stack protection fault" observed on real ESP32-C6 hardware
    // (persisted_state_store.cpp's load()/save() had the sibling
    // instance of this same pattern, fixed the same way; that fix alone
    // did not resolve the crash -- this function, one call deeper on the
    // very same startup path, was the actual remaining cause). Safe as
    // `static` here specifically because migrate_from_legacy_v1_blob() is
    // reachable only from restore_persisted_core_state(), itself called
    // only from ServiceRuntime::runtime_task_entry() (the single
    // service_runtime FreeRTOS task) gated by consume_restore_pending()'s
    // atomic exchange -- never concurrently, never from more than one
    // task.
    static LegacyPersistedCoreStateV1 s_legacy;
    s_legacy = LegacyPersistedCoreStateV1{};
    uint32_t legacy_len = 0U;
    const hal_nvs_status_t status =
        hal_nvs_get_blob(kLegacyPersistedCoreStateKey, &s_legacy, sizeof(s_legacy), &legacy_len);
    if (status == HAL_NVS_STATUS_NOT_FOUND) {
        return false;  // genuine fresh install: nothing to migrate or restore.
    }
    if (status != HAL_NVS_STATUS_OK || legacy_len != sizeof(s_legacy) ||
        s_legacy.magic != kLegacyPersistedCoreStateMagic || s_legacy.version != kLegacyPersistedCoreStateVersion) {
        SPC_LOGW("Legacy CoreState blob present but invalid; nothing migrated");
        return false;
    }

    last_migration_report_.legacy_devices_found = s_legacy.state.device_count;
    for (const core::CoreDeviceRecord& device : s_legacy.state.devices) {
        if (device.short_addr == core::kUnknownDeviceShortAddr) {
            continue;
        }
        if (device.device_id.valid()) {
            ++last_migration_report_.migrated;
        } else {
            // FD-01/FD-03: a legacy record with no resolved DeviceId is
            // quarantined by omission -- it is never guessed or rebound to a
            // current locator lookup. See docs/architecture/DEVICE_IDENTITY.md.
            ++last_migration_report_.quarantined_no_identity;
        }
    }

    static PersistedStatePayload s_payload;
    to_payload(s_legacy.state, &s_payload);
    static core::CoreState s_restored;
    apply_payload(s_payload, &s_restored);
    if (!has_restorable_devices(s_restored)) {
        SPC_LOGI(
            "Legacy CoreState migration: %u found, %u quarantined (no resolved identity), 0 restorable",
            static_cast<unsigned>(last_migration_report_.legacy_devices_found),
            static_cast<unsigned>(last_migration_report_.quarantined_no_identity));
        return false;
    }

    if (!state_store_.save(s_payload)) {
        SPC_LOGW("Legacy CoreState migration: failed to commit explicit-schema state");
        return false;
    }
    if (!registry_->publish(s_restored)) {
        SPC_LOGW("Legacy CoreState migration: registry publish rejected");
        return false;
    }

    SPC_LOGI(
        "Legacy CoreState migration committed: %u found, %u migrated, %u quarantined (no resolved identity)",
        static_cast<unsigned>(last_migration_report_.legacy_devices_found),
        static_cast<unsigned>(last_migration_report_.migrated),
        static_cast<unsigned>(last_migration_report_.quarantined_no_identity));
    return true;
}

}  // namespace service
