/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "factory_reset_namespace_erase.hpp"
#include "hal_nvs.h"
#include "reset_journal_storage_port.hpp"

namespace {

using service::NamespaceEraseResult;
using service::NvsNamespaceId;
using service::ResetJournalState;
using service::ResetJournalWriteResult;
using service::SecureStorageStatus;

void test_get_reset_journal_state_not_provisioned_before_any_write() {
    ResetJournalState state = ResetJournalState::kRequested;
    const SecureStorageStatus status = service::get_reset_journal_state(&state);
    assert(status == SecureStorageStatus::kNotProvisioned);
}

// FD-21's exact sequence: requested -> erasing -> reinitialized ->
// commissioning_ready. Every state round-trips independently.
void test_set_then_get_round_trips_every_fd21_state() {
    const ResetJournalState states[4] = {
        ResetJournalState::kRequested,
        ResetJournalState::kErasing,
        ResetJournalState::kReinitialized,
        ResetJournalState::kCommissioningReady,
    };
    for (ResetJournalState expected : states) {
        assert(service::set_reset_journal_state(expected) == ResetJournalWriteResult::kWritten);

        ResetJournalState readback = ResetJournalState::kRequested;
        assert(service::get_reset_journal_state(&readback) == SecureStorageStatus::kAvailable);
        assert(readback == expected);
    }
}

void test_set_reset_journal_state_rejects_out_of_range_value() {
    const auto invalid_state = static_cast<ResetJournalState>(99);
    const ResetJournalWriteResult result = service::set_reset_journal_state(invalid_state);
    assert(result == ResetJournalWriteResult::kRejectedInvalidState);
}

void test_corrupt_when_stored_raw_value_out_of_range() {
    assert(hal_nvs_set_u32("reset_journal", 200U) == HAL_NVS_STATUS_OK);

    ResetJournalState state = ResetJournalState::kRequested;
    const SecureStorageStatus status = service::get_reset_journal_state(&state);
    assert(status == SecureStorageStatus::kCorrupt);
}

// The concrete, end-to-end proof of plan #18's "survives erasing
// user/application namespaces": write a real state, attempt the typed
// namespace erase plan #16/#17 provide, confirm it is refused AND that
// the journal is still exactly what it was.
void test_reset_journal_survives_an_erase_namespace_attempt() {
    assert(service::set_reset_journal_state(ResetJournalState::kReinitialized) == ResetJournalWriteResult::kWritten);

    const NamespaceEraseResult erase_result = service::erase_namespace(NvsNamespaceId::kResetJournal);
    assert(erase_result == NamespaceEraseResult::kRefusedNotErasable);

    ResetJournalState readback = ResetJournalState::kRequested;
    assert(service::get_reset_journal_state(&readback) == SecureStorageStatus::kAvailable);
    assert(readback == ResetJournalState::kReinitialized);
}

}  // namespace

int main() {
    test_get_reset_journal_state_not_provisioned_before_any_write();
    test_set_then_get_round_trips_every_fd21_state();
    test_set_reset_journal_state_rejects_out_of_range_value();
    test_corrupt_when_stored_raw_value_out_of_range();
    test_reset_journal_survives_an_erase_namespace_attempt();
    return 0;
}
