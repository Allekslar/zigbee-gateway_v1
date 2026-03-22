/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "tuya_dp_parser.hpp"
#include "tuya_fingerprint.hpp"
#include "tuya_payload_view.hpp"
#include "tuya_plugin.hpp"
#include "tuya_plugin_registry.hpp"

namespace service {

struct TuyaTranslateResult {
    bool routed{false};
    bool parsed{false};
    bool plugin_found{false};
    TuyaPluginResult plugin_result{};
};

struct TuyaCommandEncodeResult {
    bool plugin_found{false};
    TuyaDpCommand dp_command{};
};

class TuyaTranslator {
public:
    TuyaTranslateResult translate(
        const TuyaPayloadView& view,
        const TuyaFingerprint& fingerprint) const noexcept;

    TuyaCommandEncodeResult encode_command(
        const TuyaFingerprint& fingerprint,
        const TuyaCommandRequest& request) const noexcept;

    bool has_plugin(const TuyaFingerprint& fingerprint) const noexcept;

    TuyaInitPlan get_init_plan(const TuyaFingerprint& fingerprint) const noexcept;

private:
    TuyaDpParser parser_{};
    TuyaPluginRegistry registry_{};
};

}  // namespace service
