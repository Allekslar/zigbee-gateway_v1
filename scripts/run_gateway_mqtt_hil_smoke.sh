#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/run_gateway_mqtt_hil_smoke.sh

Semi-automated HIL smoke for a real gateway, a real MQTT broker, and one real Zigbee end device.

Environment variables:
  GW_BASE_URL         Gateway base URL (default: http://192.168.4.1)
  MQTT_HOST           MQTT broker host (required)
  MQTT_PORT           MQTT broker port (default: 1883)
  MQTT_USER           MQTT username (optional)
  MQTT_PASS           MQTT password (optional)
  MQTT_TOPIC_ROOT     MQTT topic root (default: zigbee-gateway)
  JOIN_SECONDS        Join window duration in seconds (default: 30)
  POWER_READY_SEC     Max wait for ON/OFF state publication after MQTT command (default: 30)
  POWER_RETRY_SEC     Retry cadence for MQTT ON/OFF command publish (default: 3)
  GATEWAY_READY_SEC   Max wait for gateway HTTP API readiness at startup (default: 30)
  MQTT_READY_SEC      Max wait for gateway MQTT status connected=true (default: 30)
  FORCE_REMOVE_TIMEOUT_MS  Force-remove timeout armed in gateway after remove request (default: 15000)
  HTTP_FETCH_RETRIES  Retries per HTTP JSON fetch before giving up (default: 3)
  HTTP_RETRY_SEC      Delay between transient HTTP retries (default: 1)
  POLL_SEC            HTTP poll interval in seconds (default: 1)

The script performs:
1. Verifies MQTT status via /api/network.
2. Opens a join window and waits for one new device.
3. Verifies retained MQTT publications for availability/state/telemetry of the joined device.
4. Sends ON and OFF via MQTT and verifies resulting state publication.
5. Removes the device via HTTP API and verifies retained availability=offline on the broker.

Requires:
  - curl
  - python3
  - mosquitto_pub
  - mosquitto_sub
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

GW_BASE_URL="${GW_BASE_URL:-http://192.168.4.1}"
MQTT_HOST="${MQTT_HOST:-}"
MQTT_PORT="${MQTT_PORT:-1883}"
MQTT_USER="${MQTT_USER:-}"
MQTT_PASS="${MQTT_PASS:-}"
MQTT_TOPIC_ROOT="${MQTT_TOPIC_ROOT:-zigbee-gateway}"
JOIN_SECONDS="${JOIN_SECONDS:-30}"
POWER_READY_SEC="${POWER_READY_SEC:-30}"
POWER_RETRY_SEC="${POWER_RETRY_SEC:-3}"
GATEWAY_READY_SEC="${GATEWAY_READY_SEC:-30}"
MQTT_READY_SEC="${MQTT_READY_SEC:-30}"
FORCE_REMOVE_TIMEOUT_MS="${FORCE_REMOVE_TIMEOUT_MS:-15000}"
HTTP_FETCH_RETRIES="${HTTP_FETCH_RETRIES:-3}"
HTTP_RETRY_SEC="${HTTP_RETRY_SEC:-1}"
POLL_SEC="${POLL_SEC:-1}"

if [[ -z "${MQTT_HOST}" ]]; then
  echo "MQTT_HOST is required" >&2
  exit 1
fi

command -v curl >/dev/null 2>&1
command -v python3 >/dev/null 2>&1
command -v mosquitto_pub >/dev/null 2>&1
command -v mosquitto_sub >/dev/null 2>&1

tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

mqtt_args=("-h" "${MQTT_HOST}" "-p" "${MQTT_PORT}")
if [[ -n "${MQTT_USER}" ]]; then
  mqtt_args+=("-u" "${MQTT_USER}")
fi
if [[ -n "${MQTT_PASS}" ]]; then
  mqtt_args+=("-P" "${MQTT_PASS}")
fi

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

wait_for_mqtt_ready() {
  local timeout_sec="$1"
  local out_file="$2"
  local deadline=$((SECONDS + timeout_sec))

  while (( SECONDS < deadline )); do
    if ! fetch_json "/api/network" "${out_file}"; then
      sleep "${POLL_SEC}"
      continue
    fi

    local mqtt_enabled
    local mqtt_connected
    mqtt_enabled="$(json_eval 'data.get("mqtt", {}).get("enabled", False)' "${out_file}")"
    mqtt_connected="$(json_eval 'data.get("mqtt", {}).get("connected", False)' "${out_file}")"

    if [[ "${mqtt_enabled}" == "true" && "${mqtt_connected}" == "true" ]]; then
      return 0
    fi

    sleep "${POLL_SEC}"
  done

  echo "Gateway MQTT status did not become connected within ${timeout_sec}s" >&2
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

mqtt_get_single_message() {
  local topic="$1"
  local timeout_sec="$2"
  local out_file="$3"
  mosquitto_sub "${mqtt_args[@]}" -W "${timeout_sec}" -C 1 -t "${topic}" > "${out_file}"
}

read_mqtt_state_power() {
  local short_addr="$1"
  local timeout_sec="$2"
  local out_file="$3"
  local topic="${MQTT_TOPIC_ROOT}/devices/${short_addr}/state"

  if ! mqtt_get_single_message "${topic}" "${timeout_sec}" "${out_file}"; then
    return 1
  fi

  python3 - "${out_file}" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    payload = json.load(fh)
power_on = payload.get("power_on")
if power_on is True:
    print("true")
elif power_on is False:
    print("false")
else:
    raise SystemExit(1)
PY
}

publish_mqtt_power_until_state() {
  local short_addr="$1"
  local expected="$2"
  local timeout_sec="$3"
  local topic="${MQTT_TOPIC_ROOT}/devices/${short_addr}/power/set"
  local state_file="${tmp_dir}/mqtt_state_${short_addr}.txt"
  local payload
  local deadline=$((SECONDS + timeout_sec))

  if [[ "${expected}" == "true" ]]; then
    payload='{"power_on":true}'
  else
    payload='{"power_on":false}'
  fi

  while (( SECONDS < deadline )); do
    mosquitto_pub "${mqtt_args[@]}" -t "${topic}" -m "${payload}" -q 1

    local observed=""
    if observed="$(read_mqtt_state_power "${short_addr}" "${POWER_RETRY_SEC}" "${state_file}")" &&
       [[ "${observed}" == "${expected}" ]]; then
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

devices_before_file="${tmp_dir}/devices_before.json"
devices_after_join_file="${tmp_dir}/devices_after_join.json"
devices_check_file="${tmp_dir}/devices_check.json"
network_file="${tmp_dir}/network.json"
async_file="${tmp_dir}/async.json"

if ! wait_for_gateway_ready "${GATEWAY_READY_SEC}" "${network_file}"; then
  exit 1
fi
if ! wait_for_mqtt_ready "${MQTT_READY_SEC}" "${network_file}"; then
  cat "${network_file}" >&2
  exit 1
fi
mqtt_enabled="$(json_eval 'data.get("mqtt", {}).get("enabled", False)' "${network_file}")"
mqtt_connected="$(json_eval 'data.get("mqtt", {}).get("connected", False)' "${network_file}")"
mqtt_broker="$(json_eval 'data.get("mqtt", {}).get("broker_endpoint", "")' "${network_file}")"

if [[ "${mqtt_enabled}" != "true" ]]; then
  echo "Gateway reports MQTT transport disabled" >&2
  exit 1
fi
if [[ "${mqtt_connected}" != "true" ]]; then
  echo "Gateway reports MQTT transport not connected" >&2
  cat "${network_file}" >&2
  exit 1
fi

echo "Gateway: ${GW_BASE_URL}"
echo "MQTT broker: ${mqtt_broker:-${MQTT_HOST}:${MQTT_PORT}}"

fetch_json "/api/devices" "${devices_before_file}"
initial_count="$(json_eval 'data["device_count"]' "${devices_before_file}")"
echo "Initial device_count=${initial_count}"

join_response="$(request "POST" "/api/devices/join" "{\"duration_seconds\":${JOIN_SECONDS}}")"
join_request_id="$(python3 -c 'import json,sys; print(json.loads(sys.stdin.read())["request_id"])' <<< "${join_response}")"
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

join_window_open="$(json_eval 'data.get("join_window_open", False)' "${devices_after_join_file}")"
if [[ "${join_window_open}" == "true" ]]; then
  echo "Join window is still open after first join; expected auto-close" >&2
  exit 1
fi
echo "Join window auto-close OK"

availability_topic="${MQTT_TOPIC_ROOT}/devices/${new_short_addr}/availability"
state_topic="${MQTT_TOPIC_ROOT}/devices/${new_short_addr}/state"
telemetry_topic="${MQTT_TOPIC_ROOT}/devices/${new_short_addr}/telemetry"

availability_file="${tmp_dir}/availability.txt"
state_file="${tmp_dir}/state.txt"
telemetry_file="${tmp_dir}/telemetry.txt"

mqtt_get_single_message "${availability_topic}" 10 "${availability_file}"
mqtt_get_single_message "${state_topic}" 10 "${state_file}"
mqtt_get_single_message "${telemetry_topic}" 10 "${telemetry_file}"

if [[ "$(<"${availability_file}")" != "online" ]]; then
  echo "Expected retained availability=online for joined device" >&2
  exit 1
fi
python3 - "${state_file}" "${telemetry_file}" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    state = json.load(fh)
with open(sys.argv[2], "r", encoding="utf-8") as fh:
    telemetry = json.load(fh)

assert "power_on" in state
assert "timestamp_ms" in telemetry
PY
echo "Retained MQTT publications OK"

initial_power="$(read_mqtt_state_power "${new_short_addr}" 5 "${state_file}")"
if [[ "${initial_power}" == "true" ]]; then
  first_expected="false"
  second_expected="true"
else
  first_expected="true"
  second_expected="false"
fi

if ! publish_mqtt_power_until_state "${new_short_addr}" "${first_expected}" "${POWER_READY_SEC}"; then
  echo "Did not observe MQTT state power_on=${first_expected} for short_addr=${new_short_addr}" >&2
  exit 1
fi
echo "MQTT first power transition success OK"

if ! publish_mqtt_power_until_state "${new_short_addr}" "${second_expected}" "${POWER_READY_SEC}"; then
  echo "Did not observe MQTT state power_on=${second_expected} for short_addr=${new_short_addr}" >&2
  exit 1
fi
echo "MQTT second power transition success OK"

remove_response="$(request "POST" "/api/devices/remove" "{\"short_addr\":${new_short_addr},\"force_remove\":true,\"force_remove_timeout_ms\":${FORCE_REMOVE_TIMEOUT_MS}}")"
remove_request_id="$(python3 -c 'import json,sys; print(json.loads(sys.stdin.read())["request_id"])' <<< "${remove_response}")"
wait_for_async_result "${remove_request_id}" 10 "${async_file}"
if ! wait_for_short_addr_missing "${new_short_addr}" 30 "${devices_check_file}"; then
  echo "Joined device short_addr=${new_short_addr} still present after remove" >&2
  exit 1
fi

mqtt_get_single_message "${availability_topic}" 15 "${availability_file}"
if [[ "$(<"${availability_file}")" != "offline" ]]; then
  echo "Expected retained availability=offline after remove" >&2
  exit 1
fi
echo "MQTT offline publication after remove OK"

echo
echo "Gateway MQTT HIL smoke PASSED"
