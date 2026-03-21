<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# OTA Release Operations

This document defines the operational release flow for gateway OTA bundles after `Fix4`.

## System Requirements

- `bash`
- `python3`
- `openssl`
- `curl`
- optional for local HTTPS hosting during HIL sign-off: `docker`
- ESP-IDF `idf.py` environment plus serial access for HIL reflashing

## Channels

- `staging`: signed bundles used for validation and operator sign-off
- `production`: signed bundles published after staging sign-off

Each channel uses the same bundle layout:

```text
dist/ota/<channel>/<version>/
  zigbee_gateway.bin
  project_description.json
  ota-manifest.json
  release-metadata.json
```

## Signed Manifest Key Policy

Gateway firmware now trusts two manifest public keys:

- `ota-release-v1`: current active release key
- `ota-release-v2`: next release key used for rotation

Rotation rules:

1. Ship firmware that trusts both `ota-release-v1` and `ota-release-v2`.
2. Sign new staging bundles with `ota-release-v2`.
3. After the fleet is updated to firmware that trusts `ota-release-v2`, start publishing production bundles signed by `ota-release-v2`.
4. Remove `ota-release-v1` only in a later maintenance release after production rollout is complete and rollback images no longer depend on it.

Failure-handling rules:

- Unknown `signature_key_id`: reject the manifest and do not retry the same package.
- Invalid signature: treat as release packaging error or tampering; regenerate the manifest and re-sign.
- Staging failure: never promote that bundle to `production`.
- Key rotation cutover must happen in `staging` first; `production` promotion is allowed only after a successful staging HIL pass.

## 1. Package A Staging Bundle

```bash
python3 ./scripts/package_ota_release.py \
  --build-dir build \
  --output-dir dist/ota \
  --artifact-base-url https://staging.example.com/ota \
  --channel staging \
  --signing-key test/fixtures/ota_manifest_test_private.pem \
  --signature-key-id ota-release-v1
```

Optional rotated-key staging package:

```bash
python3 ./scripts/package_ota_release.py \
  --build-dir build \
  --output-dir dist/ota \
  --artifact-base-url https://staging.example.com/ota \
  --channel staging \
  --signing-key test/fixtures/ota_manifest_test_private_next.pem \
  --signature-key-id ota-release-v2
```

## 2. Run Staging Validation

Required sign-off:

1. `scripts/run_gateway_ota_hil_smoke.sh`
2. `scripts/run_gateway_ota_staging_hil.sh` with `SCENARIO=success`
3. `scripts/run_gateway_ota_staging_hil.sh` with `SCENARIO=tampered-signature`

Promotion to `production` is allowed only after all three pass.

## 3. Promote `staging -> production`

```bash
python3 ./scripts/promote_ota_release.py \
  --input-dir dist/ota \
  --source-channel staging \
  --target-channel production \
  --version ota-hil-b \
  --artifact-base-url https://updates.example.com/ota \
  --signing-key test/fixtures/ota_manifest_test_private.pem \
  --signature-key-id ota-release-v1
```

The promotion step copies the artifact, regenerates the manifest for the production URL,
and writes fresh `release-metadata.json`.

## Operator Checklist

- Build firmware for `esp32c6`.
- Package a signed `staging` bundle.
- Verify the HTTPS artifact host is reachable from the gateway and supports HTTP Range.
- Run OTA HIL smoke from an older image.
- Run OTA staging HIL success.
- Run OTA staging HIL tampered-signature.
- Archive sign-off logs together with the release version.
- Promote the exact signed bundle to `production`.
- Publish the production bundle URL and manifest URL to operators.
