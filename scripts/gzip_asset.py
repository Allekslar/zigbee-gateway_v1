#!/usr/bin/env python3
"""Generate deterministic gzip-compressed assets for embedded web UI files."""

from __future__ import annotations

import gzip
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: gzip_asset.py <src> <dst>", file=sys.stderr)
        return 1

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    dst.parent.mkdir(parents=True, exist_ok=True)

    data = src.read_bytes()
    with gzip.GzipFile(filename="", mode="wb", fileobj=dst.open("wb"), compresslevel=9, mtime=0) as gz_file:
        gz_file.write(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
