#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/run_gateway_zigbee_smoke.sh

Semi-automated HIL smoke for a real gateway and real Zigbee end devices.

Environment variables:
  GW_BASE_URL        Gateway base URL (default: http://192.168.4.1)
  JOIN_SECONDS       Join window duration in seconds (default: 30)
  POWER_READY_SEC    Max wait for power state update after join (default: 12)
  REBOOT_READY_SEC   Max wait for gateway after manual reboot (default: 90)
  POLL_SEC           Poll interval in seconds (default: 1)

The script performs:
1. Device snapshot before reboot.
2. Manual gateway reboot + restore verification.
3. Open join window and wait for one new device.
4. Verify ON and OFF updates for the joined device.
5. Remove the joined device and verify it disappears.

Requires:
  - curl
  - python3
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

GW_BASE_URL="${GW_BASE_URL:-http://192.168.4.1}"
JOIN_SECONDS="${JOIN_SECONDS:-30}"
POWER_READY_SEC="${POWER_READY_SEC:-12}"
REBOOT_READY_SEC="${REBOOT_READY_SEC:-90}"
POLL_SEC="${POLL_SEC:-1}"

command -v curl >/dev/null 2>&1
command -v python3 >/dev/null 2>&1

tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

request() {
  local method="$1"
  local path="$2"
  local body="${3:-}"
  if [[ -n "${body}" ]]; then
    curl -fsS -X "${method}" \
      -H "Content-Type: application/json" \
      --data "${body}" \
      "${GW_BASE_URL}${path}"
  else
    curl -fsS -X "${method}" "${GW_BASE_URL}${path}"
  fi
}

json_eval() {
  local expr="$1"
  local file="$2"
  python3 - "$expr" "$file" <<'PY'
import json
import sys

expr = sys.argv[1]
path = sys.argv[2]
with open(path, "r", encoding="utf-8") as fh:
    data = json.load(fh)

globals_dict = {"data": data}
result = eval(expr, {"__builtins__": {}}, globals_dict)
if isinstance(result, bool):
    print("true" if result else "false")
elif result is None:
    print("")
else:
    print(result)
PY
}

fetch_json() {
  local path="$1"
  local out_file="$2"
  request "GET" "${path}" > "${out_file}"
}

wait_for_http_ok() {
  local deadline=$((SECONDS + REBOOT_READY_SEC))
  while (( SECONDS < deadline )); do
    if curl -fsS "${GW_BASE_URL}/api/devices" >/dev/null 2>&1; then
      return 0
    fi
    sleep "${POLL_SEC}"
  done
  return 1
}

wait_for_async_result() {
  local request_id="$1"
  local timeout_sec="$2"
  local out_file="$3"
  local deadline=$((SECONDS + timeout_sec))
  while (( SECONDS < deadline )); do
    fetch_json "/api/network/result?request_id=${request_id}" "${out_file}"
    local ready
    ready="$(json_eval 'data.get("ready", False)' "${out_file}")"
    if [[ "${ready}" == "true" ]]; then
      local ok
      ok="$(json_eval 'data.get("ok", False)' "${out_file}")"
      if [[ "${ok}" != "true" ]]; then
        echo "Async request ${request_id} failed:" >&2
        cat "${out_file}" >&2
        return 1
      fi
      return 0
    fi
    sleep "${POLL_SEC}"
  done

  echo "Timed out waiting for async request ${request_id}" >&2
  return 1
}

wait_for_device_count() {
  local expected="$1"
  local timeout_sec="$2"
  local out_file="$3"
  local deadline=$((SECONDS + timeout_sec))
  while (( SECONDS < deadline )); do
    fetch_json "/api/devices" "${out_file}"
    local count
    count="$(json_eval 'data["device_count"]' "${out_file}")"
    if [[ "${count}" == "${expected}" ]]; then
      return 0
    fi
    sleep "${POLL_SEC}"
  done
  return 1
}

wait_for_short_addr_missing() {
  local short_addr="$1"
  local timeout_sec="$2"
  local out_file="$3"
  local deadline=$((SECONDS + timeout_sec))
  while (( SECONDS < deadline )); do
    fetch_json "/api/devices" "${out_file}"
    local present
    present="$(json_eval "any(device.get('short_addr') == ${short_addr} for device in data.get('devices', []))" "${out_file}")"
    if [[ "${present}" != "true" ]]; then
      return 0
    fi
    sleep "${POLL_SEC}"
  done
  return 1
}

wait_for_power_state() {
  local short_addr="$1"
  local expected="$2"
  local timeout_sec="$3"
  local out_file="$4"
  local deadline=$((SECONDS + timeout_sec))
  while (( SECONDS < deadline )); do
    fetch_json "/api/devices" "${out_file}"
    local state
    state="$(python3 - "${out_file}" "${short_addr}" <<'PY'
import json
import sys

path = sys.argv[1]
short_addr = int(sys.argv[2])
with open(path, "r", encoding="utf-8") as fh:
    data = json.load(fh)

for device in data.get("devices", []):
    if device.get("short_addr") == short_addr:
        value = device.get("power_on")
        if value is True:
            print("true")
        elif value is False:
            print("false")
        else:
            print("")
        sys.exit(0)

print("")
PY
)"
    if [[ "${state}" == "${expected}" ]]; then
      return 0
    fi
    sleep "${POLL_SEC}"
  done
  return 1
}

extract_new_short_addr() {
  local before_file="$1"
  local after_file="$2"
  python3 - "$before_file" "$after_file" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    before = json.load(fh)
with open(sys.argv[2], "r", encoding="utf-8") as fh:
    after = json.load(fh)

before_addrs = {device.get("short_addr") for device in before.get("devices", [])}
for device in after.get("devices", []):
    short_addr = device.get("short_addr")
    if short_addr not in before_addrs:
        print(short_addr)
        sys.exit(0)
sys.exit(1)
PY
}

prompt() {
  local message="$1"
  printf '\n%s\n' "${message}"
  read -r -p "> Press Enter to continue..."
}

devices_before_file="${tmp_dir}/devices_before.json"
devices_after_reboot_file="${tmp_dir}/devices_after_reboot.json"
devices_after_join_file="${tmp_dir}/devices_after_join.json"
devices_check_file="${tmp_dir}/devices_check.json"
async_file="${tmp_dir}/async.json"

fetch_json "/api/devices" "${devices_before_file}"
initial_count="$(json_eval 'data["device_count"]' "${devices_before_file}")"

echo "Initial device_count=${initial_count}"
echo "Gateway: ${GW_BASE_URL}"

prompt "Step 1/5: reboot the gateway now. Wait until it starts again, then continue."

if ! wait_for_http_ok; then
  echo "Gateway did not come back within ${REBOOT_READY_SEC}s" >&2
  exit 1
fi

fetch_json "/api/devices" "${devices_after_reboot_file}"
restored_count="$(json_eval 'data["device_count"]' "${devices_after_reboot_file}")"
if [[ "${restored_count}" != "${initial_count}" ]]; then
  echo "Reboot restore mismatch: expected device_count=${initial_count}, got ${restored_count}" >&2
  exit 1
fi
echo "Reboot restore OK: device_count=${restored_count}"

join_response="$(request "POST" "/api/devices/join" "{\"duration_seconds\":${JOIN_SECONDS}}")"
join_request_id="$(python3 -c 'import json,sys; print(json.loads(sys.stdin.read())["request_id"])' <<< "${join_response}")"
wait_for_async_result "${join_request_id}" 10 "${async_file}"
echo "Join window opened for ${JOIN_SECONDS}s"

prompt "Step 2/5: put exactly one new Zigbee device into pairing mode now."

expected_join_count=$((initial_count + 1))
if ! wait_for_device_count "${expected_join_count}" "${JOIN_SECONDS}" "${devices_after_join_file}"; then
  echo "Did not observe device_count=${expected_join_count} within join window" >&2
  exit 1
fi

new_short_addr="$(extract_new_short_addr "${devices_before_file}" "${devices_after_join_file}")" || {
  echo "Failed to determine newly joined device short_addr" >&2
  exit 1
}
echo "Joined device short_addr=${new_short_addr}"

join_window_open="$(json_eval 'data.get("join_window_open", False)' "${devices_after_join_file}")"
if [[ "${join_window_open}" == "true" ]]; then
  echo "Join window is still open after first join; expected auto-close" >&2
  exit 1
fi
echo "Join window auto-close OK"

request "POST" "/api/devices/power" "{\"short_addr\":${new_short_addr},\"power_on\":true}" > /dev/null
if ! wait_for_power_state "${new_short_addr}" "true" "${POWER_READY_SEC}" "${devices_check_file}"; then
  echo "Joined device did not report power_on=true within ${POWER_READY_SEC}s" >&2
  exit 1
fi
echo "Immediate ON OK"

request "POST" "/api/devices/power" "{\"short_addr\":${new_short_addr},\"power_on\":false}" > /dev/null
if ! wait_for_power_state "${new_short_addr}" "false" "${POWER_READY_SEC}" "${devices_check_file}"; then
  echo "Joined device did not report power_on=false within ${POWER_READY_SEC}s" >&2
  exit 1
fi
echo "Immediate OFF OK"

remove_response="$(request "POST" "/api/devices/remove" "{\"short_addr\":${new_short_addr}}")"
remove_request_id="$(python3 -c 'import json,sys; print(json.loads(sys.stdin.read())["request_id"])' <<< "${remove_response}")"
wait_for_async_result "${remove_request_id}" 10 "${async_file}"

if ! wait_for_short_addr_missing "${new_short_addr}" 20 "${devices_check_file}"; then
  echo "Joined device short_addr=${new_short_addr} still present after remove" >&2
  exit 1
fi
echo "Remove OK"

echo
echo "Gateway Zigbee smoke PASSED"
