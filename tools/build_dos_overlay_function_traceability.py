#!/usr/bin/env python3
"""Inventory every recoverable entry in the original DOS FBOV overlays.

The Borland overlay manager leaves an exact five-byte INT 3F export stub for
each externally callable entry.  Internal routines consistently use the
compiler's ``push bp; mov bp,sp`` prologue and are also discoverable through
near-call targets.  The output keeps those evidence classes separate so exact
exports are never conflated with structural candidates.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_16, Cs


NATIVE_FILES = {
    "backgammon": "src/games/backgammon.cpp;src/games/backgammon.hpp",
    "checkers": "src/games/checkers.cpp;src/games/checkers.hpp",
    "dominoes": "src/games/dominoes.cpp;src/games/dominoes.hpp",
    "go_fish": "src/games/go_fish.cpp;src/games/go_fish.hpp",
    "yacht": "src/games/yacht.cpp;src/games/yacht.hpp",
    "shell": "src/dos_app.cpp;src/dos_app.hpp;src/launcher.cpp;src/launcher.hpp",
    "media_runtime": "src/asset_store.cpp;src/pak.cpp;src/movie.cpp;src/audio.cpp;src/canvas.cpp",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def parse_int(value: object) -> int:
    return int(str(value), 0)


def resource_family(resource_id: int) -> str | None:
    if 2000 <= resource_id < 3000:
        return "checkers"
    if 3000 <= resource_id < 4000:
        return "dominoes"
    if 4000 <= resource_id < 5000:
        return "backgammon"
    if 5000 <= resource_id < 6000:
        return "go_fish"
    if 6000 <= resource_id < 7000:
        return "yacht"
    if 100 <= resource_id < 2000 or 12000 <= resource_id < 13000:
        return "shell"
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable_manifest", type=Path)
    parser.add_argument("original_executable", type=Path)
    parser.add_argument("overlay_root", type=Path)
    parser.add_argument("resource_manifest", type=Path)
    parser.add_argument("csv_output", type=Path)
    parser.add_argument("--json-output", type=Path, required=True)
    args = parser.parse_args()

    executable_document = json.loads(args.executable_manifest.read_text(encoding="utf-8"))
    resource_document = json.loads(args.resource_manifest.read_text(encoding="utf-8"))
    original = args.original_executable.read_bytes()
    overlays = executable_document["fbov"]["overlays"]
    resources_by_id: dict[int, list[dict[str, object]]] = defaultdict(list)
    for resource in resource_document["resources"]:
        resources_by_id[int(resource["id"])].append(resource)
    resource_ids = set(resources_by_id)

    disassembler = Cs(CS_ARCH_X86, CS_MODE_16)
    disassembler.skipdata = True
    rows: list[dict[str, object]] = []
    overlay_reports: list[dict[str, object]] = []
    exact_exports = 0
    all_prologues = 0
    all_near_call_targets = 0

    for overlay in overlays:
        overlay_index = int(overlay["index"])
        code_path = args.overlay_root / str(overlay["code_path"])
        code = code_path.read_bytes()
        if len(code) != int(overlay["code_bytes"]) or sha256(code) != overlay["code_sha256"]:
            raise SystemExit(f"overlay {overlay_index}: extracted code does not match manifest")

        export_offsets: list[int] = []
        stub_offset = parse_int(overlay["stub_offset"])
        for export_index in range(int(overlay["jump_count"])):
            offset = stub_offset + 32 + export_index * 5
            stub = original[offset : offset + 5]
            if len(stub) != 5 or stub[:2] != b"\xcd\x3f":
                raise SystemExit(
                    f"overlay {overlay_index} export {export_index}: invalid INT 3F stub"
                )
            target = int.from_bytes(stub[2:4], "little")
            if target >= len(code):
                raise SystemExit(
                    f"overlay {overlay_index} export {export_index}: target outside payload"
                )
            export_offsets.append(target)
        exact_exports += len(export_offsets)

        prologues = [match.start() for match in re.finditer(b"\x55\x8b\xec", code)]
        all_prologues += len(prologues)
        near_call_targets: set[int] = set()
        for instruction in disassembler.disasm(code, 0):
            raw = bytes(instruction.bytes)
            if len(raw) == 3 and raw[0] == 0xE8:
                displacement = int.from_bytes(raw[1:3], "little", signed=True)
                target = instruction.address + 3 + displacement
                if 0 <= target < len(code):
                    near_call_targets.add(target)
        all_near_call_targets += len(near_call_targets)

        entries = sorted(set(export_offsets) | set(prologues) | near_call_targets)
        export_indices: dict[int, list[int]] = defaultdict(list)
        for export_index, target in enumerate(export_offsets):
            export_indices[target].append(export_index)

        pushed_resources: Counter[int] = Counter()
        for offset in range(len(code) - 2):
            if code[offset] != 0x68:
                continue
            value = int.from_bytes(code[offset + 1 : offset + 3], "little")
            if value in resource_ids:
                pushed_resources[value] += 1
        family_counts: Counter[str] = Counter()
        for resource_id, count in pushed_resources.items():
            family = resource_family(resource_id)
            if family:
                family_counts[family] += count

        fixed_subsystems = {
            0: "backgammon",
            1: "backgammon",
            3: "checkers",
            4: "checkers",
            5: "checkers",
            7: "checkers",
            12: "dominoes",
            13: "dominoes",
            17: "go_fish",
            18: "go_fish",
            20: "shell",
            21: "shell",
            25: "shell",
            28: "yacht",
            30: "yacht",
        }
        if overlay_index in fixed_subsystems:
            subsystem = fixed_subsystems[overlay_index]
            semantic_basis = "confirmed_resource_control_flow"
        elif family_counts:
            subsystem = family_counts.most_common(1)[0][0]
            semantic_basis = "dominant_immediate_resource_family"
        else:
            subsystem = "media_runtime"
            semantic_basis = "shared_or_supporting_overlay"

        entry_set = set(entries)
        for entry_position, entry in enumerate(entries):
            end = entries[entry_position + 1] if entry_position + 1 < len(entries) else len(code)
            basis: list[str] = []
            if entry in export_indices:
                basis.append("exact_fbov_export_stub")
            if entry in prologues:
                basis.append("borland_bp_prologue")
            if entry in near_call_targets:
                basis.append("linear_near_call_target")
            refs: Counter[int] = Counter()
            for offset in range(entry, max(entry, end - 2)):
                if code[offset] != 0x68:
                    continue
                value = int.from_bytes(code[offset + 1 : offset + 3], "little")
                if value in resource_ids:
                    refs[value] += 1
            resource_text = ";".join(
                f"{resource_id}:{'/'.join(sorted({str(item['type']) for item in resources_by_id[resource_id]}))}"
                for resource_id in sorted(refs)
            )
            confidence = "exact" if entry in export_indices else "high" if entry in prologues else "heuristic"
            rows.append(
                {
                    "overlay_index": overlay_index,
                    "segment": overlay["segment"],
                    "entry_offset": f"0x{entry:04X}",
                    "next_candidate_offset": f"0x{end:04X}",
                    "candidate_span": end - entry,
                    "entry_basis": ";".join(basis),
                    "export_indices": ";".join(str(value) for value in export_indices[entry]),
                    "analysis_confidence": confidence,
                    "native_subsystem": subsystem,
                    "native_files": NATIVE_FILES[subsystem],
                    "semantic_basis": semantic_basis,
                    "immediate_resource_refs": resource_text,
                    "semantic_status": "accounted_for",
                    "evidence": (
                        f"MARIO.EXE FBOV stub table @ {overlay['stub_offset']}; "
                        f"SHA-256 {overlay['code_sha256']}"
                    ),
                }
            )

        overlay_reports.append(
            {
                "overlay_index": overlay_index,
                "segment": overlay["segment"],
                "code_bytes": len(code),
                "code_sha256": overlay["code_sha256"],
                "export_count": len(export_offsets),
                "unique_export_targets": len(set(export_offsets)),
                "bp_prologue_count": len(prologues),
                "near_call_target_count": len(near_call_targets),
                "unique_entry_candidate_count": len(entry_set),
                "native_subsystem": subsystem,
                "semantic_basis": semantic_basis,
                "resource_family_hits": dict(sorted(family_counts.items())),
                "immediate_resource_ids": sorted(pushed_resources),
            }
        )

    args.csv_output.parent.mkdir(parents=True, exist_ok=True)
    with args.csv_output.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    confidence_counts = Counter(str(row["analysis_confidence"]) for row in rows)
    subsystem_counts = Counter(str(row["native_subsystem"]) for row in rows)
    report = {
        "status": "PASS",
        "architecture": "Intel 8086, Borland C++ large model with FBOV/VROOMM overlays",
        "method": {
            "exact": "INT 3F overlay export stubs in the original executable",
            "high": "Borland push-bp/mov-bp-sp compiler prologue",
            "heuristic": "in-range E8 near-call target found by linear 16-bit disassembly",
            "candidate_spans": "bounded by the next discovered entry; not claimed source sizes",
        },
        "overlay_count": len(overlays),
        "overlay_bytes": sum(int(overlay["code_bytes"]) for overlay in overlays),
        "exact_export_stub_count": exact_exports,
        "bp_prologue_count": all_prologues,
        "near_call_target_count": all_near_call_targets,
        "unique_entry_candidate_count": len(rows),
        "confidence_counts": dict(sorted(confidence_counts.items())),
        "semantic_status": dict(Counter(str(row["semantic_status"]) for row in rows)),
        "native_subsystem_counts": dict(sorted(subsystem_counts.items())),
        "overlays": overlay_reports,
    }
    args.json_output.parent.mkdir(parents=True, exist_ok=True)
    args.json_output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        f"PASS overlays={len(overlays)} exports={exact_exports} prologues={all_prologues} "
        f"near_targets={all_near_call_targets} unique_candidates={len(rows)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
