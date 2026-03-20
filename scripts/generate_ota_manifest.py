#!/usr/bin/env python3
"""Generate a stable OTA manifest from a built ESP-IDF firmware artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


DEFAULT_SIGNATURE_ALGO = "ecdsa-p256-sha256"
DEFAULT_SIGNATURE_KEY_ID = "ota-release-v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        required=True,
        help="ESP-IDF build directory containing project_description.json and the app .bin",
    )
    parser.add_argument(
        "--artifact-url",
        required=True,
        help="Final published OTA URL for the firmware artifact",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Output manifest path (default: <build-dir>/ota-manifest.json)",
    )
    parser.add_argument("--version", help="Override manifest version")
    parser.add_argument("--project", help="Override manifest project")
    parser.add_argument("--board", help="Override manifest board")
    parser.add_argument("--chip-target", help="Override manifest chip target")
    parser.add_argument(
        "--min-schema",
        type=int,
        help="Override manifest min_schema (default: ConfigManager::kCurrentSchemaVersion)",
    )
    parser.add_argument(
        "--allow-downgrade",
        action="store_true",
        help="Set allow_downgrade=true in the generated manifest",
    )
    parser.add_argument(
        "--signing-key",
        type=Path,
        help="Optional PEM private key used to sign the generated manifest",
    )
    parser.add_argument(
        "--signature-algo",
        default=DEFAULT_SIGNATURE_ALGO,
        help=f"Signature algorithm token (default: {DEFAULT_SIGNATURE_ALGO})",
    )
    parser.add_argument(
        "--signature-key-id",
        default=DEFAULT_SIGNATURE_KEY_ID,
        help=f"Signature key id token (default: {DEFAULT_SIGNATURE_KEY_ID})",
    )
    return parser.parse_args()


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def require_file(path: Path, description: str) -> Path:
    if not path.is_file():
        raise SystemExit(f"{description} not found: {path}")
    return path


def read_text(path: Path) -> str:
    with path.open("r", encoding="utf-8") as handle:
        return handle.read()


def extract_regex(text: str, pattern: str, description: str) -> str:
    match = re.search(pattern, text, flags=re.MULTILINE)
    if match is None:
        raise SystemExit(f"Could not extract {description}")
    return match.group(1)


def locate_app_bin(build_dir: Path, description: dict) -> Path:
    app_bin_name = description.get("app_bin")
    if isinstance(app_bin_name, str) and app_bin_name:
        candidate = build_dir / app_bin_name
        if candidate.is_file():
            return candidate

    candidates = sorted(
        path
        for path in build_dir.glob("*.bin")
        if path.name not in {"bootloader.bin", "partition-table.bin", "ota_data_initial.bin"}
    )
    if not candidates:
        raise SystemExit(f"Could not locate app binary in build dir: {build_dir}")
    return candidates[0]


def compute_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def project_root_from_build_dir(build_dir: Path, description: dict) -> Path:
    project_path = description.get("project_path")
    if isinstance(project_path, str) and project_path:
        return Path(project_path)
    return build_dir.resolve().parent


def build_signing_payload(manifest: dict) -> str:
    return (
        f"version={manifest['version']}\n"
        f"url={manifest['url']}\n"
        f"sha256={manifest['sha256']}\n"
        f"project={manifest['project']}\n"
        f"board={manifest['board']}\n"
        f"chip_target={manifest['chip_target']}\n"
        f"min_schema={manifest['min_schema']}\n"
        f"allow_downgrade={'true' if manifest['allow_downgrade'] else 'false'}\n"
    )


def sign_manifest_payload(payload: str, private_key: Path) -> str:
    require_file(private_key, "signing key")
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        payload_path = tmp_path / "payload.txt"
        signature_path = tmp_path / "signature.der"
        payload_path.write_text(payload, encoding="utf-8")
        subprocess.run(
            [
                "openssl",
                "dgst",
                "-sha256",
                "-sign",
                str(private_key),
                "-out",
                str(signature_path),
                str(payload_path),
            ],
            check=True,
        )
        return signature_path.read_bytes().hex()


def load_repo_defaults(project_root: Path) -> dict:
    version_hpp = require_file(project_root / "components/common/include/version.hpp", "version header")
    config_manager_hpp = require_file(project_root / "components/service/include/config_manager.hpp", "config manager header")

    version_text = read_text(version_hpp)
    config_text = read_text(config_manager_hpp)

    return {
        "project": extract_regex(version_text, r'kProjectName\s*=\s*"([^"]+)"', "common::kProjectName"),
        "board": extract_regex(version_text, r'kBoardId\s*=\s*"([^"]+)"', "common::kBoardId"),
        "min_schema": int(
            extract_regex(
                config_text,
                r"kCurrentSchemaVersion\s*=\s*([0-9]+)",
                "ConfigManager::kCurrentSchemaVersion",
            )
        ),
    }


def build_manifest(args: argparse.Namespace) -> tuple[dict, Path]:
    build_dir = require_file(args.build_dir / "project_description.json", "project description").parent
    description = load_json(build_dir / "project_description.json")
    project_root = project_root_from_build_dir(build_dir, description)
    defaults = load_repo_defaults(project_root)
    app_bin = locate_app_bin(build_dir, description)

    project_version = description.get("project_version")
    target = description.get("target")
    if not isinstance(project_version, str) or not project_version:
        raise SystemExit("project_version is missing in project_description.json")
    if not isinstance(target, str) or not target:
        raise SystemExit("target is missing in project_description.json")

    manifest = {
        "version": args.version if args.version else project_version,
        "url": args.artifact_url,
        "sha256": compute_sha256(app_bin),
        "project": args.project if args.project else defaults["project"],
        "board": args.board if args.board else defaults["board"],
        "chip_target": args.chip_target if args.chip_target else target,
        "min_schema": args.min_schema if args.min_schema is not None else defaults["min_schema"],
        "allow_downgrade": args.allow_downgrade,
    }
    if args.signing_key is not None:
        payload = build_signing_payload(manifest)
        manifest["signature_algo"] = args.signature_algo
        manifest["signature_key_id"] = args.signature_key_id
        manifest["signature"] = sign_manifest_payload(payload, args.signing_key)
    return manifest, app_bin


def main() -> int:
    args = parse_args()
    manifest, app_bin = build_manifest(args)
    output_path = args.output if args.output else (args.build_dir / "ota-manifest.json")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with output_path.open("w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=False)
        handle.write("\n")

    print("Generated OTA manifest:")
    print(f"  app binary: {app_bin}")
    print(f"  output: {output_path}")
    print(f"  version: {manifest['version']}")
    print(f"  url: {manifest['url']}")
    print(f"  sha256: {manifest['sha256']}")
    print(f"  project: {manifest['project']}")
    print(f"  board: {manifest['board']}")
    print(f"  chip_target: {manifest['chip_target']}")
    print(f"  min_schema: {manifest['min_schema']}")
    print(f"  allow_downgrade: {str(manifest['allow_downgrade']).lower()}")
    if "signature" in manifest:
        print(f"  signature_algo: {manifest['signature_algo']}")
        print(f"  signature_key_id: {manifest['signature_key_id']}")
        print(f"  signature: {manifest['signature']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
