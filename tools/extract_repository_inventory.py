#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.
# pylint: disable=missing-function-docstring,missing-module-docstring
#
# Canonical companion artifact of docs/implementation/PRODUCTION_HARDENING_PLAN.md
# (section 5.3 / 5.5). Extracts a machine-readable inventory of the repository so
# an executor can perform the exact two-way comparison required before Stage S0
# permits any product-source mutation.
#
# Usage:
#   python3 tools/extract_repository_inventory.py \
#     --repo . \
#     --output implementation-evidence/current-repository-inventory.json \
#     --baseline evidence/reviewed-repository-inventory.json \
#     --diff-output implementation-evidence/repository-inventory-diff.json

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def walk_normalized(root: Path) -> dict[str, str]:
    """Return {relative_posix_path: sha256} for every normalized source file."""
    out: dict[str, str] = {}
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if _is_generated_or_vcs(rel):
            continue
        # Skip binary/large artifacts explicitly; source tree hashes are text.
        out[rel] = sha256_file(path)
    return out


# Paths that must never be part of a normalized source inventory (build/gen/evidence).
_GENERATED_PREFIXES = (
    "build",
    "build-",
    "dist",
    "coverage",
    "implementation-evidence",
    ".git",
    ".github",
    ".vscode",
    "evidence",
    "planning",
)


def _is_generated_or_vcs(rel: str) -> bool:
    parts = rel.split("/")
    for prefix in _GENERATED_PREFIXES:
        if parts[0] == prefix or parts[0].startswith(prefix):
            return True
    return False


def hash_dict(data: dict) -> str:
    return sha256_bytes(json.dumps(data, sort_keys=True, separators=(",", ":")).encode("utf-8"))


# ---------------------------------------------------------------------------
# Inventory extractors
# ---------------------------------------------------------------------------

def extract_components(root: Path) -> list[str]:
    comp = root / "components"
    if not comp.is_dir():
        return []
    return sorted(p.name for p in comp.iterdir() if p.is_dir())


def extract_kconfig_symbols(root: Path) -> list[dict]:
    symbols = []
    for cfg in [root / "main" / "Kconfig.projbuild"]:
        if not cfg.is_file():
            continue
        text = cfg.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"^\s*config\s+([A-Z0-9_]+)\s*$", text, re.MULTILINE):
            symbols.append({"file": cfg.as_posix(), "config": m.group(1)})
    return sorted(symbols, key=lambda s: s["config"])


def extract_temporary_flags(root: Path, symbol: str = "kTemporarilyDisable") -> list[str]:
    hits = []
    for path in sorted(root.rglob("*.cpp")):
        if _is_generated_or_vcs(path.relative_to(root).as_posix()):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        # count occurrence lines (declarations and usages)
        for line in text.splitlines():
            if symbol in line:
                hits.append(f"{path.relative_to(root).as_posix()}: {line.strip()}")
    return hits


def extract_http_routes(root: Path) -> list[str]:
    routes = set()
    web = root / "components" / "web_ui"
    for path in sorted(web.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if _is_generated_or_vcs(rel):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for m in re.finditer(r'"(/api/[^"\s]+)"', text):
            routes.add(m.group(1))
        for m in re.finditer(r"'(/api/[^'\s]+)'", text):
            routes.add(m.group(1))
    return sorted(routes)


def extract_mqtt_topics(root: Path) -> list[str]:
    topics = set()
    mqtt = root / "components" / "mqtt_bridge"
    for path in sorted(mqtt.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if _is_generated_or_vcs(rel):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for m in re.finditer(r'"([a-z0-9/.\-+]+)"', text):
            topic = m.group(1)
            if topic.startswith("zigbee-gateway") or "zigbee-gateway" in topic:
                topics.add(topic)
    return sorted(topic for topic in topics if "/" in topic)


def extract_ci_jobs(root: Path) -> list[str]:
    jobs = []
    ci = root / ".github" / "workflows" / "ci.yml"
    if not ci.is_file():
        return jobs
    text = ci.read_text(encoding="utf-8", errors="replace")
    for m in re.finditer(r"^\s{2}([a-zA-Z0-9_-]+):\s*(?:$|#|\{)", text, re.MULTILINE):
        jobs.append(m.group(1))
    return sorted(set(jobs))


def extract_cmake_tests(root: Path) -> list[str]:
    tests = []
    for cmake in sorted(root.glob("test/host/CMakeLists.txt")) + sorted(root.glob("test/integration/CMakeLists.txt")):
        text = cmake.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r'add_test\s*\(\s*NAME\s+([^\s)]+)', text, re.MULTILINE):
            tests.append({"cmake": cmake.as_posix(), "name": m.group(1)})
    return sorted(tests, key=lambda t: t["name"])


def extract_scripts(root: Path) -> list[str]:
    scripts_dir = root / "scripts"
    if not scripts_dir.is_dir():
        return []
    return sorted(p.name for p in scripts_dir.iterdir() if p.is_file())


def extract_hil_files(root: Path) -> list[str]:
    hil = root / "test" / "hil"
    if not hil.is_dir():
        return []
    return sorted(p.name for p in hil.iterdir() if p.is_file())


def extract_persisted_versions(root: Path) -> dict:
    versions = {}
    for path in sorted(root.rglob("*.cpp")) + sorted(root.rglob("*.hpp")):
        if _is_generated_or_vcs(path.relative_to(root).as_posix()):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for m in re.finditer(r"kCurrentSchemaVersion\s*=\s*(\d+)", text):
            versions["kCurrentSchemaVersion"] = int(m.group(1))
        for m in re.finditer(r"kStateVersion\s*=\s*(\d+)", text):
            versions["kStateVersion"] = int(m.group(1))
        for m in re.finditer(r'"(core_state_v(\d+))"', text):
            versions.setdefault("persisted_keys", []).append(m.group(1))
    versions["persisted_keys"] = sorted(set(versions.get("persisted_keys", [])))
    return versions


def extract_build_info(root: Path) -> dict:
    info = {"esp_idf": {"entry": None}}
    deps = root / "dependencies.lock"
    if deps.is_file():
        text = deps.read_text(encoding="utf-8", errors="replace")
        # Find ESP-IDF version line like: "version": "5.5.2"
        for m in re.finditer(r'"version"\s*:\s*"(5\.5\.\d+)"', text):
            if info["esp_idf"]["entry"] is None:
                info["esp_idf"]["entry"] = m.group(1)
            elif m.group(1) != info["esp_idf"]["entry"]:
                # a mismatch is recorded; caller decides BLOCKED_TOOLCHAIN
                info["esp_idf"]["mismatch"] = True
    return info


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_inventory(root: Path) -> dict:
    tree = walk_normalized(root)
    inventory = {
        "plan": "docs/implementation/PRODUCTION_HARDENING_PLAN.md",
        "repository": {
            "tree_sha256": hash_dict(tree),
            "source_files": tree,
            "source_file_count": len(tree),
        },
        "components": extract_components(root),
        "kconfig_symbols": extract_kconfig_symbols(root),
        "temporary_flags": extract_temporary_flags(root),
        "http_routes": extract_http_routes(root),
        "mqtt_topics": extract_mqtt_topics(root),
        "ci_jobs": extract_ci_jobs(root),
        "cmake_tests": extract_cmake_tests(root),
        "scripts": extract_scripts(root),
        "hil_files": extract_hil_files(root),
        "persisted_versions": extract_persisted_versions(root),
        "build_info": extract_build_info(root),
    }
    inventory["inventory_sha256"] = hash_dict(inventory)
    return inventory


def diff_inventory(current: dict, baseline: dict) -> dict:
    """Two-way set diff for all list/map inventories."""
    result = {"match": True, "sections": {}}

    # Exact tree hash comparison.
    result["repository_diff"] = {
        "match": current["repository"]["tree_sha256"] == baseline["repository"]["tree_sha256"],
        "current": current["repository"]["tree_sha256"],
        "baseline": baseline["repository"]["tree_sha256"],
    }
    if not result["repository_diff"]["match"]:
        result["match"] = False

    # Section-level two-way set diffs for list inventories.
    list_sections = [
        "components",
        "kconfig_symbols",
        "http_routes",
        "mqtt_topics",
        "ci_jobs",
        "cmake_tests",
        "scripts",
        "hil_files",
        "temporary_flags",
    ]
    for section in list_sections:
        cur = _to_keyed(current.get(section))
        base = _to_keyed(baseline.get(section))
        only_cur = cur - base
        only_base = base - cur
        section_match = not only_cur and not only_base
        result["sections"][section] = {
            "match": section_match,
            "only_current": sorted(only_cur),
            "only_baseline": sorted(only_base),
        }
        if not section_match:
            result["match"] = False

    return result


def _to_keyed(value) -> set:
    if isinstance(value, list):
        return {json.dumps(v, sort_keys=True) for v in value}
    if isinstance(value, dict):
        return {json.dumps(v, sort_keys=True) for v in value.values()}
    return set()


def main(argv) -> int:
    parser = argparse.ArgumentParser(description="Extract repository inventory for Stage S0 comparison.")
    parser.add_argument("--repo", required=True, help="Path to repository root.")
    parser.add_argument("--output", required=True, help="Output JSON path for the current inventory.")
    parser.add_argument("--baseline", required=False, help="Optional reviewed baseline JSON for diff.")
    parser.add_argument("--diff-output", required=False, help="Optional diff JSON output path.")
    args = parser.parse_args(argv)

    root = Path(args.repo).resolve()
    inventory = build_inventory(root)

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(inventory, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"WROTE {out} files={inventory['repository']['source_file_count']} "
          f"tree_sha256={inventory['repository']['tree_sha256']} "
          f"inventory_sha256={inventory['inventory_sha256']}")

    verdict = "INVENTORY_MATCH" if inventory.get("build_info", {}).get("esp_idf", {}).get("mismatch") else None

    if args.baseline:
        baseline_path = Path(args.baseline)
        if not baseline_path.is_file():
            print("BLOCKED_DELTA_REVIEW baseline file not found", file=sys.stderr)
            return 2
        baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
        diff = diff_inventory(inventory, baseline)
        if args.diff_output:
            dp = Path(args.diff_output)
            dp.parent.mkdir(parents=True, exist_ok=True)
            dp.write_text(json.dumps(diff, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(f"WROTE diff {dp}")
        if diff["match"]:
            verdict = "INVENTORY_MATCH"
            print(verdict)
            return 0
        print("BLOCKED_DELTA_REVIEW")
        return 1

    # Without a baseline we can only mark the snapshot-derived status.
    print("SNAPSHOT-DERIVED — MUST RECOMPUTE (no baseline supplied)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
