/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "factory_reset_namespace_erase.hpp"

#include <cstdio>

#include "secure_storage_port.hpp"

namespace service {

namespace {

bool namespace_is_erasable(NvsNamespaceId namespace_id) noexcept {
    return find_nvs_namespace_entry(namespace_id).reset_classification == NvsResetClassification::kEraseOnFactoryReset;
}

// Folds one secure_storage_erase() outcome into the running result,
// keeping the "worst" outcome seen so far: kPartialFailure, once set, is
// never overwritten back to kErased by a later successful erase in the
// same batch.
NamespaceEraseResult fold_erase_result(NamespaceEraseResult running, SecureStorageEraseResult single_key_result) {
    if (running == NamespaceEraseResult::kPartialFailure) {
        return running;
    }
    if (single_key_result == SecureStorageEraseResult::kEraseFailed) {
        return NamespaceEraseResult::kPartialFailure;
    }
    // kErased or kRejectedWrongNamespace (the latter should not happen
    // here since every key comes from this namespace's own registry
    // entry or a caller-supplied prefix targeting this same namespace,
    // but is treated as a failure defensively rather than silently
    // ignored).
    if (single_key_result != SecureStorageEraseResult::kErased) {
        return NamespaceEraseResult::kPartialFailure;
    }
    return running;
}

}  // namespace

NamespaceEraseResult erase_namespace(NvsNamespaceId namespace_id) noexcept {
    if (!namespace_is_erasable(namespace_id)) {
        return NamespaceEraseResult::kRefusedNotErasable;
    }

    const NvsNamespaceEntry& entry = find_nvs_namespace_entry(namespace_id);
    NamespaceEraseResult result = NamespaceEraseResult::kErased;
    for (std::size_t i = 0; i < entry.key_pattern_count; ++i) {
        const NvsKeyPattern& pattern = entry.key_patterns[i];
        if (pattern.is_prefix) {
            continue;
        }
        result = fold_erase_result(result, secure_storage_erase(namespace_id, pattern.value));
    }
    return result;
}

NamespaceEraseResult erase_namespace_key_range(
    NvsNamespaceId namespace_id,
    const char* prefix,
    const char* suffix_chars,
    uint32_t suffix_char_count,
    uint32_t index_count) noexcept {
    if (!namespace_is_erasable(namespace_id)) {
        return NamespaceEraseResult::kRefusedNotErasable;
    }
    if (prefix == nullptr || suffix_chars == nullptr) {
        return NamespaceEraseResult::kPartialFailure;
    }

    NamespaceEraseResult result = NamespaceEraseResult::kErased;
    for (uint32_t suffix_index = 0; suffix_index < suffix_char_count; ++suffix_index) {
        for (uint32_t index = 0; index < index_count; ++index) {
            char key[32]{};
            // uint32_t is `unsigned long` on the real riscv32 target but
            // `unsigned int` on the x86-64 host toolchain -- %02u alone
            // mismatches on target (caught only by a real target build,
            // not the host build, since the two platforms disagree on
            // uint32_t's underlying type). Cast explicitly, matching
            // hal_nvs.c's own established (unsigned) cast convention for
            // the same reason.
            const int written = std::snprintf(
                key, sizeof(key), "%s%c%02u", prefix, suffix_chars[suffix_index], static_cast<unsigned>(index));
            if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(key)) {
                result = NamespaceEraseResult::kPartialFailure;
                continue;
            }
            result = fold_erase_result(result, secure_storage_erase(namespace_id, key));
        }
    }
    return result;
}

}  // namespace service
