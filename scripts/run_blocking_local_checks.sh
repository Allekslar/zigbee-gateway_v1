#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/run_blocking_local_checks.sh [--strict] [--skip-target]

Runs the blocking local verification bundle for architecture-sensitive changes:
1. Architecture invariants
2. Host unit tests
3. Required migration smoke
4. Target HAL test firmware build
5. OTA slot size guard for target HAL test firmware

Options:
  --strict       Treat low-severity architecture findings as blocking too.
  --skip-target  Skip the target HAL firmware build.

Environment variables:
  IDF_EXPORT_SCRIPT  Optional explicit path to ESP-IDF export.sh
  BUILD_HOST_DIR     Host build directory (default: build-host)
  TARGET_BUILD_DIR   Target build directory (default: build-target-tests)
EOF
}

strict_mode=0
skip_target=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --strict)
      strict_mode=1
      ;;
    --skip-target)
      skip_target=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
  shift
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_HOST_DIR="${BUILD_HOST_DIR:-build-host}"
TARGET_BUILD_DIR="${TARGET_BUILD_DIR:-build-target-tests}"

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

run_step() {
  local title="$1"
  shift
  echo
  echo "==> ${title}"
  "$@"
}

cd "${ROOT_DIR}"

if [[ "${strict_mode}" -eq 1 ]]; then
  run_step "Architecture invariants (strict)" \
    env ARCH_BLOCKING_SEVERITIES=high,medium,low bash ./check_arch_invariants.sh
else
  run_step "Architecture invariants" \
    bash ./check_arch_invariants.sh
fi

run_step "Configure host tests" \
  cmake -S test/host -B "${BUILD_HOST_DIR}"

run_step "Build host tests" \
  cmake --build "${BUILD_HOST_DIR}" --parallel

run_step "Run host test suite" \
  ctest --test-dir "${BUILD_HOST_DIR}" --output-on-failure

run_step "Run migration smoke" \
  ctest --test-dir "${BUILD_HOST_DIR}" --output-on-failure -R test_config_manager_migration

if [[ "${skip_target}" -eq 0 ]]; then
  ensure_idf_env
  run_step "Build target HAL test firmware" \
    idf.py -C test/target -B "${TARGET_BUILD_DIR}" build

  run_step "Check OTA slot size (target test firmware)" \
    bash ./scripts/check_ota_slot_size.sh "${TARGET_BUILD_DIR}" "test/target/partitions.csv"
else
  echo
  echo "==> Skipping target HAL test firmware build"
fi

echo
echo "Blocking local checks PASSED"
