/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>

#include "tuya_fingerprint.hpp"
#include "tuya_plugin.hpp"

namespace service {

class TuyaPluginRegistry {
public:
    const TuyaPlugin* resolve(const TuyaFingerprint& fingerprint) const noexcept;
    std::size_t plugin_count() const noexcept;
};

}  // namespace service
