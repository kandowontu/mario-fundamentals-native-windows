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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("functions", type=Path, help="radare2 aflj JSON")
    parser.add_argument("sections", type=Path, help="rabin2 section JSON")
    parser.add_argument("executable_manifest", type=Path)
    parser.add_argument("csv_output", type=Path)
    parser.add_argument("--json-output", type=Path, required=True)
    args = parser.parse_args()

    functions = json.loads(args.functions.read_text(encoding="utf-8"))
    section_document = json.loads(args.sections.read_text(encoding="utf-8"))
    executable = json.loads(args.executable_manifest.read_text(encoding="utf-8"))
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
        elif section_index < 149:
            region = "fbov_overlay"
            overlay = overlays[section_index - 118]
            original_overlay = int(overlay["index"])
            original_segment = str(overlay["segment"])
        else:
            region = "linker_stack_tail"
            original_overlay = ""
            original_segment = ""
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
                "semantic_status": "pending_mapping",
                "native_subsystem": "",
                "native_files": "",
                "evidence": "radare2 aaa with anal.hasnext=false; exact merged-MZ section map",
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
    report = {
        "status": "PASS",
        "architecture": "Intel 8086, 16-bit Borland large-memory-model MZ with FBOV overlays",
        "interpretation": (
            "Rows are radare2 discovery candidates, not recovered symbols. Segmented far control "
            "flow can overstate function size, so semantic status remains pending until mapped "
            "to source behavior and a native subsystem."
        ),
        "candidate_count": len(rows),
        "section_count": len(sections),
        "resident_section_count": 118,
        "overlay_section_count": 31,
        "region_counts": dict(sorted(region_counts.items())),
        "sections_with_candidates": len(section_counts),
        "cross_section_candidates": sum(bool(row["crosses_section"]) for row in rows),
        "semantic_status": dict(Counter(str(row["semantic_status"]) for row in rows)),
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
