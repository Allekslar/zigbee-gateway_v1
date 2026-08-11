/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "nvs_namespace_registry.hpp"

namespace {

using service::NvsNamespaceEntry;
using service::NvsNamespaceId;
using service::NvsResetClassification;
using service::ResetJournalState;

// The plan's own Tests section names this exact check ("NVS namespace
// ownership and duplicate-key inventory"): the registry as shipped must be
// internally consistent -- no two namespace entries may claim overlapping
// keys, and every entry's implemented_today flag must agree with whether
// it actually lists any key patterns.
void test_registry_as_shipped_has_no_violations() {
    const auto result = service::validate_nvs_namespace_registry();
    assert(result.ok());
    assert(result.violation_count == 0U);
}

void test_registry_has_one_entry_per_real_namespace_id() {
    const auto& registry = service::nvs_namespace_registry();
    assert(registry.size() == service::kNvsNamespaceCount);
    for (std::size_t i = 0; i < registry.size(); ++i) {
        assert(registry[i].id == static_cast<NvsNamespaceId>(i));
    }
}

void test_find_nvs_namespace_entry_matches_direct_index() {
    const NvsNamespaceEntry& wifi = service::find_nvs_namespace_entry(NvsNamespaceId::kWifiCredentials);
    assert(wifi.id == NvsNamespaceId::kWifiCredentials);
    const NvsNamespaceEntry& reset_journal = service::find_nvs_namespace_entry(NvsNamespaceId::kResetJournal);
    assert(reset_journal.id == NvsNamespaceId::kResetJournal);
}

void test_find_owning_namespace_for_key_exact_match() {
    const NvsNamespaceEntry* owner = service::find_owning_namespace_for_key("wifi_ssid");
    assert(owner != nullptr);
    assert(owner->id == NvsNamespaceId::kWifiCredentials);

    owner = service::find_owning_namespace_for_key("mtep_a");
    assert(owner != nullptr);
    assert(owner->id == NvsNamespaceId::kMatterEndpointState);
}

void test_find_owning_namespace_for_key_prefix_match() {
    // Schema v4's dynamically-built per-reporting-profile keys
    // (config_manager.cpp's build_profile_nvs_key: "rptp_%c%02u").
    const NvsNamespaceEntry* owner = service::find_owning_namespace_for_key("rptp_d07");
    assert(owner != nullptr);
    assert(owner->id == NvsNamespaceId::kZigbeeNetworkDeviceReporting);

    owner = service::find_owning_namespace_for_key("rptp_r15");
    assert(owner != nullptr);
    assert(owner->id == NvsNamespaceId::kZigbeeNetworkDeviceReporting);

    // Legacy schema v2/v3 per-profile keys (build_legacy_v2_profile_nvs_key:
    // "cfg_rpt_%c%02u").
    owner = service::find_owning_namespace_for_key("cfg_rpt_k03");
    assert(owner != nullptr);
    assert(owner->id == NvsNamespaceId::kZigbeeNetworkDeviceReporting);
}

void test_find_owning_namespace_for_key_unknown_returns_null() {
    assert(service::find_owning_namespace_for_key("totally_unknown_key") == nullptr);
    assert(service::find_owning_namespace_for_key(nullptr) == nullptr);
    assert(service::find_owning_namespace_for_key("") == nullptr);
}

// Synthetic entries with a deliberately overlapping pair of key patterns
// must be caught by nvs_namespace_entries_conflict() -- proves the
// conflict-detection primitive validate_nvs_namespace_registry() runs over
// every entry pair actually detects the failure mode it exists for, not
// just that the current shipped registry happens to already be clean.
void test_duplicate_key_claim_is_detected_exact_vs_exact() {
    NvsNamespaceEntry entry_a{};
    entry_a.id = NvsNamespaceId::kWifiCredentials;
    entry_a.key_patterns[0] = {"shared_key", false};
    entry_a.key_pattern_count = 1;

    NvsNamespaceEntry entry_b{};
    entry_b.id = NvsNamespaceId::kMqttCredentials;
    entry_b.key_patterns[0] = {"shared_key", false};
    entry_b.key_pattern_count = 1;

    assert(service::nvs_namespace_entries_conflict(entry_a, entry_b));
}

void test_duplicate_key_claim_is_detected_exact_vs_prefix() {
    NvsNamespaceEntry entry_a{};
    entry_a.id = NvsNamespaceId::kZigbeeNetworkDeviceReporting;
    entry_a.key_patterns[0] = {"rptp_d00", false};
    entry_a.key_pattern_count = 1;

    NvsNamespaceEntry entry_b{};
    entry_b.id = NvsNamespaceId::kMatterEndpointState;
    entry_b.key_patterns[0] = {"rptp_", true};
    entry_b.key_pattern_count = 1;

    assert(service::nvs_namespace_entries_conflict(entry_a, entry_b));
}

void test_duplicate_key_claim_is_detected_overlapping_prefixes() {
    NvsNamespaceEntry entry_a{};
    entry_a.id = NvsNamespaceId::kZigbeeNetworkDeviceReporting;
    entry_a.key_patterns[0] = {"rptp_", true};
    entry_a.key_pattern_count = 1;

    NvsNamespaceEntry entry_b{};
    entry_b.id = NvsNamespaceId::kMatterEndpointState;
    entry_b.key_patterns[0] = {"rptp_d", true};
    entry_b.key_pattern_count = 1;

    assert(service::nvs_namespace_entries_conflict(entry_a, entry_b));
}

void test_disjoint_keys_do_not_conflict() {
    NvsNamespaceEntry entry_a{};
    entry_a.id = NvsNamespaceId::kWifiCredentials;
    entry_a.key_patterns[0] = {"wifi_ssid", false};
    entry_a.key_pattern_count = 1;

    NvsNamespaceEntry entry_b{};
    entry_b.id = NvsNamespaceId::kMqttCredentials;
    entry_b.key_patterns[0] = {"mqtt_broker_uri", false};
    entry_b.key_pattern_count = 1;

    assert(!service::nvs_namespace_entries_conflict(entry_a, entry_b));
}

void test_implemented_flag_agrees_with_key_pattern_count() {
    for (const NvsNamespaceEntry& entry : service::nvs_namespace_registry()) {
        assert(entry.implemented_today == (entry.key_pattern_count > 0));
    }
}

// FD-21's exact preserve/erase split (docs/implementation/
// PRODUCTION_HARDENING_PLAN.md, "Restart-safe factory reset") is this
// registry's classification source of truth -- pin the classification of
// every entry against it directly so a future edit that silently
// reclassifies e.g. Wi-Fi credentials as Preserve fails loudly here.
void test_fd21_preserve_classification() {
    assert(
        service::find_nvs_namespace_entry(NvsNamespaceId::kTlsIdentity).reset_classification ==
        NvsResetClassification::kPreserveOnFactoryReset);
    assert(
        service::find_nvs_namespace_entry(NvsNamespaceId::kManufacturingProvisioning).reset_classification ==
        NvsResetClassification::kPreserveOnFactoryReset);
}

void test_fd21_erase_classification() {
    const NvsNamespaceId erase_ids[] = {
        NvsNamespaceId::kWifiCredentials,
        NvsNamespaceId::kMqttCredentials,
        NvsNamespaceId::kAdminVerifier,
        NvsNamespaceId::kSessionSeed,
        NvsNamespaceId::kZigbeeNetworkDeviceReporting,
        NvsNamespaceId::kMatterEndpointState,
        NvsNamespaceId::kCoreDeviceState,
        NvsNamespaceId::kOperationJournalDiagnostics,
        NvsNamespaceId::kLegacyMigrationTombstone,
    };
    for (NvsNamespaceId id : erase_ids) {
        assert(
            service::find_nvs_namespace_entry(id).reset_classification ==
            NvsResetClassification::kEraseOnFactoryReset);
    }
}

void test_reset_journal_is_its_own_classification() {
    assert(
        service::find_nvs_namespace_entry(NvsNamespaceId::kResetJournal).reset_classification ==
        NvsResetClassification::kResetJournalOnly);
}

// Every entry must have exactly one of the three classifications -- with a
// scoped enum and no default-constructed "unclassified" value in the
// registry table itself, this is enforced by construction; this test
// documents that invariant explicitly rather than leaving it implicit.
void test_every_entry_has_a_classification_from_the_closed_set() {
    for (const NvsNamespaceEntry& entry : service::nvs_namespace_registry()) {
        const bool is_known_classification =
            entry.reset_classification == NvsResetClassification::kPreserveOnFactoryReset ||
            entry.reset_classification == NvsResetClassification::kEraseOnFactoryReset ||
            entry.reset_classification == NvsResetClassification::kResetJournalOnly;
        assert(is_known_classification);
    }
}

void test_encryption_required_matches_plan_9_list() {
    const NvsNamespaceId plan_9_ids[] = {
        NvsNamespaceId::kWifiCredentials,
        NvsNamespaceId::kMqttCredentials,
        NvsNamespaceId::kAdminVerifier,
        NvsNamespaceId::kTlsIdentity,
        NvsNamespaceId::kSessionSeed,
        NvsNamespaceId::kManufacturingProvisioning,
    };
    for (NvsNamespaceId id : plan_9_ids) {
        assert(service::find_nvs_namespace_entry(id).encryption_required);
    }

    const NvsNamespaceId pre_existing_ids[] = {
        NvsNamespaceId::kZigbeeNetworkDeviceReporting,
        NvsNamespaceId::kMatterEndpointState,
        NvsNamespaceId::kCoreDeviceState,
        NvsNamespaceId::kOperationJournalDiagnostics,
        NvsNamespaceId::kLegacyMigrationTombstone,
        NvsNamespaceId::kResetJournal,
    };
    for (NvsNamespaceId id : pre_existing_ids) {
        assert(!service::find_nvs_namespace_entry(id).encryption_required);
    }
}

void test_reset_journal_state_has_exactly_fd21_four_states() {
    // FD-21: "requested -> erasing -> reinitialized -> commissioning_ready".
    assert(static_cast<uint8_t>(ResetJournalState::kRequested) == 0U);
    assert(static_cast<uint8_t>(ResetJournalState::kErasing) == 1U);
    assert(static_cast<uint8_t>(ResetJournalState::kReinitialized) == 2U);
    assert(static_cast<uint8_t>(ResetJournalState::kCommissioningReady) == 3U);
}

}  // namespace

int main() {
    test_registry_as_shipped_has_no_violations();
    test_registry_has_one_entry_per_real_namespace_id();
    test_find_nvs_namespace_entry_matches_direct_index();
    test_find_owning_namespace_for_key_exact_match();
    test_find_owning_namespace_for_key_prefix_match();
    test_find_owning_namespace_for_key_unknown_returns_null();
    test_duplicate_key_claim_is_detected_exact_vs_exact();
    test_duplicate_key_claim_is_detected_exact_vs_prefix();
    test_duplicate_key_claim_is_detected_overlapping_prefixes();
    test_disjoint_keys_do_not_conflict();
    test_implemented_flag_agrees_with_key_pattern_count();
    test_fd21_preserve_classification();
    test_fd21_erase_classification();
    test_reset_journal_is_its_own_classification();
    test_every_entry_has_a_classification_from_the_closed_set();
    test_encryption_required_matches_plan_9_list();
    test_reset_journal_state_has_exactly_fd21_four_states();
    return 0;
}
