/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "tuya_plugin.hpp"

namespace service {

class TuyaSwitchPlugin final : public TuyaPlugin {
public:
    bool matches(const TuyaFingerprint& fingerprint) const noexcept override;

    TuyaPluginResult translate(
        const TuyaFingerprint& fingerprint,
        const TuyaDpParseResult& dp_result) const noexcept override;

    TuyaDpCommand encode_command(
        const TuyaFingerprint& fingerprint,
        const TuyaCommandRequest& request) const noexcept override;
};

}  // namespace service
