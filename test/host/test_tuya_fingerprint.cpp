/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "tuya_fingerprint.hpp"

int main() {
    service::TuyaFingerprintResolver resolver{};

    /* Tuya manufacturers: strings starting with "_TZ" */
    assert(resolver.is_tuya_manufacturer("_TZ3000_abc"));
    assert(resolver.is_tuya_manufacturer("_TZE200_xyz"));
    assert(resolver.is_tuya_manufacturer("_TZE204_test"));
    assert(resolver.is_tuya_manufacturer("_TZ"));

    /* Non-Tuya manufacturers */
    assert(!resolver.is_tuya_manufacturer("IKEA"));
    assert(!resolver.is_tuya_manufacturer("Philips"));
    assert(!resolver.is_tuya_manufacturer("TZ3000"));
    assert(!resolver.is_tuya_manufacturer(""));
    assert(!resolver.is_tuya_manufacturer(nullptr));

    /* resolve(): Tuya manufacturer → kMatched */
    service::TuyaFingerprint fp{};
    fp.manufacturer = "_TZ3000_abc";
    fp.model = "TS0001";
    fp.endpoint = 1;
    assert(resolver.resolve(fp) == service::TuyaFingerprintMatchResult::kMatched);

    /* resolve(): non-Tuya manufacturer → kNoMatch */
    fp.manufacturer = "IKEA";
    assert(resolver.resolve(fp) == service::TuyaFingerprintMatchResult::kNoMatch);

    /* resolve(): null manufacturer → kNoMatch */
    fp.manufacturer = nullptr;
    assert(resolver.resolve(fp) == service::TuyaFingerprintMatchResult::kNoMatch);

    return 0;
}
