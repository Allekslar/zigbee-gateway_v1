#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/run_matter_phase3_checks.sh

Runs the focused local Phase 3 (Matter) regression gate:
1. Architecture invariants
2. Host Matter regression bundle
3. HAL platform shim integration test

Environment variables:
  BUILD_HOST_DIR         Host build directory (default: build-host)
  BUILD_INTEGRATION_DIR  Integration build directory (default: build-integration)
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_HOST_DIR="${BUILD_HOST_DIR:-build-host}"
BUILD_INTEGRATION_DIR="${BUILD_INTEGRATION_DIR:-build-integration}"

run_step() {
  local title="$1"
  shift
  echo
  echo "==> ${title}"
  "$@"
}

MATTER_REGEX='test_matter_(device_map|bridge|runtime_feed|snapshot_semantics|runtime_attach_contract|runtime_robustness|runtime_command_loop)'

cd "${ROOT_DIR}"

run_step "Architecture invariants" \
  bash ./check_arch_invariants.sh

run_step "Configure host tests" \
  cmake -S test/host -B "${BUILD_HOST_DIR}"

run_step "Build host tests" \
  cmake --build "${BUILD_HOST_DIR}" --parallel

run_step "Run Matter host regression bundle" \
  ctest --test-dir "${BUILD_HOST_DIR}" --output-on-failure -R "${MATTER_REGEX}"

run_step "Configure integration tests" \
  cmake -S test/integration -B "${BUILD_INTEGRATION_DIR}"

run_step "Build HAL platform shim integration test" \
  cmake --build "${BUILD_INTEGRATION_DIR}" --target test_integration_hal_platform_shims --parallel

run_step "Run HAL platform shim integration test" \
  ctest --test-dir "${BUILD_INTEGRATION_DIR}" --output-on-failure -R test_integration_hal_platform_shims

echo
echo "Matter Phase 3 checks PASSED"
