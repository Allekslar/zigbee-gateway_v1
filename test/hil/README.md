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

Additional MQTT broker smoke covers:

1. gateway reports MQTT `enabled=true` and `connected=true` in `/api/network`;
2. a newly joined device appears as retained MQTT `availability/state/telemetry`;
3. `power/set` sent through the broker changes retained device state;
4. force-removing the device publishes retained `availability=offline`.

Additional Matter runtime smoke covers:

1. join-window -> first-device auto-close path with Matter bridge runtime feed active;
2. bounded ON/OFF command-update loop on the joined device;
3. remove path and device disappearance verification.

## Runner

Use the semi-automated runner:

```bash
scripts/run_gateway_zigbee_smoke.sh
```

MQTT broker HIL smoke:

```bash
MQTT_HOST=192.168.178.65 \
GW_BASE_URL=http://192.168.178.171 \
MQTT_USER=... \
MQTT_PASS=... \
scripts/run_gateway_mqtt_hil_smoke.sh
```

Matter runtime HIL smoke:

```bash
GW_BASE_URL=http://192.168.178.171 \
JOIN_SECONDS=30 \
MATTER_LOOP_CYCLES=2 \
POWER_READY_SEC=30 \
POWER_RETRY_SEC=3 \
scripts/run_gateway_matter_hil_smoke.sh
```

Useful environment variables:

```bash
GW_BASE_URL=http://192.168.178.171 \
JOIN_SECONDS=30 \
POWER_READY_SEC=30 \
POWER_RETRY_SEC=3 \
scripts/run_gateway_zigbee_smoke.sh
```

Useful environment variables for MQTT broker smoke:

```bash
GW_BASE_URL=http://192.168.178.171 \
MQTT_HOST=192.168.178.65 \
MQTT_PORT=1883 \
MQTT_USER=... \
MQTT_PASS=... \
JOIN_SECONDS=30 \
POWER_READY_SEC=30 \
POWER_RETRY_SEC=3 \
GATEWAY_READY_SEC=30 \
MQTT_READY_SEC=30 \
FORCE_REMOVE_TIMEOUT_MS=15000 \
scripts/run_gateway_mqtt_hil_smoke.sh
```

Useful environment variables for Matter runtime smoke:

```bash
GW_BASE_URL=http://192.168.178.171 \
JOIN_SECONDS=30 \
MATTER_LOOP_CYCLES=2 \
POWER_READY_SEC=30 \
POWER_RETRY_SEC=3 \
GATEWAY_READY_SEC=30 \
FORCE_REMOVE_TIMEOUT_MS=15000 \
scripts/run_gateway_matter_hil_smoke.sh
```

## Operator Actions

The runner pauses only when physical interaction is required:

- reboot the gateway;
- put one new Zigbee end device into pairing mode.

Everything else is verified through the public HTTP API.

The MQTT runner pauses only when physical interaction is required:

- put one new Zigbee end device into pairing mode.

The Matter runner pauses only when physical interaction is required:

- put one new Zigbee end device into pairing mode.

Everything else is verified through public HTTP API plus MQTT broker topics.

## Preconditions

- gateway firmware already flashed and reachable over HTTP;
- at least one already-paired device is present before reboot;
- one additional Zigbee device is available for join/remove;
- `curl`, `python3`, `mosquitto_pub`, and `mosquitto_sub` are installed on the operator machine.
