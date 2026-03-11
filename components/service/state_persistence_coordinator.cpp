/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "state_persistence_coordinator.hpp"

#include <type_traits>

#include "hal_nvs.h"
#include "log_tags.h"

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

constexpr const char* kPersistedCoreStateKey = "core_state_v1";
constexpr uint32_t kPersistedCoreStateMagic = 0x43535445U;  // "CSTE"
constexpr uint32_t kPersistedCoreStateVersion = 1U;

struct PersistedCoreStateV1 {
    uint32_t magic{kPersistedCoreStateMagic};
    uint32_t version{kPersistedCoreStateVersion};
    core::CoreState state{};
};

static_assert(std::is_trivially_copyable<PersistedCoreStateV1>::value, "PersistedCoreStateV1 must be POD-like");
static_assert(
    sizeof(PersistedCoreStateV1) == sizeof(core::CoreState) + sizeof(uint32_t) * 2U,
    "Persisted core-state storage size must match serialized payload");

bool sanitize_restored_core_state(core::CoreState* state) noexcept {
    if (state == nullptr) {
        return false;
    }

    state->network_connected = false;
    state->last_command_status = 0U;
    return true;
}

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
    auto* persisted = reinterpret_cast<PersistedCoreStateV1*>(persisted_core_state_storage_.bytes.data());
    *persisted = PersistedCoreStateV1{};
    persisted->state = registry_->snapshot_copy();
    return hal_nvs_set_blob(kPersistedCoreStateKey, persisted, sizeof(*persisted)) == HAL_NVS_STATUS_OK;
}

bool StatePersistenceCoordinator::restore_persisted_core_state() noexcept {
    auto* persisted = reinterpret_cast<PersistedCoreStateV1*>(persisted_core_state_storage_.bytes.data());
    *persisted = PersistedCoreStateV1{};
    uint32_t persisted_len = 0U;
    const hal_nvs_status_t status =
        hal_nvs_get_blob(kPersistedCoreStateKey, persisted, sizeof(*persisted), &persisted_len);
    if (status == HAL_NVS_STATUS_NOT_FOUND) {
        return false;
    }
    if (status != HAL_NVS_STATUS_OK || persisted_len != sizeof(*persisted) ||
        persisted->magic != kPersistedCoreStateMagic || persisted->version != kPersistedCoreStateVersion) {
        SPC_LOGW("Persisted CoreState restore skipped: status=%d len=%u", static_cast<int>(status), persisted_len);
        return false;
    }

    core::CoreState restored = persisted->state;
    if (!sanitize_restored_core_state(&restored) || !has_restorable_devices(restored)) {
        return false;
    }

    if (!registry_->publish(restored)) {
        SPC_LOGW("Persisted CoreState restore failed: registry publish rejected");
        return false;
    }

    SPC_LOGI(
        "Restored persisted CoreState devices=%u revision=%lu",
        static_cast<unsigned>(restored.device_count),
        static_cast<unsigned long>(restored.revision));
    return true;
}

}  // namespace service
