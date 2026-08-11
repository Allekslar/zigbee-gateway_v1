#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.
"""Defines and validates the machine-readable eFuse provisioning template
(plan S5 required change #5): "Define a machine-readable eFuse
provisioning template containing chip identity, secure-boot digest slots,
flash-encryption state, anti-rollback/security-version fields, JTAG/
download policy and protection status."

This module covers ONLY the template/schema itself -- a pure data
transformation with no hardware interaction. It deliberately does not
implement plan #6 (the two-phase dry-run/burn workflow), #7 (quarantine
policy) or #8's burn-time enforcement: those need a real manufacturing/
eFuse environment to dry-run and read back against, which this sandbox
does not have (`BLOCKED_SECURITY_PROVISIONING`, per the plan's own
precondition text). What #8 DOES require of this template -- "Store only
redacted public digest/evidence... never place production private keys
in the repository" -- is implemented here directly (`redact_key_material`),
since it is a property of the template format itself, not of the burn
workflow.

Every field name below is a REAL ESP32-C6 eFuse field, confirmed against
`espefuse.py`'s own JSON summary output (ESP-IDF 5.5.5, via
`espefuse.py --chip esp32c6 --virt summary --format json` -- the `--virt`
flag runs ESP-IDF's own eFuse emulation with no real hardware required),
not invented or guessed. See scripts/testdata/espefuse_esp32c6_virtual_summary.json
for the exact captured fixture this module's tests validate against, and
docs/security/PRODUCTION_HARDENING.md Section 2 for the full trail.
"""

from __future__ import annotations

import hashlib
import json
import sys
from typing import Dict, List, Optional

SCHEMA_VERSION = 1

# Grouped exactly per plan #5's own required category list. Field names
# are the real espefuse.py summary keys (category="identity"/"MAC" for
# chip identity, "security" for secure-boot/flash-encryption/anti-
# rollback fields, "jtag" for JTAG/download policy).
CHIP_IDENTITY_FIELDS: List[str] = [
    "MAC",
    "WAFER_VERSION_MAJOR",
    "WAFER_VERSION_MINOR",
    "PKG_VERSION",
    "OPTIONAL_UNIQUE_ID",
    "BLK_VERSION_MAJOR",
    "BLK_VERSION_MINOR",
]

SECURE_BOOT_FIELDS: List[str] = [
    "SECURE_BOOT_EN",
    "KEY_PURPOSE_0",
    "KEY_PURPOSE_1",
    "KEY_PURPOSE_2",
    "KEY_PURPOSE_3",
    "KEY_PURPOSE_4",
    "KEY_PURPOSE_5",
    "SECURE_BOOT_KEY_REVOKE0",
    "SECURE_BOOT_KEY_REVOKE1",
    "SECURE_BOOT_KEY_REVOKE2",
    "SECURE_BOOT_AGGRESSIVE_REVOKE",
    "SECURE_BOOT_DISABLE_FAST_WAKE",
]

FLASH_ENCRYPTION_FIELDS: List[str] = [
    "SPI_BOOT_CRYPT_CNT",
    "DIS_DOWNLOAD_MANUAL_ENCRYPT",
]

ANTI_ROLLBACK_FIELDS: List[str] = [
    "SECURE_VERSION",
]

JTAG_DOWNLOAD_POLICY_FIELDS: List[str] = [
    "DIS_PAD_JTAG",
    "SOFT_DIS_JTAG",
    "JTAG_SEL_ENABLE",
    "DIS_DOWNLOAD_MODE",
    "DIS_FORCE_DOWNLOAD",
    "ENABLE_SECURITY_DOWNLOAD",
    "DIS_DOWNLOAD_ICACHE",
    "SPI_DOWNLOAD_MSPI_DIS",
]

# "Secure-boot digest slots" (plan #5) -- the 6 raw key blocks. These are
# the actual signing/encryption key material and must never appear in a
# provisioning record except as a one-way digest (plan #8).
KEY_MATERIAL_BLOCK_FIELDS: List[str] = [
    "BLOCK_KEY0",
    "BLOCK_KEY1",
    "BLOCK_KEY2",
    "BLOCK_KEY3",
    "BLOCK_KEY4",
    "BLOCK_KEY5",
]

_CATEGORY_SECTIONS = (
    ("chip_identity", CHIP_IDENTITY_FIELDS),
    ("secure_boot", SECURE_BOOT_FIELDS),
    ("flash_encryption", FLASH_ENCRYPTION_FIELDS),
    ("anti_rollback", ANTI_ROLLBACK_FIELDS),
    ("jtag_download_policy", JTAG_DOWNLOAD_POLICY_FIELDS),
)


def _project_field(espefuse_field: dict) -> dict:
    """Keeps only the "protection status" (readable/writeable) plus the
    already-human-readable value/raw_value espefuse.py itself computes --
    drops redundant metadata (bit_len/word/pos/category/description/
    efuse_type/name) that a provisioning record doesn't need to repeat.
    """
    return {
        "value": espefuse_field.get("value"),
        "raw_value": espefuse_field.get("raw_value"),
        "readable": espefuse_field.get("readable"),
        "writeable": espefuse_field.get("writeable"),
    }


def redact_key_material(espefuse_field: dict) -> dict:
    """Plan #8: "Store only redacted public digest/evidence... never
    place production private keys in the repository." Returns the
    protection status (readable/writeable) plus a SHA-256 digest of the
    raw key bytes -- a one-way fingerprint useful for confirming two
    records reference the same key material (or detecting a re-burn),
    but never the key itself.
    """
    raw_value = espefuse_field.get("raw_value") or ""
    hex_digits = raw_value[2:] if raw_value.startswith("0x") else raw_value
    try:
        raw_bytes = bytes.fromhex(hex_digits)
    except ValueError:
        raw_bytes = raw_value.encode("utf-8")
    return {
        "redacted": True,
        "sha256_digest": hashlib.sha256(raw_bytes).hexdigest(),
        "readable": espefuse_field.get("readable"),
        "writeable": espefuse_field.get("writeable"),
    }


def build_provisioning_record(
    espefuse_summary: Dict[str, dict], gateway_id_hex: str, captured_at: str
) -> dict:
    """Projects a real `espefuse.py summary --format json` dict down to
    plan #5's required categories, redacting key material (plan #8).
    `gateway_id_hex` ties the record to the canonical FD-17 GatewayId
    (the same factory-base-MAC-derived identity used everywhere else in
    this project) so provisioning evidence can be correlated with a
    specific device without re-deriving identity from the MAC eFuse field
    a second, redundant way.
    """
    record: dict = {
        "schema_version": SCHEMA_VERSION,
        "gateway_id": gateway_id_hex,
        "captured_at": captured_at,
    }

    for section_name, field_names in _CATEGORY_SECTIONS:
        section: dict = {}
        for field_name in field_names:
            if field_name in espefuse_summary:
                section[field_name] = _project_field(espefuse_summary[field_name])
        record[section_name] = section

    key_material: dict = {}
    for field_name in KEY_MATERIAL_BLOCK_FIELDS:
        if field_name in espefuse_summary:
            key_material[field_name] = redact_key_material(espefuse_summary[field_name])
    record["key_material_digests"] = key_material

    return record


def validate_provisioning_record(record: dict) -> List[str]:
    """Returns a list of violation strings; empty = valid."""
    violations: List[str] = []

    if record.get("schema_version") != SCHEMA_VERSION:
        violations.append(f"schema_version must be {SCHEMA_VERSION}, found {record.get('schema_version')!r}")

    gateway_id = record.get("gateway_id")
    if not isinstance(gateway_id, str) or len(gateway_id) != 12 or not all(
        c in "0123456789abcdef" for c in gateway_id
    ):
        violations.append(f"gateway_id must be exactly 12 lowercase hex characters, found {gateway_id!r}")

    if not record.get("captured_at"):
        violations.append("captured_at must be a non-empty timestamp")

    for section_name, field_names in _CATEGORY_SECTIONS:
        section = record.get(section_name)
        if not isinstance(section, dict):
            violations.append(f"missing required section: {section_name}")
            continue
        for field_name in field_names:
            if field_name not in section:
                violations.append(f"{section_name}.{field_name} is missing")

    key_material = record.get("key_material_digests")
    if not isinstance(key_material, dict):
        violations.append("missing required section: key_material_digests")
    else:
        for field_name in KEY_MATERIAL_BLOCK_FIELDS:
            entry = key_material.get(field_name)
            if entry is None:
                violations.append(f"key_material_digests.{field_name} is missing")
                continue
            if entry.get("redacted") is not True:
                violations.append(f"key_material_digests.{field_name} must be marked redacted")
            if not entry.get("sha256_digest"):
                violations.append(f"key_material_digests.{field_name} is missing its sha256_digest")
            # Defense in depth: a raw key value/raw_value must never leak
            # into a redacted entry, even accidentally.
            if "value" in entry or "raw_value" in entry:
                violations.append(
                    f"key_material_digests.{field_name} leaks raw key material (value/raw_value present)")

    return violations


def main(argv: Optional[List[str]] = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_parser = subparsers.add_parser(
        "build", help="Project a real espefuse.py summary --format json file into a provisioning record.")
    build_parser.add_argument("espefuse_summary_path", help="Path to espefuse.py summary --format json output.")
    build_parser.add_argument("--gateway-id", required=True, help="Canonical 12-lowercase-hex-character GatewayId.")
    build_parser.add_argument("--captured-at", required=True, help="ISO 8601 capture timestamp.")
    build_parser.add_argument("--output", required=True, help="Output path for the provisioning record JSON.")

    validate_parser = subparsers.add_parser("validate", help="Validate an existing provisioning record.")
    validate_parser.add_argument("record_path", help="Path to a provisioning record JSON file.")

    args = parser.parse_args(argv)

    if args.command == "build":
        with open(args.espefuse_summary_path, "r", encoding="utf-8") as handle:
            summary = json.load(handle)
        record = build_provisioning_record(summary, args.gateway_id, args.captured_at)
        violations = validate_provisioning_record(record)
        if violations:
            print("BUILD PRODUCED AN INVALID RECORD (this indicates a bug in this script, not the input):",
                  file=sys.stderr)
            for violation in violations:
                print(f"  - {violation}", file=sys.stderr)
            return 1
        with open(args.output, "w", encoding="utf-8") as handle:
            json.dump(record, handle, indent=2, sort_keys=True)
            handle.write("\n")
        print(f"WROTE {args.output}")
        return 0

    if args.command == "validate":
        with open(args.record_path, "r", encoding="utf-8") as handle:
            record = json.load(handle)
        violations = validate_provisioning_record(record)
        if violations:
            print(f"FAILED: {args.record_path} is not a valid provisioning record:")
            for violation in violations:
                print(f"  - {violation}")
            return 1
        print(f"OK: {args.record_path} is a valid provisioning record.")
        return 0

    return 2


if __name__ == "__main__":
    sys.exit(main())
