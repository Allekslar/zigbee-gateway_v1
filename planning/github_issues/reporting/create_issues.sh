#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DRY_RUN=1
MILESTONE=""
STATE_LABEL=""
BOOTSTRAP_LABELS=1
BOOTSTRAP_MILESTONE=1

usage() {
    cat <<'EOF'
Usage:
  create_issues.sh [--execute] [--milestone "Milestone Name"] [--state-label status:todo] [--no-bootstrap-labels]

Options:
  --execute                 Actually create GitHub issues (default: dry-run preview).
  --milestone <name>        Milestone title to assign to every created issue.
  --state-label <label>     Extra label for issue state tracking (example: status:todo).
  --no-bootstrap-labels     Do not auto-create missing labels before issue creation.
  --no-bootstrap-milestone  Do not auto-create milestone if missing.

Environment:
  GH_REPO                   Optional explicit repo, e.g. "org/repo".

Examples:
  ./planning/github_issues/reporting/create_issues.sh
  ./planning/github_issues/reporting/create_issues.sh --execute --milestone "Reporting Phase 1"
  ./planning/github_issues/reporting/create_issues.sh --execute --milestone "Reporting Phase 1" --state-label status:todo
  ./planning/github_issues/reporting/create_issues.sh --execute --no-bootstrap-labels
  ./planning/github_issues/reporting/create_issues.sh --execute --no-bootstrap-milestone
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --execute)
            DRY_RUN=0
            shift
            ;;
        --milestone)
            MILESTONE="${2:-}"
            [[ -n "${MILESTONE}" ]] || { echo "error: --milestone requires a value"; exit 2; }
            shift 2
            ;;
        --state-label)
            STATE_LABEL="${2:-}"
            [[ -n "${STATE_LABEL}" ]] || { echo "error: --state-label requires a value"; exit 2; }
            shift 2
            ;;
        --no-bootstrap-labels)
            BOOTSTRAP_LABELS=0
            shift
            ;;
        --no-bootstrap-milestone)
            BOOTSTRAP_MILESTONE=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1"
            usage
            exit 2
            ;;
    esac
done

map_component_labels() {
    local component="$1"
    case "$component" in
        core) echo "area:core" ;;
        service) echo "area:service" ;;
        app_hal) echo "area:hal" ;;
        web_ui) echo "area:web" ;;
        mqtt_bridge) echo "area:mqtt" ;;
        matter_bridge) echo "area:matter" ;;
        tests) echo "area:tests" ;;
        docs/ci) echo "area:docs,area:ci" ;;
        service/core) echo "area:service,area:core" ;;
        mqtt_bridge/service) echo "area:mqtt,area:service" ;;
        app_hal/matter_bridge) echo "area:hal,area:matter" ;;
        *) echo "" ;;
    esac
}

map_wave_label() {
    local rpt_id="$1"
    local n="${rpt_id#RPT-}"
    local v=$((10#${n}))
    if (( v >= 1 && v <= 7 )); then echo "wave:a-foundation"; return; fi
    if (( v >= 8 && v <= 11 )); then echo "wave:b-hal"; return; fi
    if (( v >= 12 && v <= 15 )); then echo "wave:c-model-persistence"; return; fi
    if (( v >= 16 && v <= 19 )); then echo "wave:d-semantics"; return; fi
    if (( v >= 20 && v <= 23 )); then echo "wave:e-web"; return; fi
    if (( v >= 24 && v <= 27 )); then echo "wave:f-mqtt"; return; fi
    if (( v >= 28 && v <= 30 )); then echo "wave:g-matter"; return; fi
    if (( v >= 31 && v <= 34 )); then echo "wave:h-ci"; return; fi
    echo ""
}

label_color() {
    local label="$1"
    case "$label" in
        type:*) echo "0E8A16" ;;
        phase:*) echo "5319E7" ;;
        area:*) echo "1D76DB" ;;
        wave:*) echo "FBCA04" ;;
        status:*) echo "BFDADC" ;;
        *) echo "C5DEF5" ;;
    esac
}

label_description() {
    local label="$1"
    case "$label" in
        type:feature) echo "Feature work item" ;;
        phase:reporting) echo "Zigbee reporting initiative" ;;
        area:core) echo "Core layer" ;;
        area:service) echo "Service layer" ;;
        area:hal) echo "HAL/app_hal layer" ;;
        area:web) echo "Web UI/API layer" ;;
        area:mqtt) echo "MQTT bridge layer" ;;
        area:matter) echo "Matter bridge layer" ;;
        area:tests) echo "Testing scope" ;;
        area:docs) echo "Documentation scope" ;;
        area:ci) echo "CI pipeline scope" ;;
        wave:a-foundation) echo "Wave A: foundation" ;;
        wave:b-hal) echo "Wave B: HAL contract/adapter" ;;
        wave:c-model-persistence) echo "Wave C: model/persistence" ;;
        wave:d-semantics) echo "Wave D: semantics" ;;
        wave:e-web) echo "Wave E: web exposure" ;;
        wave:f-mqtt) echo "Wave F: MQTT bridge" ;;
        wave:g-matter) echo "Wave G: Matter bridge" ;;
        wave:h-ci) echo "Wave H: CI/integration/HIL" ;;
        status:todo) echo "Planned, not started" ;;
        *) echo "Auto-created by reporting issue bootstrap script" ;;
    esac
}

ensure_labels() {
    local labels=(
        "type:feature"
        "phase:reporting"
        "area:core"
        "area:service"
        "area:hal"
        "area:web"
        "area:mqtt"
        "area:matter"
        "area:tests"
        "area:docs"
        "area:ci"
        "wave:a-foundation"
        "wave:b-hal"
        "wave:c-model-persistence"
        "wave:d-semantics"
        "wave:e-web"
        "wave:f-mqtt"
        "wave:g-matter"
        "wave:h-ci"
    )
    if [[ -n "$STATE_LABEL" ]]; then
        labels+=("$STATE_LABEL")
    fi

    for lb in "${labels[@]}"; do
        local color
        color="$(label_color "$lb")"
        local desc
        desc="$(label_description "$lb")"

        local cmd=("gh" "label" "create" "$lb" "--color" "$color" "--description" "$desc" "--force")
        if [[ -n "${GH_REPO:-}" ]]; then
            cmd+=("--repo" "${GH_REPO}")
        fi

        if [[ "$DRY_RUN" -eq 1 ]]; then
            printf '[dry-run] %s\n' "${cmd[*]}"
        else
            "${cmd[@]}" >/dev/null
        fi
    done
}

ensure_milestone() {
    if [[ -z "$MILESTONE" ]]; then
        return
    fi

    local repo_arg=()
    if [[ -n "${GH_REPO:-}" ]]; then
        repo_arg=(--repo "${GH_REPO}")
    fi

    local existing
    existing="$(gh api "${repo_arg[@]}" "repos/{owner}/{repo}/milestones?state=all&per_page=100" --jq ".[] | select(.title==\"${MILESTONE}\") | .number" 2>/dev/null || true)"
    if [[ -n "$existing" ]]; then
        return
    fi

    local cmd=("gh" "api")
    cmd+=("${repo_arg[@]}")
    cmd+=("--method" "POST" "repos/{owner}/{repo}/milestones" "-f" "title=${MILESTONE}")

    if [[ "$DRY_RUN" -eq 1 ]]; then
        printf '[dry-run] %s\n' "${cmd[*]}"
    else
        "${cmd[@]}" >/dev/null
        echo "created missing milestone: ${MILESTONE}"
    fi
}

create_or_preview() {
    local file="$1"
    local id
    id="$(basename "$file" .md)"
    local title
    title="$(sed -n '1s/^# //p' "$file")"
    local component
    component="$(sed -n 's/^- \*\*Component:\*\* `\(.*\)`/\1/p' "$file" | head -n1)"
    local component_labels
    component_labels="$(map_component_labels "$component")"
    local wave_label
    wave_label="$(map_wave_label "$id")"

    local labels=("type:feature" "phase:reporting")
    if [[ -n "$component_labels" ]]; then
        IFS=',' read -r -a c_labels <<< "$component_labels"
        labels+=("${c_labels[@]}")
    fi
    if [[ -n "$wave_label" ]]; then
        labels+=("$wave_label")
    fi
    if [[ -n "$STATE_LABEL" ]]; then
        labels+=("$STATE_LABEL")
    fi

    local cmd=("gh" "issue" "create" "--title" "$title" "--body-file" "$file")
    for lb in "${labels[@]}"; do
        cmd+=("--label" "$lb")
    done
    if [[ -n "$MILESTONE" ]]; then
        cmd+=("--milestone" "$MILESTONE")
    fi
    if [[ -n "${GH_REPO:-}" ]]; then
        cmd+=("--repo" "${GH_REPO}")
    fi

    if [[ "$DRY_RUN" -eq 1 ]]; then
        printf '[dry-run] %s\n' "${cmd[*]}"
    else
        "${cmd[@]}"
    fi
}

shopt -s nullglob
files=("${ROOT_DIR}"/RPT-*.md)
if [[ ${#files[@]} -eq 0 ]]; then
    echo "error: no RPT-*.md files found in ${ROOT_DIR}"
    exit 1
fi

if [[ "$BOOTSTRAP_LABELS" -eq 1 ]]; then
    ensure_labels
fi
if [[ "$BOOTSTRAP_MILESTONE" -eq 1 ]]; then
    ensure_milestone
fi

for f in "${files[@]}"; do
    create_or_preview "$f"
done

if [[ "$DRY_RUN" -eq 1 ]]; then
    echo
    echo "Preview complete. Re-run with --execute to create issues."
fi
