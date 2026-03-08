<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# HIL Zigbee Gateway Smoke

This package documents the real-device smoke scenario for a flashed gateway and real Zigbee end devices.

## Scope

The smoke covers the regression-prone path that unit tests cannot prove on real RF hardware:

1. gateway reboot preserves paired devices in `/api/devices`;
2. opening a join window and pairing exactly one new device;
3. auto-close of the join window after the first join;
4. bounded-retry `ON/OFF` command success on the newly joined device during its post-join readiness window;
5. removing the joined device and verifying it disappears from `/api/devices`.

## Runner

Use the semi-automated runner:

```bash
scripts/run_gateway_zigbee_smoke.sh
```

Useful environment variables:

```bash
GW_BASE_URL=http://192.168.178.171 \
JOIN_SECONDS=30 \
POWER_READY_SEC=30 \
POWER_RETRY_SEC=3 \
scripts/run_gateway_zigbee_smoke.sh
```

## Operator Actions

The runner pauses only when physical interaction is required:

- reboot the gateway;
- put one new Zigbee end device into pairing mode.

Everything else is verified through the public HTTP API.

## Preconditions

- gateway firmware already flashed and reachable over HTTP;
- at least one already-paired device is present before reboot;
- one additional Zigbee device is available for join/remove;
- `curl` and `python3` are installed on the operator machine.
