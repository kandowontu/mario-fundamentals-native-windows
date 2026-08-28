#!/usr/bin/env python3
"""Verify every absolute CODE 1 tracked/direct sample call in the Mac disassembly."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


CALL_RE = re.compile(
    r"^(?P<address>[0-9A-F]{8}):.*\bjsr\s+\$(?P<target>a18|caa)\.l$",
    re.IGNORECASE,
)
IMMEDIATE_RE = re.compile(r"\bmove\.w\s+#\$(?P<value>[0-9a-f]+),\s*-\(a7\)$", re.IGNORECASE)

# These are the complete absolute call-site sets in the retained Macintosh
# CODE resources. A PC-relative local function named $CAA in CODE 8 is not the
# shared CODE 1 $CAA entry point and is deliberately excluded by CALL_RE.
EXPECTED_CALLS = {
    "tracked": {
        "CODE_05": {"00000032", "00000328"},
        "CODE_06": {"00000B12", "00000B3C", "00000B98", "00000BBC", "00000BD6", "00000C38", "00000C4A"},
        "CODE_10": {"00000D3C", "00000D50", "00000DBA", "00000DCC", "00000E30"},
        "CODE_11": {"00000908", "00000A08", "00000A50", "0000107C", "000018B8", "00002FCC", "00003454", "00003A62", "00004AAE", "000053B2"},
        "CODE_12": {"000014CC", "00002024", "00002054", "00002074", "000020C8"},
        "CODE_14": {"00001146", "00001154", "00001D12", "00002354"},
        "CODE_16": {"00001036"},
        "CODE_17": {"000007DE", "00002104", "00002E08", "00002E20"},
        "CODE_18": {"00000DD4", "000016B8", "00001972", "00001E4C", "000023DC", "00002A80", "00002D58"},
    },
    "direct_effect": {
        "CODE_03": {"00000892"},
        "CODE_12": {"00000EFA", "0000112C", "000011CE", "0000122E", "00001BA2", "00001BCC"},
        "CODE_14": {"00000EE8", "00001516", "000019D8", "00001BDE", "00002372", "000023C8", "000024DC", "00002770", "00002A5C", "00002ACE", "00002B7C", "000052F4"},
        "CODE_16": {"0000763A"},
    },
}

EXPECTED_IMMEDIATE_RESOURCES = {
    "tracked": {
        5000, 5001, 5003, 5010, 5011, 5012, 5013, 5018, 5019, 5024,
        5028, 5032, 5034, 5042, 5053, 5054, 5057, 5072, 9201, 9202, 9204,
    },
    "direct_effect": {5003, 5004, 5006, 5017, 5023, 5024, 5042, 5043, 5044, 8038, 8042},
}

EXPECTED_DYNAMIC_CALLS = {
    ("CODE_03", "00000892"),
    ("CODE_06", "00000B12"),
    ("CODE_06", "00000B3C"),
    ("CODE_06", "00000B98"),
    ("CODE_06", "00000BBC"),
    ("CODE_06", "00000BD6"),
    ("CODE_06", "00000C38"),
    ("CODE_06", "00000C4A"),
    ("CODE_11", "000053B2"),
    ("CODE_16", "0000763A"),
}


def flattened_expected() -> dict[tuple[str, str], str]:
    return {
        (resource, address): route
        for route, resources in EXPECTED_CALLS.items()
        for resource, addresses in resources.items()
        for address in addresses
    }


def scan(disassembly: Path) -> list[dict[str, object]]:
    calls: list[dict[str, object]] = []
    for path in sorted(disassembly.glob("CODE_*.asm")):
        resource = path.stem
        lines = path.read_text(encoding="utf-8").splitlines()
        for index, line in enumerate(lines):
            match = CALL_RE.match(line)
            if not match:
                continue
            route = "tracked" if match.group("target").lower() == "a18" else "direct_effect"
            argument_line = lines[index - 1] if index else ""
            immediate = IMMEDIATE_RE.search(argument_line)
            calls.append(
                {
                    "resource": resource,
                    "address": match.group("address").upper(),
                    "entry_point": "$A18" if route == "tracked" else "$CAA",
                    "route": route,
                    "argument": int(immediate.group("value"), 16) if immediate else "dynamic",
                    "argument_instruction": argument_line.strip(),
                }
            )
    return calls


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("disassembly", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()

    calls = scan(args.disassembly)
    expected = flattened_expected()
    actual = {(str(row["resource"]), str(row["address"])): str(row["route"]) for row in calls}
    if actual != expected:
        missing = sorted(set(expected.items()) - set(actual.items()))
        unexpected = sorted(set(actual.items()) - set(expected.items()))
        raise SystemExit(f"Mac audio call-site audit changed: missing={missing}, unexpected={unexpected}")

    counts = {route: sum(row["route"] == route for row in calls) for route in EXPECTED_CALLS}
    if counts != {"tracked": 45, "direct_effect": 20}:
        raise SystemExit(f"Mac audio route counts changed: {counts}")

    dynamic = {
        (str(row["resource"]), str(row["address"]))
        for row in calls
        if row["argument"] == "dynamic"
    }
    if dynamic != EXPECTED_DYNAMIC_CALLS:
        raise SystemExit(f"Mac dynamic audio arguments changed: {sorted(dynamic)}")

    immediate_resources = {
        route: {
            int(row["argument"])
            for row in calls
            if row["route"] == route and isinstance(row["argument"], int)
        }
        for route in EXPECTED_CALLS
    }
    if immediate_resources != EXPECTED_IMMEDIATE_RESOURCES:
        raise SystemExit(f"Mac immediate audio resource sets changed: {immediate_resources}")

    report = {
        "status": "PASS",
        "schema": 1,
        "source": "retained Macintosh CODE_*.asm disassembly",
        "semantics": {
            "$A18": "tracked single-sample path; CODE 1 $99C calls $A44 before replacement",
            "$CAA": "concurrent direct-effect scheduler reported by CODE 1 $B22",
        },
        "counts": {"total": len(calls), **counts, "dynamic_arguments": len(dynamic)},
        "immediate_resources": {
            route: sorted(resources) for route, resources in immediate_resources.items()
        },
        "calls": calls,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "PASS mac_audio_routes "
        f"total={len(calls)} tracked={counts['tracked']} "
        f"direct_effect={counts['direct_effect']} dynamic={len(dynamic)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
