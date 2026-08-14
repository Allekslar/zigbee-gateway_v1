/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "nvs_namespace_registry.hpp"

#include <cstring>

namespace service {

namespace {

// Aggregate-initializes a fixed key_patterns array from a short brace list,
// avoiding hand-padding every entry's std::array<NvsKeyPattern, 10> out to
// full width at every call site below.
template <std::size_t N>
constexpr std::array<NvsKeyPattern, kMaxKeyPatternsPerNamespace> make_key_patterns(
    const NvsKeyPattern (&patterns)[N]) {
    static_assert(N <= kMaxKeyPatternsPerNamespace, "too many key patterns for one namespace entry");
    std::array<NvsKeyPattern, kMaxKeyPatternsPerNamespace> result{};
    for (std::size_t i = 0; i < N; ++i) {
        result[i] = patterns[i];
    }
    return result;
}

bool key_matches_pattern(const char* key, const NvsKeyPattern& pattern) {
    if (key == nullptr || pattern.value == nullptr) {
        return false;
    }
    if (pattern.is_prefix) {
        return std::strncmp(key, pattern.value, std::strlen(pattern.value)) == 0;
    }
    return std::strcmp(key, pattern.value) == 0;
}

// True if patterns `a` and `b` (from two different namespace entries) could
// ever both match the same real key string -- exact-exact equality,
// exact-inside-prefix in either direction, or overlapping prefixes (one is
// a prefix of the other).
bool patterns_overlap(const NvsKeyPattern& a, const NvsKeyPattern& b) {
    if (a.value == nullptr || b.value == nullptr) {
        return false;
    }
    if (!a.is_prefix && !b.is_prefix) {
        return std::strcmp(a.value, b.value) == 0;
    }
    if (a.is_prefix && !b.is_prefix) {
        return key_matches_pattern(b.value, a);
    }
    if (!a.is_prefix && b.is_prefix) {
        return key_matches_pattern(a.value, b);
    }
    // Both prefixes: they overlap if one is a prefix of the other.
    const std::size_t len_a = std::strlen(a.value);
    const std::size_t len_b = std::strlen(b.value);
    const std::size_t shorter = len_a < len_b ? len_a : len_b;
    return std::strncmp(a.value, b.value, shorter) == 0;
}

// clang-format off
const std::array<NvsNamespaceEntry, kNvsNamespaceCount> kRegistry = {{
    {
        NvsNamespaceId::kWifiCredentials,
        "zigbee_gateway",  // still the shared legacy namespace; #11 migrates this to a dedicated encrypted one
        "Wi-Fi station credentials (SSID + PSK), consumed by network_manager.cpp/connectivity_manager.cpp. "
        "Currently plaintext under the shared legacy namespace -- FD-21 'Wi-Fi ... credentials/configuration'.",
        NvsResetClassification::kEraseOnFactoryReset,
        /*encryption_required=*/true,
        /*implemented_today=*/true,
        make_key_patterns<2>({
            {"wifi_ssid", false},
            {"wifi_password", false},
        }),
        2,
    },
    {
        NvsNamespaceId::kMqttCredentials,
        "zgw_mqtt_cred",
        "MQTT broker credentials and trust references consumed by mqtt_bridge. Not NVS-backed today -- "
        "compile-time sdkconfig values only (Kconfig `ZGW_MQTT_*`), no default in sdkconfig.defaults*. "
        "Reserved for S6 credential enrollment -- FD-21 'MQTT credentials/configuration'.",
        NvsResetClassification::kEraseOnFactoryReset,
        /*encryption_required=*/true,
        /*implemented_today=*/false,
        {},
        0,
    },
    {
        NvsNamespaceId::kAdminVerifier,
        "zgw_admin_verifier",
        "Administrative credential verifier: PBKDF2-HMAC-SHA256 salt+hash+iteration-count record "
        "(admin_verifier.hpp, plan #5) -- storage only, S6's full AuthenticationService/session login "
        "flow does not exist yet -- FD-21 'admin verifier and sessions'.",
        NvsResetClassification::kEraseOnFactoryReset,
        /*encryption_required=*/true,
        /*implemented_today=*/true,
        make_key_patterns<1>({
            {"admin_verifier", false},
        }),
        1,
    },
    {
        NvsNamespaceId::kTlsIdentity,
        "zgw_tls_identity",
        "Device management TLS private key, current/next certificate slots (FD-17 rotation) and product "
        "CA/trust anchor -- storage interface only (plan #13, tls_provisioning_storage_port.hpp), consumed "
        "by S6's not-yet-implemented authenticated rotation adapter, which owns activation/rollback policy. "
        "This sub-slice does not generate, read or write any real certificate/key material -- FD-21 'device "
        "management TLS identity including current/next certificate slots' and 'product CA/trust anchors' "
        "(both Preserve).",
        NvsResetClassification::kPreserveOnFactoryReset,
        /*encryption_required=*/true,
        /*implemented_today=*/true,
        make_key_patterns<5>({
            {"tls_key_cur", false},
            {"tls_key_nxt", false},
            {"tls_cert_cur", false},
            {"tls_cert_nxt", false},
            {"tls_ca", false},
        }),
        5,
    },
    {
        NvsNamespaceId::kSessionSeed,
        "zgw_session_seed",
        "Session-token seed material for S6's bounded session store. Does not exist yet -- FD-21 "
        "'admin verifier and sessions'.",
        NvsResetClassification::kEraseOnFactoryReset,
        /*encryption_required=*/true,
        /*implemented_today=*/false,
        {},
        0,
    },
    {
        NvsNamespaceId::kManufacturingProvisioning,
        "zgw_mfg_provisioning",
        "Manufacturing provisioning records: eFuse provisioning-template evidence (scripts/"
        "efuse_provisioning_template.py, plan #5), manufacturing proof-of-possession (PoP) -- storage "
        "interface only (plan #13, tls_provisioning_storage_port.hpp) -- and the raw 6-byte manufacturing-"
        "recorded GatewayId (plan #8/FD-17 'reject duplicate or cloned GatewayId enrollment', "
        "gateway_identity_verification.hpp). The two-phase dry-run/burn workflow that would actually "
        "populate any of this (plan #6-#8) remains BLOCKED_SECURITY_PROVISIONING -- FD-21 'manufacturing "
        "proof-of-possession' (Preserve).",
        NvsResetClassification::kPreserveOnFactoryReset,
        /*encryption_required=*/true,
        /*implemented_today=*/true,
        make_key_patterns<3>({
            {"mfg_pop", false},
            {"mfg_efuse_rec", false},
            {"mfg_gateway_id", false},
        }),
        3,
    },
    {
        NvsNamespaceId::kZigbeeNetworkDeviceReporting,
        "zigbee_gateway",
        "Zigbee command timeout/retry config, schema version and per-device reporting profiles -- "
        "config_manager.cpp. FD-21 'Zigbee network keys/pairings, device/descriptor/reporting state'.",
        NvsResetClassification::kEraseOnFactoryReset,
        /*encryption_required=*/false,
        /*implemented_today=*/true,
        make_key_patterns<10>({
            {"cfg_schema_ver", false},
            {"cfg_cmd_tmo_ms", false},
            {"cfg_cmd_retry", false},
            {"cfg_rpt_cnt2", false},
            {"cfg_rpt_cnt", false},     // legacy schema v3 profile-count key
            {"cfg_rpt_count", false},   // legacy schema v2 profile-count key
            {"cmd_tmo_ms", false},      // legacy schema v1/v2 timeout key
            {"cmd_retries", false},     // legacy schema v1/v2 retry key
            {"rptp_", true},            // schema v4 per-profile keys: rptp_d/c/i/r + legacy rptp_k
            {"cfg_rpt_", true},         // schema v2/v3 per-profile keys: cfg_rpt_k00..
        }),
        10,
    },
    {
        NvsNamespaceId::kMatterEndpointState,
        "zigbee_gateway",
        "Matter endpoint assignment records (dual-slot durable store) -- matter_endpoint_registry.cpp. "
        "FD-21 'Matter endpoint map'.",
        NvsResetClassification::kEraseOnFactoryReset,
        /*encryption_required=*/false,
        /*implemented_today=*/true,
        make_key_patterns<2>({
            {"mtep_a", false},
            {"mtep_b", false},
        }),
        2,
    },
    {
        NvsNamespaceId::kCoreDeviceState,
        "zigbee_gateway",
        "Core device/network state snapshot (dual-slot durable store, plus a legacy single-slot key) -- "
        "persisted_state_store.cpp/state_persistence_coordinator.cpp. FD-21 "
        "'device/descriptor/reporting state'.",
        NvsResetClassification::kEraseOnFactoryReset,
        /*encryption_required=*/false,
        /*implemented_today=*/true,
        make_key_patterns<3>({
            {"dstate_a", false},
            {"dstate_b", false},
            {"core_state_v1", false},  // legacy single-slot key, read during migration only
        }),
        3,
    },
    {
        NvsNamespaceId::kOperationJournalDiagnostics,
        "zigbee_gateway",
        "OTA debug breadcrumb/request-id diagnostics (ota_manager.cpp) and an effect-executor test hook "
        "(effect_executor.cpp). Distinct from OperationResultStore (in-RAM only, never NVS-backed) and from "
        "the dedicated FD-21 reset journal (kResetJournal, plan #18). FD-21 'operation/idempotency journal'.",
        NvsResetClassification::kEraseOnFactoryReset,
        /*encryption_required=*/false,
        /*implemented_today=*/true,
        make_key_patterns<3>({
            {"ota_dbg_req", false},
            {"ota_dbg_step", false},
            {"core_rev", false},
        }),
        3,
    },
    {
        NvsNamespaceId::kLegacyMigrationTombstone,
        "zgw_legacy_tombstone",
        "Legacy migration/quarantine/tombstone state. FD-21 names this category explicitly, but no "
        "NVS-persisted data exists yet: S4's one-time MQTT/HA legacy-discovery tombstone sweep tracks its "
        "flag in RAM only (mqtt_bridge.cpp `legacy_discovery_tombstoned_`, reset every reboot); plan #6/#7's "
        "eFuse quarantine state remains BLOCKED_SECURITY_PROVISIONING. Reserved so a future persisted marker "
        "has a pre-classified home instead of landing ad hoc in the legacy namespace.",
        NvsResetClassification::kEraseOnFactoryReset,
        /*encryption_required=*/false,
        /*implemented_today=*/false,
        {},
        0,
    },
    {
        NvsNamespaceId::kResetJournal,
        "zgw_reset_journal",
        "Plan #18's dedicated protected reset-journal storage port (reset_journal_storage_port.hpp). "
        "Represents FD-21's four states (ResetJournalState: kRequested/kErasing/kReinitialized/"
        "kCommissioningReady) as a single atomically-written u32. Survives erasing every other "
        "namespace above by construction: factory_reset_namespace_erase.hpp's erase_namespace() refuses "
        "outright for any namespace not classified kEraseOnFactoryReset, and this namespace is "
        "kResetJournalOnly. The actual reset flow that would drive this port through its real states "
        "is not implemented yet (S8 scope).",
        NvsResetClassification::kResetJournalOnly,
        /*encryption_required=*/false,
        /*implemented_today=*/true,
        make_key_patterns<1>({
            {"reset_journal", false},
        }),
        1,
    },
}};
// clang-format on

}  // namespace

const std::array<NvsNamespaceEntry, kNvsNamespaceCount>& nvs_namespace_registry() { return kRegistry; }

const NvsNamespaceEntry& find_nvs_namespace_entry(NvsNamespaceId id) {
    return kRegistry[static_cast<std::size_t>(id)];
}

const NvsNamespaceEntry* find_owning_namespace_for_key(const char* key) {
    if (key == nullptr || key[0] == '\0') {
        return nullptr;
    }
    for (const NvsNamespaceEntry& entry : kRegistry) {
        for (std::size_t i = 0; i < entry.key_pattern_count; ++i) {
            if (key_matches_pattern(key, entry.key_patterns[i])) {
                return &entry;
            }
        }
    }
    return nullptr;
}

bool nvs_namespace_entries_conflict(const NvsNamespaceEntry& a, const NvsNamespaceEntry& b) {
    for (std::size_t pa = 0; pa < a.key_pattern_count; ++pa) {
        for (std::size_t pb = 0; pb < b.key_pattern_count; ++pb) {
            if (patterns_overlap(a.key_patterns[pa], b.key_patterns[pb])) {
                return true;
            }
        }
    }
    return false;
}

NvsRegistryValidationResult validate_nvs_namespace_registry() {
    NvsRegistryValidationResult result{};

    auto push_violation = [&result](NvsRegistryViolationKind kind, NvsNamespaceId a, NvsNamespaceId b) {
        if (result.violation_count < kMaxNvsRegistryViolations) {
            result.violations[result.violation_count] = NvsRegistryViolation{kind, a, b};
            ++result.violation_count;
        }
    };

    for (const NvsNamespaceEntry& entry : kRegistry) {
        if (entry.id == NvsNamespaceId::kCount) {
            continue;
        }
        const bool has_patterns = entry.key_pattern_count > 0;
        if (entry.implemented_today != has_patterns) {
            push_violation(NvsRegistryViolationKind::kImplementedFlagKeyCountMismatch, entry.id, entry.id);
        }
    }

    for (std::size_t i = 0; i < kRegistry.size(); ++i) {
        for (std::size_t j = i + 1; j < kRegistry.size(); ++j) {
            if (nvs_namespace_entries_conflict(kRegistry[i], kRegistry[j])) {
                push_violation(NvsRegistryViolationKind::kDuplicateKeyClaim, kRegistry[i].id, kRegistry[j].id);
            }
        }
    }

    return result;
}

}  // namespace service
