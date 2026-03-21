# RCP.md

## RCP Update Track

This document tracks the separate implementation plan for `RCP update`, independent from the now-complete gateway firmware OTA flow.

Design constraints:

- `RCP update` must remain separate from gateway firmware OTA
- `core` remains unchanged; orchestration lives in `service + app_hal + web_ui`
- long-running RCP flashing work must not block the single-writer runtime loop
- any real platform flashing path must stay behind thin `hal_rcp_update_*` C ABI wrappers

Status legend:

- `[ ]` not started
- `[~]` in progress
- `[x]` done

---

## Iteration 1: Architecture And Safe Control Path

Goal:

- introduce a clean, testable RCP update flow before implementing real target flashing

### 1.1 Service And API Surface

- [ ] Add a service-owned `RcpUpdateManager`
- [ ] Extend `ServiceRuntimeApi` with:
  - [ ] `post_rcp_update_start(...)`
  - [ ] `build_rcp_update_snapshot(...)`
  - [ ] `take_rcp_update_result(...)`
- [ ] Add Web/API entry points such as `/api/rcp-update`

### 1.2 Maintenance Semantics

- [ ] Define when Zigbee activity must be paused
- [ ] Block conflicting operations while RCP update is active
- [ ] Expose `busy/paused/updating/failed` state through the service-owned read model

### 1.3 RCP Manifest / Metadata

- [ ] Define minimal RCP image metadata:
  - [ ] `version`
  - [ ] `url`
  - [ ] `sha256`
  - [ ] `board`
  - [ ] `transport/chip`
  - [ ] `gateway_min_version`
- [ ] Validate metadata before any flashing attempt

### 1.4 HAL Boundary

- [ ] Keep `hal_rcp_update_begin/write/end` as the only apply boundary
- [ ] Make the control flow fully testable with mocked HAL backend first
- [ ] Do not add platform-specific flashing policy into `core`

### 1.5 Tests

- [ ] Add host tests for request validation and state machine behavior
- [ ] Add integration tests for `/api/rcp-update`
- [ ] Add conflict tests: gateway OTA and RCP update must not run concurrently

Success criteria:

- [ ] full service/web flow exists even with a mock backend
- [ ] conflicting OTA/update flows are blocked cleanly
- [ ] progress/result reporting is observable through API

---

## Iteration 2: Real Platform Update Path

Goal:

- replace the placeholder `hal_rcp_update_*` path with a real target flashing/update flow

### 2.1 Platform Backend

- [ ] Implement real ESP/platform `hal_rcp_update_begin/write/end`
- [ ] Define the actual transport/apply strategy:
  - [ ] UART bootloader path
  - [ ] vendor maintenance mode
  - [ ] other target-specific flashing path

### 2.2 Compatibility And Safety

- [ ] Add compatibility checks between gateway firmware and target RCP image
- [ ] Verify target board/transport/protocol expectations
- [ ] Add explicit failure codes for:
  - [ ] invalid image
  - [ ] transport failure
  - [ ] verification failure
  - [ ] post-update bring-up failure

### 2.3 Post-Update Recovery

- [ ] Probe resulting RCP version after flashing
- [ ] Reinitialize Zigbee stack cleanly
- [ ] Surface recoverable vs non-recoverable failures to operators

### 2.4 HIL Validation

- [ ] Add real-device success-path validation
- [ ] Add invalid-image rejection path
- [ ] Add coexistence validation for gateway OTA plus RCP update maintenance flow

Success criteria:

- [ ] real RCP image can be applied from the device workflow
- [ ] resulting RCP version is confirmed after update
- [ ] failure paths are explicit and recoverable
- [ ] HIL sign-off exists for success and negative paths

---

## Notes

- The current repository only contains placeholder `hal_rcp_update_*` hooks; there is no real target flashing/apply strategy implemented yet.
- Closing Iteration 2 requires a concrete RCP image format, delivery path, and target-specific flashing method.
- Gateway OTA should stay complete and stable regardless of RCP update work.
