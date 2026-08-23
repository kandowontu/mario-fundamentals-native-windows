#!/usr/bin/env python3
"""Prove the DOS PRD/PRS rip, deterministic pack, and release agree exactly."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path


MAGIC = b"MARIOFPK"
PACK_HEADER = struct.Struct("<8sIII")
PACK_ENTRY = struct.Struct("<4shHIII")
PRD_ENTRY = struct.Struct("<I4sIIII")
PRS_HEADER = struct.Struct("<4sH16sHI")
PRS_SIGNATURE = b"PRS Format Resource File\r\n"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def fail(message: str) -> None:
    raise SystemExit(f"FAIL {message}")


def normalized_type(value: str) -> bytes:
    encoded = value.encode("ascii")
    if not 1 <= len(encoded) <= 4:
        fail(f"invalid resource type {value!r}")
    return encoded.ljust(4, b" ")


def verify_source_pair(manifest: dict, prd_path: Path, prs_path: Path) -> None:
    prd = prd_path.read_bytes()
    prs = prs_path.read_bytes()
    source = manifest["source"]
    if len(prd) != int(source["prd"]["bytes"]) or sha256(prd) != source["prd"]["sha256"].upper():
        fail("DOS PRD does not match the extraction manifest")
    if len(prs) != int(source["prs"]["bytes"]) or sha256(prs) != source["prs"]["sha256"].upper():
        fail("DOS PRS does not match the extraction manifest")
    if not prs.startswith(PRS_SIGNATURE):
        fail("DOS PRS signature changed")

    table = int(manifest["format"]["entry_table_offset"])
    entry_size = int(manifest["format"]["entry_size"])
    header_size = int(manifest["format"]["resource_header_size"])
    resources = manifest["resources"]
    if entry_size != PRD_ENTRY.size or header_size != PRS_HEADER.size:
        fail("DOS manifest record sizes are unsupported")
    for index, item in enumerate(resources):
        entry_offset = table + index * entry_size
        if entry_offset + entry_size > len(prd):
            fail(f"DOS PRD entry {index} is truncated")
        payload_offset, raw_type, resource_id, length, metadata, flags = PRD_ENTRY.unpack_from(
            prd, entry_offset
        )
        header_offset = payload_offset - header_size
        if header_offset < 0 or payload_offset + length > len(prs):
            fail(f"DOS resource {index} escapes the PRS")
        header_type, header_id, _name, reserved, record_length = PRS_HEADER.unpack_from(
            prs, header_offset
        )
        expected_type = normalized_type(str(item["type"])).rstrip(b" ")
        if raw_type.rstrip(b"\0") != expected_type or header_type.rstrip(b"\0") != expected_type:
            fail(f"DOS resource {index} type changed")
        if resource_id != int(item["id"]) or header_id != int(item["id"]):
            fail(f"DOS resource {index} ID changed")
        if payload_offset != int(item["offset"]) or length != int(item["bytes"]):
            fail(f"DOS resource {index} PRD location changed")
        if metadata != int(str(item["metadata"]), 16) or flags != int(item["flags"]):
            fail(f"DOS resource {index} PRD metadata changed")
        if reserved != int(item["reserved"]) or record_length != int(item["record_bytes"]):
            fail(f"DOS resource {index} PRS header changed")
        payload = prs[payload_offset : payload_offset + length]
        if sha256(payload) != str(item["sha256"]).upper():
            fail(f"DOS PRS payload changed for {item['type']} {item['id']}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("resource_root", type=Path)
    parser.add_argument("pack", type=Path)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--prd", type=Path)
    parser.add_argument("--prs", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    resources = sorted(
        manifest["resources"],
        key=lambda item: (normalized_type(str(item["type"])), int(item["id"])),
    )
    if len(resources) != int(manifest["resource_count"]):
        fail("DOS manifest resource count changed")
    actual_counts = Counter(str(item["type"]) for item in resources)
    if dict(sorted(actual_counts.items())) != {
        str(key): int(value) for key, value in manifest["type_counts"].items()
    }:
        fail("DOS manifest type counts changed")

    seen: set[tuple[bytes, int]] = set()
    payload_bytes = 0
    ripped: dict[tuple[bytes, int], bytes] = {}
    for item in resources:
        key = (normalized_type(str(item["type"])), int(item["id"]))
        if key in seen:
            fail(f"duplicate DOS resource {item['type']} {item['id']}")
        seen.add(key)
        data = (args.resource_root / str(item["path"])).read_bytes()
        if len(data) != int(item["bytes"]) or sha256(data) != str(item["sha256"]).upper():
            fail(f"ripped DOS resource changed: {item['type']} {item['id']}")
        ripped[key] = data
        payload_bytes += len(data)

    source_verified = args.prd is not None and args.prs is not None
    if (args.prd is None) != (args.prs is None):
        fail("both --prd and --prs are required to verify the source pair")
    if source_verified:
        verify_source_pair(manifest, args.prd, args.prs)

    pack = args.pack.read_bytes()
    if len(pack) < PACK_HEADER.size:
        fail("DOS asset pack is truncated")
    magic, version, count, entry_size = PACK_HEADER.unpack_from(pack)
    if magic != MAGIC or version != 1 or entry_size != PACK_ENTRY.size:
        fail("DOS asset pack header is invalid")
    if count != len(resources):
        fail("DOS asset pack count differs from the manifest")
    directory_end = PACK_HEADER.size + count * PACK_ENTRY.size
    if directory_end > len(pack):
        fail("DOS asset pack directory is truncated")

    previous_end = directory_end
    for index, item in enumerate(resources):
        type_code, resource_id, flags, offset, size, reserved = PACK_ENTRY.unpack_from(
            pack, PACK_HEADER.size + index * PACK_ENTRY.size
        )
        key = (normalized_type(str(item["type"])), int(item["id"]))
        if (type_code, resource_id) != key:
            fail(f"DOS asset pack order/key changed at entry {index}")
        if flags != int(item["flags"]) or size != int(item["bytes"]) or reserved != 0:
            fail(f"DOS asset pack metadata changed for {item['type']} {item['id']}")
        if offset % 4 or offset < previous_end or offset + size > len(pack):
            fail(f"DOS asset pack bounds changed at entry {index}")
        if any(pack[previous_end:offset]):
            fail(f"DOS asset pack alignment padding is nonzero before entry {index}")
        if pack[offset : offset + size] != ripped[key]:
            fail(f"DOS asset pack bytes differ for {item['type']} {item['id']}")
        previous_end = offset + size
    if previous_end != len(pack):
        fail("DOS asset pack has unexplained trailing bytes")

    executable = args.executable.read_bytes()
    executable_offset = executable.find(pack)
    if executable_offset < 0 or executable.find(pack, executable_offset + 1) >= 0:
        fail("release does not contain exactly one byte-identical DOS asset pack")

    report = {
        "status": "PASS",
        "source_prd_prs": {
            "verified": source_verified,
            "prd_bytes": int(manifest["source"]["prd"]["bytes"]),
            "prd_sha256": manifest["source"]["prd"]["sha256"].upper(),
            "prs_bytes": int(manifest["source"]["prs"]["bytes"]),
            "prs_sha256": manifest["source"]["prs"]["sha256"].upper(),
        },
        "resources": {
            "count": len(resources),
            "types": dict(sorted(actual_counts.items())),
            "payload_bytes": payload_bytes,
        },
        "asset_pack": {
            "bytes": len(pack),
            "sha256": sha256(pack),
            "executable_offset": executable_offset,
            "exact_occurrences_in_executable": 1,
        },
        "executable": {"bytes": len(executable), "sha256": sha256(executable)},
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "PASS "
        f"resources={len(resources)} pack={report['asset_pack']['sha256']} "
        f"exe={report['executable']['sha256']}"
    )


if __name__ == "__main__":
    main()
