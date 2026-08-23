#!/usr/bin/env python3
"""Disassemble the game's 68020 CODE resources and summarize segment metadata."""

from __future__ import annotations

import argparse
import csv
import json
import re
import struct
from collections import Counter
from pathlib import Path

from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_020, Cs


TRAPS = {
    0xA000: "_Open",
    0xA001: "_Close",
    0xA002: "_Read",
    0xA003: "_Write",
    0xA025: "_GetTrapAddress",
    0xA02E: "_BlockMove",
    0xA03B: "_Delay",
    0xA023: "_DisposeHandle",
    0xA029: "_HLock",
    0xA02A: "_HUnlock",
    0xA044: "_SetFPos",
    0xA04A: "_HNoPurge",
    0xA055: "_StripAddress",
    0xA060: "_FSDispatch",
    0xA090: "_SysEnvirons",
    0xA122: "_NewPtrClear",
    0xA126: "_NewHandleClear",
    0xA146: "_GetTrapAddress",
    0xA346: "_GetOSTrapAddress",
    0xA746: "_GetToolTrapAddress",
    0xA81F: "_Get1Resource",
    0xA83F: "_Long2Fix",
    0xA840: "_Fix2Long",
    0xA851: "_SetCursor",
    0xA860: "_WaitNextEvent",
    0xA861: "_Random",
    0xA868: "_FixMul",
    0xA869: "_FixRatio",
    0xA870: "_LocalToGlobal",
    0xA86E: "_InitGraf",
    0xA873: "_SetPort",
    0xA874: "_GetPort",
    0xA884: "_DrawString",
    0xA887: "_TextFont",
    0xA888: "_TextFace",
    0xA88A: "_TextSize",
    0xA88B: "_GetFontInfo",
    0xA88C: "_StringWidth",
    0xA892: "_Line",
    0xA893: "_MoveTo",
    0xA8A7: "_SetRect",
    0xA8A8: "_OffsetRect",
    0xA8AA: "_SectRect",
    0xA8F6: "_DrawPicture",
    0xA900: "_GetFNum",
    0xA902: "_RealFont",
    0xA908: "_ShowHide",
    0xA910: "_GetWMgrPort",
    0xA914: "_DisposeWindow",
    0xA915: "_ShowWindow",
    0xA918: "_SetWRefCon",
    0xA91B: "_MoveWindow",
    0xA91D: "_SizeWindow",
    0xA92E: "_SetWindowPic",
    0xA92F: "_GetWindowPic",
    0xA850: "_InitFonts",
    0xA912: "_InitWindows",
    0xA930: "_InitMenus",
    0xA9CC: "_TEInit",
    0xA97B: "_InitDialogs",
    0xA975: "_TickCount",
    0xA850: "_InitFonts",
    0xA9A0: "_GetResource",
    0xA9A1: "_GetNamedResource",
    0xA9A2: "_LoadResource",
    0xA9A3: "_ReleaseResource",
    0xA9A4: "_HomeResFile",
    0xA9A5: "_SizeRsrc",
    0xA9A6: "_GetResAttrs",
    0xA9A7: "_SetResAttrs",
    0xA9A8: "_GetResInfo",
    0xA9A9: "_SetResInfo",
    0xA9AA: "_ChangedResource",
    0xA9AB: "_AddResource",
    0xA9AD: "_RmveResource",
    0xA9AF: "_ResError",
    0xA9B0: "_WriteResource",
    0xA9B1: "_CreateResFile",
    0xA9BC: "_GetPicture",
    0xA9BD: "_GetNewCWindow",
    0xAA14: "_RGBForeColor",
    0xA994: "_CurResFile",
    0xA995: "_InitResources",
    0xA996: "_RsrcZoneInit",
    0xA997: "_OpenResFile",
    0xA998: "_UseResFile",
    0xA999: "_UpdateResFile",
    0xA99A: "_CloseResFile",
    0xA99B: "_SetResLoad",
    0xA99C: "_CountResources",
    0xA99D: "_GetIndResource",
    0xA99E: "_CountTypes",
    0xA99F: "_GetIndType",
    0xA9F0: "_LoadSeg",
    0xAA29: "_GetDeviceList",
    0xAA2A: "_GetMainDevice",
    0xAA2B: "_GetNextDevice",
    0xAA2C: "_TestDeviceAttribute",
    0xAB1D: "_QDExtensions",
}


SEGMENT_SYSTEMS = {
    1: ("application runtime and Macintosh Toolbox glue", "high"),
    2: ("line-interpolation point lists and counted arrays", "high"),
    3: ("Pak/movie/image resource engine", "high"),
    4: ("PixMap base-address compatibility (GetPixBaseAddr)", "high"),
    5: ("PICT title/credits windows and version-text layout", "high"),
    6: ("Pak-backed modal panels, text entry, and event-handler chaining", "high"),
    7: ("preferences resource creation, migration, load, and save", "high"),
    8: ("bundled MIDI/Sound Manager driver, sequencer, timers, and channel control", "high"),
    10: ("Pak decompression and shared asset/UI services", "high"),
    11: ("Backgammon", "high"),
    12: ("main shell, startup, menu, and Mario host", "high"),
    13: ("song control, shuffle, geometry, window-update, and delay helpers", "high"),
    14: ("Dominoes", "high"),
    15: ("display-depth negotiation, dialog placement, and text layout", "high"),
    16: ("Checkers", "high"),
    17: ("Go Fish", "high"),
    18: ("Yacht", "high"),
    20: ("system capability and File Manager compatibility", "high"),
    21: ("C/Pascal string and radix-formatting helpers", "high"),
    22: ("multi-monitor window placement and QuickDraw geometry", "high"),
}


def be16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def be32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def disassemble(
    data: bytes,
    start: int,
    stop: int,
    exported_entries: dict[int, list[int]],
) -> tuple[list[str], Counter[int], list[dict[str, object]]]:
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_020)
    decoded: list[tuple[str, int, object]] = []
    traps: Counter[int] = Counter()
    link_offsets: set[int] = set()
    direct_call_targets: Counter[int] = Counter()
    offset = start

    while offset + 1 < stop:
        word = be16(data, offset)
        if word & 0xF000 == 0xA000:
            traps[word] += 1
            decoded.append(("trap", offset, word))
            offset += 2
            continue

        instruction = next(md.disasm(data[offset:stop], offset, count=1), None)
        if instruction is None:
            decoded.append(("word", offset, word))
            offset += 2
            continue

        raw = instruction.bytes.hex(" ").upper()
        if instruction.mnemonic.startswith("link"):
            link_offsets.add(offset)
        base_mnemonic = instruction.mnemonic.split(".", 1)[0]
        if base_mnemonic in {"bsr", "jsr"}:
            target_match = re.match(r"^\$([0-9a-fA-F]+)", instruction.op_str)
            if target_match:
                direct_call_targets[int(target_match.group(1), 16)] += 1
        decoded.append(("instruction", offset, (instruction, raw)))
        offset += instruction.size

    instruction_offsets = {offset for kind, offset, _ in decoded if kind == "instruction"}
    internal_calls = Counter(
        {target: count for target, count in direct_call_targets.items() if target in instruction_offsets}
    )
    function_offsets = sorted({start, *link_offsets, *internal_calls, *exported_entries})
    function_offset_set = set(function_offsets)
    lines: list[str] = []
    for kind, record_offset, value in decoded:
        if record_offset in function_offset_set:
            lines.extend(("", f"sub_{record_offset:08X}:"))
        if kind == "trap":
            word = int(value)
            label = TRAPS.get(word, "unknown")
            lines.append(
                f"{record_offset:08X}: {word:04X}              dc.w     ${word:04X} ; {label}"
            )
        elif kind == "word":
            word = int(value)
            lines.append(f"{record_offset:08X}: {word:04X}              dc.w     ${word:04X}")
        else:
            instruction, raw = value
            lines.append(
                f"{instruction.address:08X}: {raw:<23} {instruction.mnemonic:<8} {instruction.op_str}"
            )

    routines = [
        {
            "offset": function_offset,
            "offset_hex": f"0x{function_offset:08X}",
            "has_link_prologue": function_offset in link_offsets,
            "incoming_direct_calls": internal_calls[function_offset],
            "exported_jump_table_entries": len(exported_entries.get(function_offset, [])),
            "jump_table_a5_offsets": [
                f"0x{offset:04X}" for offset in exported_entries.get(function_offset, [])
            ],
            "entry_kinds": [
                kind
                for kind, present in (
                    ("segment_start", function_offset == start),
                    ("link_prologue", function_offset in link_offsets),
                    ("direct_call_target", function_offset in internal_calls),
                    ("exported_jump_table_target", function_offset in exported_entries),
                )
                if present
            ],
        }
        for function_offset in function_offsets
    ]
    return lines, traps, routines


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("resource_directory", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--a5-world",
        type=Path,
        help="decoded flat A5-world bytes used to recover exported jump-table targets",
    )
    parser.add_argument(
        "--a5-world-summary",
        type=Path,
        help="decode_data.py JSON containing the flat A5 world's lower offset",
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    if bool(args.a5_world) != bool(args.a5_world_summary):
        parser.error("--a5-world and --a5-world-summary must be supplied together")

    resource_paths = sorted(args.resource_directory.glob("*.bin"))
    resources = {int(path.stem): path.read_bytes() for path in resource_paths}
    exports_by_segment: dict[int, dict[int, list[int]]] = {}
    if args.a5_world:
        a5_world = args.a5_world.read_bytes()
        a5_summary = json.loads(args.a5_world_summary.read_text(encoding="utf-8"))
        a5_lower_offset = int(a5_summary["a5_lower_offset"])
        code0 = resources[0]
        # CODE 1's only unloaded jump-table stub lives in CODE 0. Unlike later
        # segments, it has no patched target word in the decoded DATA image; its
        # entry is the CODE 1 code start.
        code1_a5_offset = be32(code0, 12)
        exports_by_segment[1] = {4: [code1_a5_offset]}
        for segment_id, data in resources.items():
            if segment_id in (0, 1):
                continue
            first_a5_offset = be16(data, 0)
            entry_count = be16(data, 2)
            segment_exports: dict[int, list[int]] = {}
            for entry_index in range(entry_count):
                a5_offset = first_a5_offset + entry_index * 8
                flat_offset = a5_offset - a5_lower_offset
                if flat_offset < 0 or flat_offset + 8 > len(a5_world):
                    raise ValueError(
                        f"CODE {segment_id} jump-table entry is outside the decoded A5 world"
                    )
                opcode, reserved, target, target_segment = struct.unpack_from(
                    ">HHHH", a5_world, flat_offset
                )
                if opcode != 0xA9F0 or reserved != 0 or target_segment != segment_id:
                    raise ValueError(
                        f"CODE {segment_id} has an invalid unloaded jump-table entry "
                        f"at A5+0x{a5_offset:04X}"
                    )
                segment_exports.setdefault(target, []).append(a5_offset)
            exports_by_segment[segment_id] = segment_exports

    summaries = []
    total_traps: Counter[int] = Counter()
    for resource_path in resource_paths:
        segment_id = int(resource_path.stem)
        data = resources[segment_id]

        if segment_id == 0:
            summaries.append(
                {
                    "id": 0,
                    "resource_size": len(data),
                    "above_a5_size": be32(data, 0),
                    "below_a5_size": be32(data, 4),
                    "jump_table_size": be32(data, 8),
                    "jump_table_a5_offset": be32(data, 12),
                }
            )
            continue

        first_jump_table_entry = be16(data, 0)
        exported_entries = be16(data, 2)
        if segment_id == 1:
            code_start = 4
            code_size = len(data) - code_start
            repeated_jump_table_entry = None
        else:
            code_start = 12
            repeated_jump_table_entry = be32(data, 4)
            code_stop = min(be32(data, 8), len(data))
            code_size = max(0, code_stop - code_start)

        code_stop = code_start + code_size
        lines, traps, routines = disassemble(
            data, code_start, code_stop, exports_by_segment.get(segment_id, {})
        )
        total_traps.update(traps)
        assembly_path = args.output / f"CODE_{segment_id:02d}.asm"
        heading = [
            f"; CODE resource {segment_id}",
            f"; resource bytes: {len(data)}",
            f"; first jump-table entry: 0x{first_jump_table_entry:04X}",
            f"; exported jump-table entries: {exported_entries}",
            f"; code range: 0x{code_start:X}..0x{code_stop:X}",
            f"; trailing relocation/metadata bytes: {len(data) - code_stop}",
            "",
        ]
        assembly_path.write_text("\n".join(heading + lines) + "\n", encoding="utf-8")

        summaries.append(
            {
                "id": segment_id,
                "resource_size": len(data),
                "first_jump_table_entry": first_jump_table_entry,
                "exported_entries": exported_entries,
                "repeated_jump_table_entry": repeated_jump_table_entry,
                "code_start": code_start,
                "code_size": code_size,
                "trailing_metadata_size": len(data) - code_stop,
                "link_prologue_count": sum(
                    1 for routine in routines if routine["has_link_prologue"]
                ),
                "identified_routine_count": len(routines),
                "system": SEGMENT_SYSTEMS.get(segment_id, ("unknown", "low"))[0],
                "system_confidence": SEGMENT_SYSTEMS.get(segment_id, ("unknown", "low"))[1],
                "routines": routines,
                "traps": {f"0x{key:04X}": value for key, value in sorted(traps.items())},
                "assembly": assembly_path.name,
            }
        )

    summary = {
        "architecture": "Motorola 68020, big-endian",
        "segments": summaries,
        "total_traps": {
            f"0x{key:04X}": {"name": TRAPS.get(key, "unknown"), "count": value}
            for key, value in sorted(total_traps.items())
        },
    }
    (args.output / "summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    with (args.output / "function_inventory.csv").open(
        "w", newline="", encoding="utf-8"
    ) as inventory_file:
        writer = csv.DictWriter(
            inventory_file,
            fieldnames=(
                "segment",
                "offset",
                "system",
                "system_confidence",
                "has_link_prologue",
                "incoming_direct_calls",
                "exported_jump_table_entries",
                "jump_table_a5_offsets",
                "entry_kinds",
            ),
        )
        writer.writeheader()
        for segment in summaries:
            if segment["id"] == 0:
                continue
            for routine in segment["routines"]:
                writer.writerow(
                    {
                        "segment": segment["id"],
                        "offset": routine["offset_hex"],
                        "system": segment["system"],
                        "system_confidence": segment["system_confidence"],
                        "has_link_prologue": routine["has_link_prologue"],
                        "incoming_direct_calls": routine["incoming_direct_calls"],
                        "exported_jump_table_entries": routine[
                            "exported_jump_table_entries"
                        ],
                        "jump_table_a5_offsets": ";".join(
                            routine["jump_table_a5_offsets"]
                        ),
                        "entry_kinds": ";".join(routine["entry_kinds"]),
                    }
                )
    print(f"Wrote {len(summaries) - 1} segment listings to {args.output}")
    print(f"Found {sum(s['link_prologue_count'] for s in summaries if s['id'])} LINK prologues")
    print(f"Identified {sum(s['identified_routine_count'] for s in summaries if s['id'])} routine entries")
    print(f"Found {sum(total_traps.values())} A-line trap calls ({len(total_traps)} distinct)")


if __name__ == "__main__":
    main()
