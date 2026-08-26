#!/usr/bin/env python3
"""Build a conservative DOS x86 function-candidate traceability ledger.

Radare2's 16-bit MZ analysis is useful discovery evidence, but segmented far
control flow can create overlapping or oversized candidates.  This tool keeps
those results explicitly heuristic, maps every entry to the exact merged MZ
section and original FBOV overlay, and never upgrades a candidate into a
recovered source function without a separate semantic assignment.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter, defaultdict, deque
from pathlib import Path


RESIDENT_NATIVE_FILES = (
    "src/main.cpp;src/launcher.cpp;src/dos_app.cpp;src/dos_help_overlay.cpp;"
    "src/asset_store.cpp;src/pak.cpp;src/movie.cpp;src/audio.cpp;src/canvas.cpp;"
    "src/common.hpp;src/game.hpp"
)

SHELL_NATIVE_FILES = (
    "src/main.cpp;src/launcher.cpp;src/launcher.hpp;src/dos_app.cpp;src/dos_app.hpp;"
    "src/dos_help_overlay.cpp;src/dos_help_overlay.hpp"
)

MEDIA_NATIVE_FILES = (
    "src/asset_store.cpp;src/asset_store.hpp;src/pak.cpp;src/pak.hpp;src/movie.cpp;"
    "src/movie.hpp;src/audio.cpp;src/audio.hpp;src/canvas.cpp;src/canvas.hpp"
)

SUBSYSTEM_NATIVE_FILES = {
    "backgammon": "src/games/backgammon.cpp;src/games/backgammon.hpp",
    "checkers": "src/games/checkers.cpp;src/games/checkers.hpp",
    "dominoes": "src/games/dominoes.cpp;src/games/dominoes.hpp",
    "go_fish": "src/games/go_fish.cpp;src/games/go_fish.hpp",
    "yacht": "src/games/yacht.cpp;src/games/yacht.hpp",
    "shell": "src/dos_app.cpp;src/dos_app.hpp;src/dos_help_overlay.cpp;src/dos_help_overlay.hpp;src/launcher.cpp;src/launcher.hpp",
    "media_runtime": "src/asset_store.cpp;src/pak.cpp;src/movie.cpp;src/audio.cpp;src/canvas.cpp",
}

GAME_SUBSYSTEMS = {"backgammon", "checkers", "dominoes", "go_fish", "yacht"}

RESIDENT_CLASS_FILES = {
    "shared_game_runtime": ";".join(
        SUBSYSTEM_NATIVE_FILES[name] for name in sorted(GAME_SUBSYSTEMS)
    ) + ";src/game.hpp;src/source_random.hpp",
    "shared_shell_media_runtime": SHELL_NATIVE_FILES + ";" + MEDIA_NATIVE_FILES,
    "shared_resident_services": RESIDENT_NATIVE_FILES + ";src/source_random.hpp",
    "compiler_system_runtime": "src/main.cpp;src/common.hpp;src/source_random.hpp",
    "overlay_stub_thunk": "src/main.cpp;src/dos_app.cpp;src/dos_app.hpp",
}


def integer(value: object) -> int:
    if isinstance(value, int):
        return value
    return int(str(value), 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("functions", type=Path, help="radare2 aflj JSON")
    parser.add_argument("sections", type=Path, help="rabin2 section JSON")
    parser.add_argument("executable_manifest", type=Path)
    parser.add_argument("csv_output", type=Path)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--overlay-directory", type=Path, required=True)
    parser.add_argument("--json-output", type=Path, required=True)
    parser.add_argument(
        "--overlay-summary",
        type=Path,
        required=True,
        help="exact/high-confidence FBOV overlay traceability summary",
    )
    args = parser.parse_args()

    functions = json.loads(args.functions.read_text(encoding="utf-8"))
    section_document = json.loads(args.sections.read_text(encoding="utf-8"))
    executable = json.loads(args.executable_manifest.read_text(encoding="utf-8"))
    overlay_summary = json.loads(args.overlay_summary.read_text(encoding="utf-8"))
    if overlay_summary.get("status") != "PASS":
        raise SystemExit("overlay traceability summary did not pass")
    overlay_subsystems = {
        int(item["overlay_index"]): str(item["native_subsystem"])
        for item in overlay_summary["overlays"]
    }
    expected_overlay_indices = set(range(31))
    if set(overlay_subsystems) != expected_overlay_indices:
        missing = sorted(expected_overlay_indices - set(overlay_subsystems))
        unexpected = sorted(set(overlay_subsystems) - expected_overlay_indices)
        raise SystemExit(
            "exact overlay ledger does not cover overlays 0-30; "
            f"missing={missing} unexpected={unexpected}"
        )
    unknown_subsystems = sorted(set(overlay_subsystems.values()) - set(SUBSYSTEM_NATIVE_FILES))
    if unknown_subsystems:
        raise SystemExit(
            f"exact overlay ledger contains unknown native subsystems: {unknown_subsystems}"
        )
    sections = sorted(section_document["sections"], key=lambda item: int(item["vaddr"]))
    overlays = executable["fbov"]["overlays"]
    if len(overlays) != 31:
        raise SystemExit(f"expected 31 FBOV overlays, found {len(overlays)}")
    if len(sections) != 150:
        raise SystemExit(f"expected 150 merged MZ sections, found {len(sections)}")

    executable_bytes = args.executable.read_bytes()
    source = executable["source"]
    if len(executable_bytes) != int(source["bytes"]):
        raise SystemExit("DOS executable size does not match its audited manifest")
    if hashlib.sha256(executable_bytes).hexdigest().upper() != str(source["sha256"]).upper():
        raise SystemExit("DOS executable hash does not match its audited manifest")

    segment_entries = executable["fbov"]["segments"]
    if len(segment_entries) != 133:
        raise SystemExit(f"expected 133 segment-table entries, found {len(segment_entries)}")
    entries_by_segment: dict[int, list[dict[str, object]]] = defaultdict(list)
    for entry in segment_entries:
        entries_by_segment[integer(entry["segment"])].append(entry)

    def code_entry_for_segment(segment: int) -> dict[str, object] | None:
        matches = [entry for entry in entries_by_segment.get(segment, []) if entry["is_code"]]
        if not matches:
            return None
        overlays_at_segment = [entry for entry in matches if entry["is_overlay"]]
        if overlays_at_segment:
            if len(overlays_at_segment) != 1:
                raise SystemExit(f"segment 0x{segment:04X} has multiple overlay entries")
            return overlays_at_segment[0]
        # TLINK emits a zero/tiny group marker before three large resident
        # code records at the same paragraph. The record with the greatest
        # declared maximum offset owns the actual section bytes.
        return max(matches, key=lambda entry: integer(entry["maximum_offset"]))

    # Recover exact overlay-to-segment dependencies. Every FBOV fixup word is
    # an eight-byte __SEGTABLE__ record offset, not an already-relocated DOS
    # segment. Pinning this interpretation accounts for all 2,900 fixups.
    segment_families: dict[int, set[str]] = defaultdict(set)
    direct_overlay_references: dict[int, set[int]] = defaultdict(set)
    overlay_fixup_count = 0
    overlay_fixup_edges: set[tuple[int, int]] = set()
    for overlay in overlays:
        overlay_index = int(overlay["index"])
        subsystem = overlay_subsystems[overlay_index]
        overlay_entry_index = int(overlay["segment_table_index"])
        overlay_entry = segment_entries[overlay_entry_index]
        if not overlay_entry["is_overlay"] or int(overlay_entry["overlay_index"]) != overlay_index:
            raise SystemExit(f"overlay {overlay_index} segment-table record is inconsistent")
        segment_families[overlay_entry_index].add(subsystem)
        direct_overlay_references[overlay_entry_index].add(overlay_index)

        code_path = args.overlay_directory / str(overlay["code_path"])
        fixup_path = args.overlay_directory / str(overlay["fixup_path"])
        code = code_path.read_bytes()
        fixups = fixup_path.read_bytes()
        if len(code) != int(overlay["code_bytes"]) or len(fixups) != int(overlay["fixup_bytes"]):
            raise SystemExit(f"overlay {overlay_index} payload length changed")
        if len(fixups) % 2:
            raise SystemExit(f"overlay {overlay_index} has an odd fixup stream")
        for position in range(0, len(fixups), 2):
            code_offset = int.from_bytes(fixups[position:position + 2], "little")
            if code_offset + 2 > len(code):
                raise SystemExit(
                    f"overlay {overlay_index} fixup 0x{code_offset:04X} is outside its code"
                )
            table_offset = int.from_bytes(code[code_offset:code_offset + 2], "little")
            if table_offset % 8 or table_offset // 8 >= len(segment_entries):
                raise SystemExit(
                    f"overlay {overlay_index} fixup 0x{code_offset:04X} has invalid "
                    f"segment-table offset 0x{table_offset:04X}"
                )
            target_index = table_offset // 8
            overlay_fixup_count += 1
            overlay_fixup_edges.add((overlay_index, target_index))
            if segment_entries[target_index]["is_code"]:
                canonical_target = code_entry_for_segment(
                    integer(segment_entries[target_index]["segment"])
                )
                if canonical_target is None:
                    raise SystemExit("overlay fixup code target has no canonical segment record")
                canonical_index = int(canonical_target["index"])
                segment_families[canonical_index].add(subsystem)
                direct_overlay_references[canonical_index].add(overlay_index)
    if overlay_fixup_count != 2900:
        raise SystemExit(f"expected 2,900 overlay fixups, found {overlay_fixup_count}")

    # MZ relocation words already contain their target DOS segment values.
    # Turn all 2,839 records into an exact resident inter-segment graph, then
    # propagate the overlay/shell caller families through that graph.
    resident_edges: set[tuple[int, int]] = set()
    relocation_count = 0
    for relocation in executable["mz"]["relocations"]:
        file_offset = integer(relocation["resident_file_offset"])
        if file_offset + 2 > len(executable_bytes):
            raise SystemExit("MZ relocation points beyond the original executable")
        target_segment = int.from_bytes(
            executable_bytes[file_offset:file_offset + 2], "little"
        )
        if target_segment not in entries_by_segment:
            raise SystemExit(f"MZ relocation targets unknown segment 0x{target_segment:04X}")
        caller_segment = integer(relocation["segment"])
        if caller_segment not in entries_by_segment:
            raise SystemExit(f"MZ relocation originates in unknown segment 0x{caller_segment:04X}")
        caller = code_entry_for_segment(caller_segment)
        target = code_entry_for_segment(target_segment)
        relocation_count += 1
        if caller is not None and target is not None:
            resident_edges.add((int(caller["index"]), int(target["index"])))
    if relocation_count != 2839:
        raise SystemExit(f"expected 2,839 MZ relocations, found {relocation_count}")

    entry_segment = integer(executable["mz"]["initial_cs"])
    entry_record = code_entry_for_segment(entry_segment)
    if entry_record is None:
        raise SystemExit("MZ entry segment does not map to a code record")
    segment_families[int(entry_record["index"])].add("shell")

    outgoing: dict[int, set[int]] = defaultdict(set)
    for caller, target in resident_edges:
        outgoing[caller].add(target)
    pending = deque(segment_families)
    while pending:
        caller = pending.popleft()
        for target in outgoing.get(caller, set()):
            combined = segment_families[target] | segment_families[caller]
            if combined != segment_families[target]:
                segment_families[target] = combined
                pending.append(target)

    def resident_disposition(
        segment_entry: dict[str, object],
    ) -> tuple[str, str, str, str]:
        entry_index = int(segment_entry["index"])
        families = segment_families.get(entry_index, set())
        if segment_entry["is_overlay"]:
            overlay_index = int(segment_entry["overlay_index"])
            return (
                "accounted_for_overlay_stub_thunk",
                "overlay_stub_thunk",
                RESIDENT_CLASS_FILES["overlay_stub_thunk"],
                f"original segment-table entry is the validated resident INT 3F thunk for "
                f"FBOV overlay {overlay_index}",
            )
        if len(families) == 1:
            subsystem = next(iter(families))
            return (
                "accounted_for_resident_call_graph",
                subsystem,
                SUBSYSTEM_NATIVE_FILES[subsystem],
                f"exact FBOV/MZ dependency graph reaches this resident segment only from "
                f"{subsystem}",
            )
        if families and families <= GAME_SUBSYSTEMS:
            subsystem = "shared_game_runtime"
        elif families and families <= {"shell", "media_runtime"}:
            subsystem = "shared_shell_media_runtime"
        elif families:
            subsystem = "shared_resident_services"
        else:
            subsystem = "compiler_system_runtime"
        if families:
            evidence = (
                "exact FBOV/MZ dependency graph reaches this shared resident segment from "
                + ", ".join(sorted(families))
            )
            status = "accounted_for_resident_call_graph"
        else:
            evidence = (
                "no direct or transitive path from the MZ entry point or any FBOV fixup; "
                "conservatively dispositioned as compiler/system or indirect-call support"
            )
            status = "accounted_for_compiler_system_or_indirect_runtime"
        return status, subsystem, RESIDENT_CLASS_FILES[subsystem], evidence

    def section_for(address: int) -> tuple[int, dict[str, object]] | None:
        for index, section in enumerate(sections):
            start = int(section["vaddr"])
            if start <= address < start + int(section["vsize"]):
                return index, section
        return None

    rows: list[dict[str, object]] = []
    missing: list[int] = []
    seen_offsets: set[int] = set()
    for function in sorted(functions, key=lambda item: (int(item["offset"]), str(item["name"]))):
        address = int(function["offset"])
        if address in seen_offsets:
            raise SystemExit(f"duplicate function candidate at 0x{address:X}")
        seen_offsets.add(address)
        mapped = section_for(address)
        if mapped is None:
            missing.append(address)
            continue
        section_index, section = mapped
        section_start = int(section["vaddr"])
        section_end = section_start + int(section["vsize"])
        candidate_end = address + int(function.get("realsz", function.get("size", 0)))
        resident_segment_entry: dict[str, object] | None = None
        caller_families = ""
        direct_overlays = ""
        if section_index < 118:
            region = "resident"
            section_segment = section_start // 16
            if section_start % 16:
                raise SystemExit(f"resident section {section['name']} is not paragraph-aligned")
            resident_segment_entry = code_entry_for_segment(section_segment)
            if resident_segment_entry is None:
                raise SystemExit(
                    f"resident candidate at 0x{address:X} is not in a segment-table code record"
                )
            entry_index = int(resident_segment_entry["index"])
            original_segment = str(resident_segment_entry["segment"])
            original_overlay = (
                int(resident_segment_entry["overlay_index"])
                if resident_segment_entry["is_overlay"] else ""
            )
            semantic_status, native_subsystem, native_files, semantic_evidence = (
                resident_disposition(resident_segment_entry)
            )
            caller_families = ";".join(sorted(segment_families.get(entry_index, set())))
            direct_overlays = ";".join(
                str(value) for value in sorted(direct_overlay_references.get(entry_index, set()))
            )
        elif section_index < 149:
            region = "fbov_overlay"
            overlay = overlays[section_index - 118]
            original_overlay = int(overlay["index"])
            original_segment = str(overlay["segment"])
            if original_overlay not in overlay_subsystems:
                raise SystemExit(f"overlay {original_overlay} missing from exact overlay ledger")
            semantic_status = "accounted_for_by_exact_overlay_ledger"
            native_subsystem = overlay_subsystems[original_overlay]
            native_files = SUBSYSTEM_NATIVE_FILES[native_subsystem]
            semantic_evidence = (
                f"supporting radare2 candidate; overlay {original_overlay} is covered by the "
                "INT 3F/prologue/call-target ledger"
            )
        else:
            region = "linker_stack_tail"
            original_overlay = ""
            original_segment = ""
            semantic_status = "accounted_for_linker_stack_tail"
            native_subsystem = "resident_runtime"
            native_files = RESIDENT_NATIVE_FILES
            semantic_evidence = "linker stack/tail region; no source behavior is executed"
        rows.append(
            {
                "address": f"0x{address:08X}",
                "name": str(function["name"]),
                "candidate_size": int(function.get("size", 0)),
                "reachable_size": int(function.get("realsz", 0)),
                "basic_blocks": int(function.get("nbbs", 0)),
                "edges": int(function.get("edges", 0)),
                "indegree": int(function.get("indegree", 0)),
                "outdegree": int(function.get("outdegree", 0)),
                "bits": int(function.get("bits", 0)),
                "section": str(section["name"]),
                "section_index": section_index,
                "section_offset": f"0x{address - section_start:04X}",
                "region": region,
                "original_segment_table_index": (
                    int(resident_segment_entry["index"]) if resident_segment_entry else ""
                ),
                "original_overlay_index": original_overlay,
                "original_overlay_segment": original_segment,
                "resident_caller_families": caller_families,
                "direct_overlay_references": direct_overlays,
                "crosses_section": candidate_end > section_end,
                "analysis_confidence": "heuristic",
                "semantic_status": semantic_status,
                "native_subsystem": native_subsystem,
                "native_files": native_files,
                "evidence": (
                    "radare2 aaa with anal.hasnext=false; exact merged-MZ section map; "
                    + semantic_evidence
                ),
            }
        )

    if missing:
        raise SystemExit(
            f"{len(missing)} function candidates are outside merged sections; first=0x{missing[0]:X}"
        )
    args.csv_output.parent.mkdir(parents=True, exist_ok=True)
    with args.csv_output.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    region_counts = Counter(str(row["region"]) for row in rows)
    section_counts = Counter(str(row["section"]) for row in rows)
    semantic_counts = Counter(str(row["semantic_status"]) for row in rows)
    subsystem_counts = Counter(str(row["native_subsystem"]) for row in rows)
    unaccounted = sum(not str(row["semantic_status"]).startswith("accounted_for") for row in rows)
    if unaccounted:
        raise SystemExit(f"{unaccounted} DOS discovery candidates remain unaccounted")
    expected_regions = {"fbov_overlay": 77, "resident": 1846}
    expected_semantics = {
        "accounted_for_by_exact_overlay_ledger": 77,
        "accounted_for_compiler_system_or_indirect_runtime": 20,
        "accounted_for_overlay_stub_thunk": 76,
        "accounted_for_resident_call_graph": 1750,
    }
    if dict(region_counts) != expected_regions:
        raise SystemExit(f"DOS discovery region counts changed: {dict(region_counts)}")
    if dict(semantic_counts) != expected_semantics:
        raise SystemExit(f"DOS resident dispositions changed: {dict(semantic_counts)}")
    if len(overlay_fixup_edges) != 323 or len(resident_edges) != 349:
        raise SystemExit(
            "DOS dependency graph edge counts changed: "
            f"overlay={len(overlay_fixup_edges)} resident={len(resident_edges)}"
        )
    report = {
        "status": "PASS",
        "architecture": "Intel 8086, 16-bit Borland large-memory-model MZ with FBOV overlays",
        "interpretation": (
            "Rows are conservative radare2 discovery candidates, not recovered symbols. Segmented "
            "far control flow can overstate boundaries. Overlay candidates defer to the exact "
            "INT 3F/prologue/call-target ledger. Resident classifications come from all original "
            "FBOV fixups and MZ inter-segment relocations; segments without a structural path stay "
            "explicitly conservative compiler/system-or-indirect runtime support."
        ),
        "candidate_count": len(rows),
        "section_count": len(sections),
        "resident_section_count": 118,
        "overlay_section_count": 31,
        "region_counts": dict(sorted(region_counts.items())),
        "sections_with_candidates": len(section_counts),
        "cross_section_candidates": sum(bool(row["crosses_section"]) for row in rows),
        "overlay_fixup_count": overlay_fixup_count,
        "overlay_fixup_edges": len(overlay_fixup_edges),
        "mz_relocation_count": relocation_count,
        "resident_intersegment_edges": len(resident_edges),
        "resident_segments_with_caller_families": len(
            [
                entry for entry in segment_entries
                if entry["is_code"] and not entry["is_overlay"]
                and segment_families.get(int(entry["index"]))
            ]
        ),
        "semantic_status": dict(sorted(semantic_counts.items())),
        "native_subsystem_counts": dict(sorted(subsystem_counts.items())),
        "unaccounted_candidates": unaccounted,
        "unmapped_addresses": 0,
    }
    args.json_output.parent.mkdir(parents=True, exist_ok=True)
    args.json_output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        f"PASS candidates={len(rows)} resident={region_counts['resident']} "
        f"overlay={region_counts['fbov_overlay']} tail={region_counts['linker_stack_tail']} "
        f"cross_section={report['cross_section_candidates']} "
        f"fixups={overlay_fixup_count} relocations={relocation_count} "
        f"subsystems={len(subsystem_counts)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
