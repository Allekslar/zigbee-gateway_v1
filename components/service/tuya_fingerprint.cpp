/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "tuya_fingerprint.hpp"

#include <cstring>

namespace service {

namespace {

bool starts_with(const char* str, const char* prefix) noexcept {
    if (str == nullptr || prefix == nullptr) {
        return false;
    }
    while (*prefix != '\0') {
        if (*str != *prefix) {
            return false;
        }
        ++str;
        ++prefix;
    }
    return true;
}

}  // namespace

TuyaFingerprintMatchResult TuyaFingerprintResolver::resolve(
    const TuyaFingerprint& fingerprint) const noexcept {
    if (!is_tuya_manufacturer(fingerprint.manufacturer)) {
        return TuyaFingerprintMatchResult::kNoMatch;
    }

    // Phase 0: no device-specific plugins registered yet.
    // Return kMatched to indicate Tuya-compatible manufacturer detected,
    // even though no specific plugin handles this model.
    return TuyaFingerprintMatchResult::kMatched;
}

bool TuyaFingerprintResolver::is_tuya_manufacturer(const char* manufacturer) const noexcept {
    if (manufacturer == nullptr || manufacturer[0] == '\0') {
        return false;
    }
    // Tuya devices typically have manufacturer strings starting with "_TZ"
    // (e.g. "_TZ3000_xxx", "_TZE200_xxx", "_TZE204_xxx").
    return starts_with(manufacturer, "_TZ");
}

}  // namespace service
