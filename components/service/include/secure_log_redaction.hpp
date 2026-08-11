/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace service {

// Plan S5 required change #14 (Encrypted storage foundation): "Redact all
// key/credential material from logs, crash reports and completion
// evidence." This header is the reusable building block for the "logs"
// clause -- wired into Section 2.8/2.9/2.10/2.11's storage paths
// (secure_storage_port.cpp) so their (previously nonexistent) diagnostic
// logging is redacted by construction, not by convention someone has to
// remember to follow.
//
// The redaction here is deliberately the strongest possible form: the
// summary carries ONLY a byte length, never any content-derived
// information (not even a hash/fingerprint of the value). This differs
// from scripts/efuse_provisioning_template.py's redact_key_material()
// (plan #5/#8), which keeps a SHA-256 digest specifically because
// provisioning evidence needs to answer "is this the same key as a prior
// record" without ever storing the key -- a real, narrow, justified use
// for a content-derived fingerprint. A log line has no such need; keeping
// even a digest out of it removes an entire class of "is a hash actually
// safe here" review question for every future call site.
//
// See docs/security/PRODUCTION_HARDENING.md Section 2.12 for how this
// connects to the other two clauses of plan #14 (crash reports:
// CONFIG_ESP_COREDUMP_CAPTURE_DRAM added to
// scripts/verify_production_security_profile.py's forbidden list;
// completion evidence: this repository's own implementation-evidence/*.json
// files were audited, not just assumed clean).

struct RedactedValueSummary {
    uint32_t byte_length{0};
};

// Always redacted: reads only `value_len`, never dereferences `value`'s
// content. `value == nullptr` is treated as a zero-length value regardless
// of `value_len` (matching "there is nothing to summarize"), the same
// defensive-null convention this repository's other HAL/storage
// primitives already use.
RedactedValueSummary redact_value_for_log(const void* value, uint32_t value_len) noexcept;

// Formats a RedactedValueSummary as a fixed, human-readable string safe to
// embed directly in a log line, e.g. "[REDACTED 11 bytes]". Writes at most
// out_capacity-1 characters plus a null terminator into `out` (or writes
// nothing if out/out_capacity are invalid). Returns the number of
// characters the formatted string would occupy excluding the null
// terminator, matching std::snprintf's own return-value convention, so a
// caller can detect truncation if it cares.
int format_redacted_value_summary(RedactedValueSummary summary, char* out, uint32_t out_capacity) noexcept;

}  // namespace service
