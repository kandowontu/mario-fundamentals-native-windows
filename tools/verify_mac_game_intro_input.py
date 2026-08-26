#!/usr/bin/env python3
"""Verify Macintosh selected-game intro input directly from shipped CODE resources.

The five game modules do not share one intro-input policy.  This verifier pins
the original resource payloads, decodes the key-down/mouse-down dispatch
branches in each module's first event handler, and requires the source-specific
target semantics used by the native port.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


RESOURCE_SHA256 = {
    11: "7ae97ac951636593688da5518a96129e8b9342dbdf43ca4cfd9406e985c253a6",
    14: "5515ff31786b7df3c88abdd2aab00b83c14bf39475e3ed01984dabe9e3fda4f7",
    16: "62e091d7fd8bd7715eafcf3abee304448083a1baa0a7b5fbfe5ee410cbdf4547",
    17: "e9c2e5bad14879d3067f07f2f434f46119219a9161a700a71dc6e277b4af6ecf",
    18: "5a8efb080648ac3e23057b203da79debde90edbb4154e50514282c632b33829c",
}

# Classic shell event 5 is key-down and event 6 is mouse-down.  Each tuple is
# (branch instruction offset, required target, native semantic disposition).
ROUTES = {
    11: {"game": "Backgammon", "key": (0x24, 0x1C4, "finish"),
         "mouse": (0x2A, 0x1C4, "finish")},
    14: {"game": "Dominoes", "key": (0x2C, 0x370, "ignore"),
         "mouse": (0x32, 0x2FC, "advance_one_tick")},
    16: {"game": "Checkers", "key": (0x2C, 0x456, "ignore"),
         "mouse": (0x32, 0x7F4, "ignore")},
    17: {"game": "Go Fish", "key": (0x2C, 0x3B4, "ignore"),
         "mouse": (0x32, 0x276, "ignore")},
    18: {"game": "Yacht", "key": (0x20, 0x228, "finish"),
         "mouse": (0x26, 0x228, "finish")},
}

# These target signatures distinguish completion posts, Dominoes' one-pass
# controller call, and the title-state guards which make Checkers/Go Fish no-ops.
TARGET_SIGNATURES = {
    (11, 0x1C4): bytes.fromhex("4879000003002f0a4eb9000004d8"),
    (14, 0x370): bytes.fromhex("42ad94847001600000aa"),
    (14, 0x2FC): bytes.fromhex(
        "42ad94841b7c0001983f2b6d983a98362b6efffc983a"
        "2b6efffc98322f0a3f3c000161ff0000069e"
    ),
    (16, 0x456): bytes.fromhex("42ad9a9860000546"),
    (16, 0x7F4): bytes.fromhex("42ad9a984a2d9a9c660001a4"),
    (17, 0x3B4): bytes.fromhex("3b7c0078ab0460000194"),
    (17, 0x276): bytes.fromhex("3b7c0078ab040c6d0003ab0c6640"),
    (18, 0x228): bytes.fromhex("4879000001302f0a4eb9000004d8"),
}

# CODE 18's second export is the scorecard/gameplay event handler.  Its event-6
# branch enters the normal mouse-control dispatcher; unlike the first export's
# Yacht-on-the-water title handler, it has no completion post.  This distinction
# is why clicking the visible board during "Good luck" / "I go first" does not
# skip that in-board opening in vanilla.
YACHT_GAMEPLAY_MOUSE = {
    "branch_offset": 0x3EA,
    "target": 0x6F0,
    "action": "controls_only_no_opening_skip",
    "signature": bytes.fromhex(
        "3b7c0050b1bc202db1c4028000004000671a"
        "206dfcd44aa800026710"
    ),
}


def fail(message: str) -> None:
    raise SystemExit(f"FAIL mac_game_intro_input: {message}")


def branch_target(payload: bytes, offset: int) -> int:
    if offset + 4 > len(payload) or payload[offset:offset + 2] != b"\x67\x00":
        fail(f"expected BEQ.W at resource offset 0x{offset:X}")
    displacement = int.from_bytes(payload[offset + 2:offset + 4], "big", signed=True)
    # For a 68000 word branch, the displacement is relative to the extension
    # word address (instruction + 2), matching the original disassembly.
    return offset + 2 + displacement


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("code_directory", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    report_resources = []
    yacht_gameplay_mouse = None
    counts = {"finish": 0, "advance_one_tick": 0, "ignore": 0}
    for resource_id, expected_hash in RESOURCE_SHA256.items():
        path = args.code_directory / f"+{resource_id:05d}.bin"
        if not path.is_file():
            fail(f"missing CODE resource {resource_id}: {path}")
        payload = path.read_bytes()
        actual_hash = hashlib.sha256(payload).hexdigest()
        if actual_hash != expected_hash:
            fail(f"CODE {resource_id} hash {actual_hash} != {expected_hash}")

        route_report = {}
        target_offsets = set()
        for input_kind in ("key", "mouse"):
            branch_offset, expected_target, action = ROUTES[resource_id][input_kind]
            actual_target = branch_target(payload, branch_offset)
            if actual_target != expected_target:
                fail(
                    f"CODE {resource_id} {input_kind} target 0x{actual_target:X} "
                    f"!= 0x{expected_target:X}"
                )
            target_offsets.add(expected_target)
            counts[action] += 1
            route_report[input_kind] = {
                "event": 5 if input_kind == "key" else 6,
                "branch_offset": branch_offset,
                "target": expected_target,
                "action": action,
            }

        for target in target_offsets:
            signature = TARGET_SIGNATURES[(resource_id, target)]
            if payload[target:target + len(signature)] != signature:
                fail(f"CODE {resource_id} target signature changed at 0x{target:X}")

        if resource_id == 18:
            branch_offset = YACHT_GAMEPLAY_MOUSE["branch_offset"]
            expected_target = YACHT_GAMEPLAY_MOUSE["target"]
            actual_target = branch_target(payload, branch_offset)
            if actual_target != expected_target:
                fail(
                    f"CODE 18 gameplay mouse target 0x{actual_target:X} "
                    f"!= 0x{expected_target:X}"
                )
            signature = YACHT_GAMEPLAY_MOUSE["signature"]
            if payload[expected_target:expected_target + len(signature)] != signature:
                fail("CODE 18 gameplay mouse-control signature changed at 0x6F0")
            yacht_gameplay_mouse = {
                "event": 6,
                "branch_offset": branch_offset,
                "target": expected_target,
                "action": YACHT_GAMEPLAY_MOUSE["action"],
            }

        report_resources.append(
            {
                "code_resource": resource_id,
                "game": ROUTES[resource_id]["game"],
                "bytes": len(payload),
                "sha256": actual_hash,
                "routes": route_report,
            }
        )

    if counts != {"finish": 4, "advance_one_tick": 1, "ignore": 5}:
        fail(f"unexpected route totals {counts}")
    if yacht_gameplay_mouse is None:
        fail("missing Yacht gameplay mouse disposition")

    report = {
        "format": "mario-fundamentals-mac-game-intro-input-audit-v1",
        "resources": report_resources,
        "route_counts": counts,
        "yacht_scorecard_opening_mouse": yacht_gameplay_mouse,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "PASS mac_game_intro_input "
        f"resources={len(report_resources)} finish={counts['finish']} "
        f"advance_one_tick={counts['advance_one_tick']} ignore={counts['ignore']} "
        "yacht_board_opening=controls_only_no_opening_skip"
    )


if __name__ == "__main__":
    main()
