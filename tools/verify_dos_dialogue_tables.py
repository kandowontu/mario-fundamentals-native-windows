#!/usr/bin/env python3
"""Verify the original DOS host-dialogue tables and recovered live call sites."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


EXPECTED_EXE_SHA256 = "D722F8B08E02C53020B1428A224A2D4EA4FAB4A6B3E79FC9D294613C2AE70877"
DATA_SEGMENT_FILE_OFFSET = 0x35E70
RECORD = struct.Struct("<HHH")
TABLES = {
    "backgammon": (0x0692, 59, "9D9D7FF5B77665A09FA824A3048E270FBD16FF960785CE707CF6A08576559006"),
    "checkers": (0x15C6, 91, "45D102ACE6059DFA33AD8FF2431817A25600065C15FA7366BA9F3665A2D8EDF0"),
    "go_fish": (0x358C, 87, "AE28D4BDEE5337BFA8E77C456E55ABA765AFDEE9ABB13F30088B80DBB2565BB8"),
    "yacht": (0x568A, 74, "BC6E364948E316B8510D7BDF1C609269B7E17B2BA0B8F70AE5EC5BB5C89D4817"),
}
KEY_ROUTES = {
    ("backgammon", 18): 11618,
    ("backgammon", 58): 11093,
    ("checkers", 72): 11093,
    ("go_fish", 28): 11528,
    ("go_fish", 29): 11529,
    ("go_fish", 31): 11531,
    ("go_fish", 32): 11532,
    ("go_fish", 38): 11538,
    ("go_fish", 47): 11547,
    ("go_fish", 48): 11548,
    ("go_fish", 51): 11551,
    ("go_fish", 53): 11553,
    ("go_fish", 71): 11571,
    ("go_fish", 72): 11572,
    ("yacht", 55): 11455,
    ("yacht", 56): 11456,
    ("yacht", 57): 11457,
    ("yacht", 58): 11458,
    ("yacht", 62): 11468,
    ("yacht", 63): 11469,
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def fail(message: str) -> None:
    raise SystemExit(f"FAIL {message}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable_manifest", type=Path)
    parser.add_argument("executable", type=Path)
    parser.add_argument("overlay_zero", type=Path)
    parser.add_argument("--resource-manifest", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.executable_manifest.read_text(encoding="utf-8"))
    executable = args.executable.read_bytes()
    executable_hash = sha256(executable)
    declared_hash = str(manifest["source"]["sha256"]).upper()
    if executable_hash != EXPECTED_EXE_SHA256 or declared_hash != EXPECTED_EXE_SHA256:
        fail("MARIO.EXE differs from the audited DOS 1.0 executable")

    decoded: dict[str, list[tuple[int, int, int]]] = {}
    table_report: dict[str, dict[str, object]] = {}
    for name, (relative_offset, count, expected_hash) in TABLES.items():
        start = DATA_SEGMENT_FILE_OFFSET + relative_offset
        raw = executable[start : start + count * RECORD.size]
        if len(raw) != count * RECORD.size or sha256(raw) != expected_hash:
            fail(f"{name} host-dialogue table changed")
        records = [RECORD.unpack_from(raw, index * RECORD.size) for index in range(count)]
        if any(flags != 0 for _, _, flags in records):
            fail(f"{name} host-dialogue table has unexpected nonzero flags")
        decoded[name] = records
        table_report[name] = {
            "file_offset": start,
            "records": count,
            "sha256": expected_hash,
        }

    for (table, index), expected_movie in KEY_ROUTES.items():
        actual_movie = decoded[table][index][1]
        if actual_movie != expected_movie:
            fail(f"{table} host index {index} maps to {actual_movie}, expected {expected_movie}")

    # Overlay 0 initializes the 59-entry Backgammon table, calls index 58 for
    # the first character question, and later calls index 18 for the opening
    # roll prompt. Pin the exact original bytes so the two indices cannot be
    # silently conflated again.
    overlay = args.overlay_zero.read_bytes()
    overlay_checks = {
        0x525E: bytes.fromhex("6A3B689206"),
        0x5277: bytes.fromhex("9A00004800"),
        0x0C45: bytes.fromhex("6A3A9AA0014800"),
        0x2BC9: bytes.fromhex("6A129AA0014800"),
    }
    for offset, expected in overlay_checks.items():
        if overlay[offset : offset + len(expected)] != expected:
            fail(f"Backgammon overlay call-site bytes changed at 0x{offset:04X}")

    resource_ids: set[int] = set()
    if args.resource_manifest:
        resource_manifest = json.loads(args.resource_manifest.read_text(encoding="utf-8"))
        resource_ids = {
            int(item["id"])
            for item in resource_manifest["resources"]
            if str(item["type"]).strip() == "MuV"
        }
        table_movie_ids = {
            movie_id
            for records in decoded.values()
            for _, movie_id, _ in records
            if movie_id != 0
        }
        missing_movies = sorted(table_movie_ids - resource_ids)
        if missing_movies:
            fail(f"host-dialogue movies are absent from the DOS resource manifest: {missing_movies}")

    report = {
        "status": "PASS",
        "executable_sha256": executable_hash,
        "tables": table_report,
        "records": sum(item[1] for item in TABLES.values()),
        "key_routes": {
            f"{table}[{index}]": movie for (table, index), movie in KEY_ROUTES.items()
        },
        "overlay_zero_call_sites": {
            f"0x{offset:04X}": expected.hex().upper()
            for offset, expected in overlay_checks.items()
        },
        "resource_manifest_checked": args.resource_manifest is not None,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "PASS "
        f"dos_dialogue_tables={len(TABLES)} records={report['records']} "
        f"key_routes={len(KEY_ROUTES)}"
    )


if __name__ == "__main__":
    main()
