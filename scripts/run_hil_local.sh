#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/run_hil_local.sh [smoke|full|both]

Runs local HIL tests with the same monitor strategy as CI:
- smoke: timeout 180s
- full:  timeout 300s
- both:  run smoke then full (default)

Environment variables:
  ESPPORT            Serial port (default: /dev/ttyACM0)
  IDF_EXPORT_SCRIPT  Optional explicit path to ESP-IDF export.sh
EOF
}

mode="${1:-both}"
if [[ "${mode}" == "-h" || "${mode}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ "${mode}" != "smoke" && "${mode}" != "full" && "${mode}" != "both" ]]; then
  echo "Invalid mode: ${mode}" >&2
  usage
  exit 2
fi

ESPPORT="${ESPPORT:-/dev/ttyACM0}"

ensure_idf_env() {
  if command -v idf.py >/dev/null 2>&1; then
    return
  fi

  local found=""
  local candidates=(
    "${IDF_EXPORT_SCRIPT:-}"
    "/opt/esp/idf/export.sh"
    "/opt/esp/esp-idf/export.sh"
    "${HOME}/esp/esp-idf-v5.5.2/export.sh"
    "${HOME}/esp/esp-idf/export.sh"
  )

  for candidate in "${candidates[@]}"; do
    if [[ -n "${candidate}" && -f "${candidate}" ]]; then
      # shellcheck disable=SC1090
      . "${candidate}" >/dev/null 2>&1
      found="${candidate}"
      break
    fi
  done

  if [[ -z "${found}" ]]; then
    echo "ESP-IDF export.sh not found and idf.py is not in PATH." >&2
    echo "Set IDF_EXPORT_SCRIPT or source ESP-IDF before running." >&2
    exit 1
  fi
}

run_case() {
  local name="$1"
  local timeout_sec="$2"
  local log_path="hil-target-hal-${name}.local.log"
  local summary_regex="[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored"
  local status=0
  local parse_unity_from_log=1
  local heartbeat_pid=0
  local tty_state=""
  if [[ -t 0 ]]; then
    tty_state="$(stty -g 2>/dev/null || true)"
  fi

  echo "=== Running ${name} (timeout ${timeout_sec}s) on ${ESPPORT} ==="
  rm -f "${log_path}"

  start_heartbeat() {
    (
      while true; do
        sleep 10
        echo "[${name}] running flash+monitor... (${timeout_sec}s timeout)"
      done
    ) &
    heartbeat_pid=$!
  }

  stop_heartbeat() {
    if [[ "${heartbeat_pid}" -gt 0 ]]; then
      kill "${heartbeat_pid}" >/dev/null 2>&1 || true
      wait "${heartbeat_pid}" 2>/dev/null || true
    fi
  }

  restore_terminal() {
    if [[ ! -t 0 ]]; then
      return
    fi
    if [[ -n "${tty_state}" ]]; then
      stty "${tty_state}" 2>/dev/null || stty sane 2>/dev/null || true
    else
      stty sane 2>/dev/null || true
    fi
  }

  echo "--- Starting flash+monitor (timeout ${timeout_sec}s)..."
  if [[ -t 0 ]]; then
    # Interactive mode with live logs and auto-stop after Unity summary.
    parse_unity_from_log=1
    rm -f "${log_path}"
    touch "${log_path}"

    set +e
    idf.py -C test/target -B build-target-tests -p "${ESPPORT}" flash monitor \
      2>&1 | tee "${log_path}" &
    local monitor_pid=$!

    (
      # Stop after either PASS summary or FAIL summary appears.
      tail -n 0 -F "${log_path}" | grep -m1 -E "[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored"
    ) >/dev/null 2>&1 &
    local watcher_pid=$!

    (
      sleep "${timeout_sec}"
      kill "${monitor_pid}" >/dev/null 2>&1 || true
    ) &
    local timer_pid=$!

    wait "${watcher_pid}"
    local watcher_status=$?

    kill "${timer_pid}" >/dev/null 2>&1 || true
    wait "${timer_pid}" 2>/dev/null || true

    kill "${monitor_pid}" >/dev/null 2>&1 || true
    wait "${monitor_pid}"
    local monitor_status=$?

    # If watcher timed out/not found summary, preserve monitor status.
    if [[ "${watcher_status}" -ne 0 ]]; then
      status="${monitor_status}"
    else
      status=0
    fi
    restore_terminal
    set -e
  else
    start_heartbeat
    rm -f "${log_path}"
    touch "${log_path}"

    set +e
    script -q -e -c "idf.py -C test/target -B build-target-tests -p ${ESPPORT} flash monitor" /dev/null \
      > "${log_path}" 2>&1 &
    local monitor_pid=$!

    (
      tail -n 0 -F "${log_path}" | grep -m1 -E "${summary_regex}"
    ) >/dev/null 2>&1 &
    local watcher_pid=$!

    (
      sleep "${timeout_sec}"
      kill "${monitor_pid}" >/dev/null 2>&1 || true
    ) &
    local timer_pid=$!

    wait "${watcher_pid}"
    local watcher_status=$?

    kill "${timer_pid}" >/dev/null 2>&1 || true
    wait "${timer_pid}" 2>/dev/null || true

    kill "${monitor_pid}" >/dev/null 2>&1 || true
    wait "${monitor_pid}"
    local monitor_status=$?

    if [[ "${watcher_status}" -ne 0 ]]; then
      status="${monitor_status}"
    else
      status=0
    fi
    set -e
    stop_heartbeat
  fi

  if [[ "${status}" -ne 0 && "${status}" -ne 124 && "${status}" -ne 143 ]]; then
    echo "${name}: flash/monitor failed with status=${status}" >&2
    tail -n 200 "${log_path}" || true
    exit "${status}"
  fi

  if [[ "${parse_unity_from_log}" -eq 1 ]]; then
    if ! grep -Eq "[0-9]+ Tests 0 Failures 0 Ignored" "${log_path}"; then
      echo "${name}: Unity summary indicates failure or not found" >&2
      tail -n 200 "${log_path}" || true
      exit 1
    fi

    if ! grep -q "OK" "${log_path}"; then
      echo "${name}: Unity OK marker not found" >&2
      tail -n 200 "${log_path}" || true
      exit 1
    fi
    echo "${name}: PASS (log: ${log_path})"
  fi
}

ensure_idf_env
command -v script >/dev/null 2>&1
command -v python3 >/dev/null 2>&1
command -v idf.py >/dev/null 2>&1
test -n "${ESPPORT}"
test -e "${ESPPORT}"

case "${mode}" in
  smoke)
    run_case "smoke" 180
    ;;
  full)
    run_case "full" 300
    ;;
  both)
    run_case "smoke" 180
    run_case "full" 300
    ;;
esac

echo "All requested HIL runs completed."
