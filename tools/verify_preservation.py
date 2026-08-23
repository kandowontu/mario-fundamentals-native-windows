#!/usr/bin/env python3
"""Prove the ripped resources, deterministic pack, and embedded release agree byte-for-byte."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


MAGIC = b"MARIOFPK"
HEADER = struct.Struct("<8sIII")
ENTRY = struct.Struct("<4shHIII")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fail(message: str) -> None:
    raise SystemExit(f"FAIL {message}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("pack", type=Path)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    rip_root = args.manifest.parent
    source = manifest["source"]
    application = manifest["application"]
    resources = sorted(
        manifest["resources"],
        key=lambda item: (bytes.fromhex(item["type_hex"]), int(item["id"])),
    )

    if len(resources) != int(application["resource_count"]):
        fail("manifest resource count disagrees with the application record")

    seen: set[tuple[bytes, int]] = set()
    ripped_bytes = 0
    for item in resources:
        key = (bytes.fromhex(item["type_hex"]), int(item["id"]))
        if key in seen:
            fail(f"duplicate manifest resource {key!r}")
        seen.add(key)
        data = (rip_root / item["path"]).read_bytes()
        if len(data) != int(item["size"]) or sha256(data) != item["sha256"].lower():
            fail(f"ripped resource changed: {item['type']!r} {item['id']}")
        ripped_bytes += len(data)

    if args.image:
        image = args.image.read_bytes()
        if len(image) != int(source["size"]) or sha256(image) != source["sha256"].lower():
            fail("source disk image does not match the extraction manifest")

    pack = args.pack.read_bytes()
    if len(pack) < HEADER.size:
        fail("asset pack is truncated")
    magic, version, count, entry_size = HEADER.unpack_from(pack)
    if magic != MAGIC or version != 1 or entry_size != ENTRY.size:
        fail("asset pack header is invalid")
    if count != len(resources):
        fail("asset pack resource count disagrees with the manifest")
    directory_end = HEADER.size + count * entry_size
    if directory_end > len(pack):
        fail("asset pack directory is truncated")

    previous_end = directory_end
    for index, item in enumerate(resources):
        type_code, resource_id, attributes, offset, size, reserved = ENTRY.unpack_from(
            pack, HEADER.size + index * entry_size
        )
        expected_type = bytes.fromhex(item["type_hex"])
        expected_id = int(item["id"])
        if (type_code, resource_id) != (expected_type, expected_id):
            fail(f"asset pack directory order/key changed at entry {index}")
        if attributes != int(item["attributes"]) or size != int(item["size"]):
            fail(f"asset pack metadata changed for {item['type']!r} {item['id']}")
        if reserved != 0 or offset % 4:
            fail(f"asset pack invariant changed for {item['type']!r} {item['id']}")
        if offset < previous_end or offset + size > len(pack):
            fail(f"asset pack bounds overlap or overflow at entry {index}")
        if any(pack[previous_end:offset]):
            fail(f"asset pack alignment padding is nonzero before entry {index}")
        ripped = (rip_root / item["path"]).read_bytes()
        if pack[offset : offset + size] != ripped:
            fail(f"packed bytes differ for {item['type']!r} {item['id']}")
        previous_end = offset + size
    if previous_end != len(pack):
        fail("asset pack has unexplained trailing bytes")

    executable = args.executable.read_bytes()
    first = executable.find(pack)
    if first < 0 or executable.find(pack, first + 1) >= 0:
        fail("release executable does not contain exactly one byte-identical asset pack")

    report = {
        "status": "PASS",
        "source_image": {
            "verified": args.image is not None,
            "bytes": int(source["size"]),
            "sha256": source["sha256"].upper(),
        },
        "resource_fork": {
            "bytes": int(application["resource_fork_size"]),
            "sha256": application["resource_fork_sha256"].upper(),
            "resources": len(resources),
            "resource_payload_bytes": ripped_bytes,
        },
        "asset_pack": {
            "bytes": len(pack),
            "sha256": sha256(pack).upper(),
            "executable_offset": first,
            "exact_occurrences_in_executable": 1,
        },
        "executable": {
            "bytes": len(executable),
            "sha256": sha256(executable).upper(),
        },
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "PASS "
        f"source={report['source_image']['sha256']} "
        f"resources={len(resources)} pack={report['asset_pack']['sha256']} "
        f"exe={report['executable']['sha256']}"
    )


if __name__ == "__main__":
    main()
