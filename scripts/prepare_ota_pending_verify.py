#!/usr/bin/env python3
"""Prepare otadata so the next boot enters OTA pending-verify flow.

This script mirrors the essential otadata rewrite done by esp_ota_set_boot_partition()
when rollback is enabled:
  - compute the next ota_seq for the requested OTA slot
  - mark the selected entry as ESP_OTA_IMG_NEW
  - rewrite the chosen otadata sector on the device over serial

It is intended for local/HIL verification when the full HTTP OTA transport path
is not reachable from the host environment.
"""

from __future__ import annotations

import argparse
import binascii
import os
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass


ESP_OTA_IMG_NEW = 0x0
ENTRY_STRUCT = struct.Struct("<I20sII")
ENTRY_SIZE = ENTRY_STRUCT.size
SECTOR_SIZE = 0x1000


@dataclass
class OtaEntry:
    seq: int
    label: bytes
    state: int
    crc: int

    @classmethod
    def unpack_from(cls, blob: bytes, offset: int) -> "OtaEntry":
        seq, label, state, crc = ENTRY_STRUCT.unpack_from(blob, offset)
        return cls(seq=seq, label=label, state=state, crc=crc)

    def pack(self) -> bytes:
        return ENTRY_STRUCT.pack(self.seq, self.label, self.state, self.crc)

    def is_valid(self) -> bool:
        if self.seq == 0xFFFFFFFF:
            return False
        expected_crc = binascii.crc32(struct.pack("<I", self.seq), 0xFFFFFFFF) & 0xFFFFFFFF
        return self.crc == expected_crc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--slot", type=int, required=True, help="OTA slot number, e.g. 1 for ota_1")
    parser.add_argument("--ota-partitions", type=int, default=2, help="Number of OTA app slots")
    parser.add_argument("--otadata-offset", type=lambda value: int(value, 0), default=0xF000)
    parser.add_argument("--otadata-size", type=lambda value: int(value, 0), default=0x2000)
    return parser.parse_args()


def run_esptool(args: argparse.Namespace, *tool_args: str) -> None:
    cmd = [sys.executable, "-m", "esptool", "--port", args.port, *tool_args]
    subprocess.run(cmd, check=True)


def read_otadata(args: argparse.Namespace) -> bytes:
    with tempfile.NamedTemporaryFile(delete=False) as tmp:
        tmp_path = tmp.name

    try:
        run_esptool(args, "read_flash", hex(args.otadata_offset), str(args.otadata_size), tmp_path)
        with open(tmp_path, "rb") as handle:
            blob = handle.read()
    finally:
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)

    if len(blob) != args.otadata_size:
        raise RuntimeError(f"Unexpected otadata length: {len(blob)}")
    return blob


def choose_base_entry(entries: list[OtaEntry]) -> int:
    valid = [index for index, entry in enumerate(entries) if entry.is_valid()]
    if not valid:
        return -1
    if len(valid) == 1:
        return valid[0]
    return valid[0] if entries[valid[0]].seq >= entries[valid[1]].seq else valid[1]


def compute_next_seq(base_seq: int, target_seq: int, ota_partitions: int) -> int:
    i = 0
    reduced_target = target_seq % ota_partitions
    while base_seq > reduced_target + i * ota_partitions:
        i += 1
    return reduced_target + i * ota_partitions


def prepare_sector(args: argparse.Namespace, blob: bytes) -> tuple[int, bytes, int]:
    if args.otadata_size != 2 * SECTOR_SIZE:
        raise RuntimeError(f"Expected otadata size {2 * SECTOR_SIZE}, got {args.otadata_size}")

    entries = [
        OtaEntry.unpack_from(blob, 0),
        OtaEntry.unpack_from(blob, SECTOR_SIZE),
    ]
    base_index = choose_base_entry(entries)
    next_index = 0 if base_index == -1 else ((~base_index) & 0x1)

    target_seq = args.slot + 1
    if target_seq < 1 or target_seq > args.ota_partitions:
        raise RuntimeError(f"Invalid slot {args.slot} for {args.ota_partitions} OTA partitions")

    if base_index == -1:
        next_seq = target_seq
    else:
        next_seq = compute_next_seq(entries[base_index].seq, target_seq, args.ota_partitions)

    entry = entries[next_index]
    entry.seq = next_seq
    entry.state = ESP_OTA_IMG_NEW
    entry.crc = binascii.crc32(struct.pack("<I", entry.seq), 0xFFFFFFFF) & 0xFFFFFFFF

    sector_offset = next_index * SECTOR_SIZE
    sector = bytearray(blob[sector_offset : sector_offset + SECTOR_SIZE])
    sector[:ENTRY_SIZE] = entry.pack()
    return sector_offset, bytes(sector), next_seq


def write_sector(args: argparse.Namespace, sector_offset: int, sector: bytes) -> None:
    with tempfile.NamedTemporaryFile(delete=False) as tmp:
        tmp.write(sector)
        tmp_path = tmp.name

    try:
        run_esptool(args, "write_flash", hex(args.otadata_offset + sector_offset), tmp_path)
    finally:
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)


def main() -> int:
    args = parse_args()
    blob = read_otadata(args)
    sector_offset, sector, next_seq = prepare_sector(args, blob)
    write_sector(args, sector_offset, sector)
    print(
        f"Prepared otadata for ota_{args.slot}: "
        f"sector_offset=0x{sector_offset:x} next_seq={next_seq} state=ESP_OTA_IMG_NEW"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
