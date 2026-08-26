#!/usr/bin/env python3
"""Verify the original DOS selected-game intro input dispatch tables.

The DOS shell uses event 28 for key-down and event 45 for mouse-down.  Each
selected-game intro is a two-export FBOV overlay whose first export contains a
compact event/target table.  This audit pins those exact tables and the shared
completion-call branch instead of relying on edition-neutral assumptions.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


EVENT_KEY_DOWN = 28
EVENT_MOUSE_DOWN = 45
EVENT_CONTROLLER_TICK = 102

EXPECTED = (
    # index, game, switch keys, switch targets
    (1, "Backgammon", (3, 28, 45, 102, 10000, 10001, 10002, 10003),
     (0x0126, 0x0129, 0x0129, 0x0140, 0x0027, 0x00E2, 0x00DD, 0x00DD)),
    (7, "Checkers", (3, 28, 102, 10000, 10001, 10002, 10003),
     (0x01B4, 0x01B7, 0x01CE, 0x0027, 0x0146, 0x0141, 0x0141)),
    (13, "Dominoes", (3, 28, 45, 102, 10000, 10001, 10002, 10003),
     (0x01FB, 0x01FE, 0x01FE, 0x0215, 0x0027, 0x0189, 0x0184, 0x0184)),
    (17, "Go Fish", (28, 45, 102, 10000, 10001, 10002, 10003),
     (0x011D, 0x011D, 0x0134, 0x0027, 0x00E2, 0x00DD, 0x00DD)),
    (30, "Yacht", (28, 45, 102, 10000, 10001, 10002, 10003),
     (0x01AD, 0x01AD, 0x01C4, 0x0027, 0x0157, 0x0152, 0x0152)),
)

# Every input-completion branch passes its class record (0020:xxxx) and scene
# object to the same resident completion helper at 0160:0008.
COMPLETION_SUFFIX = bytes.fromhex(
    "68 20 00 FF 76 08 FF 76 06 9A 08 00 60 01 83 C4 08"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"word at 0x{offset:04X} is outside the overlay")
    return int.from_bytes(data[offset : offset + 2], "little")


def dispatcher(code: bytes) -> tuple[tuple[int, ...], tuple[int, ...], int]:
    if not code.startswith(b"\x55\x8b\xec"):
        raise ValueError("first export has no Borland BP prologue")
    for offset in range(3, min(0x30, len(code) - 6)):
        if code[offset] != 0xB9 or code[offset + 3] != 0xBB:
            continue
        count = u16(code, offset + 1)
        table = u16(code, offset + 4)
        if count <= 0 or count > 32 or table + count * 4 > len(code):
            continue
        keys = tuple(u16(code, table + item * 2) for item in range(count))
        targets = tuple(
            u16(code, table + count * 2 + item * 2) for item in range(count)
        )
        return keys, targets, table
    raise ValueError("first-export event dispatcher was not found")


def completion_branch(code: bytes, target: int) -> bool:
    # The branch begins with PUSH imm16 for the class-record offset, followed
    # by the invariant suffix above.
    return (
        target + 3 + len(COMPLETION_SUFFIX) <= len(code)
        and code[target] == 0x68
        and code[target + 3 : target + 3 + len(COMPLETION_SUFFIX)] == COMPLETION_SUFFIX
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable_manifest", type=Path)
    parser.add_argument("overlay_root", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()

    manifest = json.loads(args.executable_manifest.read_text(encoding="utf-8"))
    overlays = {int(item["index"]): item for item in manifest["fbov"]["overlays"]}

    # Overlay 20 is the DOS publisher/title controller.  Its event 28 branch
    # reads the key event pair at BP+0C/+0E, while event 45 consumes the mouse
    # point copied to BP-04/-02.  Both reach the same completion helper.  Pin
    # these landmarks so the event names below are source-derived, not guessed.
    shell = overlays.get(20)
    if shell is None:
        raise SystemExit("overlay 20 is missing from the executable manifest")
    shell_code = (args.overlay_root / str(shell["code_path"])).read_bytes()
    if len(shell_code) != int(shell["code_bytes"]) or sha256(shell_code) != shell["code_sha256"]:
        raise SystemExit("overlay 20 does not match the executable manifest")
    shell_keys, shell_targets, _ = dispatcher(shell_code)
    expected_shell_keys = (28, 45, 102, 10000, 10001)
    expected_shell_targets = (0x0170, 0x0136, 0x01A2, 0x0033, 0x0109)
    if shell_keys != expected_shell_keys or shell_targets != expected_shell_targets:
        raise SystemExit("overlay 20 input-event table changed")
    if shell_code[0x0170 : 0x017A] != bytes.fromhex("8B 46 0E 8B 56 0C 89 56 F4 89"):
        raise SystemExit("overlay 20 event 28 is no longer the key-down branch")
    if shell_code[0x0136 : 0x013C] != bytes.fromhex("FF 76 FE FF 76 FC"):
        raise SystemExit("overlay 20 event 45 is no longer the mouse-down branch")
    if shell_code[0x019E : 0x01A2] != bytes.fromhex("EB AB EB A9"):
        raise SystemExit("overlay 20 key-down branch no longer reaches input completion")
    if not completion_branch(shell_code, 0x0159):
        raise SystemExit("overlay 20 input completion helper changed")

    results: list[dict[str, object]] = []
    for index, game, expected_keys, expected_targets in EXPECTED:
        overlay = overlays.get(index)
        if overlay is None:
            raise SystemExit(f"overlay {index} ({game}) is missing")
        code_path = args.overlay_root / str(overlay["code_path"])
        code = code_path.read_bytes()
        if len(code) != int(overlay["code_bytes"]) or sha256(code) != overlay["code_sha256"]:
            raise SystemExit(f"overlay {index} ({game}) does not match the executable manifest")
        keys, targets, table = dispatcher(code)
        if keys != expected_keys or targets != expected_targets:
            raise SystemExit(f"overlay {index} ({game}) event table changed")

        key_target = targets[keys.index(EVENT_KEY_DOWN)]
        mouse_target = targets[keys.index(EVENT_MOUSE_DOWN)] if EVENT_MOUSE_DOWN in keys else None
        if not completion_branch(code, key_target):
            raise SystemExit(f"overlay {index} ({game}) key-down no longer completes the intro")
        if mouse_target is not None:
            if mouse_target != key_target or not completion_branch(code, mouse_target):
                raise SystemExit(f"overlay {index} ({game}) mouse-down completion changed")
        if EVENT_CONTROLLER_TICK not in keys:
            raise SystemExit(f"overlay {index} ({game}) lost its controller-tick event")

        results.append(
            {
                "overlay_index": index,
                "game": game,
                "code_sha256": overlay["code_sha256"],
                "event_table_offset": f"0x{table:04X}",
                "event_keys": list(keys),
                "event_targets": [f"0x{target:04X}" for target in targets],
                "key_down": "finish",
                "mouse_down": "finish" if mouse_target is not None else "ignore",
            }
        )

    report = {
        "status": "PASS",
        "source": "original DOS MARIO.EXE FBOV overlay event dispatchers",
        "event_map": {
            str(EVENT_KEY_DOWN): "key_down",
            str(EVENT_MOUSE_DOWN): "mouse_down",
            str(EVENT_CONTROLLER_TICK): "controller_tick",
        },
        "shared_completion_helper": "0160:0008",
        "overlays": results,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS dos_game_intro_input overlays=5 key_finish=5 mouse_finish=4 mouse_ignore=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
