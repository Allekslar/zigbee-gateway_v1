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

fetch_json() {
  local path="$1"
  local out_file="$2"
  request "GET" "${path}" > "${out_file}"
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

wait_for_mqtt_state_payload() {
  local short_addr="$1"
  local expected="$2"
  local timeout_sec="$3"
  local topic="${MQTT_TOPIC_ROOT}/devices/${short_addr}/state"
  local deadline=$((SECONDS + timeout_sec))
  local out_file="${tmp_dir}/mqtt_state_${short_addr}.txt"

  while (( SECONDS < deadline )); do
    if mqtt_get_single_message "${topic}" "${POWER_RETRY_SEC}" "${out_file}"; then
      if python3 - "${out_file}" "${expected}" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    payload = json.load(fh)
expected = sys.argv[2] == "true"
sys.exit(0 if payload.get("power_on") is expected else 1)
PY
      then
        return 0
      fi
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

fetch_json "/api/network" "${network_file}"
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

mosquitto_pub "${mqtt_args[@]}" -t "${MQTT_TOPIC_ROOT}/devices/${new_short_addr}/power/set" -m '{"power_on":true}' -q 1
if ! wait_for_mqtt_state_payload "${new_short_addr}" "true" "${POWER_READY_SEC}"; then
  echo "Did not observe MQTT state power_on=true for short_addr=${new_short_addr}" >&2
  exit 1
fi
echo "MQTT ON command success OK"

mosquitto_pub "${mqtt_args[@]}" -t "${MQTT_TOPIC_ROOT}/devices/${new_short_addr}/power/set" -m '{"power_on":false}' -q 1
if ! wait_for_mqtt_state_payload "${new_short_addr}" "false" "${POWER_READY_SEC}"; then
  echo "Did not observe MQTT state power_on=false for short_addr=${new_short_addr}" >&2
  exit 1
fi
echo "MQTT OFF command success OK"

remove_response="$(request "POST" "/api/devices/remove" "{\"short_addr\":${new_short_addr}}")"
remove_request_id="$(python3 -c 'import json,sys; print(json.loads(sys.stdin.read())["request_id"])' <<< "${remove_response}")"
wait_for_async_result "${remove_request_id}" 10 "${async_file}"
if ! wait_for_short_addr_missing "${new_short_addr}" 20 "${devices_check_file}"; then
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
