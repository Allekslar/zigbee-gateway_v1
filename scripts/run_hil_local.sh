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

  echo "=== Running ${name} (timeout ${timeout_sec}s) on ${ESPPORT} ==="
  set +e
  timeout "${timeout_sec}" \
    script -q -e -c "idf.py -C test/target -B build-target-tests -p ${ESPPORT} flash monitor" "${log_path}" \
    >/dev/null 2>&1
  local status=$?
  set -e

  if [[ "${status}" -ne 0 && "${status}" -ne 124 ]]; then
    echo "${name}: flash/monitor failed with status=${status}" >&2
    tail -n 200 "${log_path}" || true
    exit "${status}"
  fi

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
