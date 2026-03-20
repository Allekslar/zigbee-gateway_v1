#!/usr/bin/env python3
"""Package a staging/production OTA release bundle from an ESP-IDF build."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True, help="ESP-IDF build directory")
    parser.add_argument("--output-dir", type=Path, required=True, help="Root directory for packaged OTA bundles")
    parser.add_argument(
        "--artifact-base-url",
        required=True,
        help="Base public URL where bundles are expected to be served, e.g. https://staging.example.com/ota",
    )
    parser.add_argument(
        "--channel",
        default="staging",
        choices=("staging", "production"),
        help="Release channel subdirectory (default: staging)",
    )
    parser.add_argument("--version", help="Override packaged version (default: project_version from build metadata)")
    parser.add_argument("--artifact-name", help="Override artifact filename inside the bundle")
    parser.add_argument("--manifest-name", default="ota-manifest.json", help="Manifest filename (default: ota-manifest.json)")
    parser.add_argument("--signing-key", type=Path, help="Optional PEM private key for signed manifests")
    parser.add_argument("--allow-downgrade", action="store_true", help="Set allow_downgrade=true in generated manifest")
    parser.add_argument(
        "--verify-artifact-url",
        action="store_true",
        help="Perform a simple HTTP HEAD/GET sanity check against the final artifact URL",
    )
    parser.add_argument(
        "--url-timeout-sec",
        type=int,
        default=10,
        help="Timeout for --verify-artifact-url (default: 10)",
    )
    return parser.parse_args()


def require_file(path: Path, description: str) -> Path:
    if not path.is_file():
        raise SystemExit(f"{description} not found: {path}")
    return path


def load_project_description(build_dir: Path) -> dict:
    path = require_file(build_dir / "project_description.json", "project_description.json")
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


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


def build_artifact_url(base_url: str, channel: str, version: str, artifact_name: str) -> str:
    return f"{base_url.rstrip('/')}/{channel}/{version}/{artifact_name}"


def verify_artifact_url(url: str, timeout_sec: int) -> None:
    request = urllib.request.Request(url, method="HEAD")
    try:
        with urllib.request.urlopen(request, timeout=timeout_sec) as response:
            status = getattr(response, "status", 200)
            if status < 200 or status >= 400:
                raise SystemExit(f"Artifact URL check failed: {url} returned HTTP {status}")
            return
    except urllib.error.HTTPError as exc:
        raise SystemExit(f"Artifact URL check failed: {url} returned HTTP {exc.code}") from exc
    except urllib.error.URLError:
        request = urllib.request.Request(url, method="GET")
        try:
            with urllib.request.urlopen(request, timeout=timeout_sec) as response:
                status = getattr(response, "status", 200)
                if status < 200 or status >= 400:
                    raise SystemExit(f"Artifact URL check failed: {url} returned HTTP {status}")
        except Exception as exc:  # noqa: BLE001
            raise SystemExit(f"Artifact URL check failed: {url} ({exc})") from exc


def run_manifest_generator(
    build_dir: Path,
    artifact_url: str,
    output_path: Path,
    version: str | None,
    signing_key: Path | None,
    allow_downgrade: bool,
) -> None:
    script_path = Path(__file__).resolve().parent / "generate_ota_manifest.py"
    command = [
        sys.executable,
        str(script_path),
        "--build-dir",
        str(build_dir),
        "--artifact-url",
        artifact_url,
        "--output",
        str(output_path),
    ]
    if version:
        command.extend(["--version", version])
    if signing_key is not None:
        command.extend(["--signing-key", str(signing_key)])
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
    signed: bool,
) -> None:
    metadata = {
        "channel": channel,
        "version": version,
        "artifact_name": artifact_name,
        "artifact_url": artifact_url,
        "manifest_name": manifest_name,
        "manifest_url": f"{artifact_url.rsplit('/', 1)[0]}/{manifest_name}",
        "signed_manifest": signed,
    }
    with output_path.open("w", encoding="utf-8") as handle:
        json.dump(metadata, handle, indent=2, sort_keys=False)
        handle.write("\n")


def main() -> int:
    args = parse_args()
    description = load_project_description(args.build_dir)
    app_bin = locate_app_bin(args.build_dir, description)

    version = args.version or description.get("project_version")
    if not isinstance(version, str) or not version:
        raise SystemExit("Could not determine release version from project_description.json")

    artifact_name = args.artifact_name or app_bin.name
    bundle_dir = args.output_dir / args.channel / version
    bundle_dir.mkdir(parents=True, exist_ok=True)

    artifact_path = bundle_dir / artifact_name
    shutil.copy2(app_bin, artifact_path)
    shutil.copy2(args.build_dir / "project_description.json", bundle_dir / "project_description.json")

    artifact_url = build_artifact_url(args.artifact_base_url, args.channel, version, artifact_name)
    manifest_path = bundle_dir / args.manifest_name
    run_manifest_generator(
        args.build_dir,
        artifact_url,
        manifest_path,
        args.version,
        args.signing_key,
        args.allow_downgrade,
    )

    write_release_metadata(
        bundle_dir / "release-metadata.json",
        channel=args.channel,
        version=version,
        artifact_name=artifact_name,
        artifact_url=artifact_url,
        manifest_name=args.manifest_name,
        signed=args.signing_key is not None,
    )

    if args.verify_artifact_url:
        verify_artifact_url(artifact_url, args.url_timeout_sec)

    print("Packaged OTA release bundle:")
    print(f"  channel: {args.channel}")
    print(f"  version: {version}")
    print(f"  bundle dir: {bundle_dir}")
    print(f"  artifact: {artifact_path}")
    print(f"  manifest: {manifest_path}")
    print(f"  artifact url: {artifact_url}")
    print(f"  signed manifest: {'yes' if args.signing_key is not None else 'no'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
