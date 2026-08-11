/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "factory_reset_namespace_erase.hpp"
#include "hal_nvs.h"
#include "hal_security_state_test.h"

namespace {

using service::NamespaceEraseResult;
using service::NvsNamespaceId;

// Plan #17's own mandate: a namespace not classified kEraseOnFactoryReset
// must be refused outright, before any key is touched. kTlsIdentity is
// PRESERVE_ON_FACTORY_RESET.
void test_erase_namespace_refuses_preserve_classified() {
    hal_security_state_set_mock_flash_encryption_enabled(true);
    assert(hal_nvs_set_blob("tls_ca", "x", 1U) == HAL_NVS_STATUS_OK);
    hal_security_state_reset_mock_flash_encryption_enabled();

    const NamespaceEraseResult result = service::erase_namespace(NvsNamespaceId::kTlsIdentity);
    assert(result == NamespaceEraseResult::kRefusedNotErasable);

    char readback[2]{};
    uint32_t readback_len = 0U;
    assert(hal_nvs_get_blob("tls_ca", readback, sizeof(readback), &readback_len) == HAL_NVS_STATUS_OK);
}

// Plan #18's "survives erasing user/application namespaces" property,
// proven concretely: kResetJournal is kResetJournalOnly, not
// kEraseOnFactoryReset, so it must be refused exactly like a Preserve
// namespace.
void test_erase_namespace_refuses_reset_journal() {
    assert(hal_nvs_set_u32("reset_journal", 0U) == HAL_NVS_STATUS_OK);

    const NamespaceEraseResult result = service::erase_namespace(NvsNamespaceId::kResetJournal);
    assert(result == NamespaceEraseResult::kRefusedNotErasable);

    uint32_t readback = 0xFFFFFFFFU;
    assert(hal_nvs_get_u32("reset_journal", &readback) == HAL_NVS_STATUS_OK);
    assert(readback == 0U);
}

void test_erase_namespace_erases_all_exact_keys() {
    const uint8_t payload[2] = {0x01, 0x02};
    assert(hal_nvs_set_blob("mtep_a", payload, sizeof(payload)) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_blob("mtep_b", payload, sizeof(payload)) == HAL_NVS_STATUS_OK);

    const NamespaceEraseResult result = service::erase_namespace(NvsNamespaceId::kMatterEndpointState);
    assert(result == NamespaceEraseResult::kErased);

    uint8_t readback[2]{};
    uint32_t readback_len = 0U;
    assert(hal_nvs_get_blob("mtep_a", readback, sizeof(readback), &readback_len) == HAL_NVS_STATUS_NOT_FOUND);
    assert(hal_nvs_get_blob("mtep_b", readback, sizeof(readback), &readback_len) == HAL_NVS_STATUS_NOT_FOUND);
}

// Documents and proves the stated limitation: erase_namespace() erases
// exact key patterns but deliberately skips prefix ones.
void test_erase_namespace_skips_prefix_patterns() {
    assert(hal_nvs_set_u32("cfg_cmd_tmo_ms", 5000U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32("rptp_d00", 0xAAU) == HAL_NVS_STATUS_OK);

    const NamespaceEraseResult result = service::erase_namespace(NvsNamespaceId::kZigbeeNetworkDeviceReporting);
    assert(result == NamespaceEraseResult::kErased);

    uint32_t readback = 0U;
    assert(hal_nvs_get_u32("cfg_cmd_tmo_ms", &readback) == HAL_NVS_STATUS_NOT_FOUND);
    // The prefix-pattern key is untouched by erase_namespace() itself --
    // erase_namespace_key_range() is required to clear it, exercised
    // below.
    assert(hal_nvs_get_u32("rptp_d00", &readback) == HAL_NVS_STATUS_OK);
    assert(readback == 0xAAU);
}

// Grounded in the real key-building shape (config_manager.cpp's
// build_profile_nvs_key: "rptp_%c%02u" for c in "dcirk",
// ConfigManager::kMaxReportingProfiles == 16) -- proves the range
// primitive actually clears the real production keyspace, not a
// hypothetical one.
void test_erase_namespace_key_range_clears_real_reporting_profile_shape() {
    assert(hal_nvs_set_u32("rptp_d00", 1U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32("rptp_c07", 2U) == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_u32("rptp_k15", 3U) == HAL_NVS_STATUS_OK);

    const NamespaceEraseResult result = service::erase_namespace_key_range(
        NvsNamespaceId::kZigbeeNetworkDeviceReporting, "rptp_", "dcirk", 5U, 16U);
    assert(result == NamespaceEraseResult::kErased);

    uint32_t readback = 0U;
    assert(hal_nvs_get_u32("rptp_d00", &readback) == HAL_NVS_STATUS_NOT_FOUND);
    assert(hal_nvs_get_u32("rptp_c07", &readback) == HAL_NVS_STATUS_NOT_FOUND);
    assert(hal_nvs_get_u32("rptp_k15", &readback) == HAL_NVS_STATUS_NOT_FOUND);
}

void test_erase_namespace_key_range_refuses_preserve_classified() {
    hal_security_state_set_mock_flash_encryption_enabled(true);
    assert(hal_nvs_set_blob("mfg_pop", "x", 1U) == HAL_NVS_STATUS_OK);
    hal_security_state_reset_mock_flash_encryption_enabled();

    const NamespaceEraseResult result =
        service::erase_namespace_key_range(NvsNamespaceId::kManufacturingProvisioning, "mfg_", "p", 1U, 1U);
    assert(result == NamespaceEraseResult::kRefusedNotErasable);

    char readback[2]{};
    uint32_t readback_len = 0U;
    assert(hal_nvs_get_blob("mfg_pop", readback, sizeof(readback), &readback_len) == HAL_NVS_STATUS_OK);
}

void test_erase_namespace_key_range_rejects_null_arguments() {
    const NamespaceEraseResult result_null_prefix =
        service::erase_namespace_key_range(NvsNamespaceId::kZigbeeNetworkDeviceReporting, nullptr, "d", 1U, 1U);
    assert(result_null_prefix == NamespaceEraseResult::kPartialFailure);

    const NamespaceEraseResult result_null_suffix =
        service::erase_namespace_key_range(NvsNamespaceId::kZigbeeNetworkDeviceReporting, "rptp_", nullptr, 1U, 1U);
    assert(result_null_suffix == NamespaceEraseResult::kPartialFailure);
}

}  // namespace

int main() {
    test_erase_namespace_refuses_preserve_classified();
    test_erase_namespace_refuses_reset_journal();
    test_erase_namespace_erases_all_exact_keys();
    test_erase_namespace_skips_prefix_patterns();
    test_erase_namespace_key_range_clears_real_reporting_profile_shape();
    test_erase_namespace_key_range_refuses_preserve_classified();
    test_erase_namespace_key_range_rejects_null_arguments();
    return 0;
}
