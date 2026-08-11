/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "nvs_namespace_registry.hpp"
#include "secure_storage_port.hpp"

namespace service {

// Plan S5 required change #18 (Encrypted storage foundation): "Add a
// dedicated protected reset-journal storage port that survives erasing
// user/application namespaces and can atomically represent the four FD-21
// states."
//
// "Survives erasing user/application namespaces" is a structural
// guarantee, not a convention this port has to remember to uphold itself:
// kResetJournal (Section 2.7 registry) is classified kResetJournalOnly,
// and factory_reset_namespace_erase.hpp's erase_namespace() (plan #16/#17)
// refuses outright, before touching any key, for any namespace not
// classified kEraseOnFactoryReset -- kResetJournal can never reach
// hal_nvs_erase_key through that mechanism no matter what a caller passes.
//
// "Atomically represent the four states": the journal is stored as a
// single u32 key, written in one hal_nvs_set_u32 call. This inherits its
// atomicity from ESP-IDF's own NVS commit semantics (a single key's
// value is never observed in a torn/partial state across a power loss --
// either the previous committed value persists or the new one does) --
// this port does not reimplement that guarantee, it relies on it, exactly
// matching plan FD-22's "prefer ESP-IDF-provided ... platform lifecycle
// mechanisms" principle.
//
// This is scaffolding, matching every other S5 sub-slice's discipline:
// nothing in this repository calls set_reset_journal_state() -- there is
// no real factory-reset flow yet (S8's job, built on top of this and
// Section 2.13's erase enforcement).

// Result of reading the reset journal. Reuses Section 2.8's
// SecureStorageStatus vocabulary directly rather than inventing a parallel
// one: kNotProvisioned before any reset has ever been requested (the
// normal steady state for a device that has never been factory-reset) is
// exactly as meaningful here as it is for any other namespace.
SecureStorageStatus get_reset_journal_state(ResetJournalState* state_out) noexcept;

enum class ResetJournalWriteResult : uint8_t {
    kWritten = 0,
    // `state` is not one of the four defined ResetJournalState values --
    // rejected before writing anything. Distinct from
    // SecureStorageWriteResult's own failure modes because this is a
    // caller-input validation failure, not a storage-layer one.
    kRejectedInvalidState = 1,
    kWriteFailed = 2,
};

// Writes the reset journal state as a single, atomic operation (see the
// module-level note above). kResetJournal has encryption_required==false
// (Section 2.7 registry -- the journal is a small state enum, not secret
// material), so this never fails due to an unverified-encryption gate the
// way a plan #9 secret-namespace write could.
ResetJournalWriteResult set_reset_journal_state(ResetJournalState state) noexcept;

}  // namespace service
