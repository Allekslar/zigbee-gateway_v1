<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Matter Bridge Module Notes

This module owns Matter-side projection and update publication logic only.

## Boundaries

- `matter_bridge` consumes service-owned snapshots via `service::MatterRuntimeApi`.
- `matter_bridge` must not read raw `core::CoreState` directly.
- `matter_bridge` must not include or reference the full `ServiceRuntimeApi` surface.

## HAL Boundary

- ESP-IDF / Matter stack calls are isolated behind `components/app_hal/hal_matter.*`.
- `hal_matter` is a thin C ABI transport adapter (init + publish update), with no domain policy.
- Domain mapping and runtime lifecycle policy stay in `matter_bridge` / service layer.

## Platform Extension Seam

`hal_matter.c` exposes weak hooks for platform-specific integration:

- `hal_matter_stack_init(...)`
- `hal_matter_stack_publish_state(...)`
- `hal_matter_stack_publish_attribute_update(...)`

Production platform integration should override these hooks in a dedicated adapter unit,
without changing service/core contracts.

## C++ Profile Exceptions (If Needed)

Default compile policy is embedded-safe (`-fno-exceptions`, `-fno-rtti`) per project rules.

If a specific Matter SDK integration requires RTTI/exceptions:

1. Scope that requirement strictly to Matter submodule files.
2. Document the exact rationale and file scope in this README.
3. Add a time-bounded ADR exception entry in `docs/architecture/ADR_EXCEPTIONS.md`.

No global project-wide relaxation is allowed for this requirement.
