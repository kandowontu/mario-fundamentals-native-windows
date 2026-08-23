#!/usr/bin/env python3
"""Audit and extract a Borland FBOV/VROOMM DOS executable.

Mario's Game Gallery 1.0 was linked by Borland TLINK 5.0 with VROOMM
overlays.  The normal MZ image is followed by a 16-byte FBOV header, an
overlay payload area, and an eight-byte-per-entry segment table.  Overlay
segment entries point at 32-byte resident stubs whose file offsets describe
the corresponding code and relocation streams in the FBOV area.

This tool deliberately does not guess functions.  It validates the exact
container layout, extracts each overlay as immutable evidence, and writes a
machine-readable manifest that later disassembly/function ledgers can cite.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path


MZ_HEADER_MINIMUM = 0x1C
FBOV_HEADER_SIZE = 16
FBOV_STUB_SIZE = 32
SEGMENT_ENTRY_SIZE = 8


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def mz_declared_size(data: bytes) -> int:
    bytes_in_last_page = u16(data, 2)
    page_count = u16(data, 4)
    if page_count == 0:
        raise ValueError("MZ page count is zero")
    return (page_count - 1) * 512 + (bytes_in_last_page or 512)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, help="original DOS MZ executable")
    parser.add_argument("output", type=Path, help="overlay extraction directory")
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    executable = args.executable.read_bytes()
    if len(executable) < MZ_HEADER_MINIMUM or executable[:2] != b"MZ":
        raise SystemExit("input is not an MZ executable")

    header_bytes = u16(executable, 8) * 16
    relocation_count = u16(executable, 6)
    relocation_table_offset = u16(executable, 0x18)
    relocation_table_end = relocation_table_offset + relocation_count * 4
    declared_size = mz_declared_size(executable)
    if header_bytes < MZ_HEADER_MINIMUM or header_bytes > declared_size:
        raise SystemExit("MZ header size is outside the declared image")
    if relocation_table_offset < MZ_HEADER_MINIMUM or relocation_table_end > header_bytes:
        raise SystemExit("MZ relocation table is outside the header")
    if declared_size + FBOV_HEADER_SIZE > len(executable):
        raise SystemExit("MZ image has no complete FBOV header")

    relocations: list[dict[str, int | str]] = []
    unique_relocation_segments: set[int] = set()
    for index in range(relocation_count):
        offset = relocation_table_offset + index * 4
        target_offset, target_segment = struct.unpack_from("<HH", executable, offset)
        target_file_offset = header_bytes + target_segment * 16 + target_offset
        if target_file_offset + 2 > declared_size:
            raise SystemExit(f"MZ relocation {index} points outside the resident image")
        unique_relocation_segments.add(target_segment)
        relocations.append(
            {
                "index": index,
                "offset": f"0x{target_offset:04X}",
                "segment": f"0x{target_segment:04X}",
                "resident_file_offset": f"0x{target_file_offset:X}",
            }
        )

    fbov_offset = declared_size
    magic, fbov_payload_bytes, segment_table_offset, segment_count = struct.unpack_from(
        "<4sIIi", executable, fbov_offset
    )
    if magic != b"FBOV":
        raise SystemExit(f"expected FBOV at MZ boundary 0x{fbov_offset:X}")
    fbov_end = fbov_offset + FBOV_HEADER_SIZE + fbov_payload_bytes
    if fbov_end != len(executable):
        raise SystemExit(
            f"FBOV size ends at 0x{fbov_end:X}, not executable end 0x{len(executable):X}"
        )
    if segment_count <= 0:
        raise SystemExit("FBOV segment count is not positive")
    segment_table_end = segment_table_offset + segment_count * SEGMENT_ENTRY_SIZE
    # TLINK stores __SEGTABLE__ in the resident image; ``stofs`` is an
    # absolute file offset, not an offset into the trailing FBOV payload.
    if segment_table_offset < header_bytes or segment_table_end > declared_size:
        raise SystemExit("FBOV segment table lies outside the resident image")

    args.output.mkdir(parents=True, exist_ok=True)
    segments: list[dict[str, object]] = []
    overlays: list[dict[str, object]] = []
    flag_counts: Counter[str] = Counter()
    overlay_ranges: list[tuple[int, int, int]] = []

    for index in range(segment_count):
        entry_offset = segment_table_offset + index * SEGMENT_ENTRY_SIZE
        segment, maximum_offset, flags, minimum_offset = struct.unpack_from(
            "<HHHH", executable, entry_offset
        )
        flag_key = f"0x{flags:04X}"
        flag_counts[flag_key] += 1
        record: dict[str, object] = {
            "index": index,
            "segment": f"0x{segment:04X}",
            "maximum_offset": f"0x{maximum_offset:04X}",
            "minimum_offset": f"0x{minimum_offset:04X}",
            "flags": flag_key,
            "is_code": bool(flags & 0x0001),
            "is_overlay": bool(flags & 0x0002),
            "is_data": bool(flags & 0x0004),
        }

        if flags & 0x0002:
            stub_offset = header_bytes + segment * 16
            if stub_offset < header_bytes or stub_offset + FBOV_STUB_SIZE > declared_size:
                raise SystemExit(f"overlay segment {index} has a stub outside the resident image")
            (
                trap,
                saved_return,
                relative_file_offset,
                code_bytes,
                fixup_bytes,
                jump_count,
                link,
                buffer_segment,
                retry_count,
                next_overlay,
                ems_page,
                ems_offset,
                user,
            ) = struct.unpack_from("<2sHiHHHHHHHHH6s", executable, stub_offset)
            if trap != b"\xCD\x3F":
                raise SystemExit(
                    f"overlay segment {index} lacks its INT 3F trap at 0x{stub_offset:X}"
                )
            code_offset = fbov_offset + FBOV_HEADER_SIZE + relative_file_offset
            code_end = code_offset + code_bytes
            fixup_end = code_end + fixup_bytes
            if code_offset < fbov_offset + FBOV_HEADER_SIZE or fixup_end > len(executable):
                raise SystemExit(f"overlay segment {index} payload lies outside the FBOV area")

            code = executable[code_offset:code_end]
            fixups = executable[code_end:fixup_end]
            stem = f"overlay-{len(overlays):02d}-seg-{segment:04X}"
            code_relative = Path("code") / f"{stem}.bin"
            fixup_relative = Path("fixups") / f"{stem}.bin"
            for relative, payload in ((code_relative, code), (fixup_relative, fixups)):
                destination = args.output / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                if destination.exists() and destination.read_bytes() != payload:
                    raise SystemExit(f"existing extraction differs: {destination}")
                if not destination.exists():
                    destination.write_bytes(payload)

            overlay_index = len(overlays)
            overlay_ranges.append((code_offset, fixup_end, overlay_index))
            overlay_record: dict[str, object] = {
                "index": overlay_index,
                "segment_table_index": index,
                "segment": f"0x{segment:04X}",
                "stub_offset": f"0x{stub_offset:X}",
                "saved_return": f"0x{saved_return:04X}",
                "relative_file_offset": f"0x{relative_file_offset:X}",
                "code_offset": f"0x{code_offset:X}",
                "code_bytes": code_bytes,
                "code_sha256": sha256(code),
                "code_path": code_relative.as_posix(),
                "fixup_offset": f"0x{code_end:X}",
                "fixup_bytes": fixup_bytes,
                "fixup_sha256": sha256(fixups),
                "fixup_path": fixup_relative.as_posix(),
                "jump_count": jump_count,
                "link": f"0x{link:04X}",
                "buffer_segment": f"0x{buffer_segment:04X}",
                "retry_count": retry_count,
                "next_overlay": f"0x{next_overlay:04X}",
                "ems_page": f"0x{ems_page:04X}",
                "ems_offset": f"0x{ems_offset:04X}",
                "user": user.hex().upper(),
            }
            overlays.append(overlay_record)
            record["overlay_index"] = overlay_index
            record["stub_offset"] = f"0x{stub_offset:X}"

        segments.append(record)

    overlay_ranges.sort()
    for previous, current in zip(overlay_ranges, overlay_ranges[1:]):
        if previous[1] > current[0]:
            raise SystemExit(
                f"overlay payloads {previous[2]} and {current[2]} overlap in the FBOV area"
            )

    manifest = {
        "schema": 1,
        "format": "Borland FBOV/VROOMM executable",
        "source": {
            "path": str(args.executable),
            "bytes": len(executable),
            "sha256": sha256(executable),
        },
        "mz": {
            "declared_bytes": declared_size,
            "header_bytes": header_bytes,
            "resident_image_bytes": declared_size - header_bytes,
            "relocation_count": relocation_count,
            "relocation_table_offset": f"0x{relocation_table_offset:X}",
            "unique_relocation_segments": len(unique_relocation_segments),
            "minimum_allocation_paragraphs": u16(executable, 0x0A),
            "maximum_allocation_paragraphs": u16(executable, 0x0C),
            "initial_ss": f"0x{u16(executable, 0x0E):04X}",
            "initial_sp": f"0x{u16(executable, 0x10):04X}",
            "initial_ip": f"0x{u16(executable, 0x14):04X}",
            "initial_cs": f"0x{u16(executable, 0x16):04X}",
            "overlay_number": u16(executable, 0x1A),
            "relocations": relocations,
        },
        "fbov": {
            "offset": f"0x{fbov_offset:X}",
            "payload_bytes": fbov_payload_bytes,
            "segment_table_offset": f"0x{segment_table_offset:X}",
            "segment_count": segment_count,
            "segment_flag_counts": dict(sorted(flag_counts.items())),
            "overlay_count": len(overlays),
            "segments": segments,
            "overlays": overlays,
        },
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        "PASS "
        f"mz={declared_size} relocs={relocation_count} segments={segment_count} "
        f"overlays={len(overlays)} fbov={fbov_payload_bytes}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
