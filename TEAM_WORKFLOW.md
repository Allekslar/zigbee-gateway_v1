<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Team Workflow: Architecture-First Development

This document describes how the team should work so code consistently follows `ARCHITECTURE.md` and `CODING_GUIDELINES.md` without recurring regressions.

## 1. Source of Truth

- Architecture and layer boundaries: `ARCHITECTURE.md`
- Technical code rules: `CODING_GUIDELINES.md`
- Machine-checkable invariants: `ARCH_COMPLIANCE_MATRIX.md`
- Exceptions (temporary): `ADR_EXCEPTIONS.md`
- Automatic invariant checks: `check_arch_invariants.sh`
- CI pipeline: `.github/workflows/ci.yml`

Rule: if prose documentation conflicts with a gate, first record consensus in `ARCH_COMPLIANCE_MATRIX.md`, then change code.

## 2. Daily Development Cycle

1. Start each task by mapping it to concrete Rule IDs from `ARCH_COMPLIANCE_MATRIX.md`.
2. Commit in small units (1 topic = 1 commit).
3. Run local gates before push (section 3).
4. If a rule deviation is required, register an exception in `ADR_EXCEPTIONS.md` before merge.
5. After merge, remove/close temporary exceptions in a dedicated follow-up commit.

## 3. Local Commands (Required Before Push)

```bash
# Canonical blocking local verification bundle
scripts/run_blocking_local_checks.sh
```

Strict local mode with low-severity treated as blocking:

```bash
scripts/run_blocking_local_checks.sh --strict
```

If needed, the manual equivalent remains:

```bash
bash ./check_arch_invariants.sh
cmake -S test/host -B build-host
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
ctest --test-dir build-host --output-on-failure -R test_config_manager_migration
idf.py -C test/target -B build-target-tests build
```

## 4. What A Failing Gate Means

- `INV-H*`: blocks merge. Fix immediately.
- `INV-M*`: also blocking in CI. Must be fixed or temporarily covered by an exception.
- `INV-L*`: warning by default, but the team can temporarily enable strict mode.

Diagnostics:

1. Find the Rule ID in the log.
2. Open its description in `ARCH_COMPLIANCE_MATRIX.md`.
3. Fix code in the indicated scope.
4. Re-run the local gate.

## 5. Exception Policy (`ADR_EXCEPTIONS`)

An exception is allowed only as a temporary mechanism when:

- there is a release/integration blocker;
- there is a clear plan to return to the rule;
- there is an end date.

Format of one active record (single line):

```text
<!-- ARCH_EXCEPTION: RULE=<RULE_ID> PATH=<regex> EXPIRES=<YYYY-MM-DD> STATUS=active ADR=<ADR-ID> -->
```

Mandatory requirements:

- `EXPIRES` must be a realistic near-term date.
- `ADR` must point to an ADR/ticket.
- after the fix, the exception is removed in the same PR or in the next short PR.

## 6. CI Jobs And Their Role

- `firmware-build`: base firmware build.
- `host-tests`: unit tests for core/service logic.
- `target-hal-tests-build`: build verification for target tests.
- `target-hal-tests-hil-smoke`: blocking HIL smoke on push/PR.
- `target-hal-tests-hil`: nightly/manual full HIL run.
- `test/hil`: manual or semi-automated real-gateway smoke scenarios such as reboot/join/on-off/remove.
- `architecture-invariants`: `check_arch_invariants.sh` + migration smoke.
- `static-analysis`: `clang-tidy` + `cppcheck` for core/service/critical app_hal/web_ui.

Merge rule: all blocking jobs must be green.

## 7. Commit And PR Practice

1. One invariant or one logical risk per commit.
2. Mention Rule ID in commit message when a change is gate-related.
3. In PR description, briefly include:
- which risk was closed;
- which Rule IDs were affected;
- which local commands were executed.
4. Do not mix architecture refactoring and functional features in one PR unless necessary.

## 8. How To Avoid Regression Cycles

1. For each repeated bug, add or strengthen an invariant in `check_arch_invariants.sh`.
2. For each new runtime scenario, add a test (host or target).
3. Do not accept manual agreements without automated checks.
4. Review weekly:
- active exceptions in `ADR_EXCEPTIONS.md`;
- flaky or slow tests;
- invariants that fail often.

## 9. Definition of Ready For A Task

Before starting a task, there must be:

- clear scope (layers and files);
- a list of invariants the task can affect;
- a test plan (host/target/HIL).

## 10. Definition of Done For A Task

A task is complete when:

- local commands from section 3 pass;
- CI blocking jobs are green;
- there are no permanent exceptions;
- documentation is updated if a contract or architecture rule changed.
