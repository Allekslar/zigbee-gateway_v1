/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "config_manager.hpp"
#include "hal_nvs.h"
#include "reporting_manager.hpp"

namespace {

void test_class_default_profile_is_used() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);

    service::ConfigManager config;
    assert(config.load());

    service::ReportingManager manager;
    service::ConfigManager::ReportingProfile profile{};
    assert(manager.resolve_profile_for_device(config, 0x2201U, 1U, 0x0402U, &profile));
    assert(profile.in_use);
    assert(profile.key.short_addr == 0x2201U);
    assert(profile.key.endpoint == 1U);
    assert(profile.key.cluster_id == 0x0402U);
    assert(profile.min_interval_seconds == 5U);
    assert(profile.max_interval_seconds == 300U);
    assert(profile.reportable_change == 10U);
    assert(config.motion_occupancy_debounce_ms() > 0U);
    assert(config.motion_occupancy_hold_ms() > 0U);
}

void test_per_device_override_has_priority_over_class_default() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);

    service::ConfigManager config;
    assert(config.load());

    service::ConfigManager::ReportingProfile override_profile{};
    override_profile.in_use = true;
    override_profile.key.short_addr = 0x2202U;
    override_profile.key.endpoint = 1U;
    override_profile.key.cluster_id = 0x0402U;
    override_profile.min_interval_seconds = 30U;
    override_profile.max_interval_seconds = 900U;
    override_profile.reportable_change = 50U;
    override_profile.capability_flags = 0xAAU;
    assert(config.set_reporting_profile(override_profile));

    service::ReportingManager manager;
    service::ConfigManager::ReportingProfile resolved{};
    assert(manager.resolve_profile_for_device(config, 0x2202U, 1U, 0x0402U, &resolved));
    assert(resolved.min_interval_seconds == 30U);
    assert(resolved.max_interval_seconds == 900U);
    assert(resolved.reportable_change == 50U);
    assert(resolved.capability_flags == 0xAAU);
}

void test_custom_class_default_applies_to_new_devices() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);

    service::ConfigManager config;
    assert(config.load());

    service::ConfigManager::ReportingPolicyDefault custom_motion{};
    custom_motion.in_use = true;
    custom_motion.cluster_id = 0x0406U;
    custom_motion.min_interval_seconds = 2U;
    custom_motion.max_interval_seconds = 45U;
    custom_motion.reportable_change = 0U;
    custom_motion.capability_flags = 0x44U;
    assert(config.set_reporting_policy_default(service::ConfigManager::ReportingDeviceClass::kMotion, custom_motion));

    service::ReportingManager manager;
    service::ConfigManager::ReportingProfile resolved{};
    assert(manager.resolve_profile_for_device(config, 0x2203U, 1U, 0x0406U, &resolved));
    assert(resolved.min_interval_seconds == 2U);
    assert(resolved.max_interval_seconds == 45U);
    assert(resolved.capability_flags == 0x44U);
    assert(config.motion_occupancy_debounce_ms() == 0U);
    assert(config.motion_occupancy_hold_ms() == 0U);
}

void test_unknown_cluster_has_no_default_profile() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);

    service::ConfigManager config;
    assert(config.load());

    service::ReportingManager manager;
    service::ConfigManager::ReportingProfile resolved{};
    assert(!manager.resolve_profile_for_device(config, 0x2204U, 1U, 0x1234U, &resolved));
}

}  // namespace

int main() {
    test_class_default_profile_is_used();
    test_per_device_override_has_priority_over_class_default();
    test_custom_class_default_applies_to_new_devices();
    test_unknown_cluster_has_no_default_profile();
    return 0;
}
