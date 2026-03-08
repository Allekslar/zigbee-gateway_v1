#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT_DIR}"

MATRIX_FILE="${ARCH_MATRIX_FILE:-${ROOT_DIR}/ARCH_COMPLIANCE_MATRIX.md}"
EXCEPTIONS_FILE="${ARCH_EXCEPTIONS_FILE:-${ROOT_DIR}/ADR_EXCEPTIONS.md}"
BLOCKING_SEVERITIES="${ARCH_BLOCKING_SEVERITIES:-high,medium}"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

declare -a EX_RULES=()
declare -a EX_PATHS=()
declare -a EX_EXPIRES=()

HIGH_COUNT=0
MEDIUM_COUNT=0
LOW_COUNT=0
SUPPRESSED_COUNT=0

print_banner() {
    echo "[arch-gate] ${1}"
}

severity_inc() {
    case "${1}" in
        high) HIGH_COUNT=$((HIGH_COUNT + 1)) ;;
        medium) MEDIUM_COUNT=$((MEDIUM_COUNT + 1)) ;;
        low) LOW_COUNT=$((LOW_COUNT + 1)) ;;
    esac
}

is_blocking_severity() {
    local severity="${1}"
    [[ ",${BLOCKING_SEVERITIES}," == *",${severity},"* ]]
}

load_exceptions() {
    if [[ ! -f "${EXCEPTIONS_FILE}" ]]; then
        print_banner "WARN: exceptions file is missing: ${EXCEPTIONS_FILE}"
        return
    fi

    while IFS= read -r line; do
        [[ "${line}" == *"ARCH_EXCEPTION:"* ]] || continue
        [[ "${line}" == *"STATUS=active"* ]] || continue

        local rule path expires
        rule="$(printf '%s\n' "${line}" | sed -n 's/.*RULE=\([^[:space:]]*\).*/\1/p')"
        path="$(printf '%s\n' "${line}" | sed -n 's/.*PATH=\([^[:space:]]*\).*/\1/p')"
        expires="$(printf '%s\n' "${line}" | sed -n 's/.*EXPIRES=\([^[:space:]]*\).*/\1/p')"

        [[ -n "${rule}" ]] || continue
        [[ -n "${path}" ]] || path=".*"
        [[ -n "${expires}" ]] || expires="9999-12-31"

        EX_RULES+=("${rule}")
        EX_PATHS+=("${path}")
        EX_EXPIRES+=("${expires}")
    done < "${EXCEPTIONS_FILE}"
}

exception_active_for() {
    local rule_id="${1}"
    local path="${2}"
    local today
    today="$(date +%F)"

    for i in "${!EX_RULES[@]}"; do
        if [[ "${EX_RULES[$i]}" != "${rule_id}" ]]; then
            continue
        fi
        if [[ "${today}" > "${EX_EXPIRES[$i]}" ]]; then
            continue
        fi
        if [[ "${path}" =~ ${EX_PATHS[$i]} ]]; then
            return 0
        fi
    done

    return 1
}

report_violation() {
    local rule_id="${1}"
    local severity="${2}"
    local path="${3}"
    local message="${4}"
    local matches_file="${5}"

    if exception_active_for "${rule_id}" "${path}"; then
        SUPPRESSED_COUNT=$((SUPPRESSED_COUNT + 1))
        print_banner "SUPPRESSED ${severity^^} ${rule_id}: ${message} (${path})"
        return
    fi

    severity_inc "${severity}"
    print_banner "VIOLATION ${severity^^} ${rule_id}: ${message} (${path})"
    if [[ -f "${matches_file}" ]]; then
        sed 's/^/[arch-gate]   /' "${matches_file}"
    fi
}

check_absent() {
    local rule_id="${1}"
    local severity="${2}"
    local path="${3}"
    local pattern="${4}"
    local message="${5}"
    local matches_file="${TMP_DIR}/${rule_id}.txt"

    if grep -E -n -r -- "${pattern}" "${path}" > "${matches_file}" 2>/dev/null; then
        report_violation "${rule_id}" "${severity}" "${path}" "${message}" "${matches_file}"
    fi
}

check_present() {
    local rule_id="${1}"
    local severity="${2}"
    local path="${3}"
    local pattern="${4}"
    local message="${5}"
    local matches_file="${TMP_DIR}/${rule_id}.txt"

    if ! grep -E -n -r -- "${pattern}" "${path}" > "${matches_file}" 2>/dev/null; then
        printf 'missing required pattern: %s\n' "${pattern}" > "${matches_file}"
        report_violation "${rule_id}" "${severity}" "${path}" "${message}" "${matches_file}"
    fi
}

check_freertos_include_order() {
    local rule_id="${1}"
    local severity="${2}"
    local search_path="${3}"
    local message="${4}"
    local matches_file="${TMP_DIR}/${rule_id}.txt"
    : > "${matches_file}"

    while IFS= read -r file; do
        [[ -n "${file}" ]] || continue
        local freertos_line task_line
        freertos_line="$(grep -n '^[[:space:]]*#include[[:space:]]\+"freertos/FreeRTOS.h"' "${file}" | head -n1 | cut -d: -f1 || true)"
        task_line="$(grep -n '^[[:space:]]*#include[[:space:]]\+"freertos/task.h"' "${file}" | head -n1 | cut -d: -f1 || true)"

        [[ -n "${task_line}" ]] || continue
        if [[ -z "${freertos_line}" || "${task_line}" -lt "${freertos_line}" ]]; then
            {
                printf '%s: include order invalid (FreeRTOS.h line=%s, task.h line=%s)\n' \
                    "${file}" "${freertos_line:-missing}" "${task_line}"
            } >> "${matches_file}"
        fi
    done < <(grep -E -l -r -- '^[[:space:]]*#include[[:space:]]+"freertos/task.h"' "${search_path}" 2>/dev/null || true)

    if [[ -s "${matches_file}" ]]; then
        report_violation "${rule_id}" "${severity}" "${search_path}" "${message}" "${matches_file}"
    fi
}

run_checks() {
    print_banner "Running architecture invariants (blocking severities: ${BLOCKING_SEVERITIES})"

    check_present "INV-H000" "high" "${MATRIX_FILE}" "INV-H001" \
        "ARCH_COMPLIANCE_MATRIX.md must exist and contain rule definitions"
    check_present "INV-H000" "high" "${EXCEPTIONS_FILE}" "ARCH_EXCEPTION" \
        "ADR_EXCEPTIONS.md must exist and define exception format"

    check_absent "INV-H001" "high" "components/core" \
        '#include[[:space:]]+[<"](esp_|freertos/|lwip/|nvs|driver/|soc/|hal/)' \
        "core layer must stay platform-agnostic (no ESP-IDF/platform headers)"

    check_absent "INV-H002" "high" "components/core" \
        'malloc[[:space:]]*\(|calloc[[:space:]]*\(|realloc[[:space:]]*\(|free[[:space:]]*\(' \
        "core layer must not use malloc/calloc/realloc/free"
    check_absent "INV-H002" "high" "components/service" \
        'malloc[[:space:]]*\(|calloc[[:space:]]*\(|realloc[[:space:]]*\(|free[[:space:]]*\(' \
        "service layer must not use malloc/calloc/realloc/free"

    check_absent "INV-H003" "high" "main/app_main.cpp" \
        'hal_zigbee_poll[[:space:]]*\(|\.process_pending[[:space:]]*\(|\.tick[[:space:]]*\(' \
        "app_main must be bootstrap-only (no runtime processing loop)"
    check_present "INV-H003" "high" "main/app_main.cpp" \
        'g_runtime\.start[[:space:]]*\(' \
        "app_main must start ServiceRuntime task"

    check_absent "INV-M001" "medium" "components/app_hal/hal_wifi.c" \
        'calloc[[:space:]]*\(|free[[:space:]]*\(' \
        "hal_wifi scan hot path must not use calloc/free"

    check_absent "INV-M002" "medium" "components/app_hal/hal_zigbee.c" \
        'is_duplicate_join|maybe_close_permit_join_after_first_join|s_permit_join_auto_closed' \
        "join dedup/auto-close policy must not live in HAL Zigbee"

    check_present "INV-M003" "medium" "components/service/service_runtime.cpp" \
        'runtime_task_entry' \
        "ServiceRuntime runtime task entry must exist"
    check_present "INV-M003" "medium" "components/service/service_runtime.cpp" \
        'post_zigbee_join_candidate' \
        "ServiceRuntime must own join policy ingress API"

    check_present "INV-M004" "medium" "cmake/ProjectCompileOptions.cmake" \
        '-fno-exceptions' \
        "compile policy must enforce -fno-exceptions"
    check_present "INV-M004" "medium" "cmake/ProjectCompileOptions.cmake" \
        '-fno-rtti' \
        "compile policy must enforce -fno-rtti"
    check_present "INV-M004" "medium" "cmake/ProjectCompileOptions.cmake" \
        '-fno-threadsafe-statics' \
        "compile policy must enforce -fno-threadsafe-statics"
    check_present "INV-M004" "medium" "cmake/ProjectCompileOptions.cmake" \
        '-Werror' \
        "compile policy must enforce -Werror (warnings as errors)"

    check_absent "INV-M005" "medium" "components/app_hal/include/hal_wifi.h" \
        '^[[:space:]]*int[[:space:]]+hal_wifi_' \
        "HAL Wi-Fi API must use typed status return values (no int contract)"
    check_absent "INV-M005" "medium" "components/app_hal/include/hal_nvs.h" \
        '^[[:space:]]*int[[:space:]]+hal_nvs_' \
        "HAL NVS API must use typed status return values (no int contract)"
    check_present "INV-M005" "medium" "components/app_hal/include/hal_wifi.h" \
        'HAL_WIFI_STATUS_OK' \
        "HAL Wi-Fi status enum must be defined in header"
    check_present "INV-M005" "medium" "components/app_hal/include/hal_nvs.h" \
        'HAL_NVS_STATUS_OK' \
        "HAL NVS status enum must be defined in header"

    check_absent "INV-M006" "medium" "components/app_hal/hal_wifi.c" \
        'scan rejected for mode|STA connect rejected' \
        "HAL Wi-Fi must not own scan/connect mode policy decisions"
    check_present "INV-M006" "medium" "components/service/service_runtime.cpp" \
        'ensure_wifi_mode_for_scan|ensure_wifi_mode_for_sta_connect' \
        "ServiceRuntime must own Wi-Fi mode policy for scan/connect"

    check_absent "INV-M007" "medium" "components/web_ui/web_handlers_device.cpp" \
        'pin_current[[:space:]]*\(' \
        "/api/devices handler must not pin registry directly (use service-owned API snapshot)"
    check_present "INV-M007" "medium" "components/web_ui/web_handlers_device.cpp" \
        'build_devices_api_snapshot[[:space:]]*\(' \
        "/api/devices handler must use ServiceRuntime atomic devices API snapshot"

    check_absent "INV-M008" "medium" "components/app_hal/hal_zigbee.c" \
        'open_network_or_queue_formation|zigbee_next_formation_retry_ms_|zigbee_formation_retry_count_|pending_join_window_seconds_|join_window_explicit_expected_|zigbee_join_window_was_open_|is_duplicate_join|maybe_auto_close_permit_join_after_first_join|s_permit_join_auto_closed' \
        "HAL Zigbee must not contain service-owned network/join policy logic"
    check_present "INV-M008" "medium" "components/service/service_runtime.cpp" \
        'process_zigbee_network_policy|request_join_window_open|maybe_auto_close_join_window_after_first_join' \
        "ServiceRuntime must own Zigbee formation/join policy flow"

    check_absent "INV-M010" "medium" "components/app_hal/hal_zigbee.c" \
        'kAutoRejoin|auto_rejoin|maybe_open_auto_rejoin_window|maybe_request_auto_rejoin_window' \
        "HAL Zigbee must not contain auto-rejoin policy state/handlers"
    check_present "INV-M010" "medium" "components/service/network_policy_manager.cpp" \
        'maybe_request_auto_rejoin_window' \
        "Service NetworkPolicyManager must own auto-rejoin policy"
    check_present "INV-M010" "medium" "components/service/service_runtime.cpp" \
        'maybe_request_auto_rejoin_window' \
        "ServiceRuntime must trigger auto-rejoin policy via NetworkPolicyManager"

    check_freertos_include_order "INV-M011" "medium" "components/service" \
        "FreeRTOS include order must be FreeRTOS.h before task.h in Service"
    check_freertos_include_order "INV-M011" "medium" "components/app_hal" \
        "FreeRTOS include order must be FreeRTOS.h before task.h in HAL"

    check_absent "INV-M012" "medium" "components/service" \
        'vTaskDelay[[:space:]]*\([[:space:]]*1[[:space:]]*\)' \
        "Service spinlock paths must not block via vTaskDelay(1); use taskYIELD/backoff-safe approach"

    check_absent "INV-M013" "medium" "components/web_ui/web_handlers_network.cpp" \
        'pin_current[[:space:]]*\(' \
        "web network handler must not pin registry directly"
    check_absent "INV-M013" "medium" "components/web_ui/web_handlers_config.cpp" \
        'pin_current[[:space:]]*\(' \
        "web config handler must not pin registry directly"
    check_absent "INV-M013" "medium" "components/web_ui/include/web_routes.hpp" \
        'CoreRegistry' \
        "web route context must not depend on CoreRegistry"
    check_present "INV-M013" "medium" "components/web_ui/web_handlers_network.cpp" \
        'build_network_api_snapshot[[:space:]]*\(' \
        "web network handler must use ServiceRuntime network snapshot API"
    check_present "INV-M013" "medium" "components/web_ui/web_handlers_config.cpp" \
        'build_config_api_snapshot[[:space:]]*\(' \
        "web config handler must use ServiceRuntime config snapshot API"

    check_absent "INV-M014" "medium" "components/service/include/service_runtime.hpp" \
        'const[[:space:]]+RuntimeStats&[[:space:]]+stats[[:space:]]*\(' \
        "ServiceRuntime stats API must return snapshot by value"

    check_absent "INV-M015" "medium" "components/service/include/service_runtime.hpp" \
        '#include[[:space:]]+"hal_zigbee\.h"|hal_zigbee_' \
        "public ServiceRuntime header must not expose HAL Zigbee boundary types"

    local manager_registry_reads="${TMP_DIR}/INV-M016.txt"
    : > "${manager_registry_reads}"
    while IFS= read -r file; do
        [[ -n "${file}" ]] || continue
        grep -E -n -- 'snapshot_copy[[:space:]]*\(|pin_current[[:space:]]*\(' "${file}" >> "${manager_registry_reads}" || true
    done < <(find components/service -maxdepth 1 -type f -name '*_manager.cpp' | sort)
    if [[ -s "${manager_registry_reads}" ]]; then
        report_violation \
            "INV-M016" \
            "medium" \
            "components/service" \
            "service managers must not read CoreRegistry snapshots directly; consume ServiceRuntime-owned state fragments" \
            "${manager_registry_reads}"
    fi

    check_absent "INV-M017" "medium" "components/app_hal/hal_zigbee.c" \
        'kDiagTarget|DIAG target|should_suppress_on_off_for_diag_target|Suppress On/Off short_addr=.*age_ms' \
        "HAL Zigbee must not contain hardcoded target-device diagnostics or per-device suppression logic"

    check_present "INV-M009" "medium" ".github/workflows/ci.yml" \
        '^  reporting-regression:' \
        "CI workflow must define reporting-regression blocking job"
    check_present "INV-M009" "medium" ".github/workflows/ci.yml" \
        'test_service_reporting_manager' \
        "reporting-regression must run dedicated reporting lifecycle tests"

    check_present "INV-L001" "low" "components/common/include/log_tags.h" \
        'LOG_TAG_SERVICE_RUNTIME' \
        "log tag registry should include ServiceRuntime tag"
    check_present "INV-L001" "low" "components/common/include/log_tags.h" \
        'LOG_TAG_HAL_ZIGBEE' \
        "log tag registry should include HAL Zigbee tag"
}

print_summary_and_exit() {
    print_banner "Summary: high=${HIGH_COUNT}, medium=${MEDIUM_COUNT}, low=${LOW_COUNT}, suppressed=${SUPPRESSED_COUNT}"

    if [[ "${HIGH_COUNT}" -gt 0 ]] && is_blocking_severity "high"; then
        print_banner "FAILED: high severity violations are blocking"
        exit 1
    fi
    if [[ "${MEDIUM_COUNT}" -gt 0 ]] && is_blocking_severity "medium"; then
        print_banner "FAILED: medium severity violations are blocking"
        exit 1
    fi
    if [[ "${LOW_COUNT}" -gt 0 ]] && is_blocking_severity "low"; then
        print_banner "FAILED: low severity violations are blocking"
        exit 1
    fi

    print_banner "PASSED"
}

load_exceptions
run_checks
print_summary_and_exit
