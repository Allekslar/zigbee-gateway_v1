#!/usr/bin/env python3
"""Promote a packaged OTA bundle from one channel to another."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, required=True, help="Root OTA bundle directory, e.g. dist/ota")
    parser.add_argument("--version", required=True, help="Release version to promote")
    parser.add_argument("--artifact-base-url", required=True, help="Base public URL for the target channel")
    parser.add_argument("--source-channel", default="staging", help="Source channel (default: staging)")
    parser.add_argument("--target-channel", default="production", help="Target channel (default: production)")
    parser.add_argument("--manifest-name", default="ota-manifest.json", help="Manifest filename (default: ota-manifest.json)")
    parser.add_argument("--artifact-name", help="Override artifact filename")
    parser.add_argument("--signing-key", type=Path, help="PEM private key used to sign the promoted manifest")
    parser.add_argument(
        "--signature-algo",
        default="ecdsa-p256-sha256",
        help="Manifest signature algorithm for the promoted manifest (default: ecdsa-p256-sha256)",
    )
    parser.add_argument(
        "--signature-key-id",
        default="ota-release-v1",
        help="Manifest signature key id for the promoted manifest (default: ota-release-v1)",
    )
    parser.add_argument("--allow-downgrade", action="store_true", help="Set allow_downgrade=true in promoted manifest")
    parser.add_argument("--verify-artifact-url", action="store_true", help="Verify the final artifact URL")
    parser.add_argument("--url-timeout-sec", type=int, default=10, help="Timeout for --verify-artifact-url")
    parser.add_argument("--force", action="store_true", help="Overwrite existing target bundle if present")
    return parser.parse_args()


def require_file(path: Path, description: str) -> Path:
    if not path.is_file():
        raise SystemExit(f"{description} not found: {path}")
    return path


def build_artifact_url(base_url: str, channel: str, version: str, artifact_name: str) -> str:
    return f"{base_url.rstrip('/')}/{channel}/{version}/{artifact_name}"


def run_manifest_generator(
    bundle_dir: Path,
    artifact_url: str,
    output_path: Path,
    signing_key: Path | None,
    signature_algo: str,
    signature_key_id: str,
    allow_downgrade: bool,
) -> None:
    script_path = Path(__file__).resolve().parent / "generate_ota_manifest.py"
    command = [
        sys.executable,
        str(script_path),
        "--build-dir",
        str(bundle_dir),
        "--artifact-url",
        artifact_url,
        "--output",
        str(output_path),
    ]
    if signing_key is not None:
        command.extend(["--signing-key", str(signing_key)])
        command.extend(["--signature-algo", signature_algo])
        command.extend(["--signature-key-id", signature_key_id])
    if allow_downgrade:
        command.append("--allow-downgrade")
    subprocess.run(command, check=True)


def write_release_metadata(
    output_path: Path,
    *,
    channel: str,
    version: str,
    artifact_name: str,
    artifact_url: str,
    manifest_name: str,
    source_channel: str,
    signed: bool,
) -> None:
    output_path.write_text(
        (
            "{\n"
            f'  "channel": "{channel}",\n'
            f'  "version": "{version}",\n'
            f'  "artifact_name": "{artifact_name}",\n'
            f'  "artifact_url": "{artifact_url}",\n'
            f'  "manifest_name": "{manifest_name}",\n'
            f'  "manifest_url": "{artifact_url.rsplit("/", 1)[0]}/{manifest_name}",\n'
            f'  "signed_manifest": {"true" if signed else "false"},\n'
            f'  "promoted_from_channel": "{source_channel}"\n'
            "}\n"
        ),
        encoding="utf-8",
    )


def main() -> int:
    args = parse_args()
    source_dir = args.input_dir / args.source_channel / args.version
    if not source_dir.is_dir():
        raise SystemExit(f"Source OTA bundle not found: {source_dir}")

    project_description = require_file(source_dir / "project_description.json", "project_description.json")
    if args.artifact_name:
        artifact_name = args.artifact_name
        require_file(source_dir / artifact_name, "artifact")
    else:
        candidates = sorted(path.name for path in source_dir.glob("*.bin"))
        if not candidates:
            raise SystemExit(f"No artifact binary found in source bundle: {source_dir}")
        artifact_name = candidates[0]

    target_dir = args.input_dir / args.target_channel / args.version
    if target_dir.exists():
        if not args.force:
            raise SystemExit(f"Target OTA bundle already exists: {target_dir} (use --force to overwrite)")
        shutil.rmtree(target_dir)
    target_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(source_dir / artifact_name, target_dir / artifact_name)
    shutil.copy2(project_description, target_dir / "project_description.json")

    artifact_url = build_artifact_url(args.artifact_base_url, args.target_channel, args.version, artifact_name)
    manifest_path = target_dir / args.manifest_name
    run_manifest_generator(
        target_dir,
        artifact_url,
        manifest_path,
        args.signing_key,
        args.signature_algo,
        args.signature_key_id,
        args.allow_downgrade,
    )

    write_release_metadata(
        target_dir / "release-metadata.json",
        channel=args.target_channel,
        version=args.version,
        artifact_name=artifact_name,
        artifact_url=artifact_url,
        manifest_name=args.manifest_name,
        source_channel=args.source_channel,
        signed=args.signing_key is not None,
    )

    if args.verify_artifact_url:
        verify_command = [
            sys.executable,
            str(Path(__file__).resolve().parent / "package_ota_release.py"),
            "--build-dir",
            str(target_dir),
            "--output-dir",
            str(args.input_dir / "_tmp_verify"),
            "--artifact-base-url",
            args.artifact_base_url,
            "--channel",
            args.target_channel,
            "--version",
            args.version,
            "--artifact-name",
            artifact_name,
            "--manifest-name",
            args.manifest_name,
            "--url-timeout-sec",
            str(args.url_timeout_sec),
            "--verify-artifact-url",
        ]
        subprocess.run(verify_command, check=True)
        shutil.rmtree(args.input_dir / "_tmp_verify", ignore_errors=True)

    print("Promoted OTA release bundle:")
    print(f"  from: {source_dir}")
    print(f"  to: {target_dir}")
    print(f"  artifact url: {artifact_url}")
    print(f"  signed manifest: {'yes' if args.signing_key is not None else 'no'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
