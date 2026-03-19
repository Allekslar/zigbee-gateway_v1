#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/check_ota_trust_config.sh [sdkconfig-path]

Validates OTA trust-related configuration for release/local verification.

Checks:
1. One OTA TLS trust mode is selected.
2. Plain HTTP OTA URLs are not enabled unless explicitly allowed.
3. If pinned CA mode is selected, the embedded PEM file contains a certificate.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDKCONFIG_PATH="${1:-${ROOT_DIR}/sdkconfig}"
PINNED_CA_PATH="${ROOT_DIR}/components/app_hal/ota_server_root_ca.pem"

test -f "${SDKCONFIG_PATH}" || {
  echo "sdkconfig not found: ${SDKCONFIG_PATH}" >&2
  exit 2
}

cfg_enabled() {
  local key="$1"
  grep -Eq "^${key}=y$" "${SDKCONFIG_PATH}"
}

cfg_disabled() {
  local key="$1"
  grep -Eq "^# ${key} is not set$" "${SDKCONFIG_PATH}"
}

cert_bundle_enabled=0
pinned_ca_enabled=0
allow_http_enabled=0

if cfg_enabled "CONFIG_ZGW_OTA_TLS_TRUST_CERT_BUNDLE"; then
  cert_bundle_enabled=1
fi
if cfg_enabled "CONFIG_ZGW_OTA_TLS_TRUST_PINNED_CA"; then
  pinned_ca_enabled=1
fi
if cfg_enabled "CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING"; then
  allow_http_enabled=1
fi

if [[ "${cert_bundle_enabled}" -eq 1 && "${pinned_ca_enabled}" -eq 1 ]]; then
  echo "OTA TLS trust config invalid: both cert bundle and pinned CA are enabled." >&2
  exit 1
fi

if [[ "${cert_bundle_enabled}" -eq 0 && "${pinned_ca_enabled}" -eq 0 ]]; then
  echo "OTA TLS trust config invalid: no trust mode selected." >&2
  exit 1
fi

if [[ "${allow_http_enabled}" -eq 1 ]]; then
  echo "OTA trust config invalid for release: CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING=y" >&2
  exit 1
fi

if [[ "${pinned_ca_enabled}" -eq 1 ]]; then
  test -f "${PINNED_CA_PATH}" || {
    echo "Pinned CA mode selected but PEM file is missing: ${PINNED_CA_PATH}" >&2
    exit 1
  }

  if ! grep -q "BEGIN CERTIFICATE" "${PINNED_CA_PATH}"; then
    echo "Pinned CA mode selected but PEM file does not contain a certificate: ${PINNED_CA_PATH}" >&2
    exit 1
  fi
fi

echo "OTA trust config OK"
