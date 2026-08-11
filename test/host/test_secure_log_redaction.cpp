/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "secure_log_redaction.hpp"

namespace {

using service::RedactedValueSummary;

void test_redact_value_for_log_keeps_only_length() {
    const uint8_t secret_bytes[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00};
    const RedactedValueSummary summary = service::redact_value_for_log(secret_bytes, sizeof(secret_bytes));
    assert(summary.byte_length == sizeof(secret_bytes));
}

void test_redact_value_for_log_null_value_is_zero_length_regardless_of_len_argument() {
    // A caller passing a stale/incorrect length alongside a null pointer
    // must never produce a nonzero-looking summary -- there is nothing to
    // summarize.
    const RedactedValueSummary summary = service::redact_value_for_log(nullptr, 999U);
    assert(summary.byte_length == 0U);
}

void test_redact_value_for_log_zero_length_value() {
    const uint8_t empty_marker = 0;
    const RedactedValueSummary summary = service::redact_value_for_log(&empty_marker, 0U);
    assert(summary.byte_length == 0U);
}

void test_format_redacted_value_summary_contains_only_the_length_never_content() {
    char buffer[32]{};
    const int written = service::format_redacted_value_summary(RedactedValueSummary{11U}, buffer, sizeof(buffer));
    assert(written > 0);
    assert(std::strcmp(buffer, "[REDACTED 11 bytes]") == 0);
    // The formatted string is short and entirely predictable from the
    // length alone -- there is no room for any byte of real content to
    // have leaked into it.
    assert(std::strstr(buffer, "REDACTED") != nullptr);
}

void test_format_redacted_value_summary_zero_length() {
    char buffer[32]{};
    const int written = service::format_redacted_value_summary(RedactedValueSummary{0U}, buffer, sizeof(buffer));
    assert(written > 0);
    assert(std::strcmp(buffer, "[REDACTED 0 bytes]") == 0);
}

void test_format_redacted_value_summary_rejects_null_or_zero_capacity_output_buffer() {
    RedactedValueSummary summary{5U};
    assert(service::format_redacted_value_summary(summary, nullptr, 32U) == -1);

    char buffer[32]{};
    assert(service::format_redacted_value_summary(summary, buffer, 0U) == -1);
}

void test_format_redacted_value_summary_truncates_safely_into_a_small_buffer() {
    char tiny_buffer[6]{};
    const int would_be_length =
        service::format_redacted_value_summary(RedactedValueSummary{123456U}, tiny_buffer, sizeof(tiny_buffer));
    // snprintf-style contract: the return value reports the length that
    // *would* have been written, even though the buffer is smaller; the
    // buffer itself is always left null-terminated within its capacity,
    // never overrun.
    assert(would_be_length > static_cast<int>(sizeof(tiny_buffer)) - 1);
    assert(std::strlen(tiny_buffer) < sizeof(tiny_buffer));
}

}  // namespace

int main() {
    test_redact_value_for_log_keeps_only_length();
    test_redact_value_for_log_null_value_is_zero_length_regardless_of_len_argument();
    test_redact_value_for_log_zero_length_value();
    test_format_redacted_value_summary_contains_only_the_length_never_content();
    test_format_redacted_value_summary_zero_length();
    test_format_redacted_value_summary_rejects_null_or_zero_capacity_output_buffer();
    test_format_redacted_value_summary_truncates_safely_into_a_small_buffer();
    return 0;
}
