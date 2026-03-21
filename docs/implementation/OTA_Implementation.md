# OTA_Implementation.md

Tracked copy of the OTA implementation roadmap previously maintained in `arh/Fix4.md`.

## OTA Implementation Plan For ESP-IDF 5.5

This document tracks the staged implementation of production-grade OTA for the Zigbee Gateway without violating the current project architecture:

- `core` remains platform-agnostic and unchanged for OTA logic
- OTA lives in `service + app_hal + web_ui`
- single-writer ownership remains in `ServiceRuntime`
- HAL stays a thin C ABI wrapper over ESP-IDF OTA APIs

Status legend:

- `[ ]` not started
- `[~]` in progress
- `[x]` done

---

## 1. Architecture Constraints

Must remain true during the whole implementation:

- [ ] Do not add OTA workflow logic into `components/core/`
- [ ] Do not add ESP-IDF headers into Core
- [ ] Do not block the single-writer runtime task with long OTA download/apply work
- [ ] Keep OTA as a service-owned subsystem, similar to async network operations
- [ ] Preserve Zigbee persistent partitions `zb_storage` and `zb_fct`
- [ ] Keep all new public HAL interfaces C-compatible
- [ ] Keep memory usage bounded and avoid unbounded dynamic allocation in hot paths

---

## 2. Target Partition Layout For 8 MB Flash

Recommended replacement for the current single-app layout:

```csv
# Name,      Type, SubType, Offset,   Size,     Flags
nvs,         data, nvs,     0x9000,   0x6000,
otadata,     data, ota,     0xF000,   0x2000,
phy_init,    data, phy,     0x11000,  0x1000,
ota_0,       app,  ota_0,   0x20000,  0x280000,
ota_1,       app,  ota_1,   0x2A0000, 0x280000,
zb_storage,  data, fat,     0x520000, 0x10000,
zb_fct,      data, fat,     0x530000, 0x4000,
coredump,    data, coredump,0x534000, 0x10000,
storage,     data, spiffs,  0x544000, 0x2BC000,
```

Capacity notes:

- Two OTA slots of `0x280000` each (`2.5 MiB`)
- SPIFFS still retains about `2.73 MiB`
- Application image should ideally stay below `~2.2 MiB`

Validation tasks:

- [x] Update root `partitions.csv`
- [x] Update `test/target/partitions.csv`
- [x] Verify build output fits into one OTA slot with headroom
- [x] Add CI/build-time size guard for OTA slot size

---

## 3. Iteration 1: OTA Foundation And Safe Boot

Goal:

- Make the firmware OTA-ready at flash/boot/HAL level before adding a user-facing update flow

### 3.1 Files To Change

- [x] `partitions.csv`
- [x] `test/target/partitions.csv`
- [x] `sdkconfig.defaults.esp32c6`
- [x] `test/target/sdkconfig.defaults`
- [x] `main/Kconfig.projbuild`
- [x] `components/app_hal/CMakeLists.txt`
- [x] `main/app_main.cpp`

### 3.2 Files To Add

- [x] `components/app_hal/include/hal_ota.h`
- [x] `components/app_hal/hal_ota.c`

### 3.3 HAL Contract

Add a minimal OTA HAL surface:

- [x] `int hal_ota_mark_running_partition_valid(void);`
- [x] `bool hal_ota_running_partition_pending_verify(void);`
- [x] `int hal_ota_schedule_restart(uint32_t delay_ms);`
- [x] `bool hal_ota_get_running_version(char* out, size_t out_len);`

Implementation notes:

- Wrap ESP-IDF OTA/boot APIs only
- No business logic in HAL
- Host/integration builds should have safe mockable behavior

### 3.4 Build/Kconfig Work

- [x] Add `CONFIG_ZGW_OTA_ENABLED`
- [x] Add `CONFIG_ZGW_OTA_BOOT_CONFIRM_ENABLED`
- [x] Add `CONFIG_ZGW_OTA_BOOT_CONFIRM_TIMEOUT_MS`
- [x] Add `CONFIG_ZGW_OTA_USE_CERT_BUNDLE`
- [x] Add `app_update` dependency to `components/app_hal/CMakeLists.txt`
- [x] Add `bootloader_support` dependency if required by the HAL implementation
- [x] No separate `esp_partition` dependency is required in the current HAL foundation

### 3.5 Boot Confirmation Flow

In `main/app_main.cpp`:

- [x] Detect whether the running partition is pending verification
- [x] Only mark app valid after critical startup succeeds
- [x] Keep rollback possible if boot fails before service stack is healthy

Success criteria:

- [x] Firmware boots from OTA partition
- [x] Pending image can be marked valid after successful startup
- [x] Rollback path remains available on early boot failure

### 3.6 Tests

- [x] Extend `test/integration/test_integration_hal_platform_shims.cpp` for `hal_ota_*`
- [x] Add host/integration test for pending-verify to mark-valid path
- [x] Add build size verification step

---

## 4. Iteration 2: Service-Owned OTA Runtime And Web API

Goal:

- Add a complete OTA update flow triggered through Web API without breaking the single-writer model

### 4.1 Files To Add

- [x] `components/service/include/ota_manager.hpp`
- [x] `components/service/ota_manager.cpp`
- [x] `components/web_ui/web_handlers_ota.cpp`

### 4.2 Files To Change

- [x] `components/service/include/service_runtime_api.hpp`
- [x] `components/service/include/service_runtime.hpp`
- [x] `components/service/service_runtime.cpp`
- [x] `components/service/include/operation_result_store.hpp`
- [x] `components/service/operation_result_store.cpp`
- [x] `components/service/CMakeLists.txt`
- [x] `components/web_ui/include/web_routes.hpp`
- [x] `components/web_ui/web_routes.cpp`
- [x] `components/web_ui/CMakeLists.txt`
- [x] `components/web_ui/assets/app.js`
- [x] `components/app_hal/include/hal_ota.h`
- [x] `components/app_hal/hal_ota.c`

### 4.3 Service API Additions

Add OTA-specific DTOs to `service_runtime_api.hpp`:

- [x] `enum class OtaStage : uint8_t`
- [x] `enum class OtaPollStatus : uint8_t`
- [x] `struct OtaStartRequest`
- [x] `struct OtaApiSnapshot`
- [x] `struct OtaResult`

Add API methods:

- [x] `bool post_ota_start(const OtaStartRequest&) noexcept`
- [x] `bool build_ota_api_snapshot(OtaApiSnapshot* out) const noexcept`
- [x] `bool take_ota_result(uint32_t request_id, OtaResult* out) noexcept`
- [x] `OtaPollStatus get_ota_poll_status(uint32_t request_id) const noexcept`

### 4.4 OTA Manager Design

Rules for `OtaManager`:

- [x] OTA download/apply runs in a dedicated worker task
- [x] Worker publishes OTA status updates and `ServiceRuntime` applies them; Core/read-model snapshots remain untouched
- [x] Worker reports progress/result back through service-owned manager/result store
- [x] `ServiceRuntime` remains the single writer for Core-backed read models and result queues
- [x] No OTA logic is added to Core reducer, Core commands, or Core effects

### 4.5 HAL OTA Apply API

Expand HAL for actual apply flow:

- [x] Add blocking `download + verify + write + set boot partition` API
- [x] Support progress callback or progress polling
- [x] Use `esp_https_ota` for the normal production path
- [x] Keep TLS/cert handling in HAL boundary

Suggested HAL shape:

- [x] Implemented as `hal_ota_perform_https_update(...)`

Exact naming may be adjusted, but the interface must stay thin and mockable.

### 4.6 Web API

Add routes:

- [x] `GET /api/ota`
- [x] `POST /api/ota`
- [x] `GET /api/ota/result?request_id=...`

Suggested POST payload:

```json
{
  "url": "https://example.com/zigbee-gateway-0.2.0.bin",
  "target_version": "0.2.0"
}
```

Suggested snapshot fields:

- [~] `enabled` not exposed separately; OTA availability is implied by route availability
- [x] `busy`
- [x] `current_version`
- [x] `target_version`
- [x] `stage`
- [x] `progress_percent`
- [x] `last_error`
- [x] `last_request_id`

### 4.7 UI

- [x] Add OTA section to Web UI
- [x] Show current version
- [x] Allow pasting OTA URL
- [x] Show async progress/status/error
- [x] Show reboot pending state clearly

### 4.8 Tests

- [x] `test/host/test_service_ota.cpp`
- [x] `test/host/test_web_handlers_ota.cpp`
- [x] `test/integration/test_integration_web_handlers_ota.cpp`
- [x] target contract tests for `hal_ota`
- [x] add `scripts/run_gateway_ota_hil_smoke.sh`
- [x] add `scripts/prepare_ota_pending_verify.py`

Success criteria:

- [x] OTA can be started from Web API
- [x] Progress is observable through service/web snapshot
- [x] New image is written and selected for next boot
- [x] Device reboots into the new image automatically after successful OTA staging
- [x] New image can be confirmed valid on next startup; verified on hardware via serial OTA-slot HIL path and post-boot `otadata` inspection

---

## 5. Iteration 3: Production Hardening And Data Migration

Goal:

- Make OTA safe and maintainable for real releases

### 5.1 Files To Add

- [x] `components/service/include/ota_manifest.hpp`
- [x] `components/service/ota_manifest.cpp`
- [x] `test/host/test_ota_manifest.cpp`
- [x] `scripts/check_ota_slot_size.sh`
- [x] `scripts/run_gateway_ota_hil_smoke.sh`
- [x] `scripts/check_ota_trust_config.sh`

### 5.2 Files To Change

- [x] `components/common/include/version.hpp`
- [x] persistence/NVS schema handling files
- [x] `main/app_main.cpp`
- [x] `.github/workflows/ci.yml`

### 5.3 Hardening Scope

- [x] Add OTA manifest support with `version`, `sha256`, `url`, `board`, `min_schema`
- [x] Validate `project name`, chip target, image metadata, and version compatibility
- [x] Add anti-downgrade policy or explicit downgrade control
- [x] Use cert bundle or pinned certificate for TLS validation
- [x] Add explicit NVS schema versioning
- [x] Add schema migration on first boot after update
- [x] Only mark app valid after migration/self-check succeeds

### 5.4 Release/CI Gates

- [x] OTA build must pass in CI
- [x] OTA slot size guard must pass in CI
- [x] Host tests for OTA manager and manifest must pass
- [x] Integration tests for OTA Web API must pass
- [x] HIL smoke tooling and firmware-side OTA validation are in place; external broker/network reachability is tracked separately from firmware hardening

Success criteria:

- [x] OTA rejects incompatible or malformed images
- [x] OTA survives reboot and rollback scenarios inside the firmware-controlled flow
- [x] Data/schema migration is explicit and test-covered
- [x] Release process includes OTA validation, not only firmware build

---

## 6. Explicit Non-Goals

To avoid architecture drift, do not do the following in the first OTA implementation:

- [ ] Do not add OTA command types into `components/core/include/core_commands.hpp`
- [ ] Do not add OTA effects into `components/core/include/core_effects.hpp`
- [x] Do not run OTA download inside the single-writer runtime loop
- [ ] Do not couple OTA logic directly into `web_ui` without service mediation
- [ ] Do not merge gateway OTA and RCP update into one implementation track
- [ ] Do not make browser-upload the only OTA path for production use

---

## 7. Recommended Delivery Order

- [x] Iteration 1 complete
- [x] Iteration 2 complete
- [x] Iteration 3 complete

Execution order:

1. Partition layout + HAL OTA foundation + boot confirmation
2. OTA manager + worker + Web API/UI flow
3. Manifest validation + migrations + CI/HIL hardening

---

## 8. Working Notes

Notes and decisions during implementation:

- [x] Record final chosen OTA slot size
- [x] Record final TLS trust strategy
- [x] Record final manifest format
- [x] Record NVS schema versioning approach
- [x] Record rollback confirmation point in boot flow

Current implementation notes:

- OTA slot size is currently set to `0x280000` per slot on 8 MB flash.
- Rollback confirmation point is currently after successful startup of runtime, Web UI, MQTT bridge, and Matter bridge in `main/app_main.cpp`.
- Host/integration sanity check for the new `hal_ota_*` surface passes via `test_integration_hal_platform_shims`.
- Host-side confirmation flow is now covered by `test_ota_bootstrap`.
- OTA slot size guard is wired into `scripts/run_blocking_local_checks.sh` and CI firmware/target build jobs.
- Real target test firmware build now passes and `build-target-tests/hal_target_tests.bin` fits into `ota_0` with headroom.
- Local HIL flash/monitor smoke on `/dev/ttyACM0` produced a passing Unity summary: `19 Tests 0 Failures 0 Ignored`.
- Local HIL runner cleanup has been fixed so `scripts/run_hil_local.sh smoke` now exits after the PASS summary is detected in the log.
- Iteration 3 manifest slice is complete: OTA requests now carry a manifest object, apply local defaults for `project`, `board`, `chip_target`, and `min_schema`, and reject basic incompatibilities before the download starts.
- `web_handlers_ota` now surfaces submit-time manifest failures as HTTP `400/409`, and host/integration tests cover manifest validation plus a project-mismatch rejection path.
- Iteration 3 slice 2 is now in place: `ConfigManager` records an explicit load report (`ready / migrated / fresh install / failed`), initializes the schema key on a truly fresh NVS, and keeps legacy v1/v2 migrations explicit instead of silently burying them.
- `ServiceRuntime::start()` now refuses to start when config bootstrap fails, and `main/app_main.cpp` logs whether the current boot is a fresh schema initialization or a schema migration before OTA boot confirmation is attempted.
- Host coverage now includes fresh-install schema initialization, migrated boot reporting, and a runtime boot-gate test for unsupported future schema; ESP target build also passes with the new config bootstrap metadata.
- Iteration 3 slice 3 is now in place: OTA trust mode is explicit in `Kconfig` with a production default of ESP certificate bundle and an optional pinned root CA PEM embedded from `components/app_hal/ota_server_root_ca.pem`.
- Plain `http://` OTA URLs are now rejected by default in manifest validation and in `hal_ota`, with an explicit testing-only escape hatch `CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING`.
- Release/local guardrails now include `scripts/check_ota_trust_config.sh`, which fails if no TLS trust mode is selected, if insecure HTTP OTA is enabled, or if pinned-CA mode is selected without a real PEM certificate file.
- CI and local blocking checks now run OTA trust validation for the production firmware config, and the gateway OTA HIL smoke script now refuses plaintext OTA URLs unless explicitly allowed for lab testing.
- Post-hardening serial traces confirm that MQTT no longer fails from socket exhaustion; remaining broker connectivity failures now show up as deterministic `ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT`, which is an external network/broker-path issue rather than an OTA firmware blocker.

---

## 9. Phase 4: Remaining OTA Follow-Up

This phase tracks follow-up work that is intentionally outside the now-complete gateway firmware OTA baseline.

### 9.1 RCP Update Track

- [ ] Define a separate `RCP update` architecture and lifecycle
- [ ] Decide whether RCP update is triggered through the same Web UI or a dedicated maintenance flow
- [ ] Implement real `hal_rcp_update_*` platform support instead of the current placeholder path
- [ ] Add compatibility/version checks between gateway firmware and target RCP image
- [ ] Add dedicated HIL validation for gateway OTA + RCP update coexistence

Notes:
- This remains intentionally open. The repository still only contains placeholder `hal_rcp_update_*` hooks without a real platform transport/apply path for the target RCP firmware.
- Closing this track requires a concrete RCP image format, delivery path, and target-side flashing strategy; that information is not yet present in the current codebase.

### 9.2 Release Security Hardening

- [x] Decide whether OTA release manifests need a cryptographic signature in addition to TLS + `sha256`
- [x] Define manifest signing format and embedded trust anchor strategy
- [x] Add signature verification before OTA apply
- [x] Add release-key rotation and failure-handling rules

### 9.3 Release Operations

- [x] Define how OTA artifacts and manifests are published for staging and production
- [x] Add a repeatable manifest generation step to the release flow
- [x] Define promotion rules `staging -> production`
- [x] Add release checklist/documentation for operators
- [x] Add one end-to-end release rehearsal using the final production-style artifact layout

### 9.4 Documentation Cleanup

- [x] Mark `Iteration 1 complete` as fully done in this file
- [x] Add a short operator note describing the final gateway OTA flow and expected HIL verification path

### 9.5 Next-Step Backlog

#### Task 1. Release Manifest Generator

- [x] Add a script that generates OTA manifest files from a built firmware artifact
- [x] Include at least: `version`, `url`, `sha256`, `project`, `board`, `chip_target`, `min_schema`, `allow_downgrade`
- [x] Document the manifest format and expected generation inputs

Acceptance criteria:
- [x] manifest is generated automatically from a real `.bin`
- [x] `sha256` is computed from the final published artifact
- [x] manifest format is stable and documented

Implementation notes:
- `scripts/generate_ota_manifest.py` now generates a stable JSON OTA manifest from an ESP-IDF build directory.
- Required input: `--build-dir <build-dir>` and `--artifact-url <published-url>`.
- Optional overrides: `--output`, `--version`, `--project`, `--board`, `--chip-target`, `--min-schema`, `--allow-downgrade`.
- Defaults are resolved from:
  - `project_description.json` for `project_version`, `target`, and app binary path
  - `components/common/include/version.hpp` for canonical `project` and `board`
  - `components/service/include/config_manager.hpp` for canonical `min_schema`
- Current manifest JSON format:

```json
{
  "version": "ota-hil-a",
  "url": "https://updates.example.local/zigbee_gateway.bin",
  "sha256": "<64-char lowercase hex>",
  "project": "zigbee_gateway",
  "board": "zigbee-gateway-esp32c6",
  "chip_target": "esp32c6",
  "min_schema": 3,
  "allow_downgrade": false
}
```

Example:

```bash
./scripts/generate_ota_manifest.py \
  --build-dir build-gateway-hil \
  --artifact-url https://updates.example.local/zigbee_gateway.bin \
  --output build-gateway-hil/ota-manifest.json
```

#### Task 2. Signed Release Policy

- [x] Add a signed-manifest path on top of the current TLS + `sha256` checks
- [x] Define signature format and embedded trust anchor strategy
- [x] Reject manifests with invalid signature, modified `url`, or modified `sha256`
- [x] Add host tests for valid and invalid signature cases

Acceptance criteria:
- [x] signed OTA path rejects a manifest with invalid signature
- [x] signed OTA path rejects tampered metadata because signature covers canonical payload fields
- [x] signature validation is covered by automated tests

Implementation notes:
- OTA manifests now support `signature_algo`, `signature_key_id`, and detached `signature`.
- Canonical signing payload is now fixed and independent from JSON formatting:
  - `version`
  - `url`
  - `sha256`
  - `project`
  - `board`
  - `chip_target`
  - `min_schema`
  - `allow_downgrade`
- Current format:
  - algorithm: `ecdsa-p256-sha256`
  - key id: `ota-release-v1`
  - signature encoding: lowercase hex of DER-encoded ECDSA signature
- Firmware trust anchor is the embedded public key in `components/app_hal/ota_release_manifest_pub.pem`.
- `scripts/generate_ota_manifest.py` now supports `--signing-key`, `--signature-algo`, and `--signature-key-id`.
- `project` was normalized to `zigbee_gateway` to match the actual ESP-IDF app descriptor and avoid false `project_name` OTA verify mismatches.
- Intentional compatibility note: unsigned lab/manual OTA requests are still accepted for now; strict mandatory-signature enforcement can be flipped once Task 3 release packaging becomes the default OTA path.

#### Task 3. Release Packaging And Publish Flow

- [x] Add one repeatable script or CI job that builds firmware, generates manifest, and prepares a staging OTA bundle
- [x] Publish both firmware and manifest in the same expected layout
- [x] Add a simple artifact URL availability/sanity check

Acceptance criteria:
- [x] one reproducible flow creates a complete OTA release bundle
- [x] output can be consumed by the device without manual editing when packaged with the real staging base URL
- [x] staging artifacts are ready for HIL use immediately after packaging

Implementation notes:
- `scripts/package_ota_release.py` now packages OTA bundles into a publish-ready layout:
  - `<output-dir>/<channel>/<version>/<artifact>.bin`
  - `<output-dir>/<channel>/<version>/ota-manifest.json`
  - `<output-dir>/<channel>/<version>/release-metadata.json`
  - `<output-dir>/<channel>/<version>/project_description.json`
- It reuses `scripts/generate_ota_manifest.py`, so manifest generation and signed-manifest handling stay in one place.
- Bundle metadata now records:
  - `channel`
  - `version`
  - `artifact_name`
  - `artifact_url`
  - `manifest_name`
  - `manifest_url`
  - `signed_manifest`
- Optional `--verify-artifact-url` performs a simple HEAD/GET sanity check against the final artifact URL.
- CI now includes an `OTA Staging Bundle` job that builds firmware, packages a staging OTA bundle, and uploads it as a workflow artifact.

Example:

```bash
python3 ./scripts/package_ota_release.py \
  --build-dir build \
  --output-dir dist/ota \
  --artifact-base-url https://staging.example.com/ota \
  --channel staging \
  --signing-key test/fixtures/ota_manifest_test_private.pem
```

#### Task 4. End-to-End Staging OTA Validation

- [x] Add one stable HIL scenario for `old version -> staging manifest -> download -> reboot -> confirm new version`
- [x] Add one negative HIL scenario for rollback or rejected update
- [x] Persist the outcome in script/log output suitable for release sign-off

Acceptance criteria:
- [x] one full end-to-end OTA cycle passes on real hardware
- [x] one negative validation case is covered on real hardware
- [x] HIL logs clearly show download, reboot, slot switch, and confirmed running version

Implementation notes:
- `scripts/run_gateway_ota_staging_hil.sh` now validates packaged staging OTA bundles against a real gateway using the signed manifest format from Task 2 and the packaged bundle layout from Task 3.
- Supported scenarios:
  - `SCENARIO=success`
  - `SCENARIO=tampered-signature`
- Manifest source can be:
  - local `OTA_MANIFEST_PATH`
  - remote `OTA_MANIFEST_URL`
- Optional `SERVE_DIR` support allows local bundle hosting through `python3 -m http.server` during HIL runs.
- For a meaningful `SCENARIO=success` run, the gateway must start on an older image than the manifest target version, for example `ota-hil-a -> ota-hil-b`.
- Rerunning the same manifest when the gateway already reports the target version is not a valid success-path verification.
- The OTA artifact host must be reachable from the gateway itself over HTTPS, not only from a desktop browser on the developer machine.
- The OTA HTTPS certificate must include the exact artifact host IP or hostname in SAN.
- Sign-off logging now emits `OTA_SIGNOFF ...` lines for:
  - submit response
  - OTA snapshots
  - OTA result polling
  - post-reboot version confirmation
  - negative-path rejection confirmation
- Real hardware sign-off status as of 2026-03-20:
  - `SCENARIO=success` now passes on real hardware end-to-end
  - full OTA cycle confirmed:
    - HTTPS download completed from `0%` to `100%`
    - total payload downloaded: `1,677,504` bytes
    - device rebooted automatically into the updated image
    - running version changed from `3f4a84b-dirty` to `ota-hil-b`
  - `SCENARIO=tampered-signature` also passes on real hardware as the negative validation case
  - signed manifest acceptance and negative-path rejection are both validated in the final HIL sign-off

Example success run:

```bash
GATEWAY_BASE_URL=http://192.168.178.171 \
OTA_MANIFEST_PATH=dist/ota/staging/1.2.3/ota-manifest.json \
SERVE_DIR=dist \
HTTP_PORT=8877 \
LOG_PATH=gateway-ota-signoff.log \
./scripts/run_gateway_ota_staging_hil.sh
```

Example negative run:

```bash
GATEWAY_BASE_URL=http://192.168.178.171 \
OTA_MANIFEST_PATH=dist/ota/staging/1.2.3/ota-manifest.json \
SCENARIO=tampered-signature \
LOG_PATH=gateway-ota-signoff.log \
./scripts/run_gateway_ota_staging_hil.sh
```
