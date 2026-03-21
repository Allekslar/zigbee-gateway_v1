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

---

## OTA HIL Tests

Two OTA test scripts exist:

| Script | Purpose |
|---|---|
| `scripts/run_gateway_ota_hil_smoke.sh` | End-to-end OTA smoke — submits URL directly, verifies version after reboot |
| `scripts/run_gateway_ota_staging_hil.sh` | Signed-manifest staging — tests success path and tampered-signature rejection |

### System dependencies

Required on the operator machine:

- `bash`
- `curl`
- `python3`
- ESP-IDF `idf.py` environment for reflashing the gateway between runs
- serial access to the board, typically `/dev/ttyACM0`

Required when using the documented local OTA host setup:

- `docker`
- `openssl`

Required in the network:

- an HTTPS artifact host reachable from the gateway on the same LAN
- HTTP Range support on the OTA server
- a server certificate whose SAN matches the exact OTA host IP or hostname used by the gateway

### Prerequisites

#### 1. HTTPS server (nginx in Docker)

The OTA firmware binary must be served over HTTPS with support for HTTP Range requests.
**Do not use Python's `SimpleHTTPRequestHandler`** — it does not support Range requests,
which causes `MBEDTLS_ERR_SSL_BAD_INPUT_DATA` failures at the `get_img_desc` stage.

Generate server certificate (one-time, or when server IP changes):

```bash
openssl x509 -req -in certs/server.csr \
  -CA certs/ca-cert.pem -CAkey certs/ca-key.pem -CAcreateserial \
  -out certs/server-cert.pem -days 365 \
  -extfile <(printf "subjectAltName=IP:<WINDOWS_LAN_IP>,IP:<WSL2_IP>,IP:127.0.0.1\nbasicConstraints=CA:FALSE")
```

Start nginx:

```bash
docker run -d --name ota-https-server \
  -p 8443:8443 \
  -v "$PWD/certs/nginx-ota.conf:/etc/nginx/conf.d/ota.conf:ro" \
  -v "$PWD/certs/server-cert.pem:/certs/cert.pem:ro" \
  -v "$PWD/certs/server-key.pem:/certs/key.pem:ro" \
  -v "$PWD/dist/ota:/data:ro" \
  nginx:alpine
```

Verify it serves range requests (expect HTTP 206):

```bash
curl --cacert certs/ca-cert.pem \
  -H "Range: bytes=0-1023" \
  -o /dev/null -w "HTTP %{http_code}\n" \
  "https://<WINDOWS_LAN_IP>:8443/staging/ota-hil-b/zigbee_gateway.bin"
```

To restart a stopped container: `docker start ota-https-server`

#### 2. Windows port forwarding (WSL2 only)

The ESP32 connects to the Windows LAN IP. Windows must forward port 8443 to WSL2.
Run in **cmd as Administrator** (replace IPs as appropriate):

```cmd
netsh interface portproxy add v4tov4 listenport=8443 listenaddress=<WINDOWS_LAN_IP> connectport=8443 connectaddress=<WSL2_IP>
netsh advfirewall firewall add rule name="OTA HTTPS 8443" protocol=TCP dir=in localport=8443 action=allow
```

To find your IPs:
- **Windows LAN IP**: `ipconfig` → Ethernet adapter → IPv4 Address
- **WSL2 IP**: `ip addr show eth0` inside WSL2

#### 3. OTA manifest (staging test only)

Generate a signed manifest pointing to the correct server IP:

```bash
python3 scripts/generate_ota_manifest.py \
  --build-dir dist/ota/staging/ota-hil-b \
  --artifact-url "https://<WINDOWS_LAN_IP>:8443/staging/ota-hil-b/zigbee_gateway.bin" \
  --version ota-hil-b \
  --project zigbee_gateway \
  --board zigbee-gateway-esp32c6 \
  --chip-target esp32c6 \
  --min-schema 3 \
  --signing-key test/fixtures/ota_manifest_test_private.pem \
  --output dist/ota/staging/ota-hil-b/ota-manifest.json
```

#### 4. Firmware with correct CA certificate

The gateway firmware embeds the OTA server CA from `components/app_hal/ota_server_root_ca.pem`.
The pre-built `ota-hil-a` and `ota-hil-b` binaries contain an **empty CA certificate** and
cannot perform OTA themselves — they are test targets only.

Always flash a freshly built firmware before each test run:

```bash
idf.py build flash -p /dev/ttyACM0
```

### Test order and device state

`ota-hil-b` does not call `mark_valid`, so the bootloader rolls back to the previous
partition after a power cycle. This means after a successful OTA to `ota-hil-b`, the
device will return to the base firmware on its own after reboot — but the staging test
waits for `stage=idle` before that rollback happens, so staging passes cleanly.

The smoke test submits the OTA URL without a manifest, so it does not test signature
verification. Run it as a basic connectivity/download/reboot check independently of staging.

**Required order per test session:**

```
idf.py flash  →  smoke test
idf.py flash  →  staging: success  →  staging: tampered-signature
```

Practical meaning:

- start `smoke` from an older image on the device, for example `ota-hil-a`
- after `smoke`, reflash the same base image before `staging: success`
- do not treat `ota-hil-b -> ota-hil-b` as a valid success-path check
- `tampered-signature` may run immediately after `staging: success` because it stops at API-level rejection and does not download firmware

Staging `tampered-signature` runs after `success` because it only tests API-level rejection
(HTTP 409) and does not attempt a download — device state does not matter.

### Running the smoke test

```bash
GATEWAY_BASE_URL=http://192.168.178.171 \
OTA_URL="https://<WINDOWS_LAN_IP>:8443/staging/ota-hil-b/zigbee_gateway.bin" \
EXPECTED_VERSION=ota-hil-b \
OTA_POLL_TIMEOUT_SEC=180 \
REBOOT_TIMEOUT_SEC=120 \
bash scripts/run_gateway_ota_hil_smoke.sh
```

### Running the staging tests

```bash
# Success scenario
GATEWAY_BASE_URL=http://192.168.178.171 \
OTA_MANIFEST_PATH=dist/ota/staging/ota-hil-b/ota-manifest.json \
SCENARIO=success \
LOG_PATH=gateway-ota-signoff.log \
OTA_POLL_TIMEOUT_SEC=180 \
REBOOT_TIMEOUT_SEC=120 \
bash scripts/run_gateway_ota_staging_hil.sh

# Tampered-signature rejection (no reflash needed after success)
GATEWAY_BASE_URL=http://192.168.178.171 \
OTA_MANIFEST_PATH=dist/ota/staging/ota-hil-b/ota-manifest.json \
SCENARIO=tampered-signature \
LOG_PATH=gateway-ota-signoff.log \
bash scripts/run_gateway_ota_staging_hil.sh
```

### Expected results

| Test | Expected output |
|---|---|
| smoke | `Gateway OTA HIL smoke PASSED` |
| staging success | `Gateway OTA staging success validation PASSED` |
| staging tampered-signature | `Gateway OTA staging negative validation PASSED` |

### Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `verify_failed` at `get_img_desc`, `tls_error_code=28928` | Server does not support HTTP Range requests (Python SimpleHTTPRequestHandler) | Use nginx |
| `download_failed` at `begin`, `transport_last_esp_err=28674` | TLS CA mismatch — firmware has empty or wrong CA cert | `idf.py build flash` to embed current CA |
| Docker container exited | nginx stopped (e.g. after WSL2 restart) | `docker start ota-https-server` |
| Same error after reflash | Stale port forwarding or firewall rule on Windows | Re-run `netsh` commands |
