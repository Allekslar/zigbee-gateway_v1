<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# ADR Exceptions Registry

This file tracks temporary architecture exceptions for the blocking gate.

## Policy

- Exceptions are temporary and must have an expiry date.
- Only `STATUS=active` exceptions are considered by the gate.
- Expired exceptions are ignored automatically.
- Every exception must reference an ADR or ticket.

## Machine-readable entries

Use this exact format (single line):

`<!-- ARCH_EXCEPTION: RULE=<RULE_ID> PATH=<regex> EXPIRES=<YYYY-MM-DD> STATUS=active ADR=<ADR-ID> -->`

## Active exceptions

None.

## Template

`<!-- ARCH_EXCEPTION: RULE=INV-M002 PATH=components/app_hal/hal_zigbee.c EXPIRES=2026-06-30 STATUS=active ADR=ADR-0012 -->`

## Human-readable table

| Exception ID | Rule | Path regex | Expires | Status | ADR/Ticket | Rationale |
|---|---|---|---|---|---|---|
| _(none)_ |  |  |  |  |  |  |
