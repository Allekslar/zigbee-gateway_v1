/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "unity.h"

void test_hal_nvs_set_get_roundtrip(void);
void test_hal_nvs_missing_key_returns_error(void);
void test_hal_zigbee_notifies_registered_callbacks(void);
void test_hal_zigbee_rejects_null_callbacks(void);
void test_config_manager_migrates_legacy_v1(void);
void test_service_join_policy_deduplicates_candidates(void);
void test_web_api_http_command_result_updates_snapshot(void);
void test_web_api_end_to_end_zigbee_core_effects_http_flow(void);
void test_web_api_network_scan_result_status_transitions(void);
void test_web_api_network_scan_result_failure_transition(void);
void test_web_api_devices_snapshot_consistency_under_runtime_updates(void);

void app_main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_hal_nvs_set_get_roundtrip);
    RUN_TEST(test_hal_nvs_missing_key_returns_error);
    RUN_TEST(test_hal_zigbee_notifies_registered_callbacks);
    RUN_TEST(test_hal_zigbee_rejects_null_callbacks);
    RUN_TEST(test_config_manager_migrates_legacy_v1);
    RUN_TEST(test_service_join_policy_deduplicates_candidates);
    RUN_TEST(test_web_api_http_command_result_updates_snapshot);
    RUN_TEST(test_web_api_end_to_end_zigbee_core_effects_http_flow);
    RUN_TEST(test_web_api_network_scan_result_status_transitions);
    RUN_TEST(test_web_api_network_scan_result_failure_transition);
    RUN_TEST(test_web_api_devices_snapshot_consistency_under_runtime_updates);

    (void)UNITY_END();
}
