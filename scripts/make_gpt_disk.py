#!/usr/bin/env python3
# MAC-QEMU-FIX: GPT disk layout (ESP + MARROW) for UEFI/@: — keep when porting Linux+QEMU build.sh
"""Create a 64MiB GPT disk image like Linux+QEMU's build.sh (ESP + data).

Layout (512-byte sectors, 64MiB = 131072 sectors):
  LBA 0          protective MBR
  LBA 1          GPT header
  LBA 2..33      partition entries
  LBA 2048..65535     p1 ESP  (FAT16, BOOTX64.EFI)  offset 1048576
  LBA 65536..131038   p2 MARROW (FAT16, kernel files) offset 33554432
  LBA 131039..131070  backup entries
  LBA 131071     backup GPT header
"""
from __future__ import annotations

import os
import struct
import sys
import uuid
import zlib

SECTOR = 512
IMG_MIB = 64
TOTAL_SECTORS = IMG_MIB * 1024 * 1024 // SECTOR
P1_START = 2048
P1_END = 65535
P2_START = 65536
LAST_USABLE = TOTAL_SECTORS - 34  # 131038
P2_END = LAST_USABLE
ENTRY_COUNT = 128
ENTRY_SIZE = 128
ARRAY_BYTES = ENTRY_COUNT * ENTRY_SIZE
ARRAY_SECTORS = ARRAY_BYTES // SECTOR  # 32
HEADER_SIZE = 92

# EFI System Partition
GUID_ESP = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
# Microsoft basic data (FAT)
GUID_DATA = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"


def guid_bytes(text: str) -> bytes:
    a, b, c, d, e = text.split("-")
    return (
        bytes.fromhex(a)[::-1]
        + bytes.fromhex(b)[::-1]
        + bytes.fromhex(c)[::-1]
        + bytes.fromhex(d)
        + bytes.fromhex(e)
    )


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def utf16le_name(name: str) -> bytes:
    raw = name.encode("utf-16le")
    return (raw + b"\x00" * 72)[:72]


def gpt_header(
    *,
    my_lba: int,
    alt_lba: int,
    part_lba: int,
    disk_guid: bytes,
    parts_crc: int,
    header_crc: int = 0,
) -> bytes:
    body = struct.pack(
        "<8sIIIIQQQQ16sQIII",
        b"EFI PART",
        0x00010000,
        HEADER_SIZE,
        header_crc,
        0,
        my_lba,
        alt_lba,
        34,
        LAST_USABLE,
        disk_guid,
        part_lba,
        ENTRY_COUNT,
        ENTRY_SIZE,
        parts_crc,
    )
    return body + b"\x00" * (SECTOR - len(body))


def protective_mbr() -> bytes:
    # one 0xEE partition covering the rest of the disk
    start = 1
    size = min(0xFFFFFFFF, TOTAL_SECTORS - 1)
    part = bytes(
        [
            0x00,
            0x00,
            0x02,
            0x00,
            0xEE,
            0xFF,
            0xFF,
            0xFF,
        ]
    ) + struct.pack("<II", start, size)
    mbr = b"\x00" * 446 + part + b"\x00" * 48 + b"\x55\xAA"
    return mbr


def partition_entry(type_guid: str, name: str, start: int, end: int, unique: bytes) -> bytes:
    return struct.pack(
        "<16s16sQQQ72s",
        guid_bytes(type_guid),
        unique,
        start,
        end,
        0,
        utf16le_name(name),
    )


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: make_gpt_disk.py <image>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)

    disk_guid = uuid.uuid4().bytes_le
    esp_guid = uuid.uuid4().bytes_le
    data_guid = uuid.uuid4().bytes_le

    entries = b"".join(
        [
            partition_entry(GUID_ESP, "ESP", P1_START, P1_END, esp_guid),
            partition_entry(GUID_DATA, "MARROW", P2_START, P2_END, data_guid),
        ]
    )
    entries += b"\x00" * (ARRAY_BYTES - len(entries))
    parts_crc = crc32(entries)

    primary = gpt_header(
        my_lba=1,
        alt_lba=TOTAL_SECTORS - 1,
        part_lba=2,
        disk_guid=disk_guid,
        parts_crc=parts_crc,
        header_crc=0,
    )
    primary_crc = crc32(primary[:HEADER_SIZE])
    primary = gpt_header(
        my_lba=1,
        alt_lba=TOTAL_SECTORS - 1,
        part_lba=2,
        disk_guid=disk_guid,
        parts_crc=parts_crc,
        header_crc=primary_crc,
    )

    backup_array_lba = TOTAL_SECTORS - 1 - ARRAY_SECTORS
    backup = gpt_header(
        my_lba=TOTAL_SECTORS - 1,
        alt_lba=1,
        part_lba=backup_array_lba,
        disk_guid=disk_guid,
        parts_crc=parts_crc,
        header_crc=0,
    )
    backup_crc = crc32(backup[:HEADER_SIZE])
    backup = gpt_header(
        my_lba=TOTAL_SECTORS - 1,
        alt_lba=1,
        part_lba=backup_array_lba,
        disk_guid=disk_guid,
        parts_crc=parts_crc,
        header_crc=backup_crc,
    )

    with open(path, "wb") as f:
        f.truncate(IMG_MIB * 1024 * 1024)
        f.seek(0)
        f.write(protective_mbr())
        f.seek(1 * SECTOR)
        f.write(primary)
        f.seek(2 * SECTOR)
        f.write(entries)
        f.seek(backup_array_lba * SECTOR)
        f.write(entries)
        f.seek((TOTAL_SECTORS - 1) * SECTOR)
        f.write(backup)

    print(f"wrote GPT {path}")
    print(f"  ESP    LBA {P1_START}..{P1_END}  mtools {path}@@{P1_START * SECTOR}")
    print(f"  MARROW LBA {P2_START}..{P2_END}  mtools {path}@@{P2_START * SECTOR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
