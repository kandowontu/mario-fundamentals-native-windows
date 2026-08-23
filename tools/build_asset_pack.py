#!/usr/bin/env python3
"""Build the deterministic resource pack embedded in the Windows executable."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


MAGIC = b"MARIOFPK"
VERSION = 1
ENTRY_SIZE = 20


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("rip_manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.rip_manifest.read_text(encoding="utf-8"))
    rip_root = args.rip_manifest.parent
    resources = sorted(
        manifest["resources"], key=lambda item: (bytes.fromhex(item["type_hex"]), item["id"])
    )
    header_size = len(MAGIC) + 12 + len(resources) * ENTRY_SIZE
    payload = bytearray()
    entries = []
    for item in resources:
        while (header_size + len(payload)) % 4:
            payload.append(0)
        data = (rip_root / item["path"]).read_bytes()
        offset = header_size + len(payload)
        type_code = bytes.fromhex(item["type_hex"])
        if len(type_code) != 4:
            raise ValueError(f"invalid type code for {item}")
        entries.append(
            struct.pack(
                "<4shHIII",
                type_code,
                int(item["id"]),
                int(item["attributes"]),
                offset,
                len(data),
                0,
            )
        )
        payload.extend(data)

    output = bytearray(MAGIC)
    output.extend(struct.pack("<III", VERSION, len(entries), ENTRY_SIZE))
    for entry in entries:
        output.extend(entry)
    output.extend(payload)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"Packed {len(entries)} resources into {args.output} ({len(output)} bytes)")


if __name__ == "__main__":
    main()
