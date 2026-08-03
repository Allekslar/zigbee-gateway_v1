# Zigbee Gateway v1 — implementation plan виправлень і production hardening

## Document status

- **Status:** `UPDATED — SAFETY-EQUIVALENT SIMPLIFICATION PATCH VALIDATED`
- **Plan mode:** repository implementation
- **Repository:** `zigbee-gateway_v1-main`
- **Reviewed archive SHA-256:** `5444574dc4b5e3b83d376f57dc1ebac25b2a926ad103261774ef940be4efe79f`
- **Reviewed normalized repository tree SHA-256:** `46c195f222e0a383ca9e63b6518e51922d2e56d7c36b42e4f6ab1a0d37b98cb4`
- **Audit date:** 2026-08-01
- **Revision:** Architecture Plan Review Auto Cycle Iteration 4 was `APPROVED`; a targeted post-review simplification patch was then applied to FD-13, FD-17, FD-18, FD-19 and FD-20 and passed focused security/consistency validation.
- **Simplification rule:** implementation complexity may be reduced only when the same threat model, fail-closed behavior, recovery guarantees and negative-test matrix remain intact.
- **Releaseability:** outputs of S0-S8 are `NON-RELEASABLE`; only a complete S9 evidence bundle may identify a production candidate.
- **Intended executor:** one implementation agent or developer per stage; stages execute in dependency order; no stage may depend on review-chat context.
- **Canonical companion artifacts:**
  - `zigbee_gateway_v1_archive_code_audit.md`
  - `evidence/reviewed-repository-inventory.json`
  - `tools/extract_repository_inventory.py`

## 1. Objective

Bring the current ESP32-C6 Zigbee gateway from `HARDENING INCOMPLETE` to a verifiable production baseline without discarding its existing modular architecture.

The plan closes these concrete risks:

1. durable device identity is incorrectly coupled to Zigbee `short_addr`;
2. management HTTP and MQTT control planes do not have a fail-closed production security profile;
3. persisted device/config data cannot evolve safely through the identity migration;
4. MQTT QoS 1 duplicates, optimistic state projection, bounded queues and Matter overflow can cause duplicate side effects or permanent state divergence;
5. temporary source booleans hide unavailable Zigbee/RCP/Matter capabilities;
6. API/MQTT contracts are unversioned and have no safe legacy cutover;
7. OTA signatures are not yet tied to a complete hardware root-of-trust and eFuse evidence chain;
8. sanitizer, integration and real-gateway HIL evidence are not mandatory release gates.

## 2. Scope

### 2.1 In scope

- Core device identity and reducer/event/command contracts.
- Zigbee HAL identity callbacks and EUI-64 ↔ short-address resolution.
- Service runtime, persistence, reporting profiles, Tuya/device descriptors and read models.
- HTTP API and Web UI contract migration.
- MQTT topics, command ingress, Home Assistant discovery and broker security.
- Matter bridge identity, endpoint persistence and overflow recovery.
- Management authentication, authorization, physical-presence controls and strict JSON parsing.
- Queue/backpressure/idempotency/result contracts.
- Secure Boot, Flash Encryption, NVS Encryption, rollback/anti-rollback and OTA release evidence.
- CI, static/sanitizer/fuzz tests, target build and HIL release gates.
- Documentation, migration/cutover and operator runbooks.

### 2.2 Explicitly out of scope

- Replacing the modular firmware with microservices or a cloud backend.
- Replacing the reducer/effect architecture.
- Introducing unbounded dynamic allocation into hot paths.
- Implementing a full target-side Matter stack adapter. The existing Matter bridge/runtime contract is preserved and hardened; target Matter stack integration remains a separate capability.
- Completing RCP update transport as part of this plan. RCP must become truthfully capability-gated; its full implementation remains the separate `docs/implementation/RCP.md` track.
- Mobile application, remote cloud access or multi-user account management.
- Supporting legacy short-address mutation commands indefinitely.

## 3. Frozen decisions

Every item below is a **FROZEN DECISION**. An executor must not choose an alternative without reopening architecture review.

### FD-01 — Canonical device identity

- Canonical durable identity is Zigbee EUI-64 represented by domain type `DeviceId`.
- Canonical text form is exactly 16 lowercase hexadecimal characters without separators, for example `00124b0001abcdef`.
- All-zero and all-`ff` values are invalid.
- `short_addr` is a mutable network locator, never a durable key.
- External commands resolve `DeviceId -> current short_addr` immediately before HAL dispatch.
- Events received by short address are resolved to `DeviceId` before entering Core. An event with unresolved identity is held in a bounded identity-resolution queue or rejected with a classified metric; it must not create a durable Core record keyed only by short address.

### FD-02 — Identity ownership

- `core` owns the `DeviceId` value type and device-state identity semantics.
- `service` owns the mutable locator registry and resolution lifecycle.
- `app_hal` owns interaction with ESP Zigbee address APIs and emits both EUI-64 and short address at the boundary.
- MQTT, Matter and Web adapters consume service-owned projections; they do not query Zigbee HAL directly.

### FD-03 — Identity migration safety

- A current or post-upgrade `short_addr -> EUI-64` lookup is locator evidence only and is never sufficient proof that a legacy short-address-only record belonged to that physical device.
- Automatic migration requires durable EUI-64 evidence that was captured before the migration decision, is bound to the exact legacy-record fingerprint and the same `NetworkGenerationId`, and remains consistent with the S2 locator revision at S3 commit time.
- A record without that proof is quarantined as `identity_unresolved`; it is never guessed, manually rebound, or exposed as commandable.
- The operator recreates the desired configuration against `DeviceId` and explicitly discards the quarantined legacy record; there is no "approve guessed mapping" path.
- Reporting configuration and descriptors are preserved only after the complete evidence contract succeeds.
- Legacy retained MQTT topics are tombstoned during cutover.

### FD-04 — API and integration versioning

- Canonical HTTP API is `/api/v1/...`.
- Canonical MQTT payload schema starts at `schema_version: 1`.
- Canonical MQTT device topic key is `DeviceId`, not short address.
- The bundled Web UI moves atomically to `/api/v1` in the same stage.
- Legacy `/api/...` read aliases remain enabled for exactly the first production DeviceId migration release and return deprecation metadata plus a non-binding sunset intent.
- Removing those read aliases is explicitly out of scope for this plan and requires a separate reviewed cleanup plan; no executor may invent a removal release.
- Legacy mutation routes and legacy short-address MQTT command subscriptions are disabled in production.

### FD-05 — Management security

- Normal management traffic uses HTTPS.
- Production management operations require an authenticated session and explicit capability checks.
- Session cookies are `Secure`, `HttpOnly`, `SameSite=Strict`; state-changing requests also require a session-bound CSRF token and same-origin validation.
- Password/verifier material and session secrets are stored only in encrypted NVS.
- Destructive or trust-changing actions additionally require a recent physical-presence grant.
- Commissioning is time-bounded and cannot use a repository-wide static password.

### FD-06 — Provisioning secret model

- Production devices receive a per-device provisioning proof-of-possession secret through manufacturing/provisioning storage.
- Development builds may print a generated one-time secret to serial.
- Production builds fail closed if a manufacturing provisioning secret is absent; they do not fall back to `12345678` or another shared default.
- The gateway never logs the secret after initial enrollment.

### FD-07 — MQTT security

- Production profile allows only `mqtts://` with server identity validation.
- Broker trust material is either a pinned CA or configured certificate bundle; hostname verification remains enabled.
- Plain `mqtt://` is permitted only in an explicit development profile.
- MQTT transport is disabled by default when required trust/auth configuration is incomplete.
- Broker ACLs grant only the gateway's configured topic root.

### FD-08 — Command and state semantics

- Observed device state is never overwritten by optimistic desired state.
- Accepted commands have a lifecycle: `accepted -> dispatched -> confirmed | failed | timed_out | expired`.
- HTTP destructive/non-idempotent commands require an idempotency key.
- MQTT power commands are target-state idempotent and deduplicated within a bounded window; reporting configuration is idempotent by canonical profile equality.
- Command result and pending status are separate from retained observed state.

### FD-09 — Bounded runtime behavior

- Fixed-capacity design is preserved.
- Critical command and command-result queues reject with explicit `no_capacity`; they are never silently dropped.
- Telemetry is coalesced by `DeviceId + attribute` to the latest value; lifecycle, identity and command events are never coalesced.
- MQTT/Matter overflow sets a durable-in-memory `resync_required` flag and forces a full snapshot replay after pressure clears.
- Queue watermarks, rejects, coalesces and resyncs are observable separately.

### FD-10 — Hardware security baseline

- Production firmware uses Secure Boot v2, Flash Encryption release mode, NVS Encryption, rollback and anti-rollback.
- Production signing keys are not stored in the repository or CI workspace beyond the isolated signing operation.
- Test private keys remain test-only and are rejected by production release tooling.
- eFuse state and firmware `secure_version` become release evidence.

### FD-11 — Capability truthfulness

- Temporary source booleans are removed.
- Zigbee, MQTT, Matter target adapter, OTA and RCP availability come from build/runtime capability contracts.
- Unsupported capability routes are not advertised and return a stable `capability_unavailable` response when directly addressed.
- A production build cannot claim Matter target support while only weak HAL stubs are linked.

### FD-12 — Release assurance

- A release cannot be green when required HIL jobs are skipped.
- The exact firmware binary that passes release HIL is the binary promoted and signed for production.
- CI actions and ESP-IDF images are pinned to immutable revisions/digests.

### FD-13 — Security operational bounds and release-profile integrity

Security controls are split into **hard production invariants** and **bounded operational tunables**.

Hard production invariants are exact and non-negotiable:

- Secure Boot v2 is enabled.
- Flash Encryption is in release mode.
- NVS Encryption is enabled and required before any production secret write.
- Production management uses HTTPS with authentication, authorization and CSRF/origin protection.
- Production MQTT uses TLS with broker identity verification; plaintext fallback is disabled.
- Development bypasses, development HTTP and untracked self-signed production certificates are disabled.
- The exact generated `sdkconfig` hash, partition hash, firmware hash and selected operational values are bound to HIL and release evidence.

Operational tunables have validated inclusive ranges and approved defaults. A value inside the range is not rejected merely because it differs from the default, but any production change must be explicit in the release manifest and rerun the affected security/load/HIL tests.

| Proposed symbol or fixed setting | Validated range | Approved default |
|---|---:|---:|
| `ZGW_COMMISSIONING_WINDOW_SECONDS` | 60–600 | 600 |
| provisioning AP passphrase length | non-configurable | 16 Base32 characters |
| `ZGW_JSON_MAX_BODY_BYTES` | 512–2048 | 2048 |
| `ZGW_JSON_MAX_DEPTH` | 2–4 | 4 |
| `ZGW_JSON_MAX_STRING_BYTES` | 64–512 | 512 |
| `ZGW_JSON_MAX_KEYS` | 8–32 | 32 |
| `ZGW_LOGIN_ATTEMPTS_PER_MINUTE` | 1–5 | 5 |
| login backoff start/maximum | non-configurable | 2 / 60 seconds |
| `ZGW_COMMANDS_PER_MINUTE` | 10–60 | 60 |
| `ZGW_MUTATIONS_PER_MINUTE` | 1–5 | 5 |
| `ZGW_FIRMWARE_OPS_PER_HOUR` | 1–2 | 2 |
| `ZGW_AUDIT_RING_RECORDS` | 32–128 | 128 |

- Fixed settings are compile-time security-policy constants and have no Kconfig override.
- Kconfig/build validation fails below the minimum or above the maximum.
- The production release verifier validates all hard invariants exactly, validates each tunable against its safe range, records the selected values and requires the exact HIL-tested `sdkconfig` hash.
- Changing an in-range production value requires a reviewed release-manifest delta and rerunning every test named for that control; it does not require rewriting the architecture plan.

### FD-14 — HTTP result and error mapping

- `200 OK`: synchronous read or completed idempotent replay.
- `202 Accepted`: newly accepted asynchronous operation.
- `400 Bad Request`: invalid request shape, field, type or value.
- `401 Unauthorized`: unauthenticated request.
- `403 Forbidden`: authenticated but unauthorized, or required physical presence is absent/expired.
- `404 Not Found`: unknown public resource without leaking hidden identity details.
- `409 Conflict`: stale locator, idempotency-key conflict or lifecycle/state conflict.
- `410 Gone`: disabled legacy mutation contract.
- `413 Content Too Large`: body, key, string or structural limit exceeded.
- `429 Too Many Requests`: rate limit or authentication backoff active.
- `503 Service Unavailable`: no queue/session capacity, capability unavailable or required secure storage unavailable.
- One versioned golden status/error matrix is authoritative for handlers, Web UI behavior and tests. No handler may invent a different mapping for the same error code.

### FD-15 — Durable legacy identity evidence

- `NetworkGenerationId` is a persisted random 128-bit identifier created once for each Zigbee network generation and replaced only when that network is recreated or factory-reset.
- S2 creates a bounded, explicit `LegacyIdentityEvidenceSnapshot` before S3 migration while join windows, network mutations and external command ingress are disabled.
- Each evidence record contains the legacy-record key and canonical fingerprint, any independently durable EUI-64 already bound to that record, short address, `NetworkGenerationId`, locator revision, source/provenance and capture boot/sequence.
- The live Zigbee address table may validate an existing historical EUI binding but may not create one retroactively. A record that has only a short address is `insufficient_proof` and is quarantined.
- Evidence is double-buffered/versioned with integrity validation and is consumed idempotently by S3. Mapping or network-generation change between capture and commit invalidates automatic migration.

### FD-16 — Matter endpoint allocation

- One dynamic Matter endpoint represents one `DeviceId`; all supported clusters for that physical device are exposed on that endpoint.
- Dynamic endpoint base is `10`; capacity equals the recomputed `service::kServiceMaxDevices`. The reviewed snapshot is `64`, therefore the reviewed range is `10-73` inclusive.
- S0 and S4 must prove that this complete range does not overlap static or reserved endpoints and must return `BLOCKED_DELTA_REVIEW` when capacity or reservations differ without an approved delta.
- Allocation is deterministic lowest-free. The optional assignment is persisted in the S3 device record.
- An endpoint is not reusable until persisted device deletion and Matter removal/tombstone confirmation are both complete.
- Class-wide fixed endpoint fallback is forbidden as production device identity behavior. The reviewed `kMatterEndpointTemperature=10`, `kMatterEndpointOccupancy=11` and `kMatterEndpointContact=12` values are legacy fallback artifacts to remove in S4, not permanent reserved endpoints; any additional real static/reserved target endpoint in 10-73 is a blocker.

### FD-17 — Gateway identity and management TLS trust

- `GatewayId` is the ESP32 factory base MAC rendered as exactly 12 lowercase hexadecimal characters without separators. It survives ordinary and factory reset and is never derived from mutable network configuration.
- Production mDNS host is exactly `zigbee-gateway-<last6>.local`, where `<last6>` is the final six hexadecimal characters of `GatewayId`.
- The production management certificate SAN contains that exact DNS name and URI `urn:zgw:<gateway_id>` and chains to the configured minimal product management CA.
- Admin/browser clients receive the product CA trust root through an authenticated out-of-band provisioning path. Plain unauthenticated TOFU is not accepted for production. Missing or invalid trust material keeps the production management listener disabled.
- Certificate storage has encrypted `current` and `next` slots plus one atomic active-slot reference. Rotation is authenticated, requires physical presence, validates key/certificate/SAN/issuer/expiry, starts a bounded local verification using `next`, atomically switches the active reference only after validation, and retains the previous confirmed slot until one successful reboot/post-activation check completes.
- Power loss or failed verification selects the last confirmed `current` slot before listener enablement. A generic fleet-PKI workflow engine and an independent four-state certificate framework are out of scope unless a later fleet-management requirement justifies them.
- Manufacturing/provisioning evidence must reject duplicate or cloned `GatewayId` enrollment.

### FD-18 — Durable idempotency journal

- The destructive-operation journal is fixed-capacity and persistent. The approved initial capacity is `64` records; implementation may increase it after NVS budget/load evidence, but may not reduce it below `32` without a reviewed delta.
- `Idempotency-Key` is 16–64 URL-safe ASCII characters and is scoped by authenticated actor identity plus canonical route/action.
- A record stores a canonical request fingerprint, lifecycle state, stable result/error, persisted record ID and persisted completion sequence only; raw request payloads, credentials, tokens and secrets are never persisted.
- Active records are never evicted. Active statuses are `accepted` and `dispatched`; terminal statuses are `confirmed`, `failed`, `timed_out` and `expired`.
- Terminal eviction is capacity-driven, not wall-clock-driven. When capacity is needed, evict the terminal record with the lowest persisted `completion_sequence`, using stable record ID as the tie break. Wall-clock/NTP changes cannot accelerate eviction because time is not an eligibility input.
- OTA, factory-reset and certificate-rotation records are not terminal/evictable until their operation-specific recovery contract has reached its confirmed safe state.
- Persisted sequence allocation is monotonic across reboot. Sequence corruption, duplicate sequence ownership or wrap without a safe free value fails closed with `503/no_capacity`; the implementation does not guess ordering.
- If all records are active or otherwise non-evictable, reject new work with `503/no_capacity`; never overwrite an existing record.
- Rate limits, authentication and physical-presence requirements remain independent defenses against intentional journal churn.

### FD-19 — Intermediate-stage releaseability and route ownership

- S0–S8 outputs are engineering-only `NON-RELEASABLE` states.
- S4 owns versioned DTOs, route definitions, serializers, handlers as unregistered application contracts, golden fixtures and non-production host/integration tests. S4 does not register a production management listener or production mutation routes.
- S6 is the first and only stage that wires production management routes into the composition root, and it does so atomically with HTTPS certificate trust, secure storage, authentication, authorization, CSRF/origin protection and rate limiting.
- A build-time invariant fails any production configuration that enables the control-plane registration path while HTTPS, certificate trust, authentication, authorization or secure storage is unavailable, or while a development bypass is enabled.
- Runtime startup remains fail-closed: if a required security dependency is unhealthy, the production listener and mutation routes are not registered.
- S9 accepts only an exact, uninterrupted S0–S9 completion-manifest chain bound to the same repository lineage and exact binary. An earlier-stage artifact is rejected with `BLOCKED_RELEASE_INCOMPLETE`.

### FD-20 — OTA health confirmation through ESP-IDF rollback/anti-rollback

- ESP-IDF owns the OTA image-state machine, `otadata` transitions and application secure-version advancement. Application code must not reproduce those platform mechanisms or write the application secure-version eFuse directly.
- Application code owns only the product health policy and call timing through a thin `OtaPlatformAdapter` exposing the running-image state, `confirm_running_image_valid()`, `rollback_invalid_image_and_reboot()` and secure-version readback.
- When the running image is `ESP_OTA_IMG_PENDING_VERIFY`, the production mutation listener remains disabled while the complete S8 health checklist runs. Network/cloud reachability is not required for health success.
- Health success calls the adapter backed by `esp_ota_mark_app_valid_cancel_rollback()` and then verifies the ESP-IDF return value, running-image state and secure-version readback.
- Health failure or timeout calls the adapter backed by `esp_ota_mark_app_invalid_rollback_and_reboot()`; if rollback cannot be initiated, the device remains fail-closed and exposes no production mutation surface.
- Before successful confirmation, the previous valid slot remains the rollback target and the application secure version is unchanged. Power loss or reset while still pending verification follows ESP-IDF rollback behavior.
- On the next boot after any interruption, the application inspects platform image state and eFuse readback before enabling the control plane. Only the previous valid slot/old secure version or the new valid slot/advanced secure version is accepted; any inconsistent state is quarantined as non-releasable.
- No custom persistent OTA image-state machine or duplicate application-owned boot-validity journal is added. The S7 operation record and release/HIL evidence record request/result history, while ESP-IDF remains authoritative for boot validity and eFuse advancement.
- After secure-version advancement, rollback to a lower security version is intentionally no longer promised.

### FD-21 — Restart-safe factory reset

- Preserve: eFuse/security state, Secure Boot and Flash/NVS encryption key material, factory `GatewayId`, manufacturing proof-of-possession, product CA/trust anchors and device management TLS identity including current/next certificate slots.
- Erase: admin verifier and sessions, Wi-Fi and MQTT credentials/configuration, Zigbee network keys/pairings, device/descriptor/reporting state, Matter endpoint map, operation/idempotency journal and legacy migration/quarantine/tombstone state.
- Reinitialize a fresh audit ring containing one redacted factory-reset record.
- A dedicated protected reset journal uses exactly `requested -> erasing -> reinitialized -> commissioning_ready`; it is separate from the operation journal being erased and resumes idempotently after reboot.
- Commissioning opens only after reset reaches `reinitialized` and a new trusted physical-presence plus manufacturing PoP check succeeds.
- Certificate compromise is handled by FD-17 rotation/revocation, not by ordinary factory reset.

### FD-22 — Platform-first, security-equivalent implementation

- Prefer ESP-IDF-provided security, rollback, TLS, NVS and platform lifecycle mechanisms when they satisfy the frozen product guarantees.
- Application-owned code may add ports/adapters, product policy, authorization, health criteria, bounded orchestration, typed errors, observability and test seams; it must not duplicate a platform state machine or cryptographic primitive without a documented unmet product requirement.
- A simplification is acceptable only when it preserves the same threat model, fail-closed behavior, power-loss/reboot recovery, durable-data guarantees and negative-test matrix.
- Fewer classes or states are not a valid reason to remove authenticated enrollment, certificate validation/rollback, durable destructive-operation idempotency, exact release-artifact binding, OTA health confirmation or production build invariants.

## 4. Non-negotiable invariants

| ID | Invariant |
|---|---|
| INV-ID-01 | One physical Zigbee device has one durable `DeviceId` across rejoin, reboot and short-address remap. |
| INV-ID-02 | At most one online `DeviceId` owns a given valid short address at a time. |
| INV-ID-03 | A short address reassigned to a different EUI-64 never inherits the previous device's config, history, MQTT identity or Matter endpoint. |
| INV-ID-04 | Current locator lookup alone never authorizes legacy migration; automatic migration requires matching historical EUI evidence, legacy-record fingerprint and `NetworkGenerationId`. |
| INV-GW-01 | `GatewayId`, mDNS identity and certificate SAN remain stable across reboot, configuration changes and factory reset. |
| INV-MATTER-01 | Matter endpoint is one persisted endpoint per `DeviceId`, allocated in the validated range and not reused before deletion plus tombstone confirmation. |
| INV-PERS-01 | Persistence uses explicit versioned fields; no new durable schema writes raw C++ aggregate memory. |
| INV-PERS-02 | Migration is restart-safe and idempotent; partial migration never produces a mixed authoritative state. |
| INV-API-01 | All production mutation contracts identify devices by `DeviceId`. |
| INV-AUTH-01 | Authentication runs before parsing or invoking a protected use case. |
| INV-AUTH-02 | Physical-presence grants are single-device, time-bounded, non-replayable and consumed by destructive actions. |
| INV-MQTT-01 | Retained state represents observed state only. |
| INV-CMD-01 | Retrying the same idempotency key cannot execute a second destructive action. |
| INV-CMD-02 | Active idempotency records are never evicted; terminal eviction follows the persisted completion-sequence and capacity policy in FD-18 and never depends on wall-clock jumps. |
| INV-QUEUE-01 | A critical queue overflow is visible to the caller and to metrics. |
| INV-RESYNC-01 | Any dropped/coalesced bridge delta eventually converges through a full snapshot resync. |
| INV-OTA-01 | A device cannot boot or install an unsigned, wrongly signed or security-version-downgraded production image. |
| INV-OTA-02 | Product health confirmation precedes the sole ESP-IDF mark-valid call; application code never duplicates the platform OTA state machine or writes application secure-version eFuse directly. |
| INV-RESET-01 | Factory reset follows the exact preserve/erase matrix and restart-safe journal without erasing hardware identity or trust roots. |
| INV-REL-01 | No S0-S8 artifact can be promoted, S4 never registers production mutation routes, and S6 cannot register them unless the complete security composition is healthy. |
| INV-ARCH-01 | Core has no dependency on ESP-IDF, HTTP, MQTT, Matter or NVS implementations. |
| INV-CONC-01 | Core mutation and `CoreRegistry::publish` have one explicit writer task; adapters post immutable requests through service queues. |
| INV-PLATFORM-01 | ESP-IDF owns platform OTA rollback/anti-rollback and other supported platform state machines; application code adds only policy/adapters/evidence required by FD-22. |

## 5. Startup contract and repository freshness gate

### 5.1 Reviewed snapshot

- Archive SHA-256: `5444574dc4b5e3b83d376f57dc1ebac25b2a926ad103261774ef940be4efe79f`
- Normalized source tree SHA-256: `46c195f222e0a383ca9e63b6518e51922d2e56d7c36b42e4f6ab1a0d37b98cb4`
- Reviewed inventory: `348` source files, `7` components, `10` CI jobs, `69` host tests, `7` integration tests, `7` HIL files, `17` validation scripts.
- Classification: **SNAPSHOT-DERIVED — MUST RECOMPUTE**.

### 5.2 First allowed stage

`S0 — Freeze implementation baseline` is the only stage allowed to start from a cold repository checkout.

No code mutation is allowed until the inventory comparison, architecture gate, baseline test commands, toolchain verification and version-control baseline have completed.

### 5.3 Required baseline commands

From the repository root, with the companion plan package available:

```bash
python3 tools/extract_repository_inventory.py \
  --repo . \
  --output implementation-evidence/current-repository-inventory.json \
  --baseline evidence/reviewed-repository-inventory.json \
  --diff-output implementation-evidence/repository-inventory-diff.json

bash ./check_arch_invariants.sh

cmake -S test/host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure

cmake -S test/integration -B build-integration -DCMAKE_BUILD_TYPE=Debug
cmake --build build-integration --parallel
ctest --test-dir build-integration --output-on-failure
```

Expected reviewed baseline:

- exact inventory comparison: `INVENTORY_MATCH`;
- architecture gate: pass;
- host: `69/69` pass;
- integration: `7/7` pass;
- ESP-IDF: exactly `5.5.2`, sourced from the root `dependencies.lock`;
- any target build container used later must resolve to an immutable digest recorded by S0 and enforced by S9.

Run the current sanitizer baseline:

```bash
cmake -S test/host -B build-host-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-host-asan --parallel
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  ctest --test-dir build-host-asan --output-on-failure
```

The reviewed snapshot has exactly these known sanitizer failures:

- `test_service_reporting_stale`;
- `test_service_reporting_semantics`;
- `test_service_reporting_faults`.

A different sanitizer result before mutation is a repository delta and requires review.

### 5.4 Stale-input stop statuses

Return one of these exact statuses and stop before code changes:

- `BLOCKED_DELTA_REVIEW` — tree hash, material inventory, test inventory, route inventory, CI job inventory or known baseline behavior differs without an approved delta.
- `BLOCKED_STALE_BASELINE` — predecessor stage commit/evidence does not match the stage input contract.
- `BLOCKED_TOOLCHAIN` — ESP-IDF is not exactly `5.5.2`, the root and target lockfiles disagree, the required immutable container digest cannot be resolved, or required signing tooling/hardware is unavailable.
- `BLOCKED_VERSION_CONTROL` — no clean usable Git baseline can be verified or initialized without including generated/build/evidence artifacts.
- `BLOCKED_HIL_ENVIRONMENT` — required release HIL devices/broker/certificates are unavailable.
- `BLOCKED_SECURITY_PROVISIONING` — production provisioning/eFuse material cannot be produced or verified.
- `BLOCKED_RELEASE_INCOMPLETE` — an artifact or manifest chain stops before S9, production management security is not complete, or release evidence does not bind the exact S0-S9 lineage.

Implementation packages must not be generated from stale counts or unresolved inventory differences.

### 5.5 Snapshot-derived inventories to recompute

| Inventory | Extraction source | Exact comparison requirement |
|---|---|---|
| Repository file tree and hashes | `tools/extract_repository_inventory.py` | Exact path/hash comparison before S0 |
| Components | direct children of `components/` | Two-way set diff |
| HTTP routes | literals under `components/web_ui` and bundled UI | Two-way set diff; classify legacy/new routes |
| MQTT topics | MQTT topic constants/builders and README contract | Two-way set diff |
| CI jobs | top-level `.github/workflows/ci.yml` jobs | Two-way set diff |
| Host/integration tests | `add_test(NAME ...)` in CMake | Two-way set diff and count recomputation |
| HIL/validation scripts | `scripts/` and `test/hil` | Two-way set diff |
| Kconfig symbols | `main/Kconfig.projbuild` | Two-way set diff |
| Temporary capability flags | all `kTemporarilyDisable*` occurrences | Exact list; target count after S1 is zero |
| Config schema versions | all `kCurrentSchemaVersion` and migration branches | Determine global applicable maximum |
| Persisted state versions | all persisted magic/version constants and keys | Determine global applicable maximum |
| NVS keys/namespaces | service/app_hal NVS constants and adapters | Two-way set diff before migration |
| Security config | sdkconfig/Kconfig secure-boot, encryption, rollback, MQTT trust symbols | Exact expected production profile |
| Legacy identity evidence | legacy record keys/fingerprints, durable EUI sources, locator mappings and network-generation markers | Exact two-way set diff; unresolved short-only records remain quarantine candidates |
| Matter endpoints | `kServiceMaxDevices`, all static/reserved/dynamic endpoint constants and persisted maps | Recompute capacity/range and prove no overlap; reviewed dynamic range 10-73 |
| Gateway/TLS identity | base-MAC accessors, mDNS names, CA/trust keys and certificate slots | Two-way inventory; prove one authoritative GatewayId and current/next slot ownership |
| Durable journals/reset state | operation/result stores, capacities, time sources, reset/migration journals and erase namespaces | Exact schema/capacity/state inventory and preserve/erase classification |

### 5.6 Global ordering rule

Do not hardcode a migration/version successor from this document.

Before creating a migration:

1. search the whole repository for config schema versions, persisted blob versions, OTA `secure_version`, API schema versions and any ordered release generations;
2. calculate the global applicable maximum;
3. record the extracted set and duplicates;
4. select exactly the next value;
5. fail with `BLOCKED_DELTA_REVIEW` if a newer predecessor exists than this reviewed snapshot.

Reviewed values (`ConfigManager` schema `3`, persisted CoreState version `1`) are **SNAPSHOT-DERIVED — MUST RECOMPUTE**, not permanent target numbers.

### 5.7 Per-stage handoff freshness

Every completed stage must create `implementation-evidence/S<N>-completion.json` containing:

- input commit SHA;
- output commit SHA;
- plan ID and stage ID;
- changed files;
- inventories intentionally changed;
- tests/commands and exact results;
- generated schemas/configs/artifacts;
- unresolved deviations;
- `releaseability`, equal to `NON_RELEASABLE` for S0-S8 and `PRODUCTION_CANDIDATE` only for a complete S9 manifest.

The next stage starts only from that exact output commit and validates the handoff manifest. A mismatch returns `BLOCKED_STALE_BASELINE`.

### 5.8 Releaseability and production-surface startup contract

- Every S0-S8 completion manifest declares `releaseability: NON_RELEASABLE`.
- S4 produces unregistered production route/application contracts only. It does not contain a production management-listener registration path.
- S6 owns the single production control-plane composition root. Build validation rejects control-plane registration unless HTTPS, FD-17 certificate trust, secure storage, authentication, authorization, CSRF/origin protection and production-safe profile invariants are enabled.
- At runtime, any unhealthy security dependency prevents listener and mutation-route registration; there is no insecure fallback mode.
- S9 is the only stage allowed to emit `releaseability: PRODUCTION_CANDIDATE`, and only after verifying the complete S0-S9 manifest chain, exact commit lineage, exact binary hash and all release gates.
- Packaging, signing and promotion scripts reject any missing stage manifest, `NON_RELEASABLE` input or mismatched lineage with `BLOCKED_RELEASE_INCOMPLETE`.

## 6. Current and target artifact classification

### Existing artifacts

- `components/core`, `components/service`, `components/app_hal`.
- `components/web_ui`, `components/mqtt_bridge`, `components/matter_bridge`.
- `ConfigManager` schema/migrations and `StatePersistenceCoordinator` raw `CoreState` persistence.
- signed OTA manifest and staging/promotion scripts.
- host/integration/target tests and HIL scripts.
- `.github/workflows/ci.yml`.

### Target artifacts — expected absent

These names are proposed canonical targets and are not prerequisites:

- `components/core/include/device_id.hpp`;
- `components/service/include/device_locator_registry.hpp` and implementation;
- explicit versioned persisted-device-state serializer/migrator;
- `components/web_ui/include/web_auth.hpp` and authentication/session implementation;
- strict JSON request adapter owned by Web/application boundary;
- `sdkconfig.production.esp32c6`;
- `scripts/verify_production_security_profile.py`;
- release evidence manifest/schema;
- versioned API contract documentation/OpenAPI-equivalent JSON schema fixtures;
- command-result MQTT topic/payload fixtures;
- versioned golden HTTP status/error matrix consumed by firmware handlers, bundled Web UI and tests;
- explicit `LegacyIdentityEvidenceSnapshot` format and migration quarantine reasons;
- stable `GatewayId` provider, mDNS identity contract and encrypted current/next management-certificate slots;
- persisted optional Matter endpoint assignment and removal/tombstone state;
- fixed-capacity durable idempotency journal with approved default 64 and persisted completion-sequence eviction;
- protected restart-safe factory-reset journal and namespace preserve/erase registry;
- thin `OtaPlatformAdapter` plus application health policy backed by ESP-IDF rollback/anti-rollback APIs;
- `implementation-evidence/git-baseline.json` recording upstream or local archive-baseline provenance;
- `docs/architecture/DEVICE_IDENTITY.md`;
- `docs/security/CONTROL_PLANE_SECURITY.md`;
- `docs/security/PRODUCTION_HARDENING.md`.

### Discovery required

- Exact ESP-IDF 5.5.2 Kconfig symbol names for the chosen security profile must be verified against the `dependencies.lock`-pinned toolchain-generated `sdkconfig`.
- Manufacturing source for per-device provisioning secrets and device TLS certificate is environment-specific, but production fail-closed behavior is fixed.
- HIL runner port, broker endpoint and physical Zigbee device identifiers are environment-specific.
- Full target-side Matter stack adapter remains outside this plan.

## 7. Target architecture

### 7.1 Dependency direction

```text
Web UI / MQTT / Matter adapters
            |
            v
     ServiceRuntimeApi
            |
            v
service application policies, locator registry, persistence, auth ports
            |
            v
core domain state, DeviceId, commands, events, reducer
            ^
            |
app_hal implements Zigbee/MQTT/NVS/HTTPS/crypto ports at composition edge
```

No adapter may mutate Core or inspect another adapter's internal cache.

### 7.2 Canonical identity model

```text
DeviceId (EUI-64)       durable identity and all external/persistence keys
DeviceLocator           current short_addr + online mapping + mapping revision
DeviceDescriptor        manufacturer/model/capabilities keyed by DeviceId
DeviceState             observed domain state keyed by DeviceId
NetworkGenerationId       stable identity of the current Zigbee network generation
LegacyIdentityEvidence    record fingerprint + independently durable EUI proof + network generation
GatewayId                 factory base MAC, independent of Zigbee/Wi-Fi configuration
MatterEndpointAssignment  optional persisted endpoint for one DeviceId
```

`DeviceLocatorRegistry` maintains both bounded indexes:

```text
DeviceId -> current short_addr, status, mapping_revision
short_addr -> current DeviceId, mapping_revision
```

A remap is one atomic service operation:

1. validate EUI-64 and short address;
2. remove any old reverse mapping for this `DeviceId`;
3. detect whether short address belongs to another `DeviceId`;
4. mark the displaced device locator offline without transferring data;
5. install the new bidirectional mapping;
6. emit one domain event keyed by `DeviceId` with locator metadata;
7. persist mapping/identity state;
8. schedule integration resync.

Legacy migration does not infer ownership from this current registry. It consumes only the FD-15 evidence snapshot and quarantines every short-only record without independent historical EUI proof.

### 7.3 Persistence model

Do not persist raw C++ aggregate layout. Use explicit records with:

- magic;
- schema version;
- generation;
- payload length;
- CRC32C payload integrity field in addition to NVS transport integrity;
- explicit serialized fields;
- commit marker/active generation.

Write new generation completely, validate it, then atomically switch active generation. Retain previous valid generation for rollback until the next successful boot/flush.

The explicit device record includes optional Matter endpoint assignment and deletion/tombstone state. Separate versioned stores own the FD-15 identity-evidence snapshot, FD-18 operation journal and FD-21 reset journal; none is represented by raw C++ layout. FD-20 boot validity remains owned by ESP-IDF `otadata`/eFuse, while S7 operation records and release evidence store only request/result/health facts.

### 7.4 Security model

- `AuthenticationService` validates credentials and creates bounded sessions.
- `AuthorizationPolicy` checks capabilities before use-case invocation.
- `PhysicalPresenceService` issues a short-lived one-time grant from a trusted button event.
- `AuditSink` records security-sensitive decisions without secrets.
- Web transport is HTTPS in production.
- MQTT trust configuration is validated before connection.
- Encrypted NVS is mandatory for credentials, TLS keys, session seed and Wi-Fi/MQTT secrets.
- `GatewayId` comes only from the factory base MAC; production mDNS/TLS identity follows FD-17.
- Product CA and current/next device certificate slots are preserved across FD-21 reset and rotate atomically.
- Factory-reset namespace ownership is explicit and restart-safe; ordinary reset never modifies eFuse or hardware encryption keys.

### 7.5 Concurrency model

- Exactly one ServiceRuntime task is the Core writer.
- HAL callbacks and adapter tasks enqueue value requests/results through bounded synchronized queues.
- `CoreRegistry::publish`, reducer execution and event-bus publication occur on the writer task.
- Read models use pinned/copy snapshots.
- No pointer obtained from a temporary snapshot may escape the full expression.

### 7.6 Integration convergence model

- MQTT and Matter cache revisions and `resync_required`.
- Delta overflow invalidates the delta cache.
- Once transport capacity returns, the adapter rebuilds and publishes a full service-owned snapshot before resuming deltas.
- Retained MQTT state is observed state only.
- Matter uses one persisted dynamic endpoint per `DeviceId`, lowest-free allocation in the validated range and delayed reuse until removal/tombstone confirmation.

## 8. Execution map

| ID | Stage | Depends on | Parallel with | Primary output | Status | Releaseability |
|---|---|---|---|---|---|---|
| S0 | Freeze implementation baseline | none | none | Verified repository, Git and ESP-IDF 5.5.2 baseline | READY | NON-RELEASABLE |
| S1 | Stabilize tests and make capabilities truthful | S0 | none | Sanitizer-clean baseline; no temporary disable flags | READY | NON-RELEASABLE |
| S2 | Introduce stable `DeviceId` and locator lifecycle | S1 | none | Domain/HAL/service identity contract and legacy-evidence snapshot | READY | NON-RELEASABLE |
| S3 | Add versioned persistence and safe migration | S2 | none | Restart-safe identity/config migration and persisted Matter slot | READY | NON-RELEASABLE |
| S4 | Migrate HTTP, MQTT, HA and Matter contracts | S3 | none | Versioned DeviceId/GatewayId contracts and deterministic Matter map | READY | NON-RELEASABLE |
| S5 | Establish hardware security foundation and encrypted storage | S4 | none | Verified Secure Boot/Flash/NVS foundation and reset namespace ownership | READY | NON-RELEASABLE |
| S6 | Secure management, provisioning, strict JSON and MQTT transport | S5 | none | Authenticated HTTPS, TLS identity lifecycle and fail-closed MQTT security | READY | NON-RELEASABLE |
| S7 | Add idempotency, backpressure and convergence guarantees | S6 | none | Exact durable journal and pressure-safe convergence | READY | NON-RELEASABLE |
| S8 | Complete signed OTA, anti-rollback and recovery lifecycle | S7 | none | Safe mark-valid/eFuse commit and restart-safe recovery/reset | READY | NON-RELEASABLE |
| S9 | Enforce CI/HIL/release evidence and compatibility cutover | S8 | none | Non-skippable release gate and final rollout evidence | READY | PRODUCTION CANDIDATE ONLY IF COMPLETE |

The sequence is intentionally serial because S2-S9 touch shared identity, persistence, public contracts, security configuration or release evidence. S5 must precede S6 because encrypted storage and hardware security are prerequisites for production credentials and TLS private keys. Parallel execution is not allowed unless a later reviewed delta proves non-overlapping ownership and contracts. `READY` means ready for engineering execution only; it never overrides the releaseability column.

## 9. Detailed stage contracts

## Stage S0 — Freeze implementation baseline

### Objective

Prove that implementation starts from the reviewed repository snapshot, establish a real version-control baseline and produce machine-readable evidence that every later stage can trust.

### Why this stage exists

It prevents implementation against stale routes, tests, schema versions, CI topology, capability flags or toolchain assumptions and ensures every later stage has a real input/output commit SHA without relying on chat setup.

### Preconditions

- The plan package and reviewed inventory are available.
- No product source mutation has started.
- The executor has filesystem permission to inspect the tree and, when necessary, initialize local Git metadata.

### Scope

- In scope: inventory extraction, architecture/test baselines, exact ESP-IDF verification, Git/upstream provenance, evidence manifest.
- Out of scope: product code changes and claims of upstream ancestry for an archive-only baseline.

### Repository mapping and freshness

- Inspect all normalized source files enumerated by `tools/extract_repository_inventory.py`.
- Recompute all inventories in §5.5 and compare both directions with `evidence/reviewed-repository-inventory.json`.
- Recalculate counts; do not copy plan values.
- Detect duplicate route, test, Kconfig and schema identifiers.
- Confirm the global current config/persisted-state version set.
- Verify root `dependencies.lock` declares ESP-IDF `5.5.2`; verify any target lockfile agrees.
- Determine whether the input is an existing Git checkout or an archive without `.git`.
- Stale-input statuses: `BLOCKED_DELTA_REVIEW`, `BLOCKED_TOOLCHAIN`, `BLOCKED_VERSION_CONTROL`.

### Required changes

No product code changes are permitted. Required evidence work:

1. Run the inventory, architecture, host and integration baseline commands from §5.3.
2. Run the sanitizer baseline and confirm the exact three reviewed failures, or stop on a different result.
3. Verify exact ESP-IDF `5.5.2` from root `dependencies.lock` and target lockfiles. Record the resolved executable/container version and immutable image digest when available; any mismatch returns `BLOCKED_TOOLCHAIN`.
4. Establish one of these version-control baselines:
   - **Existing clean Git checkout:** record remote URL when present, current commit SHA, branch/detached state and clean status; verify normalized inventory against the reviewed baseline.
   - **Archive without `.git`:** initialize a local repository, configure a repository-local review identity, add only normalized source files represented by the reviewed inventory, exclude build/generated/evidence outputs, and create exactly one `reviewed-baseline` commit.
5. Record whether the baseline commit is an upstream commit or a local archive-baseline commit. A local commit must never be described as upstream history or ancestry.
6. If Git cannot be initialized cleanly or source/evidence boundaries are ambiguous, return `BLOCKED_VERSION_CONTROL` before mutation.
7. Record whether `idf.py`, target ESP32-C6 hardware, broker, Zigbee devices and signing/eFuse tooling are available.
8. Extract `service::kServiceMaxDevices`, all current Matter endpoint constants and real target static/reserved endpoints. Classify reviewed endpoints 10/11/12 as expected legacy fallback removal; any additional permanent reservation inside reviewed dynamic range 10-73 returns `BLOCKED_DELTA_REVIEW`.
9. Inventory all GatewayId/base-MAC accessors, mDNS names, product CA/trust keys, current/next certificate storage, operation/result capacities, time providers and reset/migration journals. Record missing target artifacts as `EXPECTED ABSENT`, not current defects.
10. Create `implementation-evidence/git-baseline.json` with baseline kind, commit SHA, source inventory hash, included/excluded path sets and ancestry claim.
11. Create `implementation-evidence/S0-baseline.json` with exact commands, results, versions, immutable digest, endpoint/capacity and security-identity inventories and environment limitations.
12. Create `implementation-evidence/S0-completion.json` with exact input/output baseline commit, `releaseability: NON_RELEASABLE` and the canonical handoff fields from §5.7.

### Contracts and invariants

- No product code changes occur in S0.
- Every later stage receives a real baseline commit SHA.
- Any unexplained inventory mismatch blocks all later stages.
- Tool or hardware unavailability is recorded; it never silently converts required verification into pass.
- The authoritative target toolchain is ESP-IDF `5.5.2` from `dependencies.lock`.

### Tests and verification

- Architecture gate.
- 69 host tests.
- 7 integration tests.
- Sanitizer baseline.
- Inventory exact two-way diff.
- reviewed Matter capacity 64 and legacy endpoint constants 10/11/12 are classified; no unexplained real reservation conflicts with target range 10-73;
- GatewayId/TLS/journal/reset current-versus-target inventory classification;
- Git tracked-file set exactly matches the normalized source set for an archive baseline.
- `idf.py --version` or container metadata confirms 5.5.2; root/target lockfiles agree.

### Deliverables

- `implementation-evidence/current-repository-inventory.json`.
- `implementation-evidence/repository-inventory-diff.json`.
- `implementation-evidence/git-baseline.json`.
- `implementation-evidence/S0-baseline.json`.
- `implementation-evidence/S0-completion.json` with `releaseability: NON_RELEASABLE`.
- Baseline commit SHA and, when applicable, immutable ESP-IDF container digest.

### Exit criteria

- [ ] Exact inventory match is proven.
- [ ] Host and integration suites match the reviewed result.
- [ ] Known sanitizer baseline is recorded.
- [ ] Global version predecessors are recorded.
- [ ] ESP-IDF is exactly 5.5.2 and lockfiles agree.
- [ ] A clean upstream or local archive-baseline commit exists and is truthfully classified.
- [ ] Required unavailable environments are explicitly marked.
- [ ] Matter capacity/range/reservation, GatewayId/TLS and journal/reset inventories are recomputed and classified without unexplained delta.
- [ ] S0 completion manifest exists and is explicitly non-releasable.

### Completion evidence

Return evidence-only changed files, commands, exact test counts, tool versions/digest, baseline kind, baseline commit, endpoint/capacity and Gateway/TLS/journal/reset inventory reports, S0 completion manifest, included/excluded tracked paths and any blocker.

### Handoff to the next stage

- Newly available input: frozen baseline commit, normalized inventory and exact ESP-IDF 5.5.2 evidence.
- Next unblocked stage: `S1`.
- Frozen contract: reviewed current-state evidence and version-control provenance.

---
## Stage S1 — Stabilize tests and make capabilities truthful

### Objective

Create a sanitizer-clean, capability-honest baseline before changing identity and public contracts.

### Why this stage exists

The current test suite contains dangling snapshot pointers, and production behavior is controlled by `kTemporarilyDisable*` source constants. Later stages need trustworthy tests and explicit capabilities.

### Preconditions

- `S0` complete.
- Input commit equals `S0` output commit.
- No repository delta outside the approved S1 scope.

### Scope

- In scope: snapshot test safety, sanitizer CI target, capability registry/Kconfig, RCP/Matter/Zigbee truthfulness.
- Out of scope: stable identity migration, auth, full RCP or Matter target implementation.

### Repository mapping and freshness

Existing symbols to inspect:

- `ServiceRuntime::state()` in `components/service/service_runtime.cpp`;
- reporting tests under `test/host`;
- temporary constants in `main/app_main.cpp` and `components/service/service_runtime.cpp`;
- `hal_matter_stack_available()` weak adapter;
- RCP worker startup and Web RCP routes;
- `main/Kconfig.projbuild`, `sdkconfig.defaults*`, route registration and bundled UI.

Target artifacts expected absent:

- bounded `RuntimeCapabilities` service-owned projection;
- CI sanitizer job.

Snapshot checks:

- Recompute temporary flag inventory; reviewed count is eight occurrence lines and four declarations.
- Recompute CI job and test inventories before edits.

### Required changes

1. Fix every test that stores a pointer/reference into a temporary `runtime.state()` result:
   - bind the snapshot to a named `CoreState`, then inspect it; or
   - add a test helper that returns a copied `CoreDeviceRecord`/`std::optional<CoreDeviceRecord>`.
2. Search all tests and production code for equivalent temporary-snapshot pointer patterns and fix all occurrences, not only the three ASan failures.
3. Add a host sanitizer build configuration and blocking CI job using ASan + UBSan.
4. Define explicit build/runtime capabilities for Zigbee, MQTT, Matter target adapter, gateway OTA and RCP update.
5. Replace all `kTemporarilyDisable*` constants with Kconfig/build-profile symbols or real capability checks.
6. Set truthful defaults:
   - Zigbee enabled in the normal gateway profile unless the selected OTA/RF profile explicitly disables it;
   - RCP update disabled until a target backend and worker are enabled;
   - Matter target capability false when only weak HAL stubs are linked;
   - unavailable capability route is not advertised in UI and returns stable `capability_unavailable` on direct access.
7. Define and test the `/api/v1/capabilities` DTO and service projection in S1; register the public route in S4.
8. Add a build check that fails when `kTemporarilyDisable` appears in production sources.
9. Update README status so bridge/runtime support is not confused with target stack availability.

### Contracts and invariants

- `ServiceRuntime::state()` remains a value snapshot unless a separately reviewed pinned-view API is introduced.
- No pointer/reference into a snapshot outlives the snapshot object.
- Capability availability is a service-owned read model.
- Web/MQTT/Matter adapters do not infer capabilities from weak symbols independently.
- RCP route behavior is honest even though RCP implementation remains out of scope.

### Error, authorization, transaction and concurrency behavior

- Unsupported capability returns stable error `capability_unavailable` and does not enqueue work.
- Capability checks occur before queue allocation.
- Single-writer Core invariant is documented and asserted in debug/host builds.

### Tests and verification

- Run all host and integration tests under normal build.
- Run all host tests under ASan + UBSan; expected result `69/69` or the updated recomputed total.
- Add tests:
  - capability false hides UI action;
  - direct unsupported route returns `capability_unavailable`;
  - RCP request is rejected before queueing when worker/backend unavailable;
  - Matter target capability is false with weak stubs;
  - no temporary-snapshot pointer helper returns dangling storage.
- Architecture strict gate.
- Grep/build assertion: zero production `kTemporarilyDisable*` occurrences.

### Deliverables

- Sanitizer-clean tests.
- Capability contract and tests.
- Kconfig/profile changes.
- Updated README/capability documentation.
- Blocking sanitizer CI job.
- `implementation-evidence/S1-completion.json`.

### Exit criteria

- [ ] ASan + UBSan suite passes completely.
- [ ] Temporary capability flag inventory is zero in production code.
- [ ] RCP and Matter target capability are reported truthfully.
- [ ] No unsupported operation enters a worker queue.
- [ ] Normal host/integration/architecture gates remain green.

### Completion evidence

Return exact sanitizer output, changed files, capability matrix, Kconfig defaults, test results and output commit SHA.

### Handoff to the next stage

- Newly available inputs: safe snapshot-test pattern and frozen capability contract.
- Next unblocked stage: `S2`.
- Frozen contracts: capability semantics; single-writer runtime ownership.

---
## Stage S2 — Introduce stable `DeviceId` and locator lifecycle

### Objective

Replace short-address identity with EUI-64 `DeviceId` throughout Core, HAL boundary and Service while retaining short address only as the current locator.

### Why this stage exists

It closes `ID-01`, prevents identity/data crossover on rejoin/address reuse, and creates the canonical contract required by persistence and integrations.

### Preconditions

- `S1` complete and sanitizer-clean.
- Capability and writer-task contracts frozen.
- ESP Zigbee API availability for EUI/short resolution verified against the pinned ESP-IDF version.

### Scope

- In scope: `DeviceId`, join/leave/report event identity, locator registry, Core records/events/commands, descriptor keying, service projections, rejoin lifecycle.
- Out of scope: persistence migration and external route/topic cutover, which occur in S3/S4.

### Repository mapping and freshness

Existing areas:

- `components/core/include/core_state.hpp`;
- `components/core/include/core_events.hpp`;
- `components/core/include/core_commands.hpp`;
- Core reducer/dispatcher/registry tests;
- `components/app_hal/include/hal_zigbee.h` and `hal_zigbee.c`;
- `components/service/hal_event_adapter.cpp`;
- `DeviceIdentityStore`, device/reporting/Tuya managers and service request/snapshot types;
- bridge snapshot builders.

Target artifacts expected absent:

- `components/core/include/device_id.hpp`;
- `DeviceLocatorRegistry` service component;
- `NetworkGenerationId` and explicit `LegacyIdentityEvidenceSnapshot` records;
- identity remap/evidence tests and HIL scenario.

Snapshot-derived inventory to recompute:

- every field, function, topic/parser and persisted key containing `short_addr`;
- every map/store keyed by short address;
- every test fixture that assumes short address is identity;
- every legacy record/companion store that already contains an EUI-64 or can only supply a current short-address lookup;
- every Zigbee network reset/recreation path and existing network-generation marker;
- `service::kServiceMaxDevices` and all sources required to size the bounded evidence snapshot.

Produce a two-way migration inventory:

1. repository symbols that require identity conversion;
2. symbols listed in the stage change set;
3. repository-only and plan-only differences;
4. duplicates and intentionally locator-only uses.

Unexplained difference: `BLOCKED_DELTA_REVIEW`.

### Required changes

1. Add trivial, fixed-size `core::DeviceId`:
   - stores `std::array<uint8_t, 8>` in canonical network order, most-significant byte first; HAL normalizes any stack-specific byte order before construction;
   - equality, ordering/hash-free bounded comparison, validity check;
   - parse/format exact 16-lowercase-hex representation;
   - no heap allocation.
2. Add `DeviceId` to `CoreDeviceRecord`, Core events and commands as the authoritative key.
3. Retain `short_addr` only as locator metadata where a current network address is operationally required.
4. Change reducer lookup/update/removal to `DeviceId`.
5. Define lifecycle events explicitly:
   - `DeviceDiscovered/Joined(DeviceId, short_addr, mapping_revision)`;
   - `DeviceLocatorChanged(DeviceId, old_short_addr, new_short_addr, mapping_revision)`;
   - `DeviceOffline/Left(DeviceId, last_short_addr, reason)`;
   - telemetry/command result keyed by `DeviceId` plus observed locator revision.
6. Extend HAL callback structs to include EUI-64 and short address for join, leave, raw report, read result and operation results where possible.
7. In `hal_zigbee.c`, use the already maintained IEEE/short mapping and emit remap as one identity-aware callback rather than a synthetic leave/join pair that loses identity.
8. Implement bounded `DeviceLocatorRegistry` in Service with bidirectional indexes and monotonically increasing mapping revision.
9. Resolve all inbound short-address reports to `DeviceId` before Core mutation.
10. Handle unresolved reports:
    - enqueue a bounded resolution request;
    - do not create a short-address-only Core record;
    - expire with classified `identity_unresolved` metric/log;
    - never bind to a guessed device.
11. Rename/split `DeviceIdentityStore` so manufacturer/model descriptor data is keyed by `DeviceId`; do not overload “identity” to mean descriptor.
12. Change reporting/Tuya/device managers to accept `DeviceId` and resolve locator only at HAL dispatch.
13. Add mapping revision to tracked commands so a result from an old locator cannot be applied after remap to a different physical device.
14. Keep bridge/API snapshot structures internally dual-field temporarily (`DeviceId` authoritative, short address diagnostic) so S3/S4 can migrate without parallel incompatible edits.
15. Add architecture invariant rules forbidding durable stores and public DTOs from using short address as the only device key.
16. Introduce persisted `NetworkGenerationId`: a random 128-bit value created when a Zigbee network is formed/adopted and replaced only by network recreation/factory reset. Assign and persist one current-generation value before evidence capture on an upgraded device.
17. Before S3 migration, disable join windows, network mutations and external command ingress, then create a bounded `LegacyIdentityEvidenceSnapshot` for every legacy migration candidate.
18. Each evidence entry records legacy key plus canonical record fingerprint, short address, independently durable EUI-64 when one already exists, network generation, locator revision, evidence source and capture boot/sequence. A live address-table lookup may confirm but may not invent the durable EUI field.
19. Store the snapshot in an explicit versioned double-buffered record with length/integrity/active-generation validation; do not persist a raw C++ aggregate.
20. Mark entries with no independently durable EUI binding as `insufficient_proof`. They are expected S3 quarantine inputs even when the live address table currently resolves the short address.
21. Freeze the evidence snapshot in `S2-completion.json`; any mapping/network-generation change before S3 commit invalidates that entry and forces quarantine.

### Contracts and invariants

- `INV-ID-01`, `INV-ID-02`, `INV-ID-03` are enforced by code and tests.
- The same EUI-64 with a new short address updates one Core record.
- A reused short address for another EUI-64 creates/updates a different record and cannot inherit config.
- Locator revision is checked at async result application.
- HAL calls still use current short address, but application commands carry `DeviceId`.
- Core remains ESP-IDF-free.
- Current locator data is not historical migration proof.
- The S2 evidence snapshot is complete, bounded and immutable for the S3 input commit.

### Error, authorization, transaction and concurrency behavior

- Invalid EUI-64: reject at HAL boundary and metric `zigbee_identity_invalid_total`.
- Locator registry full: reject new mapping with `no_capacity`; never evict an online device silently.
- Conflicting remap is applied on the Service writer task as one serialized operation.
- Stale command result for old mapping revision is recorded and ignored.
- Identity mapping updates and Core event publication follow one global order: locator registry first, Core event second, persistence request third, integration resync fourth.

### Migration, compatibility and rollout behavior

- This stage changes internal contracts only.
- External APIs may still accept legacy short address behind adapters until S4, but adapters must resolve to `DeviceId` immediately and return explicit unresolved errors.
- No durable new identity schema is written until S3.

### Tests and verification

Unit:

- `DeviceId` parse/format/invalid values;
- locator insert/find/remap/remove/full capacity;
- reducer join/rejoin/update/remove by DeviceId;
- descriptor storage by DeviceId;
- stale mapping-revision result ignored.

Integration:

- HAL join callback carries EUI-64;
- same EUI, different short address retains one state record;
- different EUI, reused short address does not inherit state;
- report arriving during remap resolves deterministically;
- unresolved identity does not create a device;
- legacy record with only current address-table resolution is classified `insufficient_proof`;
- evidence entry with exact record fingerprint, historical EUI and network generation is accepted;
- address reuse before evidence capture does not migrate the old short-only record.

Concurrency/retry:

- simultaneous remap and command result serialized;
- duplicate join is idempotent;
- leave after remap cannot remove the new owner of the old short address;
- address reuse or network-generation change between evidence capture and S3 handoff invalidates the entry;
- reboot after evidence snapshot preserves the exact validated generation and does not recapture silently.

HIL to add for later release execution:

- rejoin with short-address change;
- coordinator reboot and mapping restore;
- remove/rejoin;
- address reuse if test network can force it.

### Deliverables

- `DeviceId` domain type.
- Device locator registry.
- identity-aware HAL/service/Core contracts.
- converted managers and internal projections.
- tests and architecture rules.
- `docs/architecture/DEVICE_IDENTITY.md` including the evidence provenance hierarchy.
- explicit versioned `LegacyIdentityEvidenceSnapshot` and normalized evidence inventory.
- `implementation-evidence/S2-completion.json`.

### Exit criteria

- [ ] No Core aggregate is keyed only by short address.
- [ ] All inbound device events enter Core with valid `DeviceId`.
- [ ] All outbound HAL device commands resolve current locator from `DeviceId`.
- [ ] Rejoin and address reuse tests pass.
- [ ] Architecture, normal, sanitizer and integration suites pass.
- [ ] Complete short-address usage inventory classifies every remaining occurrence as locator-only, compatibility-only or defect.
- [ ] Every legacy migration candidate has either valid historical EUI evidence or an explicit `insufficient_proof` classification; current lookup alone validates none.
- [ ] Evidence survives reboot, and mapping/network-generation change invalidates rather than rebinds it.

### Completion evidence

Return the identity usage inventory/diff, normalized legacy-evidence inventory, evidence generation/hash, changed contracts, tests, HIL additions, architecture gate output and output commit SHA.

### Handoff to the next stage

- Newly available inputs: frozen `DeviceId`, locator lifecycle, dual-field internal projections and validated `LegacyIdentityEvidenceSnapshot`.
- Next unblocked stage: `S3`.
- Frozen contracts: DeviceId representation, locator revision semantics, remap ordering, network-generation identity and evidence provenance rules.

---
## Stage S3 — Add versioned persistence and safe migration

### Objective

Persist stable device identity, descriptors, locator metadata and reporting configuration through explicit versioned schemas and migrate existing short-address data without guessing.

### Why this stage exists

The current raw `CoreState` blob and short-address reporting keys cannot safely survive the S2 identity model or future layout changes.

### Preconditions

- `S2` complete.
- `DeviceId` and locator semantics frozen.
- Global schema/version predecessor inventories recomputed.

### Scope

- In scope: persisted state serializer, identity/config migration, atomic generation switch, quarantine, rollback and recovery tests.
- Out of scope: API/topic cutover and hardware NVS encryption enablement; the latter completes in S5.

### Repository mapping and freshness

Existing artifacts:

- `components/service/state_persistence_coordinator.cpp` and header;
- `components/service/config_manager.cpp` and header;
- `hal_nvs` adapter;
- configuration migration tests;
- state persistence tests;
- reporting profile storage and descriptor store.

Snapshot-derived values:

- `ConfigManager::kCurrentSchemaVersion = 3`;
- persisted CoreState key `core_state_v1`, version `1`;
- current NVS keys and capacity limits.

These must be globally recomputed. The new values are `NEXT_CONFIG_SCHEMA_VERSION` and `NEXT_PERSISTED_STATE_VERSION`, selected only after proving the global predecessor.

Target artifacts expected absent:

- explicit persisted device-state wire structs/serializer;
- migration journal/quarantine record;
- dual-generation active marker;
- persisted optional Matter endpoint plus removal/tombstone state;
- explicit legacy-evidence reference and quarantine reason codes.

### Required changes

1. Replace writes of raw `core::CoreState` with explicit fixed-width persisted records.
2. Define a bounded persisted state containing:
   - `DeviceId`;
   - last known short address as non-authoritative locator metadata;
   - online state sanitized to offline on restore;
   - observable device attributes required for warm restoration;
   - descriptor/capability data if retained;
   - optional Matter endpoint assignment and endpoint-removal/tombstone status;
   - legacy identity evidence reference/quarantine reason where applicable;
   - record count and per-record validity.
3. Add header fields: magic, schema version, generation, payload length, integrity value and commit status.
4. Implement two-generation write protocol:
   - write inactive generation;
   - read back and validate;
   - update active-generation marker;
   - retain previous valid generation until a later successful cycle.
5. Define `ConfigManager` reporting profile key as `DeviceId + endpoint + cluster_id`.
6. Create migration from short-address profiles using only the S2 evidence contract:
   - recompute the canonical legacy-record fingerprint and require exact equality with its evidence entry;
   - require independently durable EUI-64 evidence, the same `NetworkGenerationId` and the expected locator revision;
   - use the current Zigbee mapping only as a consistency check, never as proof provenance;
   - migrate only when every proof field is valid and exactly one `DeviceId` results;
   - quarantine with stable reason `no_historical_eui_evidence`, `record_fingerprint_mismatch`, `network_generation_mismatch`, `mapping_changed_after_capture` or `ambiguous_evidence` otherwise;
   - never bind an unresolved profile to a current/new short-address owner and never expose a manual guessed-rebinding operation.
7. Migrate descriptor entries to `DeviceId`.
8. Add a migration journal with states `not_started`, `in_progress`, `committed`, `rolled_back` and counts for migrated/quarantined entries.
9. Make migration idempotent after reboot at every write boundary.
10. Preserve the previous readable schema until the new generation is committed and boot-validated.
11. Sanitize restored runtime fields: network disconnected, command queues empty, sessions absent, locators require revalidation, pending operations not restored as success.
12. Reject unsupported future schema versions without erasing data; expose `storage_schema_unsupported` and safe management status.
13. Add explicit cleanup stage condition: old keys are deleted only after one successful release/canary window and verified new-schema boot.
14. Record migration metrics without logging secrets or full credentials.
15. Persist the optional Matter endpoint field as `null` for unassigned devices. S4 becomes the sole allocator and updates it through the same versioned/two-generation transaction.
16. Persist device deletion and Matter removal/tombstone confirmation separately so an endpoint cannot be made free by only one half of removal.
17. The admin migration view permits only inspect, recreate configuration against a selected canonical `DeviceId`, and explicitly discard quarantine. It has no action that mutates the quarantined record's identity binding.

### Contracts and invariants

- No persistence of raw C++ layout.
- Migration never guesses identity.
- Active generation always references a completely validated payload.
- At least one previous valid generation remains until post-boot confirmation.
- Forward schema is non-destructive.
- Quarantined config cannot affect commands or reporting.
- Current Zigbee lookup alone can never convert a quarantine entry into migrated state.
- Matter endpoint `null`, assigned and pending-removal states are explicit and versioned.

### Error, transaction and concurrency behavior

- Persistence writes are serialized by the Service writer/persistence manager.
- Power loss before active-marker switch leaves old generation authoritative.
- Power loss after switch must restore the new validated generation.
- CRC/length/magic/version mismatch falls back to previous generation.
- NVS no-capacity returns explicit degraded storage status; it does not partially commit.
- Migration runs before external mutation routes/subscriptions become active.

### Migration and rollout behavior

Boot sequence:

1. initialize NVS;
2. load/validate active and previous generations;
3. load and validate the frozen S2 `LegacyIdentityEvidenceSnapshot`;
4. migrate only entries whose record fingerprint, historical EUI, network generation and locator revision all pass; quarantine every other entry with a stable reason;
5. commit new schema;
6. publish read-only migration status;
7. enable command ingress only after authoritative state is selected.

A device with unresolved legacy identity remains visible only in an admin migration view and is not commandable. Rediscovery/current locator resolution does not rebind it; the operator recreates desired configuration against the canonical rediscovered `DeviceId` and explicitly discards the old quarantine record.

### Tests and verification

- explicit serializer round trip;
- corrupt magic/version/length/CRC;
- future schema handling;
- full-capacity boundary;
- mapped short-address profile migration;
- ambiguous/unresolved mapping quarantine;
- address reused before evidence capture, between capture/commit and after reboot must not receive the old profile;
- current lookup without historical EUI evidence always quarantines;
- record fingerprint, network generation and locator revision mismatch reason fixtures;
- quarantine recreation/discard flow has no identity-rebind operation;
- optional Matter endpoint round trip and pending-removal/tombstone state round trip;
- power loss at every migration journal transition;
- repeat migration after reboot;
- previous-generation fallback;
- old key cleanup only after confirmed boot;
- target NVS integration tests when toolchain available.

### Deliverables

- Versioned explicit persistence format.
- Config and device-state migrations.
- Migration status projection, stable quarantine reasons and metrics.
- Device persistence schema containing optional Matter endpoint and removal/tombstone state.
- Updated tests and fixtures.
- Migration/cutover documentation.
- `implementation-evidence/S3-completion.json`.

### Exit criteria

- [ ] New writes contain explicit versioned fields, not `CoreState` memory.
- [ ] Reporting profiles are keyed by DeviceId.
- [ ] Every legacy record is migrated only with complete FD-15 evidence or quarantined with no guessed/current-lookup-only mapping.
- [ ] The persisted device schema can represent unassigned, assigned and pending-removal Matter endpoint state.
- [ ] Power-loss/retry tests pass at every transition.
- [ ] Old schema remains recoverable through the defined rollback window.
- [ ] All normal/sanitizer/integration tests pass.

### Completion evidence

Return global version extraction, selected successors, evidence-to-migration decision matrix, quarantine reason report, Matter endpoint schema fixture, NVS key diff, tests, sample migration report and output commit SHA.

### Handoff to the next stage

- Newly available inputs: durable DeviceId state, proof-safe migration status and optional persisted Matter endpoint/removal state.
- Next unblocked stage: `S4`.
- Frozen contracts: persisted identity schema, reporting key, evidence/quarantine semantics, endpoint persistence ownership and cleanup gate.
## Stage S4 — Migrate HTTP, MQTT, Home Assistant and Matter contracts

### Objective

Expose versioned DeviceId-based external contracts and perform a safe, bounded cutover from short-address identity.

### Why this stage exists

Internal identity is not sufficient while clients, retained topics, HA discovery and Matter endpoints still use short address.

### Preconditions

- `S3` complete.
- DeviceId persistence and quarantine semantics frozen.
- Migration status is available before external command ingress.

### Scope

- In scope: `/api/v1`, bundled UI, MQTT v1 topics/payloads, HA cleanup/recreation, Matter endpoint persistence, legacy compatibility window.
- Out of scope: authentication/TLS enforcement, completed in S6.

### Repository mapping and freshness

Existing areas:

- Web handlers/routes/DTO serializers and `assets/app.js`;
- `application_command_mapper`;
- `mqtt_topics`, serializer, discovery, sync and bridge;
- `MatterBridge`, endpoint/device maps and service bridge snapshots;
- README public contracts and host/integration tests.

Recompute and exact-diff:

- all HTTP route literals;
- all MQTT topic builders/wildcards;
- all external DTO fields containing short address;
- all HA object/unique/device identifiers;
- all Matter mapping keys;
- route registration count versus server `max_uri_handlers`;
- factory base-MAC accessors and every existing gateway-instance identifier;
- `service::kServiceMaxDevices`, every static/reserved Matter endpoint and any fixed endpoint fallback.

### Required changes

#### HTTP API v1

1. Add this canonical route table:
   - `GET /api/v1/capabilities`;
   - `GET /api/v1/devices`;
   - `POST /api/v1/devices/join-window`;
   - `POST /api/v1/devices/{device_id}/commands/power`;
   - `DELETE /api/v1/devices/{device_id}`;
   - `PUT /api/v1/devices/{device_id}/reporting/{endpoint}/{cluster_id}`;
   - `GET /api/v1/network`;
   - `POST /api/v1/network/scans`;
   - `POST /api/v1/network/connections`;
   - `GET /api/v1/config`;
   - `PATCH /api/v1/config`;
   - `POST /api/v1/ota/operations`;
   - `POST /api/v1/rcp-update/operations` with capability rejection when unavailable;
   - `GET /api/v1/operations/{operation_id}` for network, command, OTA and RCP asynchronous results.
2. All device DTOs include:
   - `device_id` canonical string;
   - `short_addr` always present as a nullable diagnostic field; it is an integer when the device has a current locator and `null` otherwise;
   - `locator_revision`;
   - observed state and capability fields.
3. Device mutations accept `DeviceId`, never durable short address.
4. Use stable errors:
   - `device_not_found`;
   - `identity_unresolved`;
   - `device_offline`;
   - `stale_locator`;
   - `capability_unavailable`;
   - `no_capacity`;
   - `conflict`;
   - S6 auth/security errors.
5. Add response `schema_version: 1` where payload is a durable client contract.
6. Update bundled Web UI atomically to v1 routes and DeviceId.
7. Legacy `/api/...` read aliases proxy v1 for exactly the first production DeviceId migration release and include deprecation metadata plus a non-binding sunset intent. Their removal is out of scope and requires a separate reviewed cleanup plan.
8. Legacy mutation endpoints are disabled in production and return `410 Gone` with `legacy_mutation_disabled`; development-only compatibility requires an explicit Kconfig flag.
9. Add one authoritative versioned golden status/error matrix with this mapping: `200` synchronous read or completed idempotent replay; `202` newly accepted asynchronous operation; `400` invalid request; `401` unauthenticated; `403` unauthorized or physical presence required; `404` unknown public resource; `409` stale locator/idempotency/state conflict; `410` legacy mutation disabled; `413` request limit exceeded; `429` rate limited; `503` no capacity, capability unavailable or secure storage unavailable. Firmware handlers, the bundled UI and tests consume the same matrix.

#### MQTT v1

10. Canonical topic root remains configurable; device segment becomes DeviceId:

```text
zigbee-gateway/v1/devices/<device_id>/state
zigbee-gateway/v1/devices/<device_id>/telemetry
zigbee-gateway/v1/devices/<device_id>/availability
zigbee-gateway/v1/devices/<device_id>/config/set
zigbee-gateway/v1/devices/<device_id>/power/set
zigbee-gateway/v1/devices/<device_id>/commands/<operation_id>/result
zigbee-gateway/v1/gateway/state
```

11. Every JSON payload contains `schema_version: 1`, `device_id` where applicable, canonical FD-17 `gateway_id` and observation timestamp/revision. No configurable hostname, Wi-Fi MAC or Zigbee coordinator address may substitute for `GatewayId`.
12. Topic parsers accept exactly the canonical lowercase DeviceId format and reject ambiguous/malformed path segments.
13. Legacy short-address command wildcards are not subscribed in production.
14. During upgrade, publish retained empty payload tombstones for all known legacy state, availability, telemetry and HA discovery topics.
15. Do not create a new legacy topic after cutover, even if a device rejoins with a different short address.

#### Home Assistant

16. HA `unique_id`, object ID and device identifier are derived from `DeviceId` and canonical FD-17 `GatewayId`; cloning the firmware image to different hardware therefore yields a different gateway identity while reboot/reset on the same hardware does not.
17. Publish deletion payloads for old short-address discovery entities before publishing DeviceId-based discovery.
18. Preserve entity type/capabilities while changing identity; perform exactly one controlled deletion-and-recreation migration from legacy short-address entity IDs to DeviceId entity IDs.
19. Discovery command templates target v1 DeviceId topics.

#### Matter bridge

20. Key `MatterEndpointMapEntry` by `DeviceId`; one endpoint contains all supported clusters for that physical device.
21. Consume the optional S3 endpoint field. Dynamic base is `10`, capacity is the S0-recomputed `service::kServiceMaxDevices`, and reviewed range is `10-73` for reviewed capacity `64`.
22. Before mutation, exact-diff all endpoint constants. Remove the reviewed class-wide fallback constants 10/11/12 as expected target cleanup, then fail with `BLOCKED_DELTA_REVIEW` if any additional real static/reserved target endpoint conflicts with the complete 10-73 dynamic range or capacity differs without an approved plan delta.
23. Allocate the lowest free endpoint deterministically and persist it through the S3 versioned device record before publishing Matter identity.
24. On removal, mark endpoint pending removal, remove/tombstone it through the Matter adapter, persist confirmation, delete the device record and only then make the endpoint reusable.
25. Remove class-wide fixed endpoint fallback as production identity behavior. Fixed class endpoints may remain only in explicit isolated host fixtures.
26. A short-address remap updates locator metadata only and does not emit device disappearance/recreation.
27. Target-side Matter capability remains false unless a real adapter is linked; bridge host/runtime behavior remains testable.

#### Production route ownership

28. S4 builds and exercises v1 DTOs, serializers, handlers and route definitions only as unregistered application contracts in host, integration and explicitly non-production development profiles.
29. S4 does not add a production management-listener composition root and does not register production mutation routes. S6 owns the first production registration path and wires it together with the complete security stack.
30. Add a build/static invariant proving that no S4/S5 production artifact contains an enabled production control-plane registration path. A partially completed S4/S5 image is `NON-RELEASABLE` and cannot be packaged by release tooling.

### Contracts and invariants

- `INV-API-01` holds for all production mutations.
- Short address may appear only as diagnostic locator data.
- Legacy mutation is fail-closed in production.
- HA identity and Matter endpoints survive rejoin.
- All new contracts are versioned and have golden fixtures.
- HTTP list/count/device detail predicates use the same authoritative Service snapshot.
- `GatewayId` is factory-base-MAC-derived and stable across reboot/reset.
- Matter endpoint allocation and delayed reuse follow FD-16 exactly.
- Production mutation routes are not registered before the S6 security composition root is complete.

### Error, authorization, transaction and concurrency behavior

- S4 route logic is transport-neutral and unregistered; S6 centrally registers every protected route through the authenticated HTTPS composition root.
- Command routes resolve DeviceId and locator revision at use-case execution time, not during UI rendering.
- A remap between request parsing and dispatch returns/retries through the locator contract; it cannot target the new owner of an old short address.
- MQTT retained cleanup is idempotent.

### Migration, compatibility and rollout behavior

Cutover order:

1. firmware boots and completes S3 identity migration;
2. canonical v1 routes/topics become available;
3. bundled UI uses v1;
4. old retained MQTT/HA discovery entries are tombstoned;
5. new v1 discovery/state is published;
6. legacy read aliases remain enabled for the first production DeviceId migration release with `legacy_read_aliases=true` in release evidence;
7. legacy mutations remain disabled in production;
8. alias removal is not executed by this plan and requires a separate reviewed cleanup plan.

### Tests and verification

HTTP/contract:

- golden JSON fixtures for every v1 response/error;
- golden HTTP status/error matrix fixture consumed by handler tests and bundled UI tests;
- each canonical error is asserted against its frozen status code, including idempotent replay 200 and newly accepted operation 202;
- DeviceId path parsing, invalid case/length/characters;
- legacy read deprecation metadata;
- production legacy mutation returns 410;
- UI integration tests use only v1 routes;
- route count remains within configured handler capacity.

MQTT/HA:

- exact v1 topic fixtures;
- payload `schema_version` fixtures;
- malformed DeviceId topic rejected;
- no production subscription to legacy command wildcard;
- legacy retained tombstones generated once/idempotently;
- HA unique ID stable across short-address remap;
- no duplicate entity after migration.

Matter:

- endpoint stable across remap/reboot;
- one endpoint carries all supported clusters for one `DeviceId`;
- reviewed 10-73 range and recomputed capacity/reservation conflict checks;
- deterministic lowest-free assignment;
- different DeviceIds receive different endpoints;
- capacity failure explicit;
- address reuse cannot inherit endpoint;
- endpoint is not reused before persisted deletion plus Matter tombstone confirmation;
- class-wide fixed production fallback is absent;
- unavailable target adapter remains truthfully disabled.

Releaseability/Gateway identity:

- two hardware base MACs produce distinct GatewayIds; reboot and factory-reset simulation preserve one GatewayId;
- S4 and S5 production profiles expose no management mutation listener/route;
- packaging an S4/S5 completion artifact fails with `BLOCKED_RELEASE_INCOMPLETE`.

### Deliverables

- Versioned HTTP DTO/routes and updated Web UI.
- MQTT v1 topics, serializers, command parsers and result contract skeleton.
- HA discovery migration.
- DeviceId/GatewayId-based external identity contracts.
- deterministic DeviceId-based persisted Matter endpoint map with delayed reuse state.
- unregistered production route contracts plus build/static proof that S4/S5 have no production registration path.
- Golden contract fixtures and documentation.
- `implementation-evidence/S4-completion.json`.

### Exit criteria

- [ ] Every production device mutation uses DeviceId.
- [ ] Bundled UI contains no legacy route or short-address identity dependency.
- [ ] MQTT/HA/Matter identities remain stable across remap.
- [ ] Legacy command ingress is absent in production.
- [ ] Contract fixtures and all regression tests pass.
- [ ] Complete external-contract inventory contains no unclassified short-address identity use.
- [ ] GatewayId and HA/MQTT identity remain stable across reboot/reset and distinct across different factory base MACs.
- [ ] Matter uses one persisted endpoint per DeviceId in the validated range, and deletion/tombstone completion gates reuse.
- [ ] Production S4 artifact has no reachable management mutation surface and is marked `NON-RELEASABLE`.

### Completion evidence

Return route/topic inventories and two-way diffs, GatewayId derivation fixtures, Matter reservation/capacity/range proof, allocation/removal traces, production-route absence evidence, golden schemas, migration/tombstone evidence, tests and output commit SHA.

### Handoff to the next stage

- Newly available inputs: frozen v1 HTTP/MQTT contracts, canonical GatewayId and deterministic persisted Matter endpoint lifecycle.
- Next unblocked stage: `S5`.
- Frozen contracts: v1 routes/topics, canonical HTTP status/error matrix, GatewayId/HA identity, Matter allocation/range/reuse and S4-unregistered/S6-secured route ownership.

---
## Stage S5 — Establish hardware security foundation and encrypted storage

### Objective

Create and verify the ESP32-C6 hardware-backed security foundation required before any production admin, MQTT, Wi-Fi, TLS or provisioning secret is written.

### Why this stage exists

The management-security stage cannot safely create credentials or TLS keys until Secure Boot, Flash Encryption, NVS Encryption, eFuse policy and encrypted storage ownership are available. This stage removes that dependency inversion.

### Preconditions

- `S4` complete and external contracts frozen.
- Exact ESP-IDF `5.5.2` and immutable toolchain/container evidence from S0 are available.
- Production and development hardware profiles are distinguishable.
- The manufacturing/eFuse environment is identified; if irreversible provisioning cannot be dry-run and read back, return `BLOCKED_SECURITY_PROVISIONING`.

### Scope

- In scope: production sdkconfig foundation, Secure Boot v2, Flash Encryption release mode, NVS Encryption, eFuse policy/readback, encrypted NVS namespaces, TLS/provisioning storage ports and legacy secret migration scaffolding.
- Out of scope: HTTP authentication/session behavior, MQTT connection policy and full OTA manifest/anti-rollback lifecycle; those are S6 and S8.

### Repository mapping and freshness

Existing areas:

- `sdkconfig.defaults`, `sdkconfig.defaults.esp32c6`, `main/Kconfig.projbuild`;
- NVS adapters, configuration manager and connectivity/provisioning storage;
- OTA trust store/public keys only where they affect secure-boot/eFuse partition assumptions;
- partition layout and target-build scripts;
- root and target `dependencies.lock`.

Target artifacts expected absent:

- `sdkconfig.production.esp32c6`;
- `scripts/verify_production_security_profile.py`;
- machine-readable eFuse provisioning/evidence schema;
- encrypted NVS namespace ownership registry;
- production TLS/provisioning secret storage ports;
- encrypted current/next management-certificate slot ownership;
- factory-reset preserve/erase namespace registry and protected reset-journal storage.

Snapshot/global-order checks:

- Recompute all secure-boot, flash-encryption, NVS-encryption, rollback and console/JTAG/UART Kconfig symbols from ESP-IDF 5.5.2 generated configuration.
- Recompute NVS namespaces/keys and all plaintext credential locations.
- Verify partition layout can host encrypted NVS key material and OTA rollback slots.
- Record secure-boot digest slots and current eFuse state; do not infer irreversible state from documentation.

### Required changes

#### Immutable production security profile

1. Add a dedicated ESP-IDF 5.5.2 production profile enabling Secure Boot v2, Flash Encryption release mode, NVS Encryption, rollback support and production console/JTAG/UART restrictions consistent with the recovery policy.
2. Keep development and HIL profiles separate and visibly labeled; insecure development flags are forbidden in production.
3. Add `verify_production_security_profile.py` that parses generated `sdkconfig`, verifies exact approved symbols and fails on contradictory or missing settings.
4. Production build fails when generated configuration differs from the approved profile or the resolved ESP-IDF version is not exactly 5.5.2.

#### eFuse and manufacturing foundation

5. Define a machine-readable eFuse provisioning template containing chip identity, secure-boot digest slots, flash-encryption state, anti-rollback/security-version fields, JTAG/download policy and protection status.
6. Provision in two mandatory phases: dry-run/readback approval, then irreversible burn and post-burn verification. If tooling cannot prove both phases, return `BLOCKED_SECURITY_PROVISIONING`.
7. Quarantine devices with unexpected or partially provisioned eFuse state; they cannot receive production credentials or be promoted.
8. Store only redacted public digest/evidence in implementation/release evidence; never place production private keys in the repository.

#### Encrypted storage foundation

9. Create explicit encrypted NVS namespaces/ownership for Wi-Fi credentials, MQTT credentials/trust references, admin verifier, TLS private key/certificate reference, session seed and manufacturing provisioning records.
10. Add storage ports that return typed `available`, `not_provisioned`, `corrupt` and `unavailable` results; production callers must fail closed on any non-available state.
11. Add restart-safe migration scaffolding for legacy plaintext values: read legacy, validate, write encrypted, read back/verify, then erase plaintext. No automatic migration executes until S6 authorization/physical-presence policy exists.
12. Ensure no new production secret is written before NVS Encryption and required key protection are verified at runtime.
13. Add production TLS/provisioning key storage interfaces consumed by S6; this stage does not generate untracked production certificates or shared secrets.
14. Redact all key/credential material from logs, crash reports and completion evidence.
15. Classify every eFuse/partition/NVS namespace/key as `PRESERVE_ON_FACTORY_RESET`, `ERASE_ON_FACTORY_RESET` or `RESET_JOURNAL_ONLY`; an unclassified namespace blocks S8 reset implementation.
16. Preserve eFuse/security state, flash/NVS encryption key material, factory GatewayId source, manufacturing PoP, product CA/trust anchors and encrypted device TLS `current`/`next` slots.
17. Mark admin/session, Wi-Fi, MQTT, Zigbee network/device/reporting, Matter, operation journal and legacy migration/tombstone data for erase. Provide typed namespace erase operations; do not use broad whole-partition erase that can touch preserved material.
18. Add a dedicated protected reset-journal storage port that survives erasing user/application namespaces and can atomically represent the four FD-21 states.

### Contracts and invariants

- NVS Encryption is active before any new production secret write.
- Production firmware cannot boot into an insecure fallback because encrypted storage is missing.
- Secure-boot, flash-encryption and eFuse evidence is tied to chip identity and exact generated sdkconfig hash.
- Test/private fixture keys are not accepted as production provisioning inputs.
- S6 receives typed secure-storage ports and cannot bypass them.
- Factory reset cannot erase GatewayId source, manufacturing trust, product CA, certificate slots, eFuse or hardware encryption material.
- Every resettable namespace has exactly one preserve/erase owner classification.

### Error, transaction and concurrency behavior

- Interrupted encrypted migration remains restart-safe and never deletes plaintext before encrypted readback succeeds.
- Interrupted eFuse provisioning is detected by readback and cannot be reported as success.
- Secure-storage failure returns `secure_storage_unavailable` and maps to HTTP 503 when exposed later.
- Credential migration and firmware/OTA operations share an explicit security-administration lock; no concurrent operation may mutate the same namespace/eFuse plan.

### Migration, compatibility and rollout behavior

1. Existing development devices without secure fuses remain development inventory until explicitly provisioned.
2. Production and development device inventories are separated.
3. Legacy plaintext values are classified and staged but not migrated without the S6 authenticated physical-presence workflow.
4. A canary device proves generated config, eFuse readback, encrypted storage and reboot restore before S6 production credential enrollment begins.

### Tests and verification

Host/tooling:

- ESP-IDF 5.5.2 profile verifier positive/negative fixtures;
- production-vs-development profile separation;
- NVS namespace ownership and duplicate-key inventory;
- plaintext-to-encrypted migration fault injection at every step;
- test-key/fixture fingerprint rejection;
- partition/slot/key-storage checks.

Target/HIL:

- Secure Boot v2 readback;
- Flash Encryption release-mode readback;
- NVS Encryption secret round trip and raw-flash non-disclosure check;
- reboot/power-loss during encrypted migration;
- missing/corrupt encrypted storage fails closed;
- JTAG/UART/download policy verification;
- unexpected eFuse state causes quarantine;
- reset namespace matrix test proves preserved material survives and erase-owned fixtures disappear;
- power loss during a simulated erase leaves the protected reset journal readable.

### Deliverables

- ESP-IDF 5.5.2 production sdkconfig profile and verifier.
- eFuse provisioning template, evidence schema and operator runbook.
- encrypted NVS namespace ownership registry and storage ports.
- restart-safe legacy secret migration scaffolding.
- production TLS/provisioning storage interfaces with encrypted current/next certificate slots.
- factory-reset namespace ownership registry and protected reset-journal storage port.
- initial `docs/security/PRODUCTION_HARDENING.md` hardware/storage section.
- `implementation-evidence/S5-completion.json`.

### Exit criteria

- [ ] Generated production sdkconfig passes the ESP-IDF 5.5.2 verifier.
- [ ] Secure Boot, Flash Encryption and NVS Encryption are verified on canary hardware.
- [ ] eFuse state is read back and matches policy.
- [ ] Production secret writes fail closed unless encrypted storage is verified.
- [ ] Storage ports required by S6 are frozen and tested.
- [ ] Legacy plaintext migration is restart-safe and cannot erase unverified data.
- [ ] The complete namespace inventory has one preserve/erase/reset-journal classification and broad erase cannot touch preserved trust/identity material.

### Completion evidence

Return generated sdkconfig hash, exact toolchain/container digest, eFuse readback with secrets redacted, encrypted-storage tests/HIL, complete reset preserve/erase namespace diff, certificate-slot/reset-journal storage tests and output commit SHA.

### Handoff to the next stage

- Newly available inputs: verified hardware security baseline, encrypted secret storage, current/next TLS slots, protected reset journal and complete preserve/erase ownership.
- Next unblocked stage: `S6`.
- Frozen contracts: production sdkconfig foundation, eFuse policy, secure-storage ownership/results, reset namespace ownership and migration safety.

---
## Stage S6 — Secure management, provisioning, strict JSON and MQTT transport

### Objective

Make all production control surfaces authenticated, encrypted, bounded and fail-closed.

### Why this stage exists

The current plain HTTP server, shared AP password, substring JSON parsing and plaintext MQTT default are unacceptable for a production management plane.

### Preconditions

- `S5` complete.
- `S4` contracts and golden HTTP status/error matrix frozen.
- Encrypted storage, production TLS/provisioning storage ports and hardware security evidence are available.
- Production capability matrix available.
- Manufacturing/provisioning adapter interface can be defined even if the final factory backend is environment-specific.

### Scope

- In scope: HTTPS, credential enrollment, sessions, authorization, CSRF/origin, physical presence, strict JSON, rate limiting, MQTT TLS/trust/secrets, audit.
- Out of scope: cloud identity, multi-user RBAC, remote internet exposure.

### Repository mapping and freshness

Existing areas:

- `web_server.cpp`, route registration, common parser and all handlers;
- Wi-Fi/provisioning bootstrap in `main/app_main.cpp` and service connectivity code;
- NVS/crypto/random adapters;
- MQTT Kconfig, HAL config and bridge startup;
- `main/Kconfig.projbuild`, `sdkconfig.defaults*` and generated production-profile configuration;
- OTA/RCP/device destructive handlers;
- Web UI request client.

Target artifacts expected absent:

- `AuthenticationService`, `AuthorizationPolicy`, `PhysicalPresenceService`, bounded session store;
- HTTPS server adapter/configuration;
- strict JSON schema reader;
- production MQTT trust configuration;
- security audit record projection;
- security-bound Kconfig symbols `ZGW_COMMISSIONING_WINDOW_SECONDS`, `ZGW_JSON_MAX_BODY_BYTES`, `ZGW_JSON_MAX_DEPTH`, `ZGW_JSON_MAX_STRING_BYTES`, `ZGW_JSON_MAX_KEYS`, `ZGW_LOGIN_ATTEMPTS_PER_MINUTE`, `ZGW_COMMANDS_PER_MINUTE`, `ZGW_MUTATIONS_PER_MINUTE`, `ZGW_FIRMWARE_OPS_PER_HOUR` and `ZGW_AUDIT_RING_RECORDS`;
- generated production security-profile verifier;
- canonical GatewayId/mDNS provider and certificate SAN validator;
- authenticated current/next certificate rotation adapter with atomic active-slot selection;
- factory-reset route policy metadata reserved for S8 implementation.

Recompute:

- every protected route and required capability;
- every request parser and body limit;
- every secret/config key and log statement;
- every plaintext URL/default;
- every handler that can alter device/network/trust state;
- every security-bound Kconfig symbol, its inclusive range, all profile overrides and the generated production value.

### Required changes

#### Security invariants, bounded tunables and typed accessors

Before implementing the control-plane changes, create the Kconfig definitions and one typed security-bounds accessor consumed by provisioning, Web parsing, rate limiting and audit storage.

Validated tunables and approved defaults:

- `ZGW_COMMISSIONING_WINDOW_SECONDS`: range 60–600, approved default 600;
- `ZGW_JSON_MAX_BODY_BYTES`: range 512–2048, approved default 2048;
- `ZGW_JSON_MAX_DEPTH`: range 2–4, approved default 4;
- `ZGW_JSON_MAX_STRING_BYTES`: range 64–512, approved default 512;
- `ZGW_JSON_MAX_KEYS`: range 8–32, approved default 32;
- `ZGW_LOGIN_ATTEMPTS_PER_MINUTE`: range 1–5, approved default 5;
- `ZGW_COMMANDS_PER_MINUTE`: range 10–60, approved default 60;
- `ZGW_MUTATIONS_PER_MINUTE`: range 1–5, approved default 5;
- `ZGW_FIRMWARE_OPS_PER_HOUR`: range 1–2, approved default 2;
- `ZGW_AUDIT_RING_RECORDS`: range 32–128, approved default 128.

The provisioning passphrase length remains a non-configurable 16 Base32 characters, and login backoff remains a non-configurable 2-second start with a 60-second maximum. The typed accessor is the only application-facing source for tunables; handlers/adapters must not repeat numeric literals.

Kconfig rejects out-of-range values. The production verifier enforces FD-13 hard invariants exactly, verifies tunable range membership, records the exact selected values and binds the generated `sdkconfig` hash to the exact HIL-tested binary. An in-range change from the approved default requires a reviewed release-manifest delta and rerunning the named affected tests, rather than an automatic rejection solely for differing from the default.

#### Provisioning and credentials

1. Remove `kProvisioningApPassword = "12345678"` and every shared/default production secret.
2. Introduce a `ProvisioningSecretProvider` port:
   - production adapter reads per-device manufacturing proof-of-possession material;
   - development adapter may generate and print a one-time secret;
   - production startup fails closed when material is absent.
3. Commissioning mode starts only after first-boot policy or trusted physical button action and expires according to `ZGW_COMMISSIONING_WINDOW_SECONDS`; approved default is 600 seconds and every production value must remain inside FD-13.
4. Provisioning AP SSID includes a non-secret gateway suffix; its passphrase is exactly 16 cryptographically random Base32 characters and uses WPA2/WPA3 settings supported by target.
5. Enrollment creates a PBKDF2-HMAC-SHA256 admin password verifier with a per-device random 128-bit salt. Calibrate iterations to 250-500 ms on ESP32-C6 with a hard minimum of 50,000; store the selected iteration count with the verifier. Never store or log plaintext password.
6. Generate session and CSRF secrets from hardware RNG.

#### HTTPS and sessions

7. Replace production `httpd_start` with HTTPS server configuration. Development HTTP is gated by an explicit non-production profile.
8. Read `GatewayId` only from the ESP32 factory base MAC and render exactly 12 lowercase hex characters. Verify manufacturing/provisioning uniqueness; duplicate/cloned GatewayId evidence blocks production enrollment.
9. Derive production mDNS host exactly as `zigbee-gateway-<last6>.local` and advertise only `https://`.
10. Production uses the S5 encrypted `current` certificate/key slot. Its certificate must chain to the configured product management CA and contain both the exact mDNS DNS SAN and URI SAN `urn:zgw:<gateway_id>`. Development profile may generate a visibly development-only self-signed certificate. Production never generates or accepts an untracked self-signed certificate.
11. Product CA trust is distributed to admin/browser clients out of band. Missing CA, invalid issuer/SAN/expiry/key match or unreadable current slot keeps the production management listener disabled.
12. Implement authenticated, physical-presence-protected certificate rotation with encrypted `current` and `next` slots and one atomic active-slot reference. Validate key/certificate/SAN/issuer/expiry in `next`, run a bounded local listener/handshake verification, switch atomically only after success, retain the previous confirmed slot through one successful reboot/post-activation check, and otherwise restore the last confirmed slot before listener enablement. Do not add a generic PKI workflow engine.
13. Add a bounded store of four concurrent sessions. Each session has a 15-minute idle timeout, 8-hour absolute timeout, logout/revocation and boot invalidation.
14. Use cookie name `zgw_session` with `Secure`, `HttpOnly`, `SameSite=Strict` and path `/api/v1`. CSRF token is a separate 256-bit session-bound value returned only by the authenticated session endpoint.
15. Require session-bound CSRF token and same-origin validation for every state-changing browser request.
16. Reject permissive CORS; default to same-origin only.
17. Add exact authentication routes:
   - `POST /api/v1/provisioning/enroll` available only in active commissioning mode with proof of possession and physical presence;
   - `POST /api/v1/auth/login`;
   - `POST /api/v1/auth/logout`;
   - `GET /api/v1/auth/session` returning actor capabilities and CSRF token;
   - `POST /api/v1/auth/password` requiring the current credential and recent physical presence;
   - `POST /api/v1/security/certificates/operations` for FD-17 rotation;
   - `POST /api/v1/system/factory-reset/operations`, registered with policy metadata in S6 but returning `capability_unavailable` until the S8 reset state machine is installed.

S6 introduces the only production listener/control-plane composition root. It registers production mutation routes only after certificate/trust validation, secure-storage readiness and central authentication/authorization policy initialization. A production build fails when this registration path is enabled without every FD-13 hard invariant; runtime failure leaves the routes unregistered.

#### Authorization and physical presence

18. Define capabilities:
   - `read_status`;
   - `control_device`;
   - `manage_network`;
   - `commission_device`;
   - `remove_device`;
   - `firmware_admin`;
   - `rcp_admin`;
   - `security_admin`;
   - `factory_reset`.
19. Central middleware authenticates and authorizes before request-body parsing/use-case invocation.
20. `join`, `remove`, Wi-Fi credential replacement, certificate rotation, OTA, RCP and factory reset require a recent one-time physical-presence grant.
21. Grant is created only from trusted GPIO/button event, has maximum 60-second lifetime, is bound to gateway boot/session/action class and is consumed once.
22. Factory reset additionally requires a fresh manufacturing PoP challenge. S6 owns policy validation; S8 owns the reset journal and erase execution.
23. Direct route access without capability returns non-leaky stable errors and does not reveal private state.

#### Strict request parsing

24. Replace `strstr`/`strchr` JSON readers in Web and application command mapping with one `cJSON`-backed `StrictJsonObjectReader`; the adapter owns duplicate-key detection, unknown-field policy, exact type extraction and reads body/depth/string/key limits only from the typed FD-13 accessor. Approved defaults are 2048 bytes, depth 4, string length 512 and 32 object keys. Exceeding the selected structural/body limit returns the golden `413` result.
25. For every command schema:
   - enforce object root;
   - reject duplicate keys;
   - reject unknown fields unless explicitly forward-compatible;
   - enforce exact types and integer bounds;
   - enforce body size and nesting depth;
   - validate UTF-8/string escapes;
   - reject trailing tokens.
26. Keep domain validation in application/Core; JSON validation does not replace invariants.
27. Centralize stable parse errors without echoing attacker-controlled payloads.

#### Rate limiting and audit

28. Implement rate limits through the typed FD-13 accessor. Approved defaults are login 5/minute/source with exponential 2–60 second backoff, ordinary commands 60/minute/session, join/remove/network mutation 5/minute/session and OTA/RCP 2/hour/session. Every selected production value must be inside the validated range and bound to release/HIL evidence. A limit response uses the golden `429` mapping.
29. Credential-failure backoff is bounded and automatically recoverable; it must not create permanent remote denial of service.
30. Write structured audit records for login, logout, denied actions, physical-presence grant use, network change, join/remove, OTA/RCP and credential reset.
31. Audit fields: monotonic operation ID, actor/session fingerprint, action, DeviceId if applicable, result, timestamp/boot ID. Persist a redacted bounded ring using the FD-13 accessor; approved default is 128 records. Use batched writes and never log passwords, tokens, Wi-Fi passwords or private keys.

#### MQTT production security

32. Extend `hal_mqtt_config_t` with trust mode, CA/bundle reference, optional client certificate/key reference and hostname-verification requirement.
33. Add production Kconfig/NVS settings for `mqtts://`, trust material and credentials.
34. Production build refuses to start MQTT when URI is plaintext, trust material missing, hostname verification disabled or credential storage unavailable.
35. Development plaintext MQTT requires an explicit insecure-development flag and emits a visible capability/security status.
36. Document/apply broker ACL for only the configured v1 topic root.
37. Store MQTT credentials only in encrypted NVS; redact broker credential details from logs/status.

### Contracts and invariants

- Every HTTP response follows the S4 golden status/error matrix: 200 read/completed replay, 202 new async acceptance, 400 invalid request, 401 unauthenticated, 403 unauthorized/physical presence, 404 unknown public resource, 409 conflict, 410 legacy mutation disabled, 413 request limit, 429 rate limit and 503 capacity/capability/secure-storage failure.
- `INV-AUTH-01` and `INV-AUTH-02` hold for all protected routes.
- Protected handlers cannot be registered outside central policy metadata.
- Production control traffic is encrypted.
- Production has no shared default secret or plaintext MQTT fallback.
- GatewayId, mDNS host, certificate SAN/issuer and CA trust follow FD-17 exactly.
- The production listener and mutation routes remain disabled until certificate trust plus authentication policy are healthy.
- Certificate rotation and factory-reset initiation require dedicated capability, physical presence and stable audit records.
- Request parsing is deterministic and duplicate-key safe.
- Security errors are stable but do not leak whether a hidden DeviceId exists.

### Error, transaction and concurrency behavior

- Authentication/session mutation uses a bounded synchronized store.
- Permission and physical-presence grant are rechecked immediately before destructive queue submission.
- Retry after commit with the same idempotency key is handled by S7 contract, not re-executed.
- Session expiry during a long request causes failure before mutation.
- Rate-limit state has bounded memory and deterministic eviction.
- Missing encrypted storage or invalid/missing certificate/CA trust makes production management unavailable rather than insecure.
- Certificate activation uses the atomic active-slot reference; power-loss recovery selects the last confirmed valid slot before accepting connections, without a separate generic certificate state machine.
- Factory-reset request records intent only after policy/PoP checks and hands execution to S8; it cannot directly erase namespaces from a Web handler.

### Migration, compatibility and rollout behavior

1. Upgrade boots into restricted migration mode if no admin credential exists.
2. Read-only health/capability endpoint exposes only non-sensitive setup status.
3. Physical presence plus provisioning proof is required to establish the first admin credential.
4. HTTP legacy profile remains development-only; production exposes HTTPS only.
5. Existing MQTT plaintext configuration is marked insecure and not auto-connected in production until trust material is supplied.

### Tests and verification

Kconfig and profile validation:

- build fixture at every configurable minimum;
- build fixture at every approved default;
- build fixture at every configurable maximum;
- one-below-minimum and one-above-maximum rejection for every configurable symbol;
- rejection of any attempted override for fixed settings;
- production-profile verifier rejects any disabled hard invariant or development bypass;
- an in-range non-default fixture passes range validation only when its selected values are declared in the release-manifest fixture and the affected security/load tests are rerun;
- the exact generated `sdkconfig` hash and selected tunables are bound to the HIL-tested binary;
- typed accessor values exactly match generated configuration without duplicate literals in handlers/adapters.

Authentication/authorization:

- protected route without session;
- wrong capability;
- expired/revoked session;
- CSRF missing/wrong/replayed;
- wrong Origin;
- physical grant missing/expired/reused/wrong action;
- permission loss before queue submission;
- session-store full;
- brute-force rate limit and recovery;
- exact boundary tests for login, ordinary command, join/remove/network and OTA/RCP limits;
- Web UI renders/retries each golden HTTP status without treating 202 as completed or replay 200 as a new operation.

Parser/fuzz:

- duplicate keys;
- key inside string/nested object;
- `truejunk`, numeric suffix, overflow, negative where forbidden;
- escaped strings and malformed UTF-8;
- trailing JSON tokens;
- generated boundary fixtures for each selected minimum/default/maximum parser profile; the approved-default fixture includes 2048/2049-byte bodies, depth 4/5, 512/513-byte strings and 32/33-key objects;
- unknown fields;
- fuzz corpus for every command schema under ASan/UBSan.

Transport and gateway identity:

- production HTTP disabled;
- factory base MAC formatting, reboot/reset stability and two-hardware uniqueness;
- exact mDNS DNS SAN and `urn:zgw:<gateway_id>` URI SAN;
- wrong issuer, absent product CA, expired/not-yet-valid certificate and key mismatch keep listener disabled;
- cloned/duplicate GatewayId manufacturing evidence is rejected;
- current/next staging success, failed validation, atomic activation rollback and power loss before/after active-slot switch and reboot confirmation;
- TLS certificate/key load failure is fail-closed;
- MQTT plaintext rejected in production;
- invalid CA/hostname/expired cert rejected;
- secrets absent from logs/status;
- broker ACL negative test;
- S6 production profile is the first stage where authenticated mutation reachability tests may pass; S4/S5 artifacts remain unreachable;
- factory-reset route requires `factory_reset`, physical presence and PoP and remains execution-disabled until S8.

HIL:

- first enrollment with physical presence;
- expired commissioning window;
- HTTPS management from browser/client;
- unauthorized LAN client cannot control device or start OTA;
- MQTT TLS connection and invalid-cert rejection.

### Deliverables

- Auth/session/authorization/physical-presence components and route policy table.
- HTTPS production server, canonical GatewayId/mDNS provider, SAN/CA validator and current/next certificate rotation adapter.
- Strict JSON adapter and fuzz corpus.
- Secure provisioning flow.
- MQTT TLS/trust configuration.
- Audit/rate-limit implementation.
- certificate-rotation and factory-reset route policy metadata/tests.
- Security-bound Kconfig definitions, typed accessors and production-profile verifier fixtures.
- `docs/security/CONTROL_PLANE_SECURITY.md`.
- `implementation-evidence/S6-completion.json`.

### Exit criteria

- [ ] No production management mutation is reachable without valid FD-17 certificate trust plus auth/capability checks.
- [ ] GatewayId/mDNS/SAN/CA identity is exact and stable, and certificate rotation is power-loss safe.
- [ ] Destructive/trust-changing operations require recent physical presence.
- [ ] Production has no static AP password, plain HTTP or plaintext MQTT fallback.
- [ ] Strict parser and fuzz tests pass under sanitizers.
- [ ] Secrets are encrypted/redacted.
- [ ] Every configurable FD-13 bound accepts its minimum/default/maximum and rejects one-below/one-above fixtures.
- [ ] Fixed settings reject overrides; hard invariants are exact; tunables remain inside FD-13 ranges and the exact selected profile hash is bound to HIL/release evidence.
- [ ] Security HIL negative scenarios pass or return `BLOCKED_HIL_ENVIRONMENT` for final S9 release completion.

### Completion evidence

Return route-policy matrix, GatewayId/mDNS fixtures, SAN/issuer/expiry and current/next activation/rollback traces, factory-reset policy/PoP tests, generated Kconfig/range report, hard-invariant verifier output, exact selected sdkconfig/profile hash, parser corpus results, log redaction evidence, TLS/MQTT tests, HIL results and output commit SHA.

### Handoff to the next stage

- Newly available inputs: authenticated v1 control plane, canonical GatewayId/TLS identity lifecycle, secure MQTT transport and authorized reset-intent policy.
- Next unblocked stage: `S7`.
- Frozen contracts: actor/session/capability model, physical-presence/PoP semantics, GatewayId/certificate trust/rotation, strict parser and transport trust policy.

---
## Stage S7 — Add idempotency, backpressure and convergence guarantees

### Objective

Guarantee retry-safe commands, truthful observed state and eventual convergence after queue or bridge pressure.

### Why this stage exists

Current MQTT QoS 1 messages receive new local correlation IDs, optimistic power override can misreport desired state, and Matter/MQTT overflow may leave consumers stale.

### Preconditions

- S6 complete.
- DeviceId, v1 contracts and security actor context frozen.
- All command ingress routes/topics use canonical request DTOs.

### Scope

- In scope: operation lifecycle, idempotency, dedup, observed/desired separation, queue policy, resync, metrics.
- Out of scope: distributed cloud delivery or unbounded durable message broker.

### Repository mapping and freshness

Existing areas:

- `CoreCommandDispatcher`, `CommandManager`, operation result store;
- Web operation request/result handlers;
- MQTT command receive/bridge sync/publication queue;
- Matter pending update queue;
- Service event and result queues;
- runtime metrics/read models.

Recompute:

- all bounded queue capacities and overflow branches;
- all places incrementing aggregate `dropped_events`;
- all command correlation/operation ID generators;
- all optimistic override/cache mutations;
- all result stores and polling endpoints;
- every record-ID/completion-sequence allocator and boot-ID source used by operation persistence;
- existing NVS budget for the default 64-record journal and validated minimum 32-record capacity.

Produce a queue policy matrix with every queue classified as command, result, telemetry, publication, scan/network/OTA/RCP or cache.

### Required changes

#### Operation and idempotency model

1. Define a fixed-size `OperationId` and canonical operation record:
   - operation ID;
   - actor/source;
   - DeviceId/action;
   - idempotency key/fingerprint;
   - created sequence and optional diagnostic timestamps;
   - locator revision at dispatch;
   - lifecycle status;
   - stable result/error.
2. HTTP destructive/non-idempotent mutations require `Idempotency-Key` containing 16-64 URL-safe ASCII characters (`A-Z`, `a-z`, `0-9`, `-`, `_`). Reject shorter, longer, whitespace, Unicode and other punctuation as HTTP 400.
3. Scope the key by authenticated actor identity plus canonical route/action. Persist a canonical request fingerprint and stable result/error; never persist the raw payload, password, token, PoP, certificate private material or other secrets.
4. Repeating the same scoped key with identical fingerprint returns the original operation/result. Reusing it with a different fingerprint returns `idempotency_conflict` with frozen HTTP 409.
5. Implement a durable fixed-capacity journal with approved default 64 and validated minimum 32. Active statuses are `accepted|dispatched` and are never evicted; terminal statuses are `confirmed|failed|timed_out|expired`.
6. Persist a monotonic record ID and `completion_sequence`. Allocate the next sequence transactionally; reboot preserves ordering. Duplicate/corrupt sequence ownership or wrap without a safely available value returns `503/no_capacity` and requires recovery rather than guessed ordering.
7. When capacity is needed, evict only terminal records, selecting the lowest `completion_sequence` first and stable record ID as tie break. Do not use wall time, NTP age or clock-jump-sensitive TTL as an eligibility input. OTA, factory-reset and certificate-rotation entries remain non-evictable until their operation-specific recovery contract is confirmed.
8. When all configured records are active or non-evictable, reject new work with HTTP 503 and `no_capacity`; never overwrite an existing record. Capacity and NVS-budget evidence are recorded in S7/S9.
9. For MQTT power set from HA, use target-state dedup by `DeviceId + desired state` within a bounded window and avoid repeated HAL dispatch while one equivalent operation is pending.
10. Reporting config remains idempotent by full canonical profile equality.
11. Extend the MQTT HAL message callback with `qos`, broker `message_id` and `retain` metadata. Record these for diagnostics/dedup hints, but application correctness must not depend solely on broker message ID.

#### Truthful state

12. Remove `MqttBridge` power override from observed state serialization.
13. Publish command acceptance/pending/result separately.
14. After Zigbee command success, wait for a confirming report/read or explicitly classify `dispatched_unconfirmed`; do not retain desired state as observed.
15. On timeout/failure, preserve last observed state and publish failure result.
16. Include state revision and observation timestamp in retained state.

#### Queue policies

17. For every bounded queue define:
   - owner and writers/readers;
   - capacity;
   - overflow behavior;
   - caller response;
   - metric;
   - retry/coalesce/resync policy.
18. Critical command/request/result queues:
   - reject at ingress with `no_capacity`;
   - never silently drop;
   - do not claim accepted when enqueue fails.
19. Telemetry queues:
   - coalesce latest value by DeviceId + attribute where safe;
   - preserve device join/leave/remap ordering barriers;
   - count coalesced and expired events separately.
20. MQTT publication queue:
   - coalesce retained publications by topic;
   - if capacity is exceeded, invalidate delta cache and set `resync_required`;
   - after reconnect/pressure relief, publish full snapshot and discovery before deltas.
21. Matter update queue:
   - mark `resync_required` on truncation;
   - discard stale partial delta batch;
   - emit a full snapshot update set when drain capacity returns;
   - retain deterministic bounded behavior.
22. Keep `dropped_events` only as a backward-compatible aggregate while adding authoritative classified counters for command rejects, result rejects, telemetry coalesces, telemetry expiries, MQTT resyncs and Matter resyncs.

#### Concurrency and IDs

23. Codify the single-writer rule in runtime assertions/tests.
24. Protect all multi-producer queues with the existing lock abstraction.
25. Prevent operation/correlation ID collision on wrap by checking active journal entries and returning `no_capacity` if no unique ID is available.
26. Apply result only when DeviceId, operation ID and locator revision match the dispatched command.
27. Define global lock order and document it; no callback may invoke external transport while holding a lock that transport callbacks also acquire.

### Contracts and invariants

- `INV-MQTT-01`, `INV-CMD-01`, `INV-CMD-02`, `INV-QUEUE-01`, `INV-RESYNC-01` hold.
- Accepted means stored/enqueued; it never means “request parsed only”.
- A lost HTTP response followed by retry does not duplicate destructive work.
- A bridge overflow eventually converges to the latest Service snapshot.
- Observed state changes only from authoritative device report/read/domain transition.

### Error, authorization, transaction and concurrency behavior

- Auth/capability/physical presence are revalidated before first execution, not on idempotent result replay.
- An idempotent retry returns the original authorization-time result without reopening a consumed physical grant.
- Conflicting payload under the same key is rejected.
- Duplicate workers cannot complete one operation twice.
- Expired operation is not dispatched.
- Remap after enqueue but before dispatch resolves current locator; stale result after remap is ignored.

### Persistence and rollout behavior

- Persist the FD-18 journal across reboot, including configured capacity, actor/action scope, canonical fingerprint, lifecycle, stable result, boot ID, record ID and completion sequence.
- Power target-state dedup is an in-memory bounded journal with a 30-second window. Reboot clears it; re-dispatch after reboot remains safe because power is a target-state command and is reported as a new operation.
- Journal schema uses the S3 versioned persistence mechanisms and is not part of the FD-21 protected reset journal; factory reset erases it.
- No raw transport payload or secret is persisted.
- Wall-clock and NTP state are diagnostic only and never control destructive-operation eviction. Reboot/corruption tests prove deterministic sequence ordering or fail-closed recovery.

### Tests and verification

Idempotency:

- key length 15/16/64/65 and allowed/disallowed character fixtures;
- same actor/route/key/same canonical payload before and after completion;
- same key under a different actor or route is a distinct scope;
- same scoped key/different payload fingerprint;
- retry after commit before response;
- duplicate MQTT QoS 1 delivery;
- duplicate worker/result;
- operation ID wrap/collision;
- expired command;
- newly accepted asynchronous command returns 202; completed idempotent replay returns 200; conflicting replay returns 409; full queue returns 503;
- reboot with active and terminal records preserves record/completion ordering;
- default capacity 64 and minimum supported capacity 32 pass NVS-budget and deterministic-eviction tests;
- lowest-completion-sequence-first eviction with stable record-ID tie break;
- all configured records active/non-evictable return `503/no_capacity` without overwrite;
- backward/forward clock jumps and NTP synchronization do not change eviction order;
- sequence duplicate, corruption and wrap-without-free-value fail closed;
- OTA/factory-reset/certificate-rotation records remain non-evictable until their recovery contract is confirmed;
- persisted record contains no raw payload or secret fixture.

State semantics:

- accepted power command does not change retained observed state;
- report confirms state;
- success without report becomes unconfirmed, not observed;
- timeout/failure preserves previous observed state;
- remap during command cannot affect another DeviceId.

Pressure/convergence:

- fill every critical queue and verify explicit rejection;
- telemetry coalescing preserves latest value and join/remap barriers;
- MQTT publication overflow triggers one full resync;
- disconnect/reconnect full snapshot convergence;
- Matter overflow followed by drain converges exactly to service snapshot;
- repeated overflow does not loop or leak capacity.

Concurrency:

- command || remove;
- command || remap;
- two identical commands;
- two conflicting target-state commands;
- result || timeout;
- two simulated concurrent bridge drain callers under the host concurrency harness;
- lock-order/static analysis checks.

### Deliverables

- durable fixed-capacity operation/idempotency journal, default/minimum capacity policy, persisted sequence eviction and result contracts.
- MQTT command-result topics and serializers.
- Removal of optimistic observed-state override.
- Queue policy matrix and classified metrics.
- MQTT/Matter full-resync logic.
- concurrency/retry tests.
- `implementation-evidence/S7-completion.json`.

### Exit criteria

- [ ] Duplicate/retry tests produce at most one side effect.
- [ ] Journal capacity, key format/scope, persisted sequence ordering, deterministic terminal eviction and `503/no_capacity` behavior match FD-18 exactly across reboot, corruption and clock changes.
- [ ] Retained state is observed-only.
- [ ] Every queue has explicit overflow semantics and metrics.
- [ ] MQTT/Matter overflow tests prove eventual full convergence.
- [ ] No accepted request is silently dropped.
- [ ] Normal, sanitizer, integration and target-build tests pass.

### Completion evidence

Return operation lifecycle table, journal schema/default-minimum capacity and sequence/eviction traces, NVS-budget evidence, persisted-data redaction evidence, queue policy matrix, resync traces, test outputs, metrics list and output commit SHA.

### Handoff to the next stage

- Newly available inputs: retry-safe operations and pressure-safe integrations.
- Next unblocked stage: `S8`.
- Frozen contracts: durable idempotency key/scope/capacity/sequence-eviction semantics, observed-state semantics, queue classification, resync behavior and lock order.
## Stage S8 — Complete signed OTA, anti-rollback and recovery lifecycle

### Objective

Bind the existing signed-manifest OTA flow to the S5 hardware root of trust, explicit anti-rollback policy, health-confirmed boot lifecycle and recoverable failure handling.

### Why this stage exists

Secure Boot and encrypted storage alone do not prove that production OTA rejects wrong targets, test keys, downgraded security versions or unhealthy images, nor that the exact verified binary is the one signed and promoted.

### Preconditions

- `S7` complete.
- S5 production sdkconfig/eFuse/secure-storage contracts are frozen and verified.
- S6 firmware-admin authorization and physical-presence controls are available.
- Exact ESP-IDF 5.5.2 toolchain/container digest is available.
- Production signing interface is identified; unavailable HSM/KMS signing returns `BLOCKED_SECURITY_PROVISIONING`.

### Scope

- In scope: signing-key separation, signed manifest binding, OTA authorization, anti-rollback/security version, pending-verify health confirmation, rollback, recovery and OTA negative HIL.
- Out of scope: RCP transport implementation and target-side Matter stack implementation.

### Repository mapping and freshness

Existing areas:

- OTA bootstrap/manager/trust store/transport policy;
- OTA public keys and CA material;
- manifest/package/promotion scripts;
- partitions and slot-size checks;
- OTA host/target/HIL tests and release documentation;
- S5 production security/eFuse evidence and S7 operation journal.

Target artifacts expected absent:

- production signing attestation interface;
- explicit current/next key activation and revocation registry;
- OTA health-confirmation contract covering service/persistence/security/watchdog readiness;
- complete anti-rollback/downgrade/power-loss HIL evidence;
- thin `OtaPlatformAdapter`, product health checklist and ESP-IDF rollback/anti-rollback evidence;
- restart-safe factory-reset state machine using the S5 protected reset journal.

Snapshot/global-order checks:

- Recompute OTA public-key slots, key IDs and fixture fingerprints.
- Determine the global current firmware version, OTA schema version and `secure_version` from all applicable artifacts; never use a local example as predecessor.
- Verify partition layout and rollback slot state against the S5 generated production profile.
- Exact-diff every OTA/RCP route, operation and release script consumed by this stage.

### Required changes

#### Signing and trust separation

1. Separate secure-boot signing, OTA manifest current key, OTA next/rotation key, TLS device/server keys and test fixture keys.
2. Release tooling rejects known test fixture fingerprints and any signing key not authorized for the requested release channel/product.
3. Production private signing keys are non-exportable and accessed only through the approved HSM/KMS signing interface; otherwise production signing is blocked.
4. CI/build jobs produce reproducible unsigned artifacts. Signing consumes only the exact verified binary hash and emits an attestation containing key ID, input hash and output signature/hash.
5. Preserve current/next OTA public-key rotation but add explicit activation, overlap, revocation and rollback states; no implicit key replacement is allowed.

#### Manifest and authorization contract

6. Bind manifest validation to hardware/product ID, firmware version, image SHA-256/size, minimum/current security version, signing key ID, release channel and declared validity window when used.
7. Direct URL OTA remains development/HIL-only unless it passes the same signed-manifest, S6 authorization, physical-presence and operation-journal policy as production.
8. OTA request/result uses the S7 operation lifecycle and frozen HTTP mapping: 202 for newly accepted operation, 200 for completed replay, 403 for missing firmware-admin/physical presence, 409 for lifecycle/idempotency conflict and 503 for capacity/security subsystem unavailability.
9. Recheck authenticated firmware-admin capability and one-time physical-presence grant immediately before OTA download/install start.

#### Anti-rollback and boot health

10. Reject an image below the global/eFuse application security version before download/install where possible and again through bootloader anti-rollback enforcement.
11. Increment `secure_version` only through an explicit reviewed release decision. Derive the global predecessor across all release artifacts and return `BLOCKED_DELTA_REVIEW` on mismatch.
12. Keep the ESP-IDF running image in `ESP_OTA_IMG_PENDING_VERIFY` until the product health checklist succeeds. Before confirmation, the previous valid slot remains the rollback target and application secure version remains unchanged.
13. Health confirmation requires ServiceRuntime startup, versioned persistence restore/migration, security subsystem availability, critical queue initialization and watchdog stability; network or cloud reachability alone is not required.
14. Implement one thin `OtaPlatformAdapter` over ESP-IDF 5.5.2 APIs for running-image-state query, `esp_ota_mark_app_valid_cancel_rollback()`, `esp_ota_mark_app_invalid_rollback_and_reboot()` and secure-version readback. No other code may call those production transitions or write application secure-version eFuse directly.
15. After health passes, call `confirm_running_image_valid()`. Success requires `ESP_OK`, non-pending valid image state and eFuse readback equal to the new image security version. Do not create a second application-owned OTA image-state transaction.
16. If health fails or times out, call `rollback_invalid_image_and_reboot()`. If reset/power loss occurs before confirmation, rely on ESP-IDF pending-verify rollback behavior. On every boot, keep the production mutation surface disabled until platform image state and eFuse readback match one of the FD-20 accepted states; otherwise quarantine.
17. The S7 operation journal and release evidence record request/result/health facts, not a duplicate boot-validity state machine. After successful platform secure-version advancement, lower-security-version rollback is intentionally unsupported.
18. Concurrent OTA, RCP, Wi-Fi credential migration, certificate rotation, factory reset or other security-administration mutations are rejected by the shared operation lock.

#### Recovery and factory reset

19. Define recovery for corrupt credentials/NVS that preserves Secure Boot and does not expose a shared default secret.
20. Implement factory reset only through the S5 protected journal with states `requested -> erasing -> reinitialized -> commissioning_ready`.
21. Preserve exactly: eFuse/security state, Secure Boot/Flash/NVS encryption key material, factory GatewayId source, manufacturing PoP, product CA/trust anchors and encrypted management TLS current/next slots.
22. Erase exactly: admin verifier/sessions, Wi-Fi and MQTT credentials/config, Zigbee network keys/pairings, device/descriptor/reporting state, Matter endpoint map, S7 operation/idempotency journal and legacy migration/quarantine/tombstone state.
23. Erase through typed namespace owners. After erase, reinitialize a fresh audit ring with one redacted factory-reset record. The protected reset journal remains until final transition.
24. Power loss at every state resumes idempotently from the persisted journal. Commissioning does not open automatically; `commissioning_ready` requires a new trusted physical-presence event and manufacturing PoP validation.
25. Certificate compromise uses FD-17 rotation/revocation. Ordinary factory reset preserves certificate identity and trust roots.
26. Interrupted download/write preserves the active slot; interrupted pending verification follows ESP-IDF rollback behavior and FD-20 boot-time platform-state/eFuse validation.
27. Classify invalid/tampered/expired/wrong-target/revoked-key/downgrade errors as non-retryable; bounded network/transport errors remain retryable.

### Contracts and invariants

- `INV-OTA-01` holds at manifest, application, bootloader and eFuse layers.
- Test keys cannot sign production artifacts.
- Signing input hash equals the S9 release-evidence binary hash.
- A downgraded or wrong-target image cannot be installed or booted.
- At least one rollback-capable prior image remains until health confirmation and successful ESP-IDF mark-valid processing.
- Recovery never disables the S5 hardware-security baseline or uses a shared secret.
- Factory reset follows FD-21 exactly and does not erase GatewayId, manufacturing trust, product CA or device TLS identity.

### Error, transaction and concurrency behavior

- OTA acceptance is atomic with creation of its S7 operation record.
- Duplicate/retried OTA requests do not start a second write.
- A stale manifest, key registry or security-version inventory blocks before mutation.
- Power loss at every write/boot/health/ESP-IDF-confirmation boundary is resolved by platform image-state/eFuse inspection and blocks listener/network startup while the state is not accepted by FD-20.
- The FD-21 reset journal and S7 OTA operation records are separate and serialized by the shared security-administration lock. ESP-IDF `otadata`/eFuse remains the sole boot-validity authority and cannot be erased by either application journal.
- Audit and operation results never expose private signing or credential material.

### Migration, compatibility and rollout behavior

1. Existing development devices without the S5 hardware profile cannot enter production OTA promotion.
2. Canary hardware proves update, rollback, key selection and recovery before fleet promotion.
3. Security-version increments are exceptional, explicit and supported by eFuse/global-order evidence.
4. Key rotation uses a declared overlap window; old-key revocation occurs only after canary/rollback evidence proves the next key.
5. Existing signed-manifest behavior is preserved where compatible, but insecure direct URL shortcuts remain development-only.

### Tests and verification

Host/tooling:

- wrong product, hardware, key, security version, hash, size and channel;
- expired/not-yet-valid manifest where policy is enabled;
- test-key fingerprint rejection;
- current/next key activation/revocation fixtures;
- signing attestation input/output hash equality;
- global `secure_version` predecessor and duplicate detection;
- release evidence schema compatibility for S9.

Target/HIL:

- valid signed OTA success;
- tampered signature/hash rejection;
- unknown/revoked/test key rejection;
- downgrade/anti-rollback rejection;
- wrong-target firmware rejection;
- wrong CA/hostname rejection;
- power loss during download, write, boot and pending verification;
- failed health confirmation rollback;
- successful health confirmation;
- eFuse value unchanged before health and `confirm_running_image_valid()` invocation;
- HIL fault injection before health, after health, immediately before/after the ESP-IDF mark-valid call, on unexpected reset while pending verify and before post-call readback/confirmation;
- each recovered result is exactly old-slot/old-eFuse or new-valid-slot/new-eFuse; no partial state enables network/control plane;
- release evidence records previous/new slot versions and eFuse readback before/after;
- duplicate OTA request/retry produces one write operation;
- recovery from corrupt credentials/NVS without security bypass;
- factory reset power loss at `requested`, `erasing`, `reinitialized` and commissioning transition;
- exact preserve/erase matrix, fresh reset audit record and no automatic commissioning without physical presence plus PoP;
- certificate slots/GatewayId/trust roots preserved through reset.

### Deliverables

- hardened signed-manifest and key-state contracts.
- production signing attestation adapter/verification.
- anti-rollback/global security-version tooling.
- product health-confirmation policy plus thin ESP-IDF `OtaPlatformAdapter` and deterministic platform-state/eFuse validation.
- restart-safe factory-reset state machine, exact namespace matrix and recovery runbook.
- expanded OTA negative tests and HIL scripts.
- completed OTA/recovery sections in `docs/security/PRODUCTION_HARDENING.md`.
- `implementation-evidence/S8-completion.json`.

### Exit criteria

- [ ] Test keys and unauthorized key states are rejected.
- [ ] Manifest binds product, hash, version, security version, key and channel.
- [ ] Valid OTA, tamper, downgrade, power-loss, health-failure and rollback HIL pass.
- [ ] eFuse remains unchanged before health and ESP-IDF confirmation; pending-verify reset/power-loss and mark-valid boundaries converge to an FD-20 accepted platform state or remain quarantined fail-closed.
- [ ] Duplicate/retried OTA produces at most one write operation.
- [ ] Recovery and factory reset preserve Secure Boot/encryption/GatewayId/trust/TLS identity, erase every FD-21 user/application namespace and require physical presence plus PoP before commissioning.
- [ ] Exact tested binary hash and signing attestation are handed to S9.

### Completion evidence

Return manifest/key inventories, global security-version derivation, ESP-IDF adapter/API evidence, running-image-state and eFuse before/after traces, signing attestations, exact firmware hash, factory-reset preserve/erase and power-loss results, HIL logs, rollback/recovery results and output commit SHA.

### Handoff to the next stage

- Newly available inputs: signed and HIL-validated non-releasable binary, hardware security evidence, signing attestation, exact tested hash, ESP-IDF OTA health/rollback/secure-version evidence and reset/recovery evidence.
- Next unblocked stage: `S9`.
- Frozen contracts: signing/key lifecycle, manifest validation, anti-rollback, product health policy plus ESP-IDF mark-valid/rollback ownership, factory-reset matrix and recovery semantics.

---
## Stage S9 — Enforce CI/HIL/release evidence and complete cutover

### Objective

Turn all architectural, security and runtime guarantees into non-skippable automation and complete the controlled compatibility cutover.

### Why this stage exists

A design is not production-ready when critical integration/HIL/security checks are optional, skipped or run on a different binary than the promoted release.

### Preconditions

- S1-S8 complete.
- Exact output commit and tested production binary from S8 are available.
- Required HIL environment is available; otherwise final status is `BLOCKED_HIL_ENVIRONMENT`, not pass.

### Scope

- In scope: CI topology, immutable dependencies, release workflow, full HIL matrix, artifact evidence, canary/rollback, legacy cleanup gate, final docs.
- Out of scope: optional new product features.

### Repository mapping and freshness

Existing areas:

- `.github/workflows/ci.yml`;
- local validation scripts;
- host/integration/target/HIL tests;
- OTA packaging/promotion scripts;
- coverage report/badge jobs;
- README, process and release docs.

Recompute exact inventories:

- all CI jobs and event conditions;
- required versus optional status checks;
- all validation/HIL scripts and which CI job invokes each;
- all GitHub Actions refs and container image refs;
- all test suites and component coverage filters;
- all legacy routes/topics/keys scheduled for cleanup;
- complete S0-S9 completion-manifest chain and every `releaseability` value;
- GatewayId/certificate SAN/CA and current/next activation/rollback evidence;
- Matter reserved/static endpoints, recomputed capacity/range and endpoint tombstone state;
- journal/reset/ESP-IDF OTA health and platform-state evidence schemas.

An uninvoked critical script or skip-able release gate is a blocking delta.

### Required changes

#### CI topology

1. Split fast PR CI, nightly hardware validation and production release workflow, while keeping one machine-readable gate matrix.
2. Required PR/merge gates:
   - architecture invariants in strict mode;
   - normal host unit tests;
   - host integration tests;
   - ASan + UBSan tests;
   - strict parser fuzz/corpus tests;
   - static analysis for Core, Service, Web, MQTT, Matter and HAL host-compilable code;
   - target ESP32-C6 firmware build;
   - target HAL test build;
   - production security profile verifier that proves every FD-13 hard invariant exactly, rejects out-of-range tunables and undeclared in-range changes, proves fixed settings have no override, and binds the exact selected `sdkconfig` hash to the tested binary;
   - OTA slot-size/partition checks;
   - API/MQTT golden contract tests;
   - migration tests.
3. Run architecture/static/target gates for the final merge commit/main push, not only pull-request head.
4. Required release gates:
   - exact release firmware target build;
   - target HAL HIL;
   - Zigbee identity/rejoin/remove HIL;
   - MQTT TLS/command/dedup/remap HIL;
   - Matter bridge runtime resync/remap HIL;
   - HTTPS/auth/physical-presence HIL;
   - OTA success/tamper/downgrade/power-loss/rollback HIL;
   - production security/eFuse readback verification.
5. A required release HIL job must fail or remain blocked when no runner is available. It must not report success by evaluating to `skipped`.
6. Bind all HIL results to firmware SHA-256, commit SHA, sdkconfig hash and hardware identifier.

#### Supply-chain hardening

7. Pin GitHub Actions to immutable commit SHAs.
8. Pin the ESP-IDF 5.5.2 container/toolchain to an immutable digest, verify it against root `dependencies.lock`, and record both version and digest in release evidence.
9. Set minimum workflow permissions globally and elevate only individual jobs.
10. Disable persisted checkout credentials where not required.
11. Isolate badge/Gist publishing from build/signing jobs and secrets.
12. Add dependency/license/vulnerability and secret scanning appropriate to C/C++/Python/shell dependencies.
13. Generate SBOM and provenance/attestation for the release artifact.
14. Protect signing secrets so untrusted pull requests and general build jobs cannot access them.

#### Coverage and quality gates

15. Keep Core/Service line coverage gate, but add component-level visibility for Web, MQTT, Matter and host HAL adapters.
16. Add branch coverage targets for reducers, migration state machines, auth/idempotency and parser code.
17. Do not use one global percentage to hide an uncovered security-critical component; set minimum critical-file/component thresholds.
18. Store test, sanitizer, fuzz, static-analysis and coverage artifacts for release audit.

#### Release evidence and promotion

19. Before generating release evidence, verify the exact uninterrupted S0-S9 manifest chain, commit lineage and artifact hashes. Every S0-S8 manifest must say `NON_RELEASABLE`; S9 alone may emit `PRODUCTION_CANDIDATE`. Missing/earlier-stage/mismatched input returns `BLOCKED_RELEASE_INCOMPLETE`.
20. Generate `release-evidence.json` containing:
   - source commit;
   - source inventory hash;
   - firmware binary SHA-256/size/version/security version;
   - sdkconfig and partition hashes;
   - ESP-IDF/toolchain digest;
   - SBOM/provenance IDs;
   - signing key ID and signature/attestation references;
   - eFuse policy/readback summary including previous/new slot versions, ESP-IDF running-image state and application security version before/after FD-20 confirmation;
   - GatewayId, exact mDNS DNS name, certificate SAN/issuer/expiry/current-slot fingerprint and current/next activation/rollback evidence;
   - legacy identity evidence/quarantine totals and decision reasons;
   - recomputed Matter capacity/range/reservation proof and endpoint allocation/removal/tombstone results;
   - idempotency journal policy/version, configured/default/minimum capacity, persisted-sequence eviction and NVS-budget tests;
   - factory-reset preserve/erase matrix, journal power-loss results and fresh audit record evidence;
   - every required test/HIL job and result artifact;
   - legacy compatibility state;
   - canary rollout decision.
21. Packaging/signing/promotion scripts consume only the exact S9-verified binary by hash, require `releaseability: PRODUCTION_CANDIDATE` and refuse substitution or any S0-S8 artifact.
22. Release promotion is two-step: staging/canary, then production approval based on evidence.
23. Define rollback triggers: boot failure, auth unavailable, migration error, identity mismatch, certificate/trust failure, elevated command failures, queue/resync storms, reset inconsistency or OTA health/platform-state failure.

#### Compatibility completion

24. During canary verify:
   - legacy data migrated/quarantined;
   - old MQTT/HA retained topics tombstoned;
   - no new short-address identity artifacts appear;
   - v1 UI/API/MQTT clients operate;
   - rollback retains recoverable old generation.
25. Remove old NVS keys only after the declared successful canary window and one verified reboot/update cycle.
26. Keep legacy HTTP read aliases enabled for exactly the first production DeviceId migration release. Record `legacy_read_aliases=true`, deprecation metadata and a non-binding sunset intent in release evidence. Do not select or execute a removal release; removal requires a separate reviewed cleanup plan.
27. Update README so every claimed test/HIL path maps to an enforced job or explicitly manual non-gating procedure.
28. Publish operator runbooks for enrollment, certificate/CA rotation, MQTT trust update, identity quarantine, Matter endpoint recovery, OTA rollback/security-version recovery and factory reset/recovery.

### Contracts and invariants

- A production release cannot be promoted without all required evidence.
- Required gate absence is failure/block, not skip/pass.
- The promoted binary hash equals the HIL-tested and signed binary hash.
- CI dependencies are immutable.
- Legacy cleanup occurs only after evidence-based cutover.
- Release documentation matches executable automation.
- No S0-S8 artifact can satisfy the production verifier.
- Gateway/TLS, Matter endpoint, idempotency, OTA platform confirmation and factory-reset evidence are mandatory release fields rather than optional attachments.

### Error, transaction and concurrency behavior

- Two release workflows cannot promote different binaries under the same version.
- Promotion uses a unique release ID and atomic channel pointer/update.
- A failed canary leaves production channel unchanged.
- Signing/promotion retries are idempotent by artifact hash and release ID.
- Stale release evidence or mismatched commit/hash is rejected.

### Tests and verification

Automation tests:

- simulate no HIL runner and verify release is blocked;
- artifact substitution/hash mismatch rejected;
- test action/image pin verifier, including ESP-IDF 5.5.2 lockfile-to-container-digest consistency;
- release evidence schema and completeness;
- rejection of every S0-S8 artifact, missing manifest, broken commit lineage and non-S9 releaseability value;
- GatewayId/mDNS/SAN/issuer/expiry/current-next activation/rollback evidence validation;
- Matter dynamic range/reserved-endpoint conflict and delayed-reuse evidence validation;
- legacy identity evidence/quarantine decision inventory validation;
- idempotency journal configured/default/minimum capacity, sequence-order/full-state and NVS-budget evidence validation;
- FD-20 ESP-IDF running-image-state, old/new slot, eFuse before/after and fail-closed evidence validation;
- FD-21 reset matrix/journal/fresh-audit evidence validation;
- generated sdkconfig verifier enforces hard invariants exactly, rejects out-of-range values and rejects undeclared in-range changes while accepting an explicitly declared/tested in-range profile;
- duplicate version/promotion rejected;
- untrusted PR cannot access signing secrets.

Full release matrix:

- all S1-S8 tests;
- target build/size;
- identity HIL including address reuse before capture, capture-to-commit mutation and reboot quarantine behavior;
- HTTPS/auth HIL including GatewayId/SAN/issuer/expiry, CA trust and current/next atomic activation/rollback power-loss;
- MQTT TLS/dedup HIL including default/minimum journal capacity, deterministic sequence eviction, full-state rejection and clock-jump independence;
- Matter resync HIL including 10-73 reviewed range proof, deterministic allocation and no reuse before tombstone;
- OTA/security HIL including pending-verify health, ESP-IDF mark-valid/rollback boundaries, eFuse readback and FD-20 accepted/quarantine states;
- factory-reset HIL at every journal state with exact preserve/erase evidence and physical-presence/PoP commissioning gate;
- canary upgrade and rollback;
- minimum 24-hour canary soak with queue/reboot/error/security metrics.

### Deliverables

- Hardened CI and separate release workflow.
- Immutable action/container pins.
- Release gate matrix.
- `release-evidence.json` schema/generator/verifier including complete S0-S9 releaseability/lineage validation and all FD-15-FD-22 evidence.
- SBOM/provenance outputs.
- canary/rollback and operator runbooks.
- compatibility state/deprecation report.
- `implementation-evidence/S9-completion.json`.

### Exit criteria

- [ ] Every mandatory suite/script is invoked by an enforced job.
- [ ] Required HIL cannot be skipped to green.
- [ ] All action/toolchain inputs are immutable or have a documented verified exception.
- [ ] Release evidence binds source, config, binary, signing, hardware and tests.
- [ ] Every S0-S8 artifact is rejected; only the exact complete S9 manifest chain can become a production candidate.
- [ ] Gateway/TLS, identity migration, Matter endpoint, idempotency, OTA eFuse ordering and factory-reset evidence are complete and schema-valid.
- [ ] Canary and rollback pass with no identity/config crossover.
- [ ] Legacy tombstone cleanup is complete and read aliases are truthfully recorded as enabled for the first migration release; alias removal is deferred to a separate reviewed plan.
- [ ] Documentation and automation are consistent.

### Completion evidence

Return workflow/job inventory diff, required status configuration, S0-S9 manifest/lineage verification, complete release evidence, Gateway/TLS/Matter/journal/OTA/reset HIL artifacts, canary metrics, legacy compatibility/deprecation report, final release binary hash and output commit SHA.

### Handoff after S9

- Newly available input: the only eligible production release candidate and complete S0-S9 evidence bundle.
- Next stage: none; proceed to Global Done Criteria and independent final audit.
- Frozen contracts: complete system behavior and release evidence.

## 10. Requirements and findings traceability

| Requirement/finding | Owning stage(s) | Verification |
|---|---|---|
| ID-01 durable identity | S2, S3, S4 | rejoin/address-reuse unit, integration and HIL; persistence migration |
| ID-02 proof-safe legacy migration | S2, S3, S9 | historical EUI/record fingerprint/network-generation evidence; pre-capture/capture-to-commit/reboot address-reuse tests; quarantine evidence |
| MATTER-01 deterministic persisted endpoints | S3, S4, S7, S9 | recomputed range/reservation proof; lowest-free allocation; reboot/remap stability; delayed reuse and resync HIL |
| GW-01 stable gateway/TLS identity | S4, S5, S6, S9 | base-MAC GatewayId, mDNS/SAN/CA fixtures, duplicate identity rejection, rotation/reset HIL |
| SEC-01 hardware/encrypted-storage prerequisite | S5, S9 | ESP-IDF 5.5.2 profile verifier, eFuse readback, encrypted NVS HIL and release evidence |
| SEC-02 management auth/HTTPS | S6, S9 | auth/CSRF/physical-presence tests; HTTPS HIL; secure-storage failure tests |
| SEC-03 signed OTA/anti-rollback/recovery | S8, S9 | manifest/key tests, downgrade/tamper plus pending-verify health, ESP-IDF mark-valid/rollback and eFuse boundary HIL |
| SEC-05 restart-safe factory reset | S5, S6, S8, S9 | exact preserve/erase matrix, journal-state power-loss, fresh audit and physical-presence/PoP commissioning tests |
| SEC-04 security invariants and bounded tunables | S6, S9 | exact hard-invariant checks; minimum/default/maximum and below/above fixtures; declared in-range profile plus exact sdkconfig/HIL binding |
| PERS-01 raw persistence | S3 | serializer, corruption, power-loss and migration tests |
| PARSE-01 substring JSON parsing | S6 | strict schema limits, duplicate-key tests and fuzz under sanitizers |
| MQTT-01 plaintext MQTT | S6, S9 | production config negative tests and TLS HIL |
| REL-01 QoS/idempotency/optimistic state | S7, S9 | key/scope, default/minimum capacity, persisted sequence eviction, full-capacity/reboot/corruption/clock-independence tests; duplicate/retry/result and observed-state tests |
| REL-03 route ownership and intermediate release blocking | S4, S6, S9 | S4 unregistered contracts, S6-only guarded registration/build invariant and manifest-chain rejection of all S0-S8 artifacts |
| REL-02 overflow convergence | S7, S9 | MQTT/Matter pressure + full-resync tests/HIL |
| READY-01 temporary capability booleans | S1 | zero-inventory check and capability tests |
| API-01 contract versioning/status mapping | S4, S6, S7 | golden HTTP/MQTT fixtures, UI consumption and legacy cutover tests |
| COMPAT-01 read-alias lifecycle | S4, S9 | first-migration-release evidence with `legacy_read_aliases=true`; no removal action |
| VCS-01 deterministic baseline | S0 | clean upstream or local `reviewed-baseline` commit and tracked-set diff |
| TOOL-01 exact ESP-IDF version | S0, S5, S9 | `dependencies.lock` 5.5.2 verification and immutable container digest |
| TEST-01 sanitizer failures | S1, S9 | full ASan/UBSan green and required CI status |
| CI-01 missing/skippable gates | S9 | workflow inventory and no-runner block test |
| CI-02 mutable supply chain | S9 | immutable pin verifier and permissions audit |
| CONC-01 implicit single writer | S1, S2, S7 | architecture rule, runtime assertions and concurrency tests |
| PLATFORM-01 platform-first ownership | S6, S8, S9 | no duplicate OTA platform state machine; thin-adapter tests; security-equivalence regression matrix |

## 11. Cross-stage contract freeze matrix

| Contract | Defined/frozen in | Consumers |
|---|---|---|
| Git/repository baseline and ESP-IDF 5.5.2 evidence | S0 | S1-S9 |
| Capability availability and unsupported error | S1 | S4, S6, S9 |
| `DeviceId` representation | S2 | S3-S9 |
| Locator revision/remap ordering | S2 | S3, S4, S7 |
| NetworkGenerationId and legacy evidence provenance | S2 | S3, S9 |
| Persisted identity/reporting schema | S3 | S4-S9 |
| Persisted Matter endpoint/removal state | S3 | S4, S7, S9 |
| Quarantine and cleanup rules | S3 | S4, S6, S9 |
| HTTP API v1 DTOs and golden status/error matrix | S4 | S6, S7, S9 |
| MQTT v1 topic/payload schema | S4 | S6, S7, S9 |
| GatewayId/HA/MQTT identity | S4 | S5, S6, S9 |
| Matter endpoint range/allocation/reuse | S4 | S7, S9 |
| S4 unregistered route contracts and S6 guarded production registration | S4 | S5, S6, S9 |
| Production sdkconfig/eFuse/encrypted-storage foundation | S5 | S6, S8, S9 |
| Secure-storage result and migration contract | S5 | S6, S8 |
| Reset preserve/erase ownership and protected reset journal | S5 | S6, S8, S9 |
| Auth/capability/physical-presence policy | S6 | S7-S9 |
| Strict JSON, hard security invariants and bounded operational tunables | S6 | S7, S9 |
| Gateway certificate SAN/CA plus current-next atomic activation/rollback | S6 | S8, S9 |
| MQTT TLS/trust policy | S6 | S9 |
| Idempotency key/scope/default-minimum capacity/persisted-sequence eviction | S7 | S8, S9 |
| Queue/resync and lock order | S7 | S8, S9 |
| OTA signing/key/product-health plus ESP-IDF mark-valid/rollback/eFuse/recovery policy | S8 | S9 |
| Factory-reset execution/commissioning policy | S8 | S9 |
| Release evidence schema | S9 | release operators/auditors |

## 12. Verification matrix by invariant

| Invariant | Primary tests | Required completion evidence |
|---|---|---|
| INV-ID-01 | same EUI/new short address; reboot restore | identity HIL log + state snapshot |
| INV-ID-02 | locator conflict/remap serialization | unit/concurrency output |
| INV-ID-03 | address reuse with old config/topic/endpoint | migration + integration/HIL output |
| INV-ID-04 | current lookup only; pre-capture, capture-to-commit and reboot address reuse | evidence decision matrix + quarantine HIL |
| INV-GW-01 | reboot/reset/two-device GatewayId and SAN/CA/rotation | identity/certificate HIL bundle |
| INV-MATTER-01 | range conflict, lowest-free, reboot/remap and delayed reuse | endpoint inventory/allocation/tombstone trace |
| INV-PERS-01 | serializer fixture/layout-independence | persisted schema fixture/hash |
| INV-PERS-02 | power loss at every migration step | fault-injection matrix |
| INV-API-01 | route/topic contract inventory | golden fixture and inventory diff |
| INV-AUTH-01 | unauthenticated/unauthorized route matrix | route-policy test report |
| INV-AUTH-02 | missing/expired/replayed physical grant | negative security HIL |
| INV-MQTT-01 | accepted/failed command state behavior | retained-state trace |
| INV-CMD-01 | retry after lost response/duplicate QoS | operation journal trace |
| INV-CMD-02 | default/minimum capacity, sequence ordering/corruption/wrap, full state and clock jumps | deterministic eviction/no-capacity trace |
| INV-QUEUE-01 | fill every critical queue | queue policy test matrix |
| INV-RESYNC-01 | overflow then drain/reconnect | final snapshot equality trace |
| INV-OTA-01 | tamper/downgrade/wrong key/rollback | eFuse + OTA HIL bundle |
| INV-OTA-02 | pending-verify health, ESP-IDF mark-valid/rollback and eFuse readback boundaries | accepted platform-state or quarantine trace |
| INV-RESET-01 | reset power loss at every journal state and exact namespace matrix | reset HIL + post-reset storage/audit evidence |
| INV-REL-01 | S4 unregistered contracts, S6 guarded registration failures and S0-S8 packaging attempts | route-ownership/build-invariant and release-verifier report |
| INV-ARCH-01 | architecture gate | strict gate output |
| INV-CONC-01 | writer ownership/assertion/races | concurrency test output |

## 13. Global Done Criteria

The plan is complete only when all conditions are true:

### Architecture and identity

- [ ] Core remains independent of ESP-IDF/transports/persistence implementations.
- [ ] DeviceId is canonical in Core, persistence and all production external contracts.
- [ ] Every remaining short-address use is explicitly locator-only or compatibility-only.
- [ ] Rejoin, reboot and address reuse cannot move configuration/state between physical devices.
- [ ] No legacy short-only record migrates from a current lookup alone; evidence provenance/fingerprint/network generation is complete or the record is quarantined.
- [ ] GatewayId is factory-derived, stable across reset and distinct across provisioned hardware.
- [ ] Matter uses one persisted endpoint per DeviceId in the validated recomputed range and never reuses it before deletion/tombstone completion.

### Persistence and migration

- [ ] No new durable schema writes raw C++ aggregate memory.
- [ ] Legacy state is migrated or quarantined without guessing.
- [ ] Migration is power-loss safe, idempotent and rollback-capable.
- [ ] Old keys are removed only after the verified cutover gate.

### Security

- [ ] Production Web/API uses HTTPS, authentication, authorization, CSRF/origin defense and rate limits.
- [ ] Destructive/trust-changing operations require physical presence.
- [ ] Production provisioning has no shared default secret.
- [ ] MQTT production transport uses TLS and validated broker identity.
- [ ] Credentials and private keys reside only in encrypted storage.
- [ ] Secure Boot, Flash Encryption, NVS Encryption and anti-rollback are verified on hardware.
- [ ] The production target is built with ESP-IDF 5.5.2 from the lockfile and immutable container digest.
- [ ] FD-13 hard security invariants are exact; tunables pass minimum/default/maximum/out-of-range tests; any in-range production deviation is explicitly declared and the exact sdkconfig/profile hash is bound to affected HIL evidence.
- [ ] HTTP handlers and bundled UI conform to the single FD-14 golden status/error matrix.
- [ ] Production management certificate, mDNS, SAN, issuer/CA and current/next atomic activation/rollback conform to FD-17 and survive reset without trust loss.
- [ ] Factory reset follows the exact FD-21 preserve/erase matrix, resumes after power loss and requires physical presence plus PoP before commissioning.

### Reliability

- [ ] Duplicate/retried commands produce at most one destructive side effect.
- [ ] The durable journal uses the approved default/minimum capacity policy and FD-18 key/scope/persisted-sequence eviction/no-capacity behavior across reboot, corruption and clock changes.
- [ ] Retained state is observed-only.
- [ ] Every bounded queue has explicit overflow semantics and metrics.
- [ ] MQTT/Matter converge through full resync after overflow/reconnect.
- [ ] Single-writer and lock-order contracts are tested.

### Automation and release

- [ ] Normal, integration, sanitizer, fuzz, static, target and migration gates are required.
- [ ] Required HIL absence blocks release.
- [ ] Every S0-S8 artifact is non-releasable; only the complete exact S9 manifest chain may identify a production candidate.
- [ ] OTA health precedes the sole mark-valid/eFuse commit, and every fault boundary converges to one FD-20 safe state before control-plane enablement.
- [ ] The promoted binary is byte-identical to the HIL-tested/signed binary.
- [ ] CI actions/toolchains are immutable and least-privileged.
- [ ] Complete release evidence, SBOM and provenance exist.
- [ ] Canary/rollback and operator recovery runbooks are validated.
- [ ] Legacy read aliases are truthfully reported as enabled for the first production DeviceId migration release; this plan does not remove them.

### Evidence

- [ ] Every stage has a valid completion manifest and exact output commit.
- [ ] No mandatory test is reported as passed when it was not run.
- [ ] No P0/P1 finding remains unresolved.
- [ ] The Architecture Plan Review Auto Cycle Iteration 4 reports `APPROVED`, followed by an independent final code audit after implementation.

## 14. Preserved behavior

Unless explicitly changed by a frozen contract, preserve:

- supported Zigbee device/reporting/Tuya behavior;
- fixed service capacities unless a stage documents a measured change;
- reducer/effect separation;
- narrow Matter service contract;
- OTA signed manifest and current/next public-key rotation concept;
- local Web UI functionality after authentication;
- existing successful host behavior;
- separate gateway OTA and RCP tracks.

## 15. P2 backlog — non-blocking after production baseline

- Replace ad hoc string formatting in selected diagnostics with a shared bounded formatter where it improves consistency.
- Add performance benchmarks for snapshot build, JSON serialization and large-device-count UI polling.
- Add long-duration RF coexistence soak tests beyond the release minimum.
- Add optional per-device friendly names independent of DeviceId.
- Add generated OpenAPI if the embedded contract tooling can remain deterministic and lightweight.
- Evaluate target-side Matter stack integration as a separate reviewed plan.
- Complete RCP update transport as a separate reviewed plan.

## 16. Executor return contract

For every stage, the executor must return:

1. concise implementation summary;
2. input and output commit SHA;
3. complete changed-file list;
4. frozen decisions applied;
5. inventories recomputed and exact diffs;
6. migrations/generated contracts/configs/artifacts;
7. tests/checks/HIL run and exact results;
8. release/security evidence produced;
9. deviations from the stage contract;
10. unresolved blockers with an exact stop status.

Do not claim completion when a required test, target build, HIL or security verification was not run. State the exact limitation and keep the stage blocked.

## 17. Cold-start executor procedure

An executor receiving only this plan, the repository and the companion artifacts must:

1. read §§1-6 and the target stage contract;
2. start with S0 unless a valid predecessor completion manifest for the immediately preceding stage is supplied;
3. run the inventory exact comparison before mutation;
4. stop on any stale-input status;
5. execute only the first unblocked stage;
6. apply one canonical target state, not alternative designs;
7. run every named validation available to that stage;
8. create the stage completion manifest;
9. commit the stage atomically;
10. hand only that exact commit/evidence to the next stage.

No external chat instruction is required to begin or to determine the stage order.