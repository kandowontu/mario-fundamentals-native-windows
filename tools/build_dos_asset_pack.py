#!/usr/bin/env python3
"""Build the deterministic embedded asset pack for the DOS edition."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


MAGIC = b"MARIOFPK"
VERSION = 1
ENTRY_SIZE = 20


def normalize_type(resource_type: str) -> bytes:
    encoded = resource_type.encode("ascii")
    if not 1 <= len(encoded) <= 4:
        raise ValueError(f"invalid DOS resource type {resource_type!r}")
    return encoded.ljust(4, b" ")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="manifest from rip_dos_prs.py")
    parser.add_argument("resource_root", type=Path, help="extracted DOS resource directory")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    resources = sorted(
        manifest["resources"], key=lambda item: (normalize_type(str(item["type"])), int(item["id"]))
    )
    header_bytes = len(MAGIC) + 12 + len(resources) * ENTRY_SIZE
    payload = bytearray()
    entries: list[bytes] = []
    for item in resources:
        while (header_bytes + len(payload)) % 4:
            payload.append(0)
        resource = (args.resource_root / str(item["path"])).read_bytes()
        if len(resource) != int(item["bytes"]):
            raise ValueError(f"resource length differs from manifest: {item['path']}")
        offset = header_bytes + len(payload)
        entries.append(
            struct.pack(
                "<4shHIII",
                normalize_type(str(item["type"])),
                int(item["id"]),
                int(item["flags"]),
                offset,
                len(resource),
                0,
            )
        )
        payload.extend(resource)

    output = bytearray(MAGIC)
    output.extend(struct.pack("<III", VERSION, len(entries), ENTRY_SIZE))
    output.extend(b"".join(entries))
    output.extend(payload)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"PASS resources={len(entries)} bytes={len(output)} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
