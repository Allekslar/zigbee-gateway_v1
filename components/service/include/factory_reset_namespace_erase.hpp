/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "nvs_namespace_registry.hpp"

namespace service {

// Plan S5 required changes #16 and #17 (Encrypted storage foundation).
// #16: "Preserve eFuse/security state, flash/NVS encryption key material,
// factory GatewayId source, manufacturing PoP, product CA/trust anchors
// and encrypted device TLS current/next slots."
// #17: "Mark admin/session, Wi-Fi, MQTT, Zigbee network/device/reporting,
// Matter, operation journal and legacy migration/tombstone data for
// erase. Provide typed namespace erase operations; do not use broad
// whole-partition erase that can touch preserved material."
//
// #16's classification itself was already completed by Section 2.7's
// registry (plan #15) -- every namespace already carries exactly one
// NvsResetClassification. What #16/#17 add here is the *enforcement*: a
// typed erase operation that is structurally incapable of touching a
// namespace classified anything other than kEraseOnFactoryReset, no
// matter what a caller passes in. This is the plan's own "do not use
// broad whole-partition erase that can touch preserved material" -- the
// refusal happens before any key is touched, not as an after-the-fact
// check.
//
// This is scaffolding, matching every other S5 sub-slice's discipline:
// nothing in this repository calls erase_namespace() or
// erase_namespace_key_range() -- there is no real factory-reset flow yet
// (that is S8's job, built on top of this and Section 2.10's migration
// scaffolding). Built directly on secure_storage_erase (Section 2.10), so
// its own namespace-ownership gate and idempotent-erase semantics apply
// automatically.

enum class NamespaceEraseResult : uint8_t {
    // Every key pattern (or, for erase_namespace_key_range, every key in
    // the requested range) was erased -- or was already absent, matching
    // secure_storage_erase's own idempotent contract.
    kErased = 0,
    // The namespace's Section 2.7 classification is not
    // kEraseOnFactoryReset -- refused before touching any key. This is
    // the structural guarantee plan #17 asks for.
    kRefusedNotErasable = 1,
    // The classification allowed the erase, but at least one key's
    // secure_storage_erase call failed (kEraseFailed) -- some keys may
    // still have been erased before the failure; the namespace should be
    // considered incompletely erased and worth retrying.
    kPartialFailure = 2,
};

// Erases every EXACT (non-prefix) key pattern the Section 2.7 registry
// lists for `namespace_id`. Does NOT expand prefix key patterns (e.g.
// kZigbeeNetworkDeviceReporting's "rptp_"/"cfg_rpt_" dynamically-built
// per-reporting-profile keys) -- the registry only knows the prefix
// string, not the real bounded keyspace a specific owning module (e.g.
// config_manager.cpp, which defines kMaxReportingProfiles) builds from
// it. Use erase_namespace_key_range() below for that, once, per prefix a
// namespace actually has.
NamespaceEraseResult erase_namespace(NvsNamespaceId namespace_id) noexcept;

// Erases a bounded, explicitly-sized range of dynamically-built keys
// sharing one prefix, matching this repository's real key-building
// convention (config_manager.cpp's build_profile_nvs_key/
// build_legacy_v2_profile_nvs_key: "<prefix><suffix_char><%02u index>",
// e.g. "rptp_d00".."rptp_d15"). For each character in `suffix_chars` and
// each index in [0, index_count), builds "<prefix><char><index,
// 2-digit-zero-padded>" and erases it via secure_storage_erase. Refuses
// outright (kRefusedNotErasable), before touching any key, if
// `namespace_id`'s classification is not kEraseOnFactoryReset -- the same
// structural guarantee erase_namespace() provides.
//
// `prefix` + 1 suffix character + 2 digits must fit within
// kMaxKeyPatternsPerNamespace's implied key-name budget (this repository's
// real prefixes -- "rptp_", "cfg_rpt_" -- are well within any reasonable
// NVS key-name length limit, so no explicit length parameter is needed
// here; a caller-supplied absurdly long prefix would simply fail at the
// hal_nvs_set_str layer's own key-length validation, which this function
// does not attempt to duplicate).
NamespaceEraseResult erase_namespace_key_range(
    NvsNamespaceId namespace_id,
    const char* prefix,
    const char* suffix_chars,
    uint32_t suffix_char_count,
    uint32_t index_count) noexcept;

}  // namespace service
