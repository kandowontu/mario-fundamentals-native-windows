#!/usr/bin/env python3
"""Build a complete, conservative CODE-entry-to-native-subsystem traceability ledger."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path


SEGMENT_COVERAGE = {
    1: {
        "disposition": "mixed_native_runtime_and_semantic_reimplementation",
        "native": "Win32 process/event runtime, typed state, source QuickDraw RNG and direct-sound arbitration",
        "files": "src/main.cpp;src/app.cpp;src/audio.cpp;src/audio.hpp;src/source_random.hpp;src/common.hpp",
        "verification": "release self-test;static PE audit;docs/FUNCTION_AUDIT.md",
    },
    2: {
        "disposition": "semantic_native_reimplementation",
        "native": "typed Point paths and standard-library counted containers",
        "files": "src/common.hpp;src/game.hpp;src/games/backgammon.cpp;src/games/checkers.cpp",
        "verification": "release self-test;docs/REVERSE_ENGINEERING.md#shared-and-late-loaded-compatibility-segments",
    },
    3: {
        "disposition": "semantic_native_reimplementation",
        "native": "Pak sprite drawing and MuV/Ply/Img movie compositor",
        "files": "src/pak.cpp;src/pak.hpp;src/movie.cpp;src/movie.hpp;src/canvas.cpp;src/canvas.hpp",
        "verification": "release self-test;byte-exact asset verification;docs/FUNCTION_AUDIT.md",
    },
    4: {
        "disposition": "native_platform_replacement",
        "native": "owned 32-bit DIB storage replaces relocatable PixMap access",
        "files": "src/canvas.cpp;src/canvas.hpp",
        "verification": "release self-test;docs/FUNCTION_AUDIT.md",
    },
    5: {
        "disposition": "semantic_native_reimplementation",
        "native": "embedded title/about/credits presentation, exact source NFNT text, and native window lifetime",
        "files": "src/about.cpp;src/source_fonts.cpp;src/canvas.cpp;src/app.cpp;src/resources.rc.in;src/audio_catalog.hpp",
        "verification": "release About raster self-test;byte-exact asset verification;docs/FUNCTION_AUDIT.md",
    },
    6: {
        "disposition": "semantic_native_reimplementation",
        "native": "native modal-state and name-edit controllers using source Pak panels",
        "files": "src/app.cpp;src/app.hpp;src/canvas.cpp",
        "verification": "release self-test;docs/FUNCTION_AUDIT.md",
    },
    7: {
        "disposition": "native_platform_replacement",
        "native": "per-user Windows Registry preferences with recovered source fields",
        "files": "src/app.cpp;src/app.hpp",
        "verification": "release self-test;docs/REVERSE_ENGINEERING.md#shared-and-late-loaded-compatibility-segments",
    },
    8: {
        "disposition": "semantic_native_reimplementation",
        "native": "in-memory Sound Manager decoding and WinMM MIDI/sample playback",
        "files": "src/audio.cpp;src/audio.hpp;src/audio_catalog.hpp;src/movie.cpp",
        "verification": "release self-test;docs/AUDIO_AUDIT.md",
    },
    10: {
        "disposition": "semantic_native_reimplementation",
        "native": "Pak outer LZSS decoder and shared source-asset services",
        "files": "src/asset_store.cpp;src/asset_store.hpp;src/pak.cpp;src/pak.hpp",
        "verification": "release self-test;byte-exact asset verification;docs/FUNCTION_AUDIT.md",
    },
    11: {
        "disposition": "semantic_native_reimplementation",
        "native": "Backgammon rules, controller, AI, geometry, dialogue, and outcomes",
        "files": "src/games/backgammon.cpp;src/games/backgammon.hpp;src/game.hpp",
        "verification": "release self-test Backgammon regressions;docs/FUNCTION_AUDIT.md",
    },
    12: {
        "disposition": "semantic_native_reimplementation",
        "native": "startup, menu, title sequence, selection actor, and Mario host",
        "files": "src/app.cpp;src/app.hpp;src/menu_catalog.hpp;src/audio_catalog.hpp",
        "verification": "release self-test startup/menu regressions;docs/FUNCTION_AUDIT.md",
    },
    13: {
        "disposition": "mixed_shared_semantic_and_platform_replacement",
        "native": "song control, source shuffle/RNG helpers, typed geometry, native updates/delays",
        "files": "src/audio.cpp;src/audio.hpp;src/source_random.hpp;src/app.cpp;src/canvas.cpp",
        "verification": "release self-test;docs/FUNCTION_AUDIT.md",
    },
    14: {
        "disposition": "semantic_native_reimplementation",
        "native": "Dominoes rules, controller, AI, geometry, dialogue, and outcomes",
        "files": "src/games/dominoes.cpp;src/games/dominoes.hpp;src/game.hpp",
        "verification": "release self-test Dominoes regressions;docs/FUNCTION_AUDIT.md",
    },
    15: {
        "disposition": "mixed_shared_semantic_and_platform_replacement",
        "native": "source Pak text layout plus native display-depth/modal placement",
        "files": "src/canvas.cpp;src/canvas.hpp;src/app.cpp",
        "verification": "release self-test text raster/metrics;docs/FUNCTION_AUDIT.md",
    },
    16: {
        "disposition": "semantic_native_reimplementation",
        "native": "Checkers rules, full-path minimax, controller, dialogue, and outcomes",
        "files": "src/games/checkers.cpp;src/games/checkers.hpp;src/game.hpp",
        "verification": "release self-test Checkers regressions;docs/FUNCTION_AUDIT.md",
    },
    17: {
        "disposition": "semantic_native_reimplementation",
        "native": "Go Fish rules, hand layout, strategy, controller, dialogue, and outcomes",
        "files": "src/games/go_fish.cpp;src/games/go_fish.hpp;src/game.hpp",
        "verification": "release self-test Go Fish regressions;docs/FUNCTION_AUDIT.md",
    },
    18: {
        "disposition": "semantic_native_reimplementation",
        "native": "Yacht rules, scorecard, adviser, controller, dialogue, and outcomes",
        "files": "src/games/yacht.cpp;src/games/yacht.hpp;src/game.hpp",
        "verification": "release self-test Yacht regressions;docs/FUNCTION_AUDIT.md",
    },
    20: {
        "disposition": "native_platform_replacement",
        "native": "Win32 capability/resource/filesystem APIs replace classic compatibility wrappers",
        "files": "src/main.cpp;src/app.cpp;src/asset_store.cpp",
        "verification": "static PE audit;release self-test;docs/FUNCTION_AUDIT.md",
    },
    21: {
        "disposition": "native_language_runtime_replacement",
        "native": "bounded C++ strings and native numeric formatting",
        "files": "src/common.hpp;src/app.cpp;src/games/backgammon.cpp;src/games/go_fish.cpp;src/games/yacht.cpp",
        "verification": "release self-test;docs/FUNCTION_AUDIT.md",
    },
    22: {
        "disposition": "native_platform_replacement",
        "native": "monitor-aware Win32 placement, viewport scaling, and owned DIB geometry",
        "files": "src/app.cpp;src/canvas.cpp;src/app.manifest",
        "verification": "release self-test;static PE audit;docs/FUNCTION_AUDIT.md",
    },
}


# Addresses are recovered control-flow landmarks. Some are routine entries; some
# deliberately name important branches inside the containing structural entry.
LANDMARKS = {
    1: {
        0xB22: "direct-sound channel busy query",
        0x352C: "QuickDraw random-range scaler",
    },
    3: {
        0x596: "movie command locator and Pak text helper container",
        0x5B8: "Pak glyph draw branch",
        0x5C0: "Pak glyph width branch",
        0x62A: "active-interval movie compositor",
        0x9EE: "glyph/sprite draw export",
        0xA28: "glyph metrics export",
        0xCAA: "shared sound entry",
        0x1C5A: "Pak span blitter",
    },
    5: {
        0x2E: "PICT 128 title/about and sound 5057 path",
        0x324: "PICT 129 credits and sound 5072 path",
    },
    10: {
        0x1E: "Pak resource loader",
        0xE2: "Pak outer decompression",
        0x1A4: "Pak decode helper",
    },
    11: {
        0xDD8: "Backgammon startup controller",
        0xF78: "initial checker reveal controller",
        0x1320: "live post-roll thinking selector",
        0x15B4: "player handoff selector",
        0x161A: "ordered Mario move dispatcher",
        0x18D4: "bar entry selector",
        0x1A90: "point-making/consolidation selector",
        0x1BD0: "hit selector",
        0x1C58: "safe destination selector",
        0x1D1E: "safe destination continuation",
        0x1D76: "contact/fallback selector",
        0x1E90: "reinforcement/doubles selector",
        0x1FB6: "bearing-off selector",
        0x214A: "stalled-turn speech selector",
        0x2588: "idle controller",
        0x26AC: "idle speech pool",
        0x3B88: "Mario-win outcome selector",
        0x3BB6: "player-win outcome selector",
        0x3CF0: "replay prompt controller",
        0x558A: "checker actor layout",
        0x5756: "checker rendering/layout path",
        0x59A6: "shell checker geometry",
        0x5A82: "egg checker geometry",
    },
    12: {
        0x1032: "sound-bearing random menu idle",
        0x12F8: "independent blink scheduler",
        0x1400: "posted game-launch controller",
        0x1B74: "publisher-screen direct sound path",
        0x1F74: "eight-state title controller",
    },
    13: {
        0x2AC: "shared descending Fisher-Yates shuffle",
        0x6CC: "startup random-seed stirring",
    },
    14: {
        0x852: "three-pass Dominoes deck shuffle",
        0xDCA: "deal speech selector",
        0xF7E: "blocked-game outcome controller",
        0x1246: "Mario last-tile ending",
        0x1304: "player last-tile ending",
        0x169E: "Mario-turn dialogue selector",
        0x1810: "pass dialogue selector",
        0x1870: "smallest-exposed-pip AI ordering",
        0x1CAC: "move/draw commentary controller",
        0x1E10: "idle/joke controller",
        0x2112: "player-handoff dialogue selector",
        0x23DA: "draw dialogue controller",
        0x276C: "drag/drop placement controller",
        0x27E0: "endpoint proximity resolver",
        0x2A50: "overlapping-end disambiguation",
        0x2D80: "endgame move dialogue selector",
        0x2EFE: "legal-placement enumerator",
        0x5300: "subroutine formerly misidentified as sound ID",
    },
    15: {
        0x1C8: "centered Pak text layout export branch",
    },
    16: {
        0xE98: "Checkers result controller branch",
        0x1196: "replay selector branch",
        0x27BA: "idle controller",
        0x3DC4: "ordinary move/minimax selector",
        0x3FBE: "recursive complete-jump enumeration",
        0x40DA: "minimax scoring",
        0x47F0: "32-square link-table initialization",
    },
    17: {
        0x11F8: "thinking branch with required Random(1) advance",
        0x151A: "rank-suffix question tables",
        0x1B88: "alternating idle controller",
        0x371A: "persistent hand-rank layout records",
        0x3852: "hand-layout record update tail",
        0x4616: "four-cards-per-rank deck numbering",
        0x467A: "300-pair deck swap",
        0x484A: "Mario rank-request strategy",
    },
    18: {
        0x190: "thinking/reaction selector container",
        0xCF8: "optional opening-line branch",
        0x12A0: "player idle controller",
        0x142E: "idle conversation continuation",
        0x19E2: "outcome controller",
        0x215C: "remaining-roll marker controller",
        0x23EA: "score-category dispatch",
        0x2B42: "white reroll lane layout",
        0x2C40: "red held-die lane layout",
        0x2F22: "scorecard controls and score placement",
        0x3596: "Mario scoring adviser",
    },
}


def fail(message: str) -> None:
    raise SystemExit(f"FAIL {message}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("summary", type=Path)
    parser.add_argument("csv_output", type=Path)
    parser.add_argument("--json-output", type=Path)
    args = parser.parse_args()

    summary = json.loads(args.summary.read_text(encoding="utf-8"))
    rows: list[dict[str, object]] = []
    landmark_rows: dict[tuple[int, int], list[str]] = defaultdict(list)

    for segment in summary["segments"]:
        segment_id = int(segment["id"])
        if segment_id == 0:
            continue
        if segment_id not in SEGMENT_COVERAGE:
            fail(f"CODE {segment_id} has no native coverage assignment")
        routines = segment["routines"]
        starts = [int(routine["offset"]) for routine in routines]
        code_end = int(segment["code_start"]) + int(segment["code_size"])
        for address, label in LANDMARKS.get(segment_id, {}).items():
            if address < starts[0] or address >= code_end:
                fail(f"CODE {segment_id} landmark 0x{address:X} is outside its code range")
            containing = max(start for start in starts if start <= address)
            landmark_rows[(segment_id, containing)].append(f"0x{address:X} {label}")

        coverage = SEGMENT_COVERAGE[segment_id]
        for index, routine in enumerate(routines):
            start = int(routine["offset"])
            end = starts[index + 1] if index + 1 < len(starts) else code_end
            landmarks = landmark_rows[(segment_id, start)]
            exported = int(routine.get("exported_jump_table_entries", 0))
            if landmarks:
                granularity = "explicit_control_flow_landmark"
                confidence = "high"
            elif exported:
                granularity = "exported_entry_with_segment_family_mapping"
                confidence = "medium"
            else:
                granularity = "structural_entry_with_segment_family_mapping"
                confidence = "medium"
            rows.append(
                {
                    "segment": segment_id,
                    "offset": routine["offset_hex"],
                    "end_exclusive": f"0x{end:08X}",
                    "candidate_span_bytes": end - start,
                    "entry_kinds": ";".join(routine.get("entry_kinds", [])),
                    "has_link_prologue": bool(routine["has_link_prologue"]),
                    "incoming_direct_calls": int(routine["incoming_direct_calls"]),
                    "exported_jump_table_entries": exported,
                    "jump_table_a5_offsets": ";".join(
                        routine.get("jump_table_a5_offsets", [])
                    ),
                    "original_subsystem": segment["system"],
                    "native_disposition": coverage["disposition"],
                    "native_implementation": coverage["native"],
                    "native_files": coverage["files"],
                    "verification": coverage["verification"],
                    "mapping_granularity": granularity,
                    "mapping_confidence": confidence,
                    "landmarks": "; ".join(landmarks),
                    "assembly": f"work/disassembly/{segment['assembly']}",
                    "coverage_status": "accounted_for",
                }
            )

    expected = sum(
        int(segment.get("identified_routine_count", 0))
        for segment in summary["segments"]
    )
    if len(rows) != expected:
        fail(f"ledger has {len(rows)} rows but disassembly identifies {expected}")
    if any(row["coverage_status"] != "accounted_for" for row in rows):
        fail("not every structural entry is accounted for")
    export_count = sum(int(row["exported_jump_table_entries"]) for row in rows)
    declared_exports = sum(
        int(segment.get("exported_entries", 0))
        for segment in summary["segments"]
        if int(segment["id"]) != 0
    )
    if export_count != declared_exports:
        fail(f"mapped {export_count} exports but segment metadata declares {declared_exports}")

    args.csv_output.parent.mkdir(parents=True, exist_ok=True)
    with args.csv_output.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    counts = Counter(str(row["mapping_granularity"]) for row in rows)
    report = {
        "status": "PASS",
        "architecture": summary["architecture"],
        "structural_entries": len(rows),
        "link_prologue_entries": sum(bool(row["has_link_prologue"]) for row in rows),
        "exported_jump_table_entries": export_count,
        "exported_target_rows": sum(
            int(row["exported_jump_table_entries"]) > 0 for row in rows
        ),
        "direct_call_target_rows": sum(
            int(row["incoming_direct_calls"]) > 0 for row in rows
        ),
        "mapping_granularity": dict(sorted(counts.items())),
        "unaccounted_entries": 0,
        "segments": {
            str(segment_id): sum(int(row["segment"]) == segment_id for row in rows)
            for segment_id in sorted(SEGMENT_COVERAGE)
        },
        "interpretation": (
            "Rows are conservative structural CODE entry candidates. Native semantic or platform "
            "replacements may absorb multiple compiler-generated 68k entries; the ledger does not "
            "claim original symbol recovery or instruction-for-instruction translation."
        ),
    }
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        f"PASS entries={len(rows)} exports={export_count} "
        f"landmarked={counts['explicit_control_flow_landmark']} unaccounted=0"
    )


if __name__ == "__main__":
    main()
