/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace service {

// Plan S5 required changes #9 and #15
// (docs/implementation/PRODUCTION_HARDENING_PLAN.md, Stage S5, "Encrypted
// storage foundation"): "Create explicit encrypted NVS namespaces/ownership
// for [...]" and "Classify every eFuse/partition/NVS namespace/key as
// PRESERVE_ON_FACTORY_RESET, ERASE_ON_FACTORY_RESET or RESET_JOURNAL_ONLY;
// an unclassified namespace blocks S8 reset implementation."
//
// This header is the foundation for the rest of S5's "Encrypted storage
// foundation" cluster (#10-#18) but does not itself implement them: no
// storage-port typed results (#10), no migration scaffolding (#11), no
// runtime encryption-verified gate (#12), no TLS/provisioning storage
// interfaces (#13), no redaction (#14), no erase primitives (#16/#17), no
// reset-journal storage port (#18). Those each need this registry to exist
// first and are separate, later sub-slices -- see
// docs/security/PRODUCTION_HARDENING.md for the up-to-date status split.
//
// Every entry below is derived from a real, grepped inventory of this
// repository's actual `hal_nvs_*` call sites (config_manager.cpp,
// connectivity_manager.cpp, network_manager.cpp, matter_endpoint_registry.cpp,
// persisted_state_store.cpp, state_persistence_coordinator.cpp,
// ota_manager.cpp, effect_executor.cpp) -- not invented. Every namespace
// still shares the single hardcoded ESP-IDF NVS namespace string
// "zigbee_gateway" that `hal_nvs.c` bakes in today (see `nvs_namespace`
// below, and `hal_nvs.h`'s own docstring once #11 lands); this registry
// records the *conceptual* ownership/classification split that #11's
// restart-safe migration will later use to actually separate them into
// real, distinct ESP-IDF NVS namespaces.
//
// FD-21 (docs/implementation/PRODUCTION_HARDENING_PLAN.md, "Restart-safe
// factory reset") is this registry's classification source of truth:
//   Preserve: eFuse/security state, Secure Boot and Flash/NVS encryption
//     key material, factory GatewayId, manufacturing proof-of-possession,
//     product CA/trust anchors and device management TLS identity
//     including current/next certificate slots.
//   Erase: admin verifier and sessions, Wi-Fi and MQTT
//     credentials/configuration, Zigbee network keys/pairings,
//     device/descriptor/reporting state, Matter endpoint map,
//     operation/idempotency journal and legacy migration/quarantine/
//     tombstone state.
// Three of FD-21's "Preserve" items are NOT NVS namespaces at all and so
// deliberately have no entry below: eFuse/security state and Secure
// Boot/Flash/NVS encryption key material live in eFuse and the
// hardware-managed `nvs_keys` partition (see `partitions.production.csv`),
// not an app-owned NVS namespace; the factory GatewayId is derived at
// runtime from the eFuse-backed factory base MAC
// (`hal_identity_get_factory_base_mac`) and is never written to NVS at all.
// All three are preserved trivially, by construction -- there is nothing
// app-erasable to classify. "Product CA/trust anchors" is folded into
// `kTlsIdentity`'s ownership description below, alongside the device
// management TLS private key/certificate slots it is inseparable from.

enum class NvsResetClassification : uint8_t {
    kPreserveOnFactoryReset = 0,
    kEraseOnFactoryReset = 1,
    kResetJournalOnly = 2,
};

enum class NvsNamespaceId : uint8_t {
    // Plan #9's explicit encrypted-namespace list, in the order the plan
    // names them.
    kWifiCredentials = 0,
    kMqttCredentials,
    kAdminVerifier,
    kTlsIdentity,
    kSessionSeed,
    kManufacturingProvisioning,
    // Existing namespaces this registry classifies for FD-21/#15, even
    // though they predate S5 and are not part of plan #9's "new encrypted
    // namespace" list.
    kZigbeeNetworkDeviceReporting,
    kMatterEndpointState,
    kCoreDeviceState,
    kOperationJournalDiagnostics,
    kLegacyMigrationTombstone,
    // Plan #18's dedicated protected reset-journal storage port.
    kResetJournal,
    kCount,  // sentinel -- not a real namespace
};

inline constexpr std::size_t kNvsNamespaceCount = static_cast<std::size_t>(NvsNamespaceId::kCount);

// FD-21/plan #18's exact four reset-journal states: "requested -> erasing
// -> reinitialized -> commissioning_ready". Defined here (not in a later
// #18-specific header) so this registry's `kResetJournal` entry can refer
// to the real state names in its documentation/tests now, even though the
// storage port itself is not implemented until a later sub-slice.
enum class ResetJournalState : uint8_t {
    kRequested = 0,
    kErasing = 1,
    kReinitialized = 2,
    kCommissioningReady = 3,
};

inline constexpr std::size_t kMaxKeyPatternsPerNamespace = 10;

// A single known/reserved NVS key name this namespace owns. `is_prefix`
// covers config_manager.cpp's dynamically-built per-reporting-profile keys
// (e.g. "rptp_" for schema v4's `rptp_d00`.."rptp_r15`, "cfg_rpt_" for
// schema v2/v3's `cfg_rpt_k00` etc.) where enumerating every concrete key
// would just restate `ConfigManager::kMaxReportingProfiles` in another
// form.
struct NvsKeyPattern {
    const char* value{nullptr};
    bool is_prefix{false};
};

struct NvsNamespaceEntry {
    NvsNamespaceId id;
    // The real ESP-IDF NVS namespace string this data lives (or, for
    // `implemented_today == false` entries, will live) under. Entries that
    // predate S5 all say "zigbee_gateway" today -- `hal_nvs.c` hardcodes
    // that single namespace for every call site -- because separating them
    // into their own real namespaces is #11's restart-safe migration job,
    // not this registry's. New (`implemented_today == false`) entries name
    // their intended future namespace directly.
    const char* nvs_namespace;
    const char* owner_description;
    NvsResetClassification reset_classification;
    // Plan #9: these namespaces MUST live under NVS Encryption once
    // populated. Namespaces predating S5 that were never in plan #9's list
    // are not marked required here even though the whole "zigbee_gateway"
    // namespace will incidentally be encrypted too once NVS Encryption is
    // active on real hardware (sdkconfig.production.esp32c6,
    // docs/security/PRODUCTION_HARDENING.md Section 2.1) -- this flag
    // tracks the plan's *requirement*, not an implementation side effect.
    bool encryption_required;
    // false = reserved for a later stage (mostly S6) with no current code
    // path writing to it; true = real hal_nvs_* call sites exist today.
    bool implemented_today;
    std::array<NvsKeyPattern, kMaxKeyPatternsPerNamespace> key_patterns{};
    std::size_t key_pattern_count{0};
};

const std::array<NvsNamespaceEntry, kNvsNamespaceCount>& nvs_namespace_registry();

const NvsNamespaceEntry& find_nvs_namespace_entry(NvsNamespaceId id);

// Returns the entry whose key_patterns claims `key` (exact match, or a
// prefix match against a `is_prefix` pattern), or nullptr if no entry
// claims it. Two different entries claiming overlapping keys is exactly
// what `validate_nvs_namespace_registry()` below checks for; this function
// answers the single-key question "who owns this?" for future call sites
// (e.g. a future storage port wrapper) that want to look up ownership
// without hand-maintaining a second copy of the key list.
const NvsNamespaceEntry* find_owning_namespace_for_key(const char* key);

// True if `a` and `b`'s key_patterns could ever both match the same real
// key string (exact-exact equality, exact-inside-prefix in either
// direction, or overlapping prefixes). This is the pairwise primitive
// `validate_nvs_namespace_registry()` runs over every entry pair below;
// exposed directly so tests can exercise the conflict-detection logic
// itself against synthetic entries, not just assert the shipped registry
// happens to already be clean.
bool nvs_namespace_entries_conflict(const NvsNamespaceEntry& a, const NvsNamespaceEntry& b);

enum class NvsRegistryViolationKind : uint8_t {
    // Two different NvsNamespaceId entries both claim a key (exact-exact,
    // exact-inside-prefix, or overlapping prefixes) -- the plan's own Tests
    // section names this exact check ("NVS namespace ownership and
    // duplicate-key inventory").
    kDuplicateKeyClaim = 0,
    // `implemented_today` and `key_pattern_count` disagree (true with zero
    // patterns, or false with any patterns) -- catches a future edit that
    // updates one without the other.
    kImplementedFlagKeyCountMismatch = 1,
};

struct NvsRegistryViolation {
    NvsRegistryViolationKind kind;
    NvsNamespaceId namespace_a;
    // Only meaningful for kDuplicateKeyClaim; equals namespace_a otherwise.
    NvsNamespaceId namespace_b;
};

inline constexpr std::size_t kMaxNvsRegistryViolations = 16;

struct NvsRegistryValidationResult {
    std::array<NvsRegistryViolation, kMaxNvsRegistryViolations> violations{};
    std::size_t violation_count{0};

    bool ok() const noexcept { return violation_count == 0; }
};

NvsRegistryValidationResult validate_nvs_namespace_registry();

}  // namespace service
