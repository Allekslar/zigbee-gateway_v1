<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Versioned MQTT `/v1` contract and Home Assistant discovery migration (S4 MQTT required changes #10-#19)

This document records the S4 MQTT+HA v1 slice: what is frozen and tested
today, and what is explicitly deferred. It complements
`docs/architecture/HTTP_API_V1.md`, `docs/architecture/GATEWAY_IDENTITY.md`
and `docs/architecture/MATTER_ENDPOINT_IDENTITY.md` (the other S4 slices).

This document covers three passes. The first (items 10-12) built the v1
topic/payload schema foundation as new, additive code that nothing in the
live bridge called. The second (items 13-15) wired that foundation into
the live `MqttBridge` -- the actual production cutover, deliberately
excluding Home Assistant discovery out of caution for live entities. The
third (this update, items 16-19) migrates HA discovery itself, revisiting
that caution: **the user confirmed this project has no production
deployment yet** ("проект ще не в продакшн, тож зміни можливі радикальні
без додаткових міграцій" -- the project isn't in production yet, so
radical changes are possible without additional migrations), so the
second pass's careful protect-live-entities deferral no longer applies and
this pass completes plan #14's HA-discovery-topic tombstoning alongside
the #16-#19 migration itself, exactly as the plan's own cutover-order
section describes (tombstone old, then publish new, in the same window).

## 1. Problem this piece closes

Before this pass, every MQTT topic was unversioned (`zigbee-gateway/...`),
keyed by `short_addr`, and payloads carried no `schema_version`,
`device_id` or `gateway_id`. This is the plan's versioned, `DeviceId`-keyed
MQTT contract, now live.

## 2. Why the cutover was sequenced as its own pass

Unlike the HTTP `/api/v1` mutation routes (`docs/architecture/HTTP_API_V1.md`,
which plan FD-19/#28-#29 explicitly forbid S4 from wiring into production),
no equivalent exemption exists for MQTT, and `main/app_main.cpp` already
instantiates and unconditionally starts `mqtt_bridge::MqttBridge g_mqtt`
today (gated only by the `mqtt_available` capability). Implementing items
10-15 in one pass would have meant designing a new payload/topic schema
*and* flipping a live, already-running production subsystem over to it in
the same change. The first pass built and tested the schema in isolation;
this pass performs the actual cutover as a separate, reviewable unit of
live-behavior change, now that the schema itself was already proven out.

## 3. What is implemented and tested

### 3.1 `mqtt_topics_v1.hpp`/`.cpp` and `mqtt_serializer_v1.hpp`/`.cpp` (plan #10-#12)

Unchanged from the first pass -- see the topic list, payload envelope
shape and `parse_v1_device_id_from_topic()` contract already documented
below in Section 3.1 of the prior revision of this file (topic root stays
the single named `"zigbee-gateway"` constant with a `/v1` segment
inserted; every payload carries `schema_version:1`, `gateway_id`,
`device_id` where applicable, and a `revision`/observation timestamp).

```text
zigbee-gateway/v1/devices/<device_id>/state
zigbee-gateway/v1/devices/<device_id>/telemetry
zigbee-gateway/v1/devices/<device_id>/availability
zigbee-gateway/v1/devices/<device_id>/config/set
zigbee-gateway/v1/devices/<device_id>/power/set
zigbee-gateway/v1/devices/<device_id>/commands/<operation_id>/result
zigbee-gateway/v1/gateway/state
```

### 3.2 `MqttBridgeDeviceSnapshot` gains `device_id_hex` (needed to wire anything at all)

`components/service/include/service_runtime_api.hpp`:
`MqttBridgeDeviceSnapshot` gained `device_id_hex` (empty when the device's
identity has not resolved to a valid `DeviceId` yet), populated in
`bridge_snapshot_builder.cpp` from `core::CoreDeviceRecord::device_id` the
same way `DevicesApiSnapshotBuilder` already populates the HTTP v1
equivalent field. Without this, nothing downstream could know which v1
topic a given device belongs under. A device without a resolved identity
is skipped by all v1 publishing (never gated out of the snapshot itself,
just never gets a v1 topic written) until it resolves.

### 3.3 Inbound command topics: subscription switched to v1 (plan #13)

`MqttBridge::subscribe_command_topics()` now subscribes to
`topic_v1_device_config_set_wildcard()`/`topic_v1_device_power_set_wildcard()`
instead of the legacy `topic_device_config_wildcard()`/
`topic_device_power_set_wildcard()`. **This is the literal implementation
of plan #13**: legacy short-address command wildcards are not subscribed
in production.

The legacy parsing/dispatch code (`handle_config_command`/
`handle_power_command`, and `parse_mqtt_device_power_request`/
`parse_mqtt_reporting_profile_request` in `application_command_mapper.cpp`)
is **not deleted** -- it stays reachable directly via
`handle_command_message()` for host tests and any explicit dev-profile
use, mirroring the HTTP v1 precedent of keeping legacy handlers present
but unreachable in production. Since a real broker only ever delivers a
message for a topic it is subscribed to, and production no longer
subscribes to any legacy wildcard, the legacy branch is dead code on a
real target from this pass onward without needing to be physically
removed.

New versioned command parsers make this possible without giving
`components/service` a dependency on `components/mqtt_bridge` (wrong
direction) and without `mqtt_bridge` naming a Core-namespaced type
(INV-M026):

- `application_command_mapper.hpp`/`.cpp` gained
  `parse_mqtt_v1_device_power_request()` and
  `parse_mqtt_v1_reporting_profile_request()`, plus a private
  `extract_device_id_hex_from_v1_topic()` helper that duplicates (rather
  than depends on) `mqtt_topics_v1.hpp`'s prefix/hex/suffix validation
  shape -- consistent with this file's existing precedent of duplicating
  its own small topic/JSON parsing helpers instead of sharing them across
  components.
- `MqttBridge::handle_config_command_v1()`/`handle_power_command_v1()`
  (new) resolve the parsed `device_id_hex` to a `short_addr` via
  `ServiceRuntimeApi::resolve_short_addr_for_device_id_hex()` -- the exact
  same API HTTP v1's mutation routes already use for this -- and, for the
  reporting-profile case, on to a real `core::DeviceId` via the
  established `auto`-typed `resolve_device_id_for_short_addr()` pattern
  (INV-M026-safe). A command for an unknown or currently-unlocatable
  `device_id` is rejected the same way HTTP v1 rejects one: no synthetic
  identity is ever written (FD-01/FD-03).
- `MqttBridge::handle_command_message()` routes by topic prefix
  (`"zigbee-gateway/v1/devices/"`) to the v1 handlers first, falling
  through to the legacy suffix-based dispatch only for non-v1-shaped
  topics -- needed because the v1 and legacy `.../power/set` suffixes are
  identical strings; only the prefix disambiguates them.

### 3.4 Outbound publishing: v1-only, identity-keyed diffing (plan #13, #15)

`mqtt_device_sync.cpp` (`MqttBridge::sync_snapshot()`) was rewritten so
its per-device diff cache is keyed by `device_id_hex` instead of
`short_addr`. This is the concrete mechanism behind plan #15 ("do not
create a new legacy topic after cutover, even if a device rejoins with a
different short address"): a `short_addr` remap is recognized as *the same
device* (its `device_id_hex` is unchanged), so it never looks like a
disappearance-plus-recreation the way short_addr-keyed diffing would --
no availability flicker, and critically, remap never re-triggers any
per-device "first time seen" logic that could touch a legacy topic.
`MqttBridge::sync_device_state()` (the optimistic post-command state
publish) was switched the same way: it now publishes exclusively to the
v1 state topic, looked up via the command's already-resolved
`device_id_hex` in the sync cache.

A device whose identity has not resolved yet (`device_id_hex` empty) is
tracked in the cache (so it is not later mistaken for "new" once it does
resolve) but generates no v1 publications until it does.

`MqttBridge::ensure_gateway_id_hex()` (new) formats
`ServiceRuntimeApi::gateway_id()` into a cached hex string once
(`common::GatewayId` is named directly here -- INV-M026 forbids `core::`,
not `common::`; see Section 3.1/3.2 of the first pass's revision for why
this is a deliberate, rule-compliant choice), called from
`attach_runtime()` and defensively again from `sync_snapshot()`/
`sync_device_state()`. No v1 publication is attempted until it succeeds.

### 3.5 One-time legacy retained-topic tombstone (plan #14)

`sync_snapshot()` tracks whether this is the very first sync since
`start()`/`reset_sync_cache()` (`cache_initialized_` transitioning
false->true). On that first sync only, for every device active at that
moment, it publishes a retained **empty** payload to that device's three
legacy topics (state/telemetry/availability) via a new
`build_legacy_tombstone_publication()` helper that takes a legacy topic
builder function pointer, sharing one implementation across the three
topic kinds instead of three near-identical copies. This is the literal
implementation of plan #14 for the three per-device legacy topics that
`mqtt_bridge.cpp` actually ever published to.

Legacy Home Assistant discovery topics were **not** tombstoned by this
sub-pass -- see Section 3.7 for why that changed in the following pass.

Because the sweep fires **at most once per boot**, republishing the same
empty payload on a later boot is still idempotent by ordinary MQTT
retained-message semantics (publishing empty-to-empty is a no-op from the
broker's perspective), satisfying "MQTT retained cleanup is idempotent."

### 3.6 Test coverage

- `test/host/test_mqtt_bridge_commands.cpp` -- extended with v1 config/
  power command coverage (success, malformed device_id, unknown device_id)
  alongside the pre-existing legacy-topic coverage (kept, since the legacy
  handlers are still directly callable). The device fixture now carries a
  resolved `DeviceId` from the start (previously it did not need one) so
  the new device_id_hex-gated v1 publishing has something to publish to --
  the same kind of collateral fixture fix the HTTP v1 pass already needed
  in several places.
- `test/host/test_mqtt_device_sync.cpp` -- rewritten: v1 topic/payload
  assertions replace the legacy ones for live content; new assertions
  cover the first-sync tombstone sweep (6 publications on first sync: 3
  v1 + 3 legacy-empty, only once), a `short_addr` remap producing zero
  publications (proving #15), and a device whose identity resolves on a
  *later* sync (proving the tombstone sweep is truly one-time per boot,
  not per-device-first-seen). Needed a new test-only seam,
  `MqttBridgeTestAccess::set_gateway_id_hex_for_test()`, since this test
  exercises `sync_snapshot()` directly without a `ServiceRuntime` to
  resolve a real `gateway_id` from.
- `test/host/test_mqtt_runtime_feed.cpp` -- rewritten the same way, through
  the real `ServiceRuntime`/`CoreState` path instead of a fabricated
  snapshot.
- `test/host/test_application_command_mapper.cpp` -- new coverage for
  `parse_mqtt_v1_device_power_request()`/
  `parse_mqtt_v1_reporting_profile_request()`: success, wrong suffix,
  uppercase hex rejection, legacy-shaped topic rejection, undersized
  output buffer, malformed payload, invalid profile bounds.

### 3.7 Home Assistant discovery migration (plan #16-#19) and its tombstone (plan #14)

`mqtt_discovery.hpp`/`.cpp` rewritten in place -- unlike the topic/
serializer files, there is no parallel "legacy discovery" code path kept
around, since the previous pass's rationale for caution (protecting live
HA entities) no longer applies (project has no production deployment; see
Section 1). All five entity builders (switch/power, sensor/temperature,
binary_sensor/occupancy, binary_sensor/contact, sensor/battery) now:

- derive `unique_id` and the discovery topic's `object_id` segment from
  **both** the device's `device_id_hex` and the canonical FD-17
  `gateway_id_hex` (`zgw_<gateway_id_hex>_<device_id_hex>_<suffix>`) --
  plan #16: cloning the firmware image to different hardware yields a
  different gateway identity (different `gateway_id_hex`), while
  reboot/reset on the same hardware does not;
  the device `identifiers` field is `["zgw_<gateway_id_hex>_<device_id_hex>"]`
  for the same reason;
- point every `state_topic`/`command_topic`/`availability_topic` at the
  v1 topic tree (plan #19 for `command_topic` specifically; the plan text
  only names command templates, but leaving `state_topic`/
  `availability_topic` on the legacy tree while publishing state
  exclusively to v1 topics -- Section 3.4 -- would make the discovered
  entity permanently stale, so all three moved together);
- gained an `availability_template` field
  (`"{{ 'online' if value_json.online else 'offline' }}"`) that did not
  exist before: the legacy availability topic's payload was the bare
  string `"online"`/`"offline"`, which HA's `payload_available`/
  `payload_not_available` matched directly with no template, but the v1
  availability payload is JSON (`docs/architecture/MQTT_API_V1.md`
  Section 3.2's `serialize_v1_availability_payload()`), so a template is
  now required to extract the boolean -- a necessary correctness
  consequence of the payload-format upgrade, not a discovery-specific
  design choice.

`build_homeassistant_discovery_messages()`'s signature gained a
`gateway_id_hex` parameter and now also requires `device.device_id_hex`
to be non-empty (a device with no resolved identity cannot be published
under a v1-keyed unique_id at all); `MqttBridge::publish_homeassistant_
discovery()` passes `gateway_id_hex_` and skips such devices, and its
own schema-change cache lookup (deciding whether to republish) switched
from `short_addr`-keyed to `device_id_hex`-keyed, mirroring Section 3.4's
remap-safety change for the same reason (a `short_addr` remap must not
look like "new device" and trigger a spurious republish).

**The HA-discovery portion of plan #14 is now implemented**, via a new
`build_legacy_homeassistant_discovery_tombstones()` builder: for a given
`short_addr`, it unconditionally builds retained-empty-payload tombstones
for all 5 possible legacy discovery topics (unconditionally, since a
caller cannot generally know which of the 5 entity types a given device
actually had published under the legacy scheme -- tombstoning one that
was never retained is a harmless no-op). This fires from a **new**
one-time-per-`MqttBridge`-lifetime trigger inside `publish_homeassistant_
discovery()` itself (`legacy_discovery_tombstoned_`, reset alongside the
rest of the sync state in `reset_sync_cache()`), separate from Section
3.5's `sync_snapshot()`-based trigger: discovery publishing bypasses the
pending-publication queue entirely (it calls `hal_mqtt_publish()`
directly), so it needs its own one-time gate rather than reusing
`cache_initialized_`. The tombstone loop runs **before** the new-discovery
loop in the same function call, matching plan #17's literal ordering
("publish deletion payloads for old short-address discovery entities
before publishing DeviceId-based discovery").

Tested in `test/host/test_mqtt_discovery.cpp`: every entity builder's v1
identifiers/topics/`availability_template`, the four rejection paths (no
`gateway_id_hex`, no resolved `device_id_hex`, offline device, unknown
`short_addr`), and `build_legacy_homeassistant_discovery_tombstones()`'s
5-tombstone output plus its own capacity/unknown-`short_addr` edge cases.

## 4. What is explicitly deferred

### 4.1 Command-result publishing

`topic_v1_device_command_result()`'s payload shape is still undesigned and
nothing publishes to it, but the blocker named in the first two passes
(the `OperationResultStore` domain-unification) is now resolved --
`docs/architecture/HTTP_API_V1.md` Section 3.6 built
`OperationResultStore::poll_operation_status()` and the `OperationDomain`/
`OperationStatusSnapshot` types for the HTTP operations-poll route, and
that same API is directly reusable here (`poll_operation_status()` +
`take_*_result()`, the same pair `operations_get_handler_v1` uses). What
remains is MQTT-specific, not a shared-infrastructure gap anymore:

- deciding **when** to publish a command-result message (HTTP is
  pull/poll-based -- a client calls `GET /api/v1/operations/{id}` on its
  own schedule; MQTT is push-based -- the bridge would need to actively
  publish the moment a result becomes ready, most naturally from the same
  `sync_runtime_snapshot()`/`run_loop()` tick that already drains
  publications, which needs its own drain-and-publish sweep over
  recently-completed operation ids);
- the topic is explicitly device-scoped
  (`zigbee-gateway/v1/devices/<device_id>/commands/<operation_id>/result`),
  but `OperationResultStore`'s four domains are not uniformly
  device-scoped: `NetworkResult` carries a `device_short_addr` only for
  `kRemoveDevice`, OTA/RCP results are gateway-level operations with no
  device concept at all, and device **power** commands (the operation an
  MQTT client would most plausibly want a push result for) do not use
  `OperationResultStore` at all -- they go through the entirely separate
  `core::CoreCommand`/`CoreCommandResult` mechanism (`handle_command_
  result()`, polled today only via `ConfigApiSnapshot::last_command_
  status`). A faithful MQTT command-result implementation needs to decide
  how (or whether) to bridge both mechanisms into one topic, which is a
  new design question this pass's HTTP-side unification does not answer.

## 5. Environment limitation

Verified via the Docker host-tools workaround (`zgw-host-tools:s0` for
build/test/sanitizer; `zgw-host-tools:cppcheck` for static analysis, run
manually since `components/mqtt_bridge` still has no CI cppcheck job --
unchanged pre-existing gap from the first two passes. This pass's cppcheck
run surfaced the same 5 findings as the cutover pass (`mqtt_bridge.cpp`'s
`publish_pending_publications`/`set_power_override`, plus one each inside
`publish_homeassistant_discovery()` and `build_homeassistant_discovery_
messages()` -- both functions this pass rewrote, but each finding is the
exact same conditional idiom carried over verbatim from the pre-cutover
code, e.g. `previous == nullptr || (previous != nullptr && ...)`, not a
new issue introduced by the rewrite); every genuinely new line this pass
added is cppcheck-clean.
ESP-IDF/`idf.py` remains unavailable, so real MQTT broker interaction
(actual wildcard subscription behavior, actual retained-tombstone
delivery to subscribers) is unverified against hardware -- see
`unresolved_blockers` in the evidence JSON.
