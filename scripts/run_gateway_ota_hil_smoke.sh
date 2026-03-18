#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/run_gateway_ota_hil_smoke.sh

Semi-automated HIL smoke for real gateway OTA flow over /api/ota.

Required environment:
  GATEWAY_BASE_URL   Base HTTP URL of the gateway, e.g. http://192.168.4.1 or http://zigbee-gateway.local

OTA source:
  Either provide OTA_URL directly,
  or provide all of:
    OTA_BIN_PATH     Local .bin path to serve
    HOST_IP          Host IP reachable from the gateway

Optional environment:
  EXPECTED_VERSION      Expected version after reboot
  ESPPORT               Serial port for optional reboot observation (default: /dev/ttyACM0)
  OTA_HTTP_PORT         Local HTTP server port when OTA_BIN_PATH is used (default: 8787)
  OTA_POLL_TIMEOUT_SEC  OTA result poll timeout (default: 180)
  REBOOT_TIMEOUT_SEC    Wait time for post-OTA reboot/recovery (default: 120)
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

test -n "${GATEWAY_BASE_URL:-}" || {
  echo "GATEWAY_BASE_URL is required" >&2
  usage
  exit 2
}

ESPPORT="${ESPPORT:-/dev/ttyACM0}"
OTA_HTTP_PORT="${OTA_HTTP_PORT:-8787}"
OTA_POLL_TIMEOUT_SEC="${OTA_POLL_TIMEOUT_SEC:-180}"
REBOOT_TIMEOUT_SEC="${REBOOT_TIMEOUT_SEC:-120}"

json_get() {
  local payload="$1"
  local expression="$2"
  python3 -c 'import json,sys
payload=json.loads(sys.argv[1])
expr=sys.argv[2]
value=payload
for part in expr.split("."):
    if not part:
        continue
    value = value.get(part) if isinstance(value, dict) else None
    if value is None:
        break
if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print("")
else:
    print(value)' "$payload" "$expression"
}

json_post() {
  local url="$1"
  local body="$2"
  curl --fail --silent --show-error \
    -H 'Content-Type: application/json' \
    -X POST \
    --data "$body" \
    "$url"
}

json_get_http() {
  local url="$1"
  curl --fail --silent --show-error "$url"
}

SERVER_PID=0
cleanup() {
  if [[ "${SERVER_PID}" -gt 0 ]]; then
    kill "${SERVER_PID}" >/dev/null 2>&1 || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

OTA_URL="${OTA_URL:-}"
if [[ -z "${OTA_URL}" ]]; then
  test -n "${OTA_BIN_PATH:-}" || {
    echo "Provide OTA_URL or OTA_BIN_PATH + HOST_IP" >&2
    exit 2
  }
  test -n "${HOST_IP:-}" || {
    echo "HOST_IP is required when OTA_BIN_PATH is used" >&2
    exit 2
  }
  test -f "${OTA_BIN_PATH}" || {
    echo "OTA_BIN_PATH does not exist: ${OTA_BIN_PATH}" >&2
    exit 2
  }

  local_dir="$(cd "$(dirname "${OTA_BIN_PATH}")" && pwd)"
  local_name="$(basename "${OTA_BIN_PATH}")"
  echo "Starting local OTA HTTP server from ${local_dir} on ${HOST_IP}:${OTA_HTTP_PORT}"
  (
    cd "${local_dir}"
    python3 -m http.server "${OTA_HTTP_PORT}" --bind 0.0.0.0 >/tmp/gateway-ota-http.log 2>&1
  ) &
  SERVER_PID=$!
  sleep 2
  OTA_URL="http://${HOST_IP}:${OTA_HTTP_PORT}/${local_name}"
fi

echo "Gateway base URL: ${GATEWAY_BASE_URL}"
echo "OTA URL: ${OTA_URL}"

snapshot="$(json_get_http "${GATEWAY_BASE_URL}/api/ota")"
echo "Initial OTA snapshot: ${snapshot}"

payload='{"url":"'"${OTA_URL}"'"'
if [[ -n "${EXPECTED_VERSION:-}" ]]; then
  payload+=',"target_version":"'"${EXPECTED_VERSION}"'"'
fi
payload+='}'

accepted="$(json_post "${GATEWAY_BASE_URL}/api/ota" "${payload}")"
echo "Accepted response: ${accepted}"
request_id="$(json_get "${accepted}" "request_id")"
test -n "${request_id}" || {
  echo "request_id missing in accepted response" >&2
  exit 1
}

deadline=$(( $(date +%s) + OTA_POLL_TIMEOUT_SEC ))
result=""
while (( $(date +%s) <= deadline )); do
  snapshot="$(json_get_http "${GATEWAY_BASE_URL}/api/ota" || true)"
  if [[ -n "${snapshot}" ]]; then
    echo "OTA snapshot: ${snapshot}"
  fi

  result="$(json_get_http "${GATEWAY_BASE_URL}/api/ota/result?request_id=${request_id}" || true)"
  if [[ -n "${result}" ]]; then
    echo "OTA result: ${result}"
    ready="$(json_get "${result}" "ready")"
    if [[ "${ready}" == "true" ]]; then
      ok="$(json_get "${result}" "ok")"
      if [[ "${ok}" != "true" ]]; then
        echo "OTA failed: ${result}" >&2
        exit 1
      fi
      break
    fi
  fi

  sleep 2
done

test -n "${result}" || {
  echo "OTA result not observed before timeout" >&2
  exit 1
}

reboot_required="$(json_get "${result}" "reboot_required")"
if [[ "${reboot_required}" == "true" ]]; then
  echo "Waiting for OTA reboot/recovery..."
  reboot_deadline=$(( $(date +%s) + REBOOT_TIMEOUT_SEC ))
  while (( $(date +%s) <= reboot_deadline )); do
    post_snapshot="$(json_get_http "${GATEWAY_BASE_URL}/api/ota" || true)"
    if [[ -z "${post_snapshot}" ]]; then
      sleep 2
      continue
    fi

    echo "Post-reboot OTA snapshot: ${post_snapshot}"
    if [[ -n "${EXPECTED_VERSION:-}" ]]; then
      current_version="$(json_get "${post_snapshot}" "current_version")"
      if [[ "${current_version}" == "${EXPECTED_VERSION}" ]]; then
        echo "Observed expected version after reboot: ${current_version}"
        echo "Gateway OTA HIL smoke PASSED"
        exit 0
      fi
    else
      current_stage="$(json_get "${post_snapshot}" "stage")"
      if [[ "${current_stage}" != "failed" ]]; then
        echo "Gateway OTA HIL smoke PASSED"
        exit 0
      fi
    fi

    sleep 2
  done

  echo "Gateway did not come back with the expected OTA state before timeout" >&2
  exit 1
fi

echo "Gateway OTA HIL smoke PASSED"
