#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/run_gateway_matter_hil_smoke.sh

Semi-automated HIL smoke for Matter runtime path regressions on a real gateway
and one real Zigbee end device.

Environment variables:
  GW_BASE_URL         Gateway base URL (default: http://192.168.4.1)
  JOIN_SECONDS        Join window duration in seconds (default: 30)
  MATTER_LOOP_CYCLES  Number of ON/OFF verification loops after join (default: 2)
  POWER_READY_SEC     Max wait for each ON/OFF command completion and state reflection (default: 30)
  POWER_RETRY_SEC     Retry cadence for ON/OFF requests (default: 3)
  GATEWAY_READY_SEC   Max wait for gateway HTTP API readiness at startup (default: 30)
  FORCE_REMOVE_TIMEOUT_MS  Force-remove timeout armed in gateway after remove request (default: 15000)
  HTTP_FETCH_RETRIES  Retries per HTTP JSON fetch before giving up (default: 3)
  HTTP_REQUEST_RETRIES  Retries for HTTP POST/GET request payload operations (default: 3)
  HTTP_RETRY_SEC      Delay between transient HTTP retries (default: 1)
  POLL_SEC            HTTP poll interval in seconds (default: 1)

The script performs:
1. Waits for gateway HTTP API readiness.
2. Opens a join window and waits for one new device.
3. Verifies join-window auto-close after first join.
4. Runs bounded ON/OFF command/update loop for the joined device.
5. Removes the device and verifies it disappears.

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
MATTER_LOOP_CYCLES="${MATTER_LOOP_CYCLES:-2}"
POWER_READY_SEC="${POWER_READY_SEC:-30}"
POWER_RETRY_SEC="${POWER_RETRY_SEC:-3}"
GATEWAY_READY_SEC="${GATEWAY_READY_SEC:-30}"
FORCE_REMOVE_TIMEOUT_MS="${FORCE_REMOVE_TIMEOUT_MS:-15000}"
HTTP_FETCH_RETRIES="${HTTP_FETCH_RETRIES:-3}"
HTTP_REQUEST_RETRIES="${HTTP_REQUEST_RETRIES:-3}"
HTTP_RETRY_SEC="${HTTP_RETRY_SEC:-1}"
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

json_file_is_valid() {
  local file="$1"
  python3 - "$file" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    json.load(fh)
PY
}

fetch_json() {
  local path="$1"
  local out_file="$2"
  local attempt=1
  local tmp_file="${out_file}.tmp"

  while (( attempt <= HTTP_FETCH_RETRIES )); do
    if request "GET" "${path}" > "${tmp_file}" 2>/dev/null && [[ -s "${tmp_file}" ]] && json_file_is_valid "${tmp_file}" 2>/dev/null; then
      mv "${tmp_file}" "${out_file}"
      return 0
    fi
    rm -f "${tmp_file}"
    if (( attempt < HTTP_FETCH_RETRIES )); then
      sleep "${HTTP_RETRY_SEC}"
    fi
    attempt=$((attempt + 1))
  done

  echo "Failed to fetch valid JSON from ${GW_BASE_URL}${path} after ${HTTP_FETCH_RETRIES} attempts" >&2
  return 1
}

request_with_retries() {
  local method="$1"
  local path="$2"
  local body="${3:-}"
  local out_file="$4"
  local attempt=1
  local tmp_file="${out_file}.tmp"

  while (( attempt <= HTTP_REQUEST_RETRIES )); do
    if [[ -n "${body}" ]]; then
      if curl -fsS -X "${method}" -H "Content-Type: application/json" --data "${body}" \
          "${GW_BASE_URL}${path}" > "${tmp_file}" 2>/dev/null; then
        mv "${tmp_file}" "${out_file}"
        return 0
      fi
    else
      if curl -fsS -X "${method}" "${GW_BASE_URL}${path}" > "${tmp_file}" 2>/dev/null; then
        mv "${tmp_file}" "${out_file}"
        return 0
      fi
    fi

    rm -f "${tmp_file}"
    if (( attempt < HTTP_REQUEST_RETRIES )); then
      sleep "${HTTP_RETRY_SEC}"
    fi
    attempt=$((attempt + 1))
  done

  echo "Failed request ${method} ${GW_BASE_URL}${path} after ${HTTP_REQUEST_RETRIES} attempts" >&2
  return 1
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

wait_for_gateway_ready() {
  local timeout_sec="$1"
  local out_file="$2"
  local deadline=$((SECONDS + timeout_sec))

  while (( SECONDS < deadline )); do
    if fetch_json "/api/network" "${out_file}"; then
      return 0
    fi
    sleep "${POLL_SEC}"
  done

  echo "Gateway HTTP API did not become ready within ${timeout_sec}s" >&2
  return 1
}

wait_for_async_result() {
  local request_id="$1"
  local timeout_sec="$2"
  local out_file="$3"
  local deadline=$((SECONDS + timeout_sec))
  while (( SECONDS < deadline )); do
    if ! fetch_json "/api/network/result?request_id=${request_id}" "${out_file}"; then
      sleep "${POLL_SEC}"
      continue
    fi
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
    if ! fetch_json "/api/devices" "${out_file}"; then
      sleep "${POLL_SEC}"
      continue
    fi
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
    if ! fetch_json "/api/devices" "${out_file}"; then
      sleep "${POLL_SEC}"
      continue
    fi
    local present
    present="$(python3 - "${out_file}" "${short_addr}" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    data = json.load(fh)
short_addr = int(sys.argv[2])

for device in data.get("devices", []):
    if device.get("short_addr") == short_addr:
        print("true")
        sys.exit(0)
print("false")
PY
)"
    if [[ "${present}" != "true" ]]; then
      return 0
    fi
    sleep "${POLL_SEC}"
  done
  return 1
}

wait_for_join_window_closed() {
  local timeout_sec="$1"
  local out_file="$2"
  local deadline=$((SECONDS + timeout_sec))

  while (( SECONDS < deadline )); do
    if ! fetch_json "/api/devices" "${out_file}"; then
      sleep "${POLL_SEC}"
      continue
    fi
    local join_window_open
    join_window_open="$(json_eval 'data.get("join_window_open", False)' "${out_file}")"
    if [[ "${join_window_open}" != "true" ]]; then
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

wait_for_power_state() {
  local short_addr="$1"
  local expected="$2"
  local timeout_sec="$3"
  local out_file="$4"
  local deadline=$((SECONDS + timeout_sec))
  while (( SECONDS < deadline )); do
    if ! fetch_json "/api/devices" "${out_file}"; then
      sleep "${POLL_SEC}"
      continue
    fi
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

request_power_state() {
  local short_addr="$1"
  local desired="$2"
  local out_file="$3"
  request_with_retries "POST" "/api/devices/power" \
    "{\"short_addr\":${short_addr},\"power_on\":${desired}}" "${out_file}" > /dev/null
}

wait_for_command_success_after_revision() {
  local previous_revision="$1"
  local timeout_sec="$2"
  local out_file="$3"
  local deadline=$((SECONDS + timeout_sec))

  while (( SECONDS < deadline )); do
    if ! fetch_json "/api/config" "${out_file}"; then
      sleep "${POLL_SEC}"
      continue
    fi
    local revision
    local status
    revision="$(json_eval 'data["revision"]' "${out_file}")"
    status="$(json_eval 'data["last_command_status"]' "${out_file}")"
    if [[ "${revision}" -gt "${previous_revision}" && "${status}" == "1" ]]; then
      return 0
    fi
    sleep "${POLL_SEC}"
  done

  return 1
}

ensure_power_command_success_with_retries() {
  local short_addr="$1"
  local desired="$2"
  local timeout_sec="$3"
  local out_file="$4"
  local deadline=$((SECONDS + timeout_sec))
  local attempt_window="${POWER_RETRY_SEC}"
  local request_file="${tmp_dir}/request_power.json"

  if (( attempt_window < 1 )); then
    attempt_window=1
  fi

  while (( SECONDS < deadline )); do
    if ! fetch_json "/api/config" "${out_file}"; then
      sleep "${POLL_SEC}"
      continue
    fi
    local before_revision
    before_revision="$(json_eval 'data["revision"]' "${out_file}")"
    if ! request_power_state "${short_addr}" "${desired}" "${request_file}"; then
      sleep "${POLL_SEC}"
      continue
    fi
    if wait_for_command_success_after_revision "${before_revision}" "${attempt_window}" "${out_file}"; then
      return 0
    fi
  done

  return 1
}

prompt() {
  local message="$1"
  printf '\n%s\n' "${message}"
  read -r -p "> Press Enter to continue..."
}

network_file="${tmp_dir}/network.json"
devices_before_file="${tmp_dir}/devices_before.json"
devices_after_join_file="${tmp_dir}/devices_after_join.json"
devices_check_file="${tmp_dir}/devices_check.json"
async_file="${tmp_dir}/async.json"
config_file="${tmp_dir}/config.json"

wait_for_gateway_ready "${GATEWAY_READY_SEC}" "${network_file}"

fetch_json "/api/devices" "${devices_before_file}"
initial_count="$(json_eval 'data["device_count"]' "${devices_before_file}")"
echo "Gateway: ${GW_BASE_URL}"
echo "Initial device_count=${initial_count}"

join_request_file="${tmp_dir}/request_join.json"
if ! request_with_retries "POST" "/api/devices/join" "{\"duration_seconds\":${JOIN_SECONDS}}" "${join_request_file}"; then
  echo "Failed to open join window" >&2
  exit 1
fi
join_request_id="$(python3 - "${join_request_file}" <<'PY'
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fh:
    data = json.load(fh)
print(data["request_id"])
PY
)"
wait_for_async_result "${join_request_id}" 10 "${async_file}"
echo "Join window opened for ${JOIN_SECONDS}s"

prompt "Step 1/3: put exactly one new Zigbee device into pairing mode now."

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

if ! wait_for_join_window_closed 10 "${devices_check_file}"; then
  echo "Join window did not auto-close after first join within 10s" >&2
  exit 1
fi
echo "Join window auto-close OK"

for ((cycle = 1; cycle <= MATTER_LOOP_CYCLES; ++cycle)); do
  if ! ensure_power_command_success_with_retries "${new_short_addr}" "true" "${POWER_READY_SEC}" "${config_file}"; then
    echo "Cycle ${cycle}: ON command did not complete successfully within ${POWER_READY_SEC}s" >&2
    exit 1
  fi
  if ! wait_for_power_state "${new_short_addr}" "true" "${POWER_READY_SEC}" "${devices_check_file}"; then
    echo "Cycle ${cycle}: did not observe power_on=true for short_addr=${new_short_addr}" >&2
    exit 1
  fi

  if ! ensure_power_command_success_with_retries "${new_short_addr}" "false" "${POWER_READY_SEC}" "${config_file}"; then
    echo "Cycle ${cycle}: OFF command did not complete successfully within ${POWER_READY_SEC}s" >&2
    exit 1
  fi
  if ! wait_for_power_state "${new_short_addr}" "false" "${POWER_READY_SEC}" "${devices_check_file}"; then
    echo "Cycle ${cycle}: did not observe power_on=false for short_addr=${new_short_addr}" >&2
    exit 1
  fi

  echo "Matter loop cycle ${cycle}/${MATTER_LOOP_CYCLES} OK"
done

remove_request_file="${tmp_dir}/request_remove.json"
if ! request_with_retries "POST" "/api/devices/remove" \
    "{\"short_addr\":${new_short_addr},\"force_remove\":true,\"force_remove_timeout_ms\":${FORCE_REMOVE_TIMEOUT_MS}}" \
    "${remove_request_file}"; then
  echo "Failed to submit remove request for short_addr=${new_short_addr}" >&2
  exit 1
fi
remove_request_id="$(python3 - "${remove_request_file}" <<'PY'
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fh:
    data = json.load(fh)
print(data["request_id"])
PY
)"
wait_for_async_result "${remove_request_id}" 20 "${async_file}"

if ! wait_for_short_addr_missing "${new_short_addr}" 30 "${devices_check_file}"; then
  echo "Joined device short_addr=${new_short_addr} still present after remove" >&2
  exit 1
fi
echo "Remove/offline transition OK"

echo
echo "Gateway Matter runtime smoke PASSED"
