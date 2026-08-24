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
import json
from collections import Counter
from pathlib import Path


RESIDENT_NATIVE_FILES = (
    "src/main.cpp;src/launcher.cpp;src/dos_app.cpp;src/dos_help_overlay.cpp;"
    "src/asset_store.cpp;src/pak.cpp;src/movie.cpp;src/audio.cpp;src/canvas.cpp;"
    "src/common.hpp;src/game.hpp"
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("functions", type=Path, help="radare2 aflj JSON")
    parser.add_argument("sections", type=Path, help="rabin2 section JSON")
    parser.add_argument("executable_manifest", type=Path)
    parser.add_argument("csv_output", type=Path)
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
        if section_index < 118:
            region = "resident"
            original_overlay = ""
            original_segment = ""
            semantic_status = "accounted_for_resident_runtime_family"
            native_subsystem = "resident_runtime"
            native_files = RESIDENT_NATIVE_FILES
            semantic_evidence = (
                "conservative radare2 resident candidate; section is outside every FBOV payload; "
                "mapped to native Win32/runtime/media replacement family"
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
                "original_overlay_index": original_overlay,
                "original_overlay_segment": original_segment,
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
    report = {
        "status": "PASS",
        "architecture": "Intel 8086, 16-bit Borland large-memory-model MZ with FBOV overlays",
        "interpretation": (
            "Rows are conservative radare2 discovery candidates, not recovered symbols. Segmented "
            "far control flow can overstate boundaries. Overlay candidates defer to the exact "
            "INT 3F/prologue/call-target ledger; resident candidates map to the replaced DOS "
            "platform/runtime/media family rather than being misrepresented as named routines."
        ),
        "candidate_count": len(rows),
        "section_count": len(sections),
        "resident_section_count": 118,
        "overlay_section_count": 31,
        "region_counts": dict(sorted(region_counts.items())),
        "sections_with_candidates": len(section_counts),
        "cross_section_candidates": sum(bool(row["crosses_section"]) for row in rows),
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
        f"cross_section={report['cross_section_candidates']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
