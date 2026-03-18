#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/check_ota_slot_size.sh <build_dir> [partition_csv]

Checks that the main application binary in <build_dir> fits into the ota_0 slot
defined in [partition_csv] (default: partitions.csv).
EOF
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage >&2
  exit 2
fi

BUILD_DIR="$1"
PARTITION_CSV="${2:-partitions.csv}"

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "Build directory not found: ${BUILD_DIR}" >&2
  exit 1
fi

if [[ ! -f "${PARTITION_CSV}" ]]; then
  echo "Partition table not found: ${PARTITION_CSV}" >&2
  exit 1
fi

parse_size_token() {
  local token="$1"
  token="${token//[[:space:]]/}"
  if [[ -z "${token}" ]]; then
    return 1
  fi
  if [[ "${token}" =~ ^0[xX][0-9a-fA-F]+$ ]]; then
    printf '%u\n' "$((token))"
    return 0
  fi
  if [[ "${token}" =~ ^[0-9]+[Kk]$ ]]; then
    printf '%u\n' "$(( ${token%[Kk]} * 1024 ))"
    return 0
  fi
  if [[ "${token}" =~ ^[0-9]+[Mm]$ ]]; then
    printf '%u\n' "$(( ${token%[Mm]} * 1024 * 1024 ))"
    return 0
  fi
  if [[ "${token}" =~ ^[0-9]+$ ]]; then
    printf '%u\n' "${token}"
    return 0
  fi
  return 1
}

slot_size_token="$(
  awk -F',' '
    $0 !~ /^[[:space:]]*#/ && $1 ~ /^[[:space:]]*ota_0[[:space:]]*$/ {
      gsub(/[[:space:]]/, "", $5)
      print $5
      exit
    }
  ' "${PARTITION_CSV}"
)"

if [[ -z "${slot_size_token}" ]]; then
  echo "Could not find ota_0 slot size in ${PARTITION_CSV}" >&2
  exit 1
fi

slot_size_bytes="$(parse_size_token "${slot_size_token}")"
if [[ -z "${slot_size_bytes}" ]]; then
  echo "Could not parse ota_0 slot size token: ${slot_size_token}" >&2
  exit 1
fi

app_bin_path="$(
  find "${BUILD_DIR}" -maxdepth 1 -type f -name '*.bin' \
    ! -name 'bootloader.bin' \
    ! -name 'partition-table.bin' \
    ! -name 'ota_data_initial.bin' \
    | sort \
    | head -n 1
)"

if [[ -z "${app_bin_path}" ]]; then
  echo "Could not find app binary in ${BUILD_DIR}" >&2
  exit 1
fi

app_size_bytes="$(stat -c '%s' "${app_bin_path}")"
usage_percent=$(( (app_size_bytes * 100) / slot_size_bytes ))

echo "OTA slot size check:"
echo "  build dir: ${BUILD_DIR}"
echo "  partition table: ${PARTITION_CSV}"
echo "  app binary: ${app_bin_path}"
echo "  app size: ${app_size_bytes} bytes"
echo "  ota_0 size: ${slot_size_bytes} bytes"
echo "  usage: ${usage_percent}%"

if (( app_size_bytes > slot_size_bytes )); then
  echo "ERROR: app binary exceeds ota_0 slot size" >&2
  exit 1
fi

echo "OTA slot size check PASSED"
