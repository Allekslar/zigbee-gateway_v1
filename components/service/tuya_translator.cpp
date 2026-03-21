/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "tuya_translator.hpp"

namespace service {

TuyaTranslateResult TuyaTranslator::translate(
    const TuyaPayloadView& view,
    const TuyaFingerprint& fingerprint) const noexcept {
    TuyaTranslateResult result{};

    if (!view.is_valid() || !view.is_tuya_cluster()) {
        return result;
    }
    result.routed = true;

    TuyaDpParseResult dp_result = parser_.parse(view);
    if (dp_result.status != TuyaDpParseStatus::kOk) {
        return result;
    }
    result.parsed = true;

    const TuyaPlugin* plugin = registry_.resolve(fingerprint);
    if (plugin == nullptr) {
        return result;
    }
    result.plugin_found = true;

    result.plugin_result = plugin->translate(fingerprint, dp_result);
    return result;
}

}  // namespace service
