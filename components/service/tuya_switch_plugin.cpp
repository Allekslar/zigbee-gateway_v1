/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "tuya_switch_plugin.hpp"

#include <cstring>

namespace service {

namespace {

/*
 * Tuya single-gang switch/relay — DP mapping:
 *   DP 1 (bool):   power state (true = on, false = off)
 *
 * Known matching models (non-exhaustive):
 *   _TZ3000_* / TS0001
 *   _TZ3000_* / TS0011
 */
constexpr uint8_t kDpPower = 1U;

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

bool is_switch_model(const char* model) noexcept {
    if (model == nullptr || model[0] == '\0') {
        return false;
    }
    return std::strcmp(model, "TS0001") == 0 || std::strcmp(model, "TS0011") == 0;
}

}  // namespace

bool TuyaSwitchPlugin::matches(const TuyaFingerprint& fingerprint) const noexcept {
    if (!starts_with(fingerprint.manufacturer, "_TZ")) {
        return false;
    }
    return is_switch_model(fingerprint.model);
}

TuyaPluginResult TuyaSwitchPlugin::translate(
    const TuyaFingerprint& fingerprint,
    const TuyaDpParseResult& dp_result) const noexcept {
    (void)fingerprint;

    TuyaPluginResult result{};

    const TuyaDpItem* power_dp = dp_result.find_dp(kDpPower);
    if (power_dp != nullptr && power_dp->dp_type == TuyaDpType::kBool) {
        result.add(TuyaNormalizedKind::kPowerOn, power_dp->as_bool() ? 1 : 0);
        result.handled = true;
    }

    return result;
}

TuyaDpCommand TuyaSwitchPlugin::encode_command(
    const TuyaFingerprint& fingerprint,
    const TuyaCommandRequest& request) const noexcept {
    (void)fingerprint;

    TuyaDpCommand cmd{};

    if (request.kind == TuyaNormalizedKind::kPowerOn) {
        cmd.supported = true;
        cmd.dp_id = kDpPower;
        cmd.dp_type = TuyaDpType::kBool;
        cmd.value[0] = (request.value != 0) ? 1U : 0U;
        cmd.value_len = 1;
        cmd.endpoint = 1;
    }

    return cmd;
}

}  // namespace service
