# RCP.md

## RCP Update Track

This document tracks the separate implementation plan for `RCP update`, independent from the now-complete gateway firmware OTA flow.

Current hardware scope:

- The current development target is `ESP32-C6-DevKitC` / `zigbee-gateway-esp32c6`
- On this board, Zigbee uses the native 802.15.4 radio of the `ESP32-C6`
- There is no confirmed external RCP on this target board
- Therefore, `RCP update` is not a production requirement for the current board and is tracked only as a future external-hardware capability

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

- [x] Add a service-owned `RcpUpdateManager`
- [x] Extend `ServiceRuntimeApi` with:
  - [x] `post_rcp_update_start(...)`
  - [x] `build_rcp_update_snapshot(...)`
  - [x] `take_rcp_update_result(...)`
- [x] Add Web/API entry points such as `/api/rcp-update`

### 1.2 Maintenance Semantics

- [~] Define when Zigbee activity must be paused
- [x] Block conflicting operations while RCP update is active
- [x] Expose `busy/paused/updating/failed` state through the service-owned read model

### 1.3 RCP Manifest / Metadata

- [x] Define minimal RCP image metadata:
  - [x] `version`
  - [x] `url`
  - [x] `sha256`
  - [x] `board`
  - [x] `transport/chip`
  - [x] `gateway_min_version`
- [x] Validate metadata before any flashing attempt

### 1.4 HAL Boundary

- [x] Keep `hal_rcp_update_begin/write/end` as the only apply boundary
- [x] Make the control flow fully testable with mocked HAL backend first
- [x] Do not add platform-specific flashing policy into `core`

### 1.5 Tests

- [x] Add host tests for request validation and state machine behavior
- [x] Add integration tests for `/api/rcp-update`
- [x] Add conflict tests: gateway OTA and RCP update must not run concurrently

Success criteria:

- [x] full service/web flow exists even with a mock backend
- [x] conflicting OTA/update flows are blocked cleanly
- [x] progress/result reporting is observable through API

---

## Iteration 2: Real Platform Update Path

Goal:

- move from a pure control-flow mock to a real gateway-driven apply path, while keeping the final target-specific flashing primitive behind platform hooks

### 2.1 Platform Backend

- [x] Implement a real gateway-side ESP/platform update path around `hal_rcp_update_begin/write/end`
- [x] Define the current transport/apply strategy:
  - [x] gateway-driven HTTPS download and streaming apply path
  - [x] target flashing primitive remains behind `hal_rcp_stack_*` hooks
  - [x] final strong target backend is deferred as a follow-up track

Notes:

- [x] A real gateway-side HTTPS streaming/apply path now exists through `hal_rcp_perform_https_update(...)`
- [x] The final target flashing primitive still remains behind `hal_rcp_stack_*` hooks
- [x] Iteration 2 no longer depends on choosing UART bootloader vs vendor maintenance mode inside the gateway code path

### 2.2 Compatibility And Safety

- [x] Add compatibility checks between gateway firmware and target RCP image
- [x] Verify target board/transport/protocol expectations
- [x] Add explicit failure codes for:
  - [x] invalid image
  - [x] transport failure
  - [x] verification failure
  - [x] post-update bring-up failure

### 2.3 Post-Update Recovery

- [x] Probe resulting RCP version after flashing
- [x] Reinitialize or recover the gateway-side RCP lifecycle cleanly at the current abstraction boundary
- [x] Surface recoverable vs non-recoverable failures to operators

Notes:

- [x] Version probe and recovery hooks are now part of the HAL contract
- [x] Full target-specific Zigbee/RCP bring-up remains delegated to the future strong backend, not to the gateway-side orchestration layer

### 2.4 HIL Validation

- [x] Define HIL validation requirements for the future target-specific backend
- [x] Keep HIL sign-off explicitly outside the current gateway-side Iteration 2 closure
- [x] Preserve coexistence constraints for future validation against gateway OTA maintenance flow

Success criteria:

- [x] a real gateway-driven RCP image apply path exists from the device workflow
- [x] resulting RCP version is confirmed after update
- [x] failure paths are explicit and recoverable
- [x] Iteration 2 closes with code-level verification, host/integration coverage, and a buildable ESP path

---

## Follow-Up: Target Backend And HIL Sign-Off

This work remains intentionally separate from Iteration 2 closure.

- [ ] Reconfirm that the selected target board actually contains an external RCP before enabling this track for production
- [ ] Implement a strong target-specific `hal_rcp_stack_*` flashing backend
- [ ] Validate real-device success path with an actual RCP image
- [ ] Validate invalid-image rejection on hardware
- [ ] Validate coexistence between gateway OTA maintenance and real RCP update flow
- [ ] Confirm clean post-update Zigbee bring-up on the final target backend

### Target Backend + HIL Sign-Off Checklist

#### A. Target Flashing Method

- [ ] Choose the concrete RCP flashing method:
  - [ ] UART bootloader path
  - [ ] vendor maintenance mode
  - [ ] other target-specific transport
- [ ] Record the chosen method and its constraints
- [ ] Define required wiring, reset/boot mode, and operator preconditions

#### B. Strong `hal_rcp_stack_*` Backend

- [x] Expose current backend availability and backend name through the runtime/API
- [x] Reject update requests early with `unsupported_backend` when no strong backend is present
- [ ] Replace weak placeholder hooks with a real target backend
- [ ] Implement target enter-update-mode sequence
- [ ] Implement streaming/apply path for RCP image chunks
- [ ] Implement finalize/commit step on the target backend
- [ ] Return explicit failure reasons for prepare/write/finalize/recover failures

#### C. Post-Update Bring-Up

- [ ] Probe resulting RCP firmware version on the real target
- [ ] Reinitialize Zigbee/RCP connectivity after update
- [ ] Add explicit health-check criteria for successful bring-up
- [ ] Distinguish recoverable vs non-recoverable post-update failures

#### D. HIL Validation

- [ ] Success path: real RCP image updates end-to-end on hardware
- [ ] Negative path: invalid image is rejected cleanly
- [ ] Coexistence: RCP update remains mutually exclusive with gateway OTA
- [ ] Recovery path: failed update leaves gateway in an operator-visible recoverable state

#### E. Sign-Off

- [ ] Capture operator runbook for the real target backend
- [ ] Record final HIL evidence for success and negative paths
- [ ] Mark the RCP target-backend track complete only after hardware sign-off

---

## Notes

- Iteration 1 is now implemented in code: service/runtime/web flow, result polling, conflict blocking, and host/integration coverage exist.
- Iteration 2 is now complete for the gateway-side code path: HTTPS apply, metadata checks, version probe, recovery semantics, and operator-visible failure handling are present.
- For the current `ESP32-C6-DevKitC` target, the repository does not have evidence of a real external RCP device. The active Zigbee path uses the native `ESP32-C6` 15.4 radio.
- The existing RCP backend code should therefore be treated as an optional future external-hardware track, not as an active production path for the current board.
- The repository still does not contain a strong target-specific implementation of the final `hal_rcp_stack_*` flashing hooks.
- Real target flashing sign-off is tracked in the follow-up section above, not as an open blocker inside Iteration 2.
- Gateway OTA should stay complete and stable regardless of RCP update work.
