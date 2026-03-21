/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "tuya_contact_sensor_plugin.hpp"

#include <cstring>

namespace service {

namespace {

/*
 * Tuya contact sensors — DP mapping:
 *   DP 1 (bool):   contact state (true = open, false = closed)
 *   DP 3 (value):  battery percentage (0-100)
 *
 * Known matching models (non-exhaustive):
 *   _TZ3000_* / TS0203
 *   _TZE200_* / TS0601 (contact variants)
 */
constexpr uint8_t kDpContact = 1U;
constexpr uint8_t kDpBattery = 3U;

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

bool is_contact_sensor_model(const char* model) noexcept {
    if (model == nullptr || model[0] == '\0') {
        return false;
    }
    return std::strcmp(model, "TS0203") == 0;
}

}  // namespace

bool TuyaContactSensorPlugin::matches(const TuyaFingerprint& fingerprint) const noexcept {
    if (!starts_with(fingerprint.manufacturer, "_TZ")) {
        return false;
    }
    return is_contact_sensor_model(fingerprint.model);
}

TuyaPluginResult TuyaContactSensorPlugin::translate(
    const TuyaFingerprint& fingerprint,
    const TuyaDpParseResult& dp_result) const noexcept {
    (void)fingerprint;

    TuyaPluginResult result{};

    const TuyaDpItem* contact_dp = dp_result.find_dp(kDpContact);
    if (contact_dp != nullptr && contact_dp->dp_type == TuyaDpType::kBool) {
        /* Tuya: true = open, false = closed.
         * Normalize to: 1 = open, 0 = closed. */
        result.add(TuyaNormalizedKind::kContactOpen, contact_dp->as_bool() ? 1 : 0);
        result.handled = true;
    }

    const TuyaDpItem* battery_dp = dp_result.find_dp(kDpBattery);
    if (battery_dp != nullptr && battery_dp->dp_type == TuyaDpType::kValue) {
        const int32_t percent = static_cast<int32_t>(battery_dp->as_u32());
        if (percent >= 0 && percent <= 200) {
            result.add(TuyaNormalizedKind::kBatteryPercent, percent);
            result.handled = true;
        }
    }

    return result;
}

}  // namespace service
