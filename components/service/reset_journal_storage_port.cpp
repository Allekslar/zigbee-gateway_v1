/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "reset_journal_storage_port.hpp"

namespace service {

namespace {

constexpr const char* kResetJournalKey = "reset_journal";

bool is_valid_state(uint32_t raw_state) noexcept {
    return raw_state <= static_cast<uint32_t>(ResetJournalState::kCommissioningReady);
}

}  // namespace

SecureStorageStatus get_reset_journal_state(ResetJournalState* state_out) noexcept {
    uint32_t raw_state = 0U;
    const SecureStorageStatus status =
        secure_storage_get_u32(NvsNamespaceId::kResetJournal, kResetJournalKey, &raw_state);
    if (status != SecureStorageStatus::kAvailable) {
        return status;
    }
    if (!is_valid_state(raw_state)) {
        // A value exists but is not one of the four defined states --
        // exactly the plan #10 "corrupt" case: present, but not usable as
        // a well-formed record.
        return SecureStorageStatus::kCorrupt;
    }
    if (state_out != nullptr) {
        *state_out = static_cast<ResetJournalState>(raw_state);
    }
    return SecureStorageStatus::kAvailable;
}

ResetJournalWriteResult set_reset_journal_state(ResetJournalState state) noexcept {
    const uint32_t raw_state = static_cast<uint32_t>(state);
    if (!is_valid_state(raw_state)) {
        return ResetJournalWriteResult::kRejectedInvalidState;
    }
    const SecureStorageWriteResult result =
        secure_storage_set_u32(NvsNamespaceId::kResetJournal, kResetJournalKey, raw_state);
    return result == SecureStorageWriteResult::kWritten ? ResetJournalWriteResult::kWritten
                                                          : ResetJournalWriteResult::kWriteFailed;
}

}  // namespace service
