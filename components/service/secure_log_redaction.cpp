/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "secure_log_redaction.hpp"

#include <cstdio>

namespace service {

RedactedValueSummary redact_value_for_log(const void* value, uint32_t value_len) noexcept {
    if (value == nullptr) {
        return RedactedValueSummary{0};
    }
    return RedactedValueSummary{value_len};
}

int format_redacted_value_summary(RedactedValueSummary summary, char* out, uint32_t out_capacity) noexcept {
    if (out == nullptr || out_capacity == 0U) {
        return -1;
    }
    return std::snprintf(out, out_capacity, "[REDACTED %u bytes]", static_cast<unsigned>(summary.byte_length));
}

}  // namespace service
