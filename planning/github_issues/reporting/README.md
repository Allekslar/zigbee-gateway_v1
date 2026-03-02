# Reporting Issues Backlog (GitHub-ready)

Generated from `arh/Reporting_Backlog.md`.

## Files
- `RPT-001.md` ... `RPT-034.md`: one GitHub issue body per backlog item.
- `create_issues.sh`: batch creator with labels + optional milestone.

## Suggested labels
- `area:core`
- `area:service`
- `area:hal`
- `area:web`
- `area:mqtt`
- `area:matter`
- `type:feature`
- `phase:reporting`

## Automatic Batch Create (Recommended)

Dry-run preview (no issue creation):

```bash
./planning/github_issues/reporting/create_issues.sh
```

Create all issues with milestone + status label:

```bash
./planning/github_issues/reporting/create_issues.sh \
  --execute \
  --milestone "Reporting Phase 1" \
  --state-label "status:todo"
```

Optional target repo override:

```bash
GH_REPO="org/repo" ./planning/github_issues/reporting/create_issues.sh --execute --milestone "Reporting Phase 1"
```

Disable auto-creation of labels (if labels are already managed manually):

```bash
./planning/github_issues/reporting/create_issues.sh --execute --no-bootstrap-labels
```

Disable auto-creation of milestone (if milestones are managed manually):

```bash
./planning/github_issues/reporting/create_issues.sh --execute --no-bootstrap-milestone
```

## Label mapping used by script

- Missing labels are auto-created via `gh label create --force` by default.
- Missing milestone (when `--milestone` passed) is auto-created via `gh api .../milestones` by default.
- Base labels for every issue:
  - `type:feature`
  - `phase:reporting`
- Component labels:
  - `core` -> `area:core`
  - `service` -> `area:service`
  - `app_hal` -> `area:hal`
  - `web_ui` -> `area:web`
  - `mqtt_bridge` -> `area:mqtt`
  - `matter_bridge` -> `area:matter`
  - composite components get multiple `area:*` labels
- Wave labels by ID range:
  - `RPT-001..007` -> `wave:a-foundation`
  - `RPT-008..011` -> `wave:b-hal`
  - `RPT-012..015` -> `wave:c-model-persistence`
  - `RPT-016..019` -> `wave:d-semantics`
  - `RPT-020..023` -> `wave:e-web`
  - `RPT-024..027` -> `wave:f-mqtt`
  - `RPT-028..030` -> `wave:g-matter`
  - `RPT-031..034` -> `wave:h-ci`

## Manual Create via GitHub CLI

Example for one issue:

```bash
gh issue create \
  --title "$(sed -n '1s/^# //p' planning/github_issues/reporting/RPT-001.md)" \
  --body-file planning/github_issues/reporting/RPT-001.md
```

Batch create all issues:

```bash
for f in planning/github_issues/reporting/RPT-*.md; do
  gh issue create \
    --title "$(sed -n '1s/^# //p' "$f")" \
    --body-file "$f"
done
```
