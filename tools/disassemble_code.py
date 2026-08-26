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

from mac_code_relocations import parse_loader_relocations, parse_segment_relocations


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


def instruction_boundaries(data: bytes, start: int, stop: int) -> set[int]:
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_020)
    result: set[int] = set()
    offset = start
    while offset + 1 < stop:
        result.add(offset)
        word = be16(data, offset)
        if word & 0xF000 == 0xA000:
            offset += 2
            continue
        instruction = next(md.disasm(data[offset:stop], offset, count=1), None)
        offset += instruction.size if instruction is not None else 2
    return result


def resolve_call_edges(
    resources: dict[int, bytes],
    code_ranges: dict[int, tuple[int, int]],
    a5_targets: dict[int, tuple[int, int]],
    code1_relocation_kinds: dict[int, str],
) -> list[dict[str, object]]:
    """Recover local and custom-loader-patched calls as one global graph."""
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_020)
    boundaries = {
        segment_id: instruction_boundaries(resources[segment_id], start, stop)
        for segment_id, (start, stop) in code_ranges.items()
    }
    edges: dict[tuple[int, int], dict[str, object]] = {}

    for segment_id, (start, stop) in sorted(code_ranges.items()):
        data = resources[segment_id]
        relocations = None if segment_id == 1 else parse_segment_relocations(data)
        relocation_kinds = (
            code1_relocation_kinds if segment_id == 1 else relocations.by_offset
        )

        for source in sorted(boundaries[segment_id]):
            instruction = next(md.disasm(data[source:stop], source, count=1), None)
            if instruction is None:
                continue
            mnemonic = instruction.mnemonic.split(".", 1)[0]
            if mnemonic not in {"bsr", "jsr"}:
                continue
            opcode = be16(data, source)
            route = "pc_relative"
            target_segment = segment_id
            target_offset = None
            if opcode == 0x4EB9:
                literal = be32(data, source + 2)
                route = relocation_kinds.get(source + 2)
                if route is None:
                    raise ValueError(
                        f"CODE {segment_id} absolute JSR 0x{source:X} has no relocation"
                    )
                if route == "a5_relative":
                    if literal not in a5_targets:
                        raise ValueError(
                            f"CODE {segment_id} JSR 0x{source:X} has unknown A5 target "
                            f"0x{literal:X}"
                        )
                    target_segment, target_offset = a5_targets[literal]
                elif route == "main_segment_relative":
                    target_segment, target_offset = 1, literal
                else:
                    target_offset = literal
            else:
                match = re.match(r"^\$([0-9a-fA-F]+)$", instruction.op_str)
                if match:
                    target_offset = int(match.group(1), 16)

            if target_offset is None:
                continue
            if target_segment not in boundaries or target_offset not in boundaries[target_segment]:
                # A linear sweep necessarily decodes inline strings and switch
                # tables as instructions.  Bytes in those data islands can
                # resemble a PC-relative BSR/JSR; an odd or non-boundary target
                # proves that the apparent caller is data, not executable code.
                # Relocation-backed absolute calls remain fail-closed below.
                if route == "pc_relative":
                    continue
                raise ValueError(
                    f"CODE {segment_id} call 0x{source:X} targets CODE {target_segment} "
                    f"non-instruction 0x{target_offset:X}"
                )
            edges[(segment_id, source)] = {
                "source_segment": segment_id,
                "source_offset": source,
                "target_segment": target_segment,
                "target_offset": target_offset,
                "route": route,
                "mnemonic": mnemonic,
                "linear_decode": True,
            }

        # Relocations also identify calls that lie immediately after inline
        # switch tables.  Linear Capstone decoding intentionally emits those
        # tables as data-like instructions and misses CODE 16 $1AF6, so scan
        # every relocated operand and add any raw absolute JSR/JMP owner.
        if relocation_kinds:
            for operand, route in sorted(relocation_kinds.items()):
                source = operand - 2
                opcode = be16(data, source)
                if opcode not in (0x4EB9, 0x4EF9) or (segment_id, source) in edges:
                    continue
                literal = be32(data, operand)
                if route == "a5_relative":
                    if literal not in a5_targets:
                        raise ValueError(
                            f"CODE {segment_id} raw call 0x{source:X} has unknown A5 target"
                        )
                    target_segment, target_offset = a5_targets[literal]
                elif route == "main_segment_relative":
                    target_segment, target_offset = 1, literal
                else:
                    target_segment, target_offset = segment_id, literal
                if target_offset not in boundaries[target_segment]:
                    raise ValueError(
                        f"CODE {segment_id} raw call 0x{source:X} targets non-instruction"
                    )
                edges[(segment_id, source)] = {
                    "source_segment": segment_id,
                    "source_offset": source,
                    "target_segment": target_segment,
                    "target_offset": target_offset,
                    "route": route,
                    "mnemonic": "jsr" if opcode == 0x4EB9 else "jmp",
                    "linear_decode": False,
                }

    return [edges[key] for key in sorted(edges)]


def disassemble(
    segment_id: int,
    data: bytes,
    start: int,
    stop: int,
    exported_entries: dict[int, list[int]],
    call_edges: list[dict[str, object]],
) -> tuple[list[str], Counter[int], list[dict[str, object]], dict[str, int]]:
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_020)
    decoded: list[tuple[str, int, object]] = []
    traps: Counter[int] = Counter()
    link_offsets: set[int] = set()
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
        decoded.append(("instruction", offset, (instruction, raw)))
        offset += instruction.size

    incoming_edges = [
        edge for edge in call_edges if int(edge["target_segment"]) == segment_id
    ]
    outgoing_edges = [
        edge for edge in call_edges if int(edge["source_segment"]) == segment_id
    ]
    incoming_calls = Counter(int(edge["target_offset"]) for edge in incoming_edges)
    function_offsets = sorted({start, *link_offsets, *incoming_calls, *exported_entries})
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

    routines = []
    for index, function_offset in enumerate(function_offsets):
        end = function_offsets[index + 1] if index + 1 < len(function_offsets) else stop
        incoming = [
            edge for edge in incoming_edges if int(edge["target_offset"]) == function_offset
        ]
        outgoing = [
            edge for edge in outgoing_edges
            if function_offset <= int(edge["source_offset"]) < end
        ]
        callees = sorted({
            f"CODE_{int(edge['target_segment']):02d}:0x{int(edge['target_offset']):08X}"
            for edge in outgoing
        })
        routines.append(
            {
                "offset": function_offset,
                "offset_hex": f"0x{function_offset:08X}",
                "has_link_prologue": function_offset in link_offsets,
                "incoming_direct_calls": len(incoming),
                "incoming_local_calls": sum(
                    int(edge["source_segment"]) == segment_id for edge in incoming
                ),
                "incoming_cross_segment_calls": sum(
                    int(edge["source_segment"]) != segment_id for edge in incoming
                ),
                "outgoing_direct_call_sites": len(outgoing),
                "outgoing_local_call_sites": sum(
                    int(edge["target_segment"]) == segment_id for edge in outgoing
                ),
                "outgoing_cross_segment_call_sites": sum(
                    int(edge["target_segment"]) != segment_id for edge in outgoing
                ),
                "direct_callees": callees,
                "exported_jump_table_entries": len(exported_entries.get(function_offset, [])),
                "jump_table_a5_offsets": [
                    f"0x{offset:04X}" for offset in exported_entries.get(function_offset, [])
                ],
                "entry_kinds": [
                    kind
                    for kind, present in (
                        ("segment_start", function_offset == start),
                        ("link_prologue", function_offset in link_offsets),
                        ("direct_call_target", function_offset in incoming_calls),
                        ("cross_segment_call_target", any(
                            int(edge["source_segment"]) != segment_id for edge in incoming
                        )),
                        ("exported_jump_table_target", function_offset in exported_entries),
                    )
                    if present
                ],
            }
        )

    decoded_call_sites = {
        record_offset
        for kind, record_offset, value in decoded
        if kind == "instruction" and value[0].mnemonic.split(".", 1)[0] in {"bsr", "jsr"}
    }
    resolved_linear_sites = {
        int(edge["source_offset"]) for edge in outgoing_edges if bool(edge["linear_decode"])
    }
    stats = {
        "resolved_direct_call_sites": len(outgoing_edges),
        "local_direct_call_sites": sum(
            int(edge["target_segment"]) == segment_id for edge in outgoing_edges
        ),
        "cross_segment_call_sites": sum(
            int(edge["target_segment"]) != segment_id for edge in outgoing_edges
        ),
        "unique_direct_call_edges": len({
            (int(edge["target_segment"]), int(edge["target_offset"]))
            for edge in outgoing_edges
        }),
        "nonlinear_relocation_call_sites": sum(
            not bool(edge["linear_decode"]) for edge in outgoing_edges
        ),
        "indirect_or_unresolved_call_sites": len(decoded_call_sites - resolved_linear_sites),
    }
    return lines, traps, routines, stats


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
    parser.add_argument(
        "--data-resource",
        type=Path,
        help="raw DATA 0 resource containing CODE 1's own relocation streams",
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    if not (args.a5_world and args.a5_world_summary and args.data_resource):
        parser.error(
            "exact global call recovery requires --a5-world, --a5-world-summary, "
            "and --data-resource"
        )

    resource_paths = sorted(args.resource_directory.glob("*.bin"))
    resources = {int(path.stem): path.read_bytes() for path in resource_paths}
    exports_by_segment: dict[int, dict[int, list[int]]] = {}
    a5_targets: dict[int, tuple[int, int]] = {}
    code1_relocation_kinds: dict[int, str] = {}
    if args.a5_world:
        a5_world = args.a5_world.read_bytes()
        a5_summary = json.loads(args.a5_world_summary.read_text(encoding="utf-8"))
        a5_lower_offset = int(a5_summary["a5_lower_offset"])
        a5_upper_offset = int(a5_summary["a5_upper_offset"])
        code0 = resources[0]
        # CODE 1's only unloaded jump-table stub lives in CODE 0. Unlike later
        # segments, it has no patched target word in the decoded DATA image; its
        # entry is the CODE 1 code start.
        code1_a5_offset = be32(code0, 12)
        exports_by_segment[1] = {4: [code1_a5_offset]}
        a5_targets[code1_a5_offset] = (1, 4)
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
                a5_targets[a5_offset] = (segment_id, target)
            exports_by_segment[segment_id] = segment_exports

        loader_relocations = parse_loader_relocations(
            args.data_resource.read_bytes(),
            4 + int(a5_summary["compressed_bytes_consumed"]),
            a5_lower_offset=a5_lower_offset,
            a5_upper_offset=a5_upper_offset,
            code1_size=len(resources[1]),
        )
        code1_relocation_kinds = loader_relocations.for_target("code1").by_offset

    code_ranges = {
        segment_id: (
            4 if segment_id == 1 else 12,
            len(data) if segment_id == 1 else min(be32(data, 8), len(data)),
        )
        for segment_id, data in resources.items()
        if segment_id != 0
    }
    call_edges = resolve_call_edges(
        resources, code_ranges, a5_targets, code1_relocation_kinds
    )

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
        lines, traps, routines, call_stats = disassemble(
            segment_id,
            data,
            code_start,
            code_stop,
            exports_by_segment.get(segment_id, {}),
            call_edges,
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
                **call_stats,
                "system": SEGMENT_SYSTEMS.get(segment_id, ("unknown", "low"))[0],
                "system_confidence": SEGMENT_SYSTEMS.get(segment_id, ("unknown", "low"))[1],
                "routines": routines,
                "traps": {f"0x{key:04X}": value for key, value in sorted(traps.items())},
                "assembly": assembly_path.name,
            }
        )

    summary = {
        "architecture": "Motorola 68020, big-endian",
        "call_graph": {
            "resolved_call_sites": len(call_edges),
            "unique_edges": len({
                (
                    int(edge["source_segment"]),
                    int(edge["target_segment"]),
                    int(edge["target_offset"]),
                )
                for edge in call_edges
            }),
            "cross_segment_call_sites": sum(
                int(edge["source_segment"]) != int(edge["target_segment"])
                for edge in call_edges
            ),
            "relocation_backed_call_sites": sum(
                str(edge["route"]) in {
                    "a5_relative", "main_segment_relative", "self_segment_relative"
                }
                for edge in call_edges
            ),
            "nonlinear_relocation_call_sites": sum(
                not bool(edge["linear_decode"]) for edge in call_edges
            ),
            "edges": [
                {
                    **edge,
                    "source_offset_hex": f"0x{int(edge['source_offset']):08X}",
                    "target_offset_hex": f"0x{int(edge['target_offset']):08X}",
                }
                for edge in call_edges
            ],
        },
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
                "incoming_local_calls",
                "incoming_cross_segment_calls",
                "outgoing_direct_call_sites",
                "outgoing_local_call_sites",
                "outgoing_cross_segment_call_sites",
                "direct_callees",
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
                        "incoming_local_calls": routine["incoming_local_calls"],
                        "incoming_cross_segment_calls": routine[
                            "incoming_cross_segment_calls"
                        ],
                        "outgoing_direct_call_sites": routine[
                            "outgoing_direct_call_sites"
                        ],
                        "outgoing_local_call_sites": routine[
                            "outgoing_local_call_sites"
                        ],
                        "outgoing_cross_segment_call_sites": routine[
                            "outgoing_cross_segment_call_sites"
                        ],
                        "direct_callees": ";".join(routine["direct_callees"]),
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
    print(
        f"Resolved {len(call_edges)} direct call sites "
        f"({sum(int(edge['source_segment']) != int(edge['target_segment']) for edge in call_edges)} cross-segment)"
    )
    print(f"Found {sum(total_traps.values())} A-line trap calls ({len(total_traps)} distinct)")


if __name__ == "__main__":
    main()
