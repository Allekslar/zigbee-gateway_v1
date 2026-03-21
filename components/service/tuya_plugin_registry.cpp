/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "tuya_plugin_registry.hpp"

#include "tuya_contact_sensor_plugin.hpp"

namespace service {

namespace {

const TuyaContactSensorPlugin kContactSensorPlugin{};

const TuyaPlugin* const kAllPlugins[] = {
    &kContactSensorPlugin,
};

constexpr std::size_t kPluginCount = sizeof(kAllPlugins) / sizeof(kAllPlugins[0]);

}  // namespace

const TuyaPlugin* TuyaPluginRegistry::resolve(const TuyaFingerprint& fingerprint) const noexcept {
    for (std::size_t i = 0; i < kPluginCount; ++i) {
        if (kAllPlugins[i]->matches(fingerprint)) {
            return kAllPlugins[i];
        }
    }
    return nullptr;
}

std::size_t TuyaPluginRegistry::plugin_count() const noexcept {
    return kPluginCount;
}

}  // namespace service
