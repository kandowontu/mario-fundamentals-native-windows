#!/usr/bin/env python3
"""Extract and verify Presage's DOS PRD/PRS resource pair.

The Mario's Game Gallery 1.0 directory is stored in MARIO.PRD.  Each
24-byte directory entry points past a 28-byte resource header in MARIO.PRS
to the corresponding payload.  This tool verifies both copies of the type,
ID, and length before writing any data.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path


PRS_SIGNATURE = b"PRS Format Resource File\r\n"
PRD_DIRECTORY_OFFSET = 0x80
PRD_ENTRY_COUNT_OFFSET = 0x8C
PRD_ENTRY_TABLE_OFFSET = 0xB0
PRD_ENTRY_SIZE = 24
PRS_RESOURCE_HEADER_SIZE = 28


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def decode_c_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("cp437")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prd", type=Path, help="MARIO.PRD directory")
    parser.add_argument("prs", type=Path, help="MARIO.PRS payload file")
    parser.add_argument("output", type=Path, help="resource output directory")
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    prd = args.prd.read_bytes()
    prs = args.prs.read_bytes()
    if len(prd) < PRD_ENTRY_TABLE_OFFSET:
        raise SystemExit("PRD is shorter than its fixed directory header")
    if not prs.startswith(PRS_SIGNATURE):
        raise SystemExit("PRS signature is missing")

    resource_file = decode_c_string(prd[2:PRD_DIRECTORY_OFFSET])
    directory_version = struct.unpack_from("<I", prd, 0x84)[0]
    entry_count = struct.unpack_from("<H", prd, PRD_ENTRY_COUNT_OFFSET)[0]
    entry_table_end = PRD_ENTRY_TABLE_OFFSET + entry_count * PRD_ENTRY_SIZE
    if entry_table_end > len(prd):
        raise SystemExit("PRD entry count extends beyond the file")
    if any(prd[entry_table_end:]):
        raise SystemExit("PRD has unexplained nonzero data after its entry table")

    args.output.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, object]] = []
    seen: set[tuple[str, int]] = set()

    for index in range(entry_count):
        entry_offset = PRD_ENTRY_TABLE_OFFSET + index * PRD_ENTRY_SIZE
        payload_offset, raw_type, resource_id, length, metadata, flags = struct.unpack_from(
            "<I4sIIII", prd, entry_offset
        )
        resource_type = raw_type.rstrip(b"\0").decode("ascii")
        key = (resource_type, resource_id)
        if key in seen:
            raise SystemExit(f"duplicate resource key {resource_type}:{resource_id}")
        seen.add(key)

        header_offset = payload_offset - PRS_RESOURCE_HEADER_SIZE
        payload_end = payload_offset + length
        if header_offset < len(PRS_SIGNATURE) or payload_end > len(prs):
            raise SystemExit(f"resource {index} points outside PRS")
        header_type, header_id, raw_name, reserved, record_length = struct.unpack_from(
            "<4sH16sHI", prs, header_offset
        )
        if header_type.rstrip(b"\0").decode("ascii") != resource_type:
            raise SystemExit(f"resource {index} type differs between PRD and PRS")
        if header_id != resource_id:
            raise SystemExit(f"resource {index} ID differs between PRD and PRS")
        if record_length != length + PRS_RESOURCE_HEADER_SIZE:
            raise SystemExit(f"resource {index} length differs between PRD and PRS")
        if index == 0:
            if header_offset != 0x30:
                raise SystemExit("first PRS resource does not follow the fixed file header")
        else:
            previous = records[-1]
            expected_header = int(previous["offset"]) + int(previous["bytes"])
            if header_offset != expected_header:
                raise SystemExit(f"resource {index} breaks the PRS payload chain")

        payload = prs[payload_offset:payload_end]
        suffix = resource_type.lower()
        relative = Path(resource_type) / f"{resource_id:05d}.{suffix}"
        destination = args.output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            if destination.read_bytes() != payload:
                raise SystemExit(f"existing extracted resource differs: {destination}")
        else:
            destination.write_bytes(payload)

        records.append(
            {
                "index": index,
                "type": resource_type,
                "id": resource_id,
                "name": decode_c_string(raw_name),
                "reserved": reserved,
                "record_bytes": record_length,
                "offset": payload_offset,
                "bytes": length,
                "sha256": sha256(payload),
                "metadata": f"0x{metadata:08X}",
                "flags": flags,
                "path": relative.as_posix(),
            }
        )

    final_end = int(records[-1]["offset"]) + int(records[-1]["bytes"])
    if final_end != len(prs):
        raise SystemExit(f"PRS has {len(prs) - final_end} unexplained trailing bytes")

    counts = Counter(str(record["type"]) for record in records)
    manifest = {
        "schema": 1,
        "format": {
            "name": "Presage DOS PRD/PRS",
            "directory_version": directory_version,
            "directory_offset": PRD_DIRECTORY_OFFSET,
            "entry_table_offset": PRD_ENTRY_TABLE_OFFSET,
            "entry_size": PRD_ENTRY_SIZE,
            "resource_header_size": PRS_RESOURCE_HEADER_SIZE,
        },
        "source": {
            "resource_file": resource_file,
            "prd": {
                "path": str(args.prd),
                "bytes": len(prd),
                "sha256": sha256(prd),
            },
            "prs": {
                "path": str(args.prs),
                "bytes": len(prs),
                "sha256": sha256(prs),
            },
        },
        "resource_count": len(records),
        "type_counts": dict(sorted(counts.items())),
        "resources": records,
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        "PASS "
        f"resources={len(records)} "
        + " ".join(f"{name.lower()}={count}" for name, count in sorted(counts.items()))
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
