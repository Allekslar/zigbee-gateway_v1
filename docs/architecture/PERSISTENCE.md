<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Versioned persistence and safe migration (S3)

This document records the S3 device-state persistence contract: what is
frozen and enforced today, and what remains open. It is the canonical
reference named by `docs/implementation/PRODUCTION_HARDENING_PLAN.md` Stage
S3, and complements `docs/architecture/DEVICE_IDENTITY.md` (S2).

## 1. Problem this stage closes

Before this stage, `StatePersistenceCoordinator` wrote a single NVS blob
(`core_state_v1`) that was a magic/version header wrapped directly around a
`reinterpret_cast` of the live `core::CoreState` aggregate. This violated
INV-PERS-01 ("no new durable schema writes raw C++ aggregate memory") in two
concrete ways: a future change to `CoreState`'s layout would silently
misread old persisted data, and there was no rollback path -- a single write
was the only copy, so a power loss mid-write could corrupt the only record.

## 2. What is implemented and tested

### 2.1 Explicit versioned wire schema

`components/service/include/persisted_device_state.hpp` defines
`PersistedDeviceRecord`/`PersistedStatePayload`/`PersistedStateHeader` as
dedicated, explicitly-named wire structs -- deliberately separate from
`core::CoreDeviceRecord`/`core::CoreState`. `to_payload()`/`apply_payload()`
convert between them field by field; nothing `reinterpret_cast`s the live
domain struct (enforced by architecture rule `INV-H006`, which also asserts
the conversion functions and store class continue to exist).

`PersistedStateHeader` carries `magic`, `schema_version`, `generation`,
`payload_length` and a `payload_crc32c` integrity field
(`components/common/crc32c.{hpp,cpp}`, CRC-32C/Castagnoli, verified against
the standard `"123456789"` -> `0xE3069283` test vector) -- in addition to,
not instead of, the NVS layer's own transport integrity.

`to_payload()` drops any device slot whose `device_id` is not
`core::DeviceId`-valid (FD-01): the explicit schema can never contain an
identity-less record, by construction, not by convention.

`apply_payload()` sanitizes every restored device: `online = false`,
`last_report_at_ms = 0`, `stale = false` (plan Section 9 S3: "sanitize
restored runtime fields" -- a device must rejoin/report before Core
considers it present again). This is a **behavior change** from the old raw
blob, which restored `online` as-was; the old behavior was already
non-compliant with the plan text, not a deliberately preserved contract.

### 2.2 Two-generation write/validate/commit protocol

`components/service/include/persisted_state_store.hpp` /
`persisted_state_store.cpp`. Two NVS blob slots (`dstate_a`, `dstate_b`).
`generation` in the header is the sole commit marker:

- `load()` reads both slots, validates magic/schema/length/CRC on each, and
  returns the payload from whichever valid slot has the higher generation.
  If neither slot's data validates, it returns `kCorrupt` rather than
  fabricating a result. If neither key exists at all, it returns
  `kNotFound` (see migration, below).
- `save()` always targets whichever slot does **not** currently hold the
  highest valid generation, writes generation = (highest known generation,
  from either slot) + 1, then reads the write back and validates it before
  returning success. If the readback fails, the other (previously
  committed) slot is untouched and remains what `load()` returns next --
  an interrupted write can never be mistaken for authoritative, and a prior
  valid generation is never destroyed by a failed write.

Tested in `test/host/test_persisted_state_store.cpp`: round trip;
generation rollover (second save wins); both-slots-corrupted correctly
fails closed (`kCorrupt`, never fabricated); a single corrupted slot is
tolerated (the valid one is still selected) without the test needing to
know or assume which physical NVS key holds which generation.

### 2.3 Migration from the pre-S3 raw blob

`StatePersistenceCoordinator::migrate_from_legacy_v1_blob()`
(`components/service/state_persistence_coordinator.cpp`) runs exactly once,
only when `PersistedStateStore::load()` returns `kNotFound` (i.e. the
explicit schema has never been written on this device). It reads the old
`core_state_v1` blob read-only -- **never deletes it** (plan Section 9 S3:
"old keys are deleted only after one successful release/canary window") --
and, for every legacy device record:

- if it has a resolved `core::DeviceId` (true for any record written by
  S2-or-later firmware, since `CoreDeviceRecord::device_id` has existed
  since S2), it is migrated;
- if it has no resolved identity (a genuine pre-S2 legacy record), it is
  **quarantined by omission**: silently dropped, never guessed, never
  rebound from a current locator lookup (FD-01/FD-03).

The outcome is exposed via `StatePersistenceCoordinator::MigrationReport`
(`legacy_devices_found` / `migrated` / `quarantined_no_identity`) for
operator visibility and testing, without persisting any additional record
of the decision (there is no evidence-snapshot infrastructure yet -- see
§3). A successful migration immediately commits the result through the new
two-generation store, so subsequent boots take the normal `load()` path and
do not re-read the legacy blob.

Tested in `test/host/test_state_persistence_migration.cpp`: a legacy blob
with one identity-resolved and one identity-less record migrates exactly
the resolved one, reports the correct counts, and a second restore in a
fresh process no longer re-attempts migration.

### 2.4 Matter endpoint field (structural readiness only)

`PersistedDeviceRecord` includes `matter_endpoint_state`
(`kUnassigned`/`kAssigned`/`kPendingRemoval`) and `matter_endpoint_id`,
round-tripped by `to_payload()`/`apply_payload()`. Nothing populates these
yet -- per FD-16/plan Section 9 S3 required change #15, S4 is the sole
allocator. This stage only ensures the schema can represent all three
states without a future schema bump.

## 3. What is explicitly deferred

### 3.1 `ConfigManager` reporting-profile re-keying to `DeviceId` -- DONE

Originally deferred at the end of the `CoreState` persistence rewrite (see
history below), this was completed as a follow-up pass. `ConfigManager::
ReportingProfileKey` is now `{ core::DeviceId device_id, uint8_t endpoint,
uint16_t cluster_id }` (`config_manager.hpp`). The bit-packed `uint32_t`
key word (`encode_profile_key_word()`/`decode_profile_key_word()`) is gone
from the current schema; `device_id` is stored as an 8-byte NVS blob
(`'d'` key) and `cluster_id`/`capability_flags`/`endpoint` are packed into
one repurposed word (`encode_profile_cluster_caps_endpoint_word()`:
bits 0-15 cluster, 16-23 capability flags, 24-31 endpoint). `kCurrentSchemaVersion`
is now 4.

**Migration (schema 3 -> 4):** schema-3 reporting profiles are
`short_addr`-keyed, and `ConfigManager` has no access to the
`DeviceLocatorRegistry` (only `ServiceRuntime` does), so it cannot resolve a
`DeviceId` for them at migration time. Consistent with FD-03 and the same
"quarantine by omission" default used for the S3 `CoreState` migration
(see §2.3 above), the 3->4 step drops every schema-3 profile (`reporting_profile_count()` becomes 0
after a full migration) rather than guessing or rebinding an identity. The
intermediate v2->v3 legacy step (short_addr-keyed, pre-existing) still runs
unchanged first, so a v2 install's profile is provably carried into the v3
key-space before being quarantined at v3->v4 -- evidence it wasn't silently
dropped earlier in the chain than the identity-aware decision point. Tested
in `test/host/test_config_manager_migration.cpp` (`test_migrate_v2_legacy_
reporting_keys_quarantined_at_v4`).

**Call-site resolution:** `ConfigManager` itself stays locator-registry-free
(unchanged layering). Every production write path resolves `short_addr ->
DeviceId` at the caller, which does have registry access, and rejects the
write if unresolved:

- `ServiceRuntime::post_command`'s `kUpdateReportingProfile` handler
  (`service_runtime.cpp`) resolves via `resolve_device_id_for_short_addr()`
  and returns `CoreError::kInvalidArgument` if the device is unknown.
- The Web (`web_handlers_config.cpp`) and MQTT (`mqtt_bridge.cpp`) HTTP/topic
  parsers (`application_command_mapper.cpp`) no longer resolve or embed a
  `DeviceId` themselves -- they are leaf JSON/topic parsers with no registry
  access. They now return a `ReportingProfileWriteRequest{ short_addr,
  ConfigManager::ReportingProfile }` (`application_requests.hpp`); the Web
  and MQTT adapters resolve the `DeviceId` via
  `ServiceRuntimeApi::resolve_device_id_for_short_addr()` (added to the
  service facade for this purpose) immediately before calling
  `post_reporting_profile_write()`, and reject with `404 unknown_device`
  (Web) / silently refuse (MQTT, matching its existing no-ack contract) if
  unresolved. Both adapter files use `auto` rather than spelling out the
  `DeviceId` type, to keep INV-M026/INV-M030 (adapters must not name Core
  symbols directly) intact -- confirmed by `check_arch_invariants.sh`.

Tested in `test/host/test_service_runtime.cpp`,
`test_web_handlers_config_post.cpp` (including the new unresolved-device
404 case), `test_mqtt_bridge_commands.cpp` (including the new
unresolved-device silent-refusal case), `test_service_reporting_profiles.cpp`
and `test_application_command_mapper.cpp`.

A latent bug was caught during this pass and fixed before it shipped: the
new `cluster_caps_endpoint_word` capability-flags mask (`kProfileCapsMask`)
was computed as `0x0000FF00U << 16U`, which evaluates to `0xFF000000`  --
identical to the endpoint mask -- so capability flags and endpoint silently
overwrote each other on every round trip. Caught by
`test_reporting_profile_persist_restore` failing after the rest of the
rewrite compiled clean; fixed to the intended `0x00FF0000UL` (bits 16-23).

### 3.2 `NetworkGenerationId` and `LegacyIdentityEvidenceSnapshot` (FD-15)

Still not implemented (carried over from S2's `DEVICE_IDENTITY.md` §3.2).
The migration in §2.3 above deliberately does **not** depend on this
evidence contract: it uses the simpler, safe default the plan mandates
when no evidence exists ("a record without proof is quarantined... never
guessed") by keying migration eligibility on whether `core::DeviceId` was
already resolved by S2 machinery, rather than on record
fingerprint/network-generation/locator-revision matching against a
snapshot that does not exist yet. If/when FD-15 is built, it would
sit **in front of** this migration path (supplying resolved DeviceIds for
records that currently have none), not replace it.

### 3.3 Old-key cleanup

`core_state_v1` is read-only and permanent until a separate, explicitly
reviewed cleanup decision (plan Section 9 S3: "old keys are deleted only
after one successful release/canary window and verified new-schema boot").
No such window exists in this development-stage repository; cleanup is out
of scope for this stage by design, not an oversight.

## 4. Environment limitation

All verification in this stage ran via the Docker host-tools workaround
from S0-S2 (`zgw-host-tools:s0`). ESP-IDF/`idf.py` remains unavailable; the
persistence code itself has no `ESP_PLATFORM`-guarded branches (it goes
through `hal_nvs_set_blob`/`hal_nvs_get_blob`, which are already
target-abstracted), so this stage does not add a new BLOCKED_TOOLCHAIN
surface beyond what S0 already recorded for target/HIL builds in general.
