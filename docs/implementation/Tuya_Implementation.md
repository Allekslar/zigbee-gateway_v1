# Tuya Implementation

## Purpose

Describe a concrete implementation path for adding Tuya support to `zigbee-gateway_v1` without:
- breaking Immutable Core State;
- turning Core into a generic vendor attribute store;
- destabilizing MQTT / Matter / Web snapshot contracts.

This document is the canonical implementation-oriented version of the Tuya architecture for the current codebase.

## Short Conclusion

Tuya support is realistic, but it should be implemented as a Service-side compatibility subsystem, not as a Core schema expansion.

For MVP:
- map Tuya datapoints into existing domain events only;
- keep Core closed-schema;
- route commands through Service;
- keep protocol-specific Tuya encode/send below Service;
- support hybrid standard-ZCL plus Tuya devices;
- use compiled-in device plugins registered through a registry;
- keep per-model quirk data behind a future-proof abstraction.

## Current Project Status

- Zigbee ingress already reaches Service as raw reports.
- Core remains closed-schema and reducer-driven.
- No production Tuya `0xEF00` support exists yet.
- The current board is still standard-path oriented; Tuya support would be a new Service-owned compatibility layer.
- Current interview flow does not yet establish a Tuya-ready manufacturer/model seam.

## Architectural Decision

Accepted direction:
- implement Tuya support as a protocol-specific compatibility subsystem in `components/service/`;
- normalize Tuya datapoints into existing domain events for MVP;
- keep Core state schema closed and immutable;
- keep command validation/routing in Service;
- keep protocol-specific Tuya encoding in HAL/device adapters;
- support hybrid devices where standard ZCL and Tuya datapoints coexist.

Deferred:
- generic vendor attributes in Core;
- generic dynamic capability schema in Core snapshots;
- broad "support all Tuya devices" scope;
- any Core reducer/state changes for MVP.

## Why Core Must Stay Closed

Core should continue to own:
- immutable domain state;
- deterministic reduce;
- fixed event schema.

Core should not absorb:
- vendor-specific runtime state;
- Tuya DP parsing;
- quirk lifecycle;
- protocol-specific command encoding.

This preserves:
- reducer testability;
- stable snapshots;
- bounded complexity;
- predictable downstream behavior.

## Current Code Seams

### Raw Zigbee ingress

Raw attribute reports already enter Service here:
- [service_runtime_api.hpp](../../components/service/include/service_runtime_api.hpp)
- [hal_event_adapter.cpp](../../components/service/hal_event_adapter.cpp)
- [service_runtime.cpp](../../components/service/service_runtime.cpp)

Relevant current entry point:
- `ServiceRuntime::post_zigbee_attribute_report_raw(...)`

### Device identity seam

Tuya fingerprinting depends on manufacturer/model identity, but the current project does not yet provide a concrete, completed seam for that data.

For MVP, the project must add a deterministic identity acquisition step for Basic cluster attributes:
- manufacturer name `0x0004`
- model identifier `0x0005`

This should be done either by:
- extending the existing interview flow in [hal_zigbee.c](../../components/app_hal/hal_zigbee.c), or
- adding a dedicated post-interview query step owned by Service.

Without this, fingerprint-based Tuya activation starts effectively blind.

### Existing domain event path

Service currently maps standard reports into closed Core telemetry/events:
- [core_events.hpp](../../components/core/include/core_events.hpp)
- [core_reducer.cpp](../../components/core/core_reducer.cpp)

Existing MVP-compatible target telemetry already present:
- temperature
- occupancy
- contact
- battery percent
- battery voltage

### Reporting and downstream consumers

Normalized events already flow into stable downstream seams:
- [reporting_manager.cpp](../../components/service/reporting_manager.cpp)
- [mqtt_bridge.cpp](../../components/mqtt_bridge/mqtt_bridge.cpp)
- [matter_bridge.cpp](../../components/matter_bridge/matter_bridge.cpp)
- [web_handlers_device.cpp](../../components/web_ui/web_handlers_device.cpp)

## Recommended Implementation Layout

Add a Service-owned Tuya subsystem under `components/service/`:

- `include/tuya_fingerprint.hpp`
- `include/tuya_payload_view.hpp`
- `include/tuya_dp_parser.hpp`
- `include/tuya_plugin.hpp`
- `include/tuya_plugin_registry.hpp`
- `include/tuya_quirk_data.hpp`
- `include/tuya_translator.hpp`
- `include/tuya_init_coordinator.hpp`
- `tuya_fingerprint.cpp`
- `tuya_dp_parser.cpp`
- `tuya_plugin_registry.cpp`
- `tuya_quirk_data.cpp`
- `tuya_translator.cpp`
- `tuya_init_coordinator.cpp`

## Layer Responsibilities

### Core

Responsible for:
- immutable domain state;
- deterministic reduction;
- fixed event schema.

Not responsible for:
- vendor-specific runtime state;
- DP parsing;
- quirk lifecycle;
- transport-specific command encoding.

### Service

Responsible for:
- routing;
- validation;
- orchestration;
- quirk lookup;
- per-device runtime init state;
- deciding whether standard or Tuya path is used.

Not responsible for:
- low-level Tuya DP wire encoding.

### HAL / device adapter

Responsible for:
- raw Zigbee ingress;
- bounded payload handoff;
- standard ZCL send/encode;
- Tuya DP send/encode.

Implementation note:
- Tuya control encoding should not be added as ad-hoc logic inside a growing Zigbee monolith.
- The preferred direction is a separate HAL-side file such as `hal_tuya_transport.c` or equivalent adapter-local unit, invoked from the existing Zigbee boundary.
- This keeps the HAL boundary cleaner and reduces coupling in [hal_zigbee.c](../../components/app_hal/hal_zigbee.c).

### Tuya compatibility subsystem

Responsible for:
- fingerprint-based activation;
- `0xEF00` parsing;
- DP translation;
- semantic dedup/coalescing;
- plugin lookup;
- quirk data lookup;
- vendor-specific init support.

## MVP Data Path

1. HAL reports raw Zigbee attribute/manufacturer-specific payload.
2. Service detects whether the frame is:
   - standard-only, or
   - Tuya-compatible `0xEF00`, or
   - hybrid.
3. `TuyaFingerprintResolver` matches the device using:
   - manufacturer
   - model
   - observed endpoints/clusters
   - optional DP signature hints
4. `TuyaDpParser` parses bounded DP records from the payload.
5. `TuyaTranslator` maps supported DP values into Service-level normalized outputs.
6. `ReportingManager` applies generic throttle/debounce.
7. Existing Service code maps those normalized outputs into fixed-schema Core events.

Rule:
- standard frames continue through the existing standard path;
- Tuya-compatible frames are translated before they reach Core;
- one device may legitimately use both paths.

## MVP Control Path

1. Adapter submits an existing service-level command request.
2. Service validates:
   - device fingerprint
   - supported command
   - whether standard ZCL or Tuya DP path is required
3. HAL/device adapter performs:
   - standard ZCL encoding, or
   - Tuya DP encode/send

Rule:
- validation/routing stays in Service;
- protocol encoding stays below Service.

This means the control encoder must not live in Service.

## Contract Sketch

### Tuya fingerprint

`TuyaFingerprint` should be a compact, static descriptor:
- manufacturer string
- model string
- endpoint
- required cluster bitmap or list
- optional DP signature markers

Resolver matching should prefer a combination of:
- manufacturer;
- model;
- observed endpoints/clusters;
- optional DP signature hints.

### Payload handling

Parser input should be a bounded view, not an owning heap blob.

Recommended shape:
- pointer
- length
- endpoint
- cluster id
- manufacturer code

Lifetime contract:
- `TuyaPayloadView` is valid only for the lifetime of the HAL callback scope that produced it.
- If translator, quirk init logic, or retry orchestration need the payload after callback return, they must perform exactly one explicit bounded copy into owned storage.

Hot path rules:
- no unbounded allocations per message;
- no cascaded payload copies;
- one copy only if callback lifetime requires it.

The implementation target is bounded low-allocation parsing in the hot path.

### Parse result

`TuyaDpParseResult` should contain typed DP items, not raw JSON-like state.

For MVP:
- max frame length bounded
- max DP count bounded
- max DP value length bounded
- malformed payloads rejected early

The parser should emit typed compact parse results, not a generic dynamic blob.

### Translation output

`TuyaTranslator` should emit only:
- zero or more Service-level normalized updates, or
- "unsupported for MVP"

It should not mutate Core state directly.

This keeps the Tuya subsystem isolated from direct `core_*.hpp` dependencies.

### Translator vs Plugin Responsibility

Recommended split:
- parser produces bounded `TuyaDpParseResult`;
- plugin is the model-specific strategy that understands DP semantics;
- translator is a thin orchestrator that calls the selected plugin and converts plugin output into Service-level normalized results;
- existing ServiceRuntime code remains the place that turns normalized results into Core events.

In other words:
- plugin knows model semantics;
- translator knows orchestration and output shape;
- ServiceRuntime owns the final Core-event construction boundary.

## Hybrid Device Rule

A device may use both:
- standard ZCL path
- Tuya `0xEF00` path

Fingerprinting must therefore enable capability composition, not exclusive routing.

## Quirk Registry

MVP should move from a plain quirk-registry mindset to a plugin-registry-plus-quirk-data model.

The registry selects a device plugin.
The plugin may use compiled-in quirk data.
Later, quirk data may become data-driven without changing the overall plugin contract.

## Plugin Model

### Stage 1: Compiled-In Plugins

The first stage should use compiled-in device plugins registered through a central registry.

Example modules:
- `tuya_ts011f_plugin.cpp`
- `tuya_ts0601_presence_plugin.cpp`
- `tuya_generic_switch_plugin.cpp`

Benefits:
- strong per-model separation;
- easy host unit testing;
- no runtime-loader risk;
- deterministic memory behavior;
- clean migration path toward data-driven definitions.

### Stage 2: Data-Driven Plugins

The second stage may move part of the behavior into declarative data:
- fingerprint rules;
- DP schema;
- field mapping;
- init scenarios;
- command mapping;
- dedup hints.

In that model:
- the execution engine stays in code;
- simple device behavior becomes table-driven;
- complex models may still use small custom code hooks.

### Recommended Interfaces

#### `TuyaPlugin`

Represents one device-family behavior module.

It should be able to:
- match a fingerprint;
- interpret supported DP payload semantics for that model;
- provide init-plan steps;
- map supported commands;
- provide dedup/coalescing hints.

It must not:
- modify Core state directly;
- publish to MQTT, Matter, or Web directly;
- bypass Service orchestration;
- send Zigbee frames directly outside HAL/device adapter seams;
- perform uncontrolled heap allocation.

#### `TuyaPluginRegistry`

Owns plugin registration and selection.

Suggested responsibility:
- resolve `fingerprint -> plugin`
- expose a stable lookup API regardless of whether backing data is compiled-in or data-driven.

For Stage 1, the preferred registration mechanism is an explicit table in `tuya_plugin_registry.cpp`, for example:
- `const TuyaPlugin* kAllPlugins[] = { ... };`

Why:
- simpler to debug;
- easy to grep;
- no linker-section complexity;
- sufficient for a small to medium number of supported models.

#### `TuyaPluginContext`

Provides bounded runtime context to a plugin.

Recommended contents:
- resolved fingerprint;
- endpoint/cluster information;
- manufacturer/model strings or ids;
- bounded payload view;
- lightweight device session hints;
- capability flags for hybrid standard/Tuya behavior.

#### `TuyaPluginResult`

Represents the output of plugin processing.

It should carry only Service-facing results such as:
- normalized domain-intent updates;
- init actions to schedule;
- command encode requests;
- dedup/coalescing metadata;
- unsupported or partial-support status.

It should not be a direct Core event or transport command object.

### Plugin Boundary Rule

Plugins should speak in Service-level normalized outputs, not in direct Core internals.

This keeps:
- Core closed;
- Service as the orchestration owner;
- HAL as the protocol execution boundary.

As a result, the preferred architecture is:
- plugins must not include `core_*.hpp`;
- `tuya_translator.cpp` must not include `core_*.hpp`;
- final Core-event construction happens in existing ServiceRuntime normalization code.

### Relationship To Quirk Data

Quirk data remains important, but it becomes subordinate to the plugin model.

Recommended split:
- plugin = behavior module / strategy;
- quirk data = per-model declarative data used by that behavior.

This is a better long-term shape than a bare registry of quirks alone.

Suggested interface:
- `resolve_plugin(fingerprint) -> const TuyaPlugin*`
- `find_quirk_data(fingerprint) -> const TuyaQuirkData*`

Initial implementation:
- `StaticCompiledTuyaPluginRegistry`
- compiled-in quirk data tables per plugin or profile

Future-compatible backends:
- FS-backed quirk data
- NVS-backed quirk data

The parser, translator, and plugin engine must not know where quirk data is physically stored.

## Quirk Init State

Vendor-specific init state must live outside Core snapshot.

Suggested owner:
- `TuyaInitCoordinator`

Per-device states may include:
- `NotStarted`
- `FingerprintResolved`
- `InitPending`
- `WaitingAck`
- `Ready`
- `Degraded`
- `Failed`

Typical future actions may include:
- time sync;
- manufacturer handshake;
- retry/timeout handling;
- ack tracking.

## Anti-Flood Policy

Split responsibilities:
- `TuyaTranslator`: semantic dedup/coalescing of noisy DP bursts
- `ReportingManager`: generic device-agnostic throttle/debounce

This keeps Tuya-specific noise filtering out of Core.

## Why Not Generic Capabilities Yet

Dynamic vendor-capability state in Core would:
- complicate `core_reduce(prev, event)`;
- weaken snapshot predictability;
- blur the boundary between domain state and transport/vendor state;
- increase validation and testing cost.

For that reason, generic vendor attributes and dynamic Core schema are explicitly out of MVP scope.

## MVP Scope

Recommended first supported class:
- one Tuya contact sensor, or
- one Tuya temperature/humidity sensor, or
- one Tuya relay/switch

Do not start with a generic multi-device framework.

## Suggested First Integration Step

Extend [service_runtime.cpp](../../components/service/service_runtime.cpp) so that `post_zigbee_attribute_report_raw(...)` can:
- detect `cluster_id == 0xEF00`;
- route to `TuyaDpParser + TuyaTranslator`;
- emit existing normalized events into the current event pipeline.

This gives the narrowest MVP without changing Core.

## Suggested MVP Phases

### Phase 0 ✅

- add manufacturer/model query support through interview or post-interview Basic-cluster reads;
- persist or publish the resolved identity into a Service-visible seam;
- add fingerprint infrastructure with no device-specific quirks yet;
- expose enough diagnostics to observe which candidate Tuya devices join and what identity they advertise.

This phase is a prerequisite for meaningful Tuya model support.

### Phase 0 Backlog

1. Add Basic-cluster identity acquisition.
   Extend the current interview flow or add a post-interview read for:
   - manufacturer name `0x0004`
   - model identifier `0x0005`

2. Add a Service-visible identity DTO.
   Introduce a bounded device-identity type in `components/service/include/` that can carry:
   - short address
   - endpoint
   - manufacturer
   - model
   - identity resolution status

3. Persist identity into a read-model seam.
   Publish the resolved identity into a Service-owned snapshot so Web/API and future fingerprinting can inspect it without touching Core.
   Preferred direction: extend the existing `DevicesApiSnapshot` / `DevicesApiDeviceSnapshot` with bounded manufacturer/model fields instead of introducing a brand-new snapshot type.

4. Add fingerprint infrastructure without quirks.
   Introduce:
   - `TuyaFingerprint`
   - `TuyaFingerprintResolver`
   - empty or stub registry
   but do not yet parse `0xEF00` or add model-specific behavior.

5. Add diagnostics for candidate devices.
   Expose enough information in logs or API so that joined candidate Tuya devices can be observed with:
   - manufacturer
   - model
   - endpoint
   - cluster hints

### Phase 0 Acceptance

- interview or post-interview flow can resolve manufacturer/model for a joined device
- resolved identity is visible through a Service-owned seam
- no Core schema change is required
- no direct `core_*.hpp` dependency is introduced into the new Tuya files
- host tests cover identity DTO and fingerprint resolver bootstrap
- integration tests confirm standard interview flow is not regressed

### Phase 1 ✅

- choose one concrete Tuya device or one narrow device family;
- add fingerprint rule;
- add bounded `0xEF00` parser;
- add DP to existing-domain-event translator;
- add one compiled-in plugin plus its quirk data;
- prove downstream MQTT/Matter/Web still work unchanged.

### Phase 2 ✅

For controllable devices:
- add Service-side command routing;
- add HAL Tuya DP encode/send;
- add hybrid-device tests.

### Phase 3

For devices needing vendor init:
- add `TuyaInitCoordinator`;
- add timeout/retry/ack tracking;
- keep all of this outside Core snapshot.

## Non-Goals For MVP

- generic attribute bag in Core
- dynamic reducer schema
- user-editable quirk definitions
- universal Tuya support

## Architecture Guardrail

The Tuya subsystem must not include `core_*.hpp` directly.

Allowed relationship:
- Tuya subsystem translates protocol-specific input into existing domain events that are already understood by Service/Core.

Disallowed relationship:
- Tuya parser/translator depending directly on Core internal headers or evolving Core schema to carry vendor-specific payloads.

This should eventually be enforced by `check_arch_invariants.sh` the same way other application-to-core boundaries are enforced.

## Consequences

### Benefits

- preserves the current Core contract;
- fits the existing reducer/event model;
- keeps host-based testing practical;
- enables realistic support for 1-2 concrete Tuya models;
- supports hybrid devices without rewriting the standard path;
- localizes vendor-specific complexity.

### Costs

- wide Tuya coverage will remain incremental, not automatic;
- many quirks will still be per-model;
- some future devices may require a separate ADR if they do not fit current domain events;
- compiled-in quirks mean firmware updates for new support in MVP.

## Acceptance Criteria

- one concrete Tuya model is fingerprinted correctly
- `0xEF00` payload is parsed without unbounded allocation
- parsed DPs map into existing domain events
- plugin selection is deterministic for the chosen model
- downstream MQTT/Matter/Web continue to work unchanged
- no Core schema changes are required
- host tests cover parser, translator, plugin selection, and quirk-data lookup
- integration tests prove standard + hybrid paths do not regress
