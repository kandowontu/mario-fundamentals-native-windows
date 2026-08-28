#!/usr/bin/env python3
"""Verify and disposition every custom-loader relocation and absolute 68K call."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path

from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_020, Cs

from mac_code_relocations import (
    be32,
    parse_loader_relocations,
    parse_segment_relocations,
)


EXPECTED_LOADER_HASHES = {
    "relocation_decoder_0x212_0x286": (
        0x212,
        0x286,
        "D8307B48BD5FED28B353873896C258F56B5C792124B3E5AD20354D631D6CB367",
    ),
    "three_base_dispatch_0x286_0x2B2": (
        0x286,
        0x2B2,
        "46DFA5DF78C3A337726F495EFBCD7D8C5ACC3BBE76EF974AA2119B5D03CD2B01",
    ),
}

EXPECTED_DATA_RESOURCE = (
    23167,
    "31A9CD060C33D149C5969E1F9E885C8A236959EAD5BD06AA2282D20B8C6ABE3C",
)
EXPECTED_DATA_RELOCATION_TAIL = (
    704,
    "A583DCA512C644E047CA06829E62A179D213D9DE86478F6361022FA8DF9DBE3E",
)

# (records, encoded bytes, negative deltas) for the two groups consumed by
# CODE 1 $6C/$74.  These are separate from the per-segment trailer streams.
EXPECTED_LOADER_STREAMS = {
    "a5_world": {
        "a5_relative": (493, 512, 98),
        "main_segment_relative": (1, 2, 0),
        "self_segment_relative": (0, 0, 0),
    },
    "code1": {
        "a5_relative": (119, 157, 14),
        "main_segment_relative": (6, 9, 0),
        "self_segment_relative": (0, 0, 0),
    },
}

EXPECTED_CODE1_CALLS = {
    "a5_relative_jsr": 117,
    "main_segment_relative_jsr": 4,
}
EXPECTED_CODE1_NONCALL_RELOCATIONS = {
    ("a5_relative", 0x43F9): 1,
    ("a5_relative", 0x4879): 1,
    ("main_segment_relative", 0x41F9): 1,
    ("main_segment_relative", 0x4879): 1,
}
EXPECTED_CODE1_RESOLVED_CALLS = {
    0x686: (13, 0x6B4),
    0x68E: (13, 0x6A8),
}

EXPECTED_CODE2_MAIN_OFFSETS = (
    0xE0,
    0x12E,
    0x158,
    0x16C,
    0x182,
    0x1AE,
    0x1DC,
    0x1C6,
    0x204,
    0x21A,
    0x234,
)

# Fail closed on the complete retained loader output, not only the one signed
# delta example above.  The fourth value is the number of relocation-backed
# absolute JSR/JMP operands in the segment.  CODE 16 has one real JSR at
# $1AF6 immediately after an inline switch table; a purely linear sweep treats
# the table as an ORI instruction and misses that reachable call.
EXPECTED_SEGMENT_COUNTS = {
    2: (0, 11, 0, 11),
    3: (27, 101, 0, 128),
    4: (0, 0, 0, 0),
    5: (8, 13, 0, 21),
    6: (46, 55, 0, 99),
    7: (8, 0, 0, 8),
    8: (24, 8, 69, 98),
    10: (36, 18, 0, 53),
    11: (315, 80, 0, 389),
    12: (236, 136, 0, 361),
    13: (7, 3, 0, 10),
    14: (219, 95, 0, 304),
    15: (7, 0, 0, 7),
    16: (161, 110, 0, 266),
    17: (373, 68, 0, 432),
    18: (220, 76, 0, 292),
    20: (0, 0, 0, 0),
    21: (0, 0, 0, 0),
    22: (0, 0, 0, 0),
}

EXPECTED_TOTALS = {
    "a5_relative_relocations": 1687,
    "a5_relative_encoded_bytes": 2261,
    "a5_relative_negative_deltas": 710,
    "main_segment_relative_relocations": 774,
    "main_segment_relative_encoded_bytes": 1082,
    "main_segment_relative_negative_deltas": 223,
    "self_segment_relative_relocations": 69,
    "self_segment_relative_encoded_bytes": 102,
    "self_segment_relative_negative_deltas": 7,
    "a5_relative_jsr": 1637,
    "main_segment_relative_jsr": 773,
    "self_segment_relative_jsr": 69,
}

EXPECTED_NONCALL_RELOCATIONS = {
    ("a5_relative", 0x41F9): 1,   # LEA absolute long
    ("a5_relative", 0x4879): 49,  # PEA absolute long
    ("main_segment_relative", 0x4879): 1,
}

EXPECTED_NONLINEAR_CALLERS = {(16, 0x1AF6)}


def be16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def fail(message: str) -> None:
    raise SystemExit(f"FAIL mac_relocations: {message}")


def instruction_offsets(data: bytes, start: int, stop: int) -> set[int]:
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_020)
    offsets: set[int] = set()
    offset = start
    while offset + 1 < stop:
        offsets.add(offset)
        word = be16(data, offset)
        if word & 0xF000 == 0xA000:
            offset += 2
            continue
        instruction = next(md.disasm(data[offset:stop], offset, count=1), None)
        offset += instruction.size if instruction is not None else 2
    return offsets


def jump_table_targets(
    resources: dict[int, bytes], a5_world: bytes, a5_lower_offset: int
) -> dict[int, tuple[int, int]]:
    result = {be32(resources[0], 12): (1, 4)}
    for segment_id, data in resources.items():
        if segment_id in (0, 1):
            continue
        first = be16(data, 0)
        count = be16(data, 2)
        for index in range(count):
            a5_offset = first + index * 8
            flat_offset = a5_offset - a5_lower_offset
            if flat_offset < 0 or flat_offset + 8 > len(a5_world):
                fail(f"CODE {segment_id} A5+0x{a5_offset:X} is outside decoded DATA")
            opcode, reserved, target, target_segment = struct.unpack_from(
                ">HHHH", a5_world, flat_offset
            )
            if opcode != 0xA9F0 or reserved != 0 or target_segment != segment_id:
                fail(f"CODE {segment_id} has an invalid A5 stub at 0x{a5_offset:X}")
            if a5_offset in result:
                fail(f"duplicate A5 jump-table entry 0x{a5_offset:X}")
            result[a5_offset] = (segment_id, target)
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("resource_directory", type=Path)
    parser.add_argument("a5_world", type=Path)
    parser.add_argument("a5_world_summary", type=Path)
    parser.add_argument("data_resource", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    resources = {
        int(path.stem): path.read_bytes()
        for path in sorted(args.resource_directory.glob("*.bin"))
    }
    if 0 not in resources or 1 not in resources:
        fail("CODE 0/1 resources are missing")

    code1 = resources[1]
    loader_hashes = {}
    for name, (start, stop, expected) in EXPECTED_LOADER_HASHES.items():
        actual = hashlib.sha256(code1[start:stop]).hexdigest().upper()
        if actual != expected:
            fail(f"CODE 1 {name} hash {actual} != {expected}")
        loader_hashes[name] = actual

    a5_summary = json.loads(args.a5_world_summary.read_text(encoding="utf-8"))
    data_resource = args.data_resource.read_bytes()
    expected_data_size, expected_data_hash = EXPECTED_DATA_RESOURCE
    actual_data_hash = hashlib.sha256(data_resource).hexdigest().upper()
    if len(data_resource) != expected_data_size or actual_data_hash != expected_data_hash:
        fail(
            f"DATA 0 identity {(len(data_resource), actual_data_hash)} != "
            f"{EXPECTED_DATA_RESOURCE}"
        )
    loader_stream_start = 4 + int(a5_summary["compressed_bytes_consumed"])
    relocation_tail = data_resource[loader_stream_start:]
    expected_tail_size, expected_tail_hash = EXPECTED_DATA_RELOCATION_TAIL
    actual_tail_hash = hashlib.sha256(relocation_tail).hexdigest().upper()
    if len(relocation_tail) != expected_tail_size or actual_tail_hash != expected_tail_hash:
        fail(
            f"DATA relocation tail identity {(len(relocation_tail), actual_tail_hash)} "
            f"!= {EXPECTED_DATA_RELOCATION_TAIL}"
        )
    loader_relocations = parse_loader_relocations(
        data_resource,
        loader_stream_start,
        a5_lower_offset=int(a5_summary["a5_lower_offset"]),
        a5_upper_offset=int(a5_summary["a5_upper_offset"]),
        code1_size=len(code1),
    )
    loader_report = {}
    loader_record_count = 0
    for target in loader_relocations.targets:
        stream_report = {}
        for stream in target.streams:
            actual = (
                len(stream.offsets), stream.encoded_bytes, stream.negative_deltas
            )
            expected = EXPECTED_LOADER_STREAMS[target.target][stream.kind]
            if actual != expected:
                fail(
                    f"DATA {target.target} {stream.kind} stream {actual} != {expected}"
                )
            loader_record_count += len(stream.offsets)
            stream_report[stream.kind] = {
                "records": len(stream.offsets),
                "encoded_bytes": stream.encoded_bytes,
                "absolute_resets": stream.absolute_resets,
                "negative_deltas": stream.negative_deltas,
            }
        loader_report[target.target] = stream_report

    a5_targets = jump_table_targets(
        resources, args.a5_world.read_bytes(), int(a5_summary["a5_lower_offset"])
    )
    boundaries = {
        segment_id: instruction_offsets(
            data,
            4 if segment_id == 1 else 12,
            len(data) if segment_id == 1 else be32(data, 8),
        )
        for segment_id, data in resources.items()
        if segment_id != 0
    }

    code1_calls: Counter[str] = Counter()
    code1_edges: dict[int, tuple[int, int]] = {}
    code1_noncalls: Counter[tuple[str, int]] = Counter()
    for operand_offset, kind in sorted(
        loader_relocations.for_target("code1").by_offset.items()
    ):
        opcode_offset = operand_offset - 2
        opcode = be16(code1, opcode_offset)
        if opcode not in (0x4EB9, 0x4EF9):
            if opcode not in (0x41F9, 0x43F9, 0x4879):
                fail(
                    f"CODE 1 relocation 0x{operand_offset:X} has unclassified "
                    f"absolute opcode 0x{opcode:04X}"
                )
            code1_noncalls[(kind, opcode)] += 1
            continue
        literal = be32(code1, operand_offset)
        if kind == "a5_relative":
            if literal not in a5_targets:
                fail(
                    f"CODE 1 call 0x{opcode_offset:X} targets unknown A5 entry "
                    f"0x{literal:X}"
                )
            target_segment, target_offset = a5_targets[literal]
        else:
            target_segment, target_offset = 1, literal
        if target_offset not in boundaries[target_segment]:
            fail(
                f"CODE 1 call 0x{opcode_offset:X} targets CODE {target_segment} "
                f"non-instruction 0x{target_offset:X}"
            )
        call_kind = "jsr" if opcode == 0x4EB9 else "jmp"
        code1_calls[f"{kind}_{call_kind}"] += 1
        code1_edges[opcode_offset] = (target_segment, target_offset)

    if dict(code1_calls) != EXPECTED_CODE1_CALLS:
        fail(f"CODE 1 relocation-backed calls {dict(code1_calls)} != {EXPECTED_CODE1_CALLS}")
    if dict(code1_noncalls) != EXPECTED_CODE1_NONCALL_RELOCATIONS:
        fail(
            f"CODE 1 non-call relocations {dict(code1_noncalls)} != "
            f"{EXPECTED_CODE1_NONCALL_RELOCATIONS}"
        )
    for source, expected_target in EXPECTED_CODE1_RESOLVED_CALLS.items():
        if code1_edges.get(source) != expected_target:
            fail(
                f"CODE 1 call 0x{source:X} target {code1_edges.get(source)} != "
                f"{expected_target}"
            )

    totals: Counter[str] = Counter()
    reports = []
    for segment_id, data in resources.items():
        if segment_id in (0, 1):
            continue
        relocations = parse_segment_relocations(data)
        by_offset = relocations.by_offset
        if segment_id == 2:
            actual = relocations.streams[1].offsets
            if actual != EXPECTED_CODE2_MAIN_OFFSETS:
                fail(f"CODE 2 signed-delta vector changed: {actual}")

        calls: Counter[str] = Counter()
        call_edges: Counter[tuple[int, int, str]] = Counter()
        decoded_absolute_operands: set[int] = set()
        for offset in sorted(boundaries[segment_id]):
            if offset + 6 > relocations.code_stop:
                continue
            opcode = be16(data, offset)
            if opcode not in (0x4EB9, 0x4EF9):
                continue
            operand_offset = offset + 2
            decoded_absolute_operands.add(operand_offset)
            kind = by_offset.get(operand_offset)
            if kind is None:
                fail(
                    f"CODE {segment_id} absolute call operand 0x{operand_offset:X} "
                    "is absent from all three relocation streams"
                )
        noncall_relocations: Counter[tuple[str, int]] = Counter()
        nonlinear_callers: list[int] = []
        for operand_offset, kind in sorted(by_offset.items()):
            opcode_offset = operand_offset - 2
            opcode = be16(data, opcode_offset)
            if opcode not in (0x4EB9, 0x4EF9):
                if opcode not in (0x41F9, 0x4879):
                    fail(
                        f"CODE {segment_id} relocation 0x{operand_offset:X} has "
                        f"unclassified absolute opcode 0x{opcode:04X}"
                    )
                noncall_relocations[(kind, opcode)] += 1
                continue

            literal = be32(data, operand_offset)
            if kind == "a5_relative":
                if literal not in a5_targets:
                    fail(
                        f"CODE {segment_id} call at 0x{opcode_offset:X} targets unknown "
                        f"A5 entry 0x{literal:X}"
                    )
                target_segment, target_offset = a5_targets[literal]
            elif kind == "main_segment_relative":
                target_segment, target_offset = 1, literal
            else:
                target_segment, target_offset = segment_id, literal
            if target_offset not in boundaries[target_segment]:
                fail(
                    f"CODE {segment_id} call at 0x{opcode_offset:X} targets CODE "
                    f"{target_segment} non-instruction 0x{target_offset:X}"
                )
            call_kind = "jsr" if opcode == 0x4EB9 else "jmp"
            calls[f"{kind}_{call_kind}"] += 1
            call_edges[(target_segment, target_offset, kind)] += 1
            if operand_offset not in decoded_absolute_operands:
                nonlinear_callers.append(opcode_offset)

        actual_nonlinear = {(segment_id, offset) for offset in nonlinear_callers}
        expected_nonlinear = {
            item for item in EXPECTED_NONLINEAR_CALLERS if item[0] == segment_id
        }
        if actual_nonlinear != expected_nonlinear:
            fail(
                f"CODE {segment_id} nonlinear absolute callers "
                f"{sorted(actual_nonlinear)} != {sorted(expected_nonlinear)}"
            )

        stream_counts = tuple(len(stream.offsets) for stream in relocations.streams)
        actual_segment_counts = (*stream_counts, sum(calls.values()))
        if actual_segment_counts != EXPECTED_SEGMENT_COUNTS[segment_id]:
            fail(
                f"CODE {segment_id} relocation/call counts {actual_segment_counts} "
                f"!= {EXPECTED_SEGMENT_COUNTS[segment_id]}"
            )

        stream_report = {}
        for stream in relocations.streams:
            totals[f"{stream.kind}_relocations"] += len(stream.offsets)
            totals[f"{stream.kind}_encoded_bytes"] += stream.encoded_bytes
            totals[f"{stream.kind}_negative_deltas"] += stream.negative_deltas
            stream_report[stream.kind] = {
                "records": len(stream.offsets),
                "encoded_bytes": stream.encoded_bytes,
                "absolute_resets": stream.absolute_resets,
                "negative_deltas": stream.negative_deltas,
            }
        totals.update(calls)
        for key, count in noncall_relocations.items():
            totals[f"noncall_{key[0]}_opcode_{key[1]:04X}"] += count
        reports.append(
            {
                "code_resource": segment_id,
                "code_stop": relocations.code_stop,
                "streams": stream_report,
                "absolute_calls": sum(calls.values()),
                "call_routes": dict(sorted(calls.items())),
                "unique_call_edges": len(call_edges),
                "noncall_relocations": {
                    f"{kind}_opcode_{opcode:04X}": count
                    for (kind, opcode), count in sorted(noncall_relocations.items())
                },
                "nonlinear_callers": [f"0x{offset:X}" for offset in nonlinear_callers],
            }
        )

    for key, expected in EXPECTED_TOTALS.items():
        if totals[key] != expected:
            fail(f"total {key} {totals[key]} != {expected}")
    if any(key.endswith("_jmp") and value for key, value in totals.items()):
        fail("unexpected relocation-backed absolute JMP")
    actual_noncall = {
        (kind, opcode): totals[f"noncall_{kind}_opcode_{opcode:04X}"]
        for kind, opcode in EXPECTED_NONCALL_RELOCATIONS
    }
    if actual_noncall != EXPECTED_NONCALL_RELOCATIONS:
        fail(f"non-call relocation totals {actual_noncall} != {EXPECTED_NONCALL_RELOCATIONS}")
    total_noncall = sum(
        value for key, value in totals.items() if key.startswith("noncall_")
    )
    if total_noncall != sum(EXPECTED_NONCALL_RELOCATIONS.values()):
        fail(f"unexpected non-call relocation total {total_noncall}")

    report = {
        "status": "PASS",
        "format": "mario-fundamentals-mac-relocation-audit-v2",
        "loader_hashes": loader_hashes,
        "data_resource": {
            "bytes": len(data_resource),
            "sha256": actual_data_hash,
            "relocation_tail_offset": loader_stream_start,
            "relocation_tail_bytes": len(relocation_tail),
            "relocation_tail_sha256": actual_tail_hash,
            "targets": loader_report,
        },
        "code1": {
            "absolute_calls": sum(code1_calls.values()),
            "call_routes": dict(sorted(code1_calls.items())),
            "noncall_relocations": {
                f"{kind}_opcode_{opcode:04X}": count
                for (kind, opcode), count in sorted(code1_noncalls.items())
            },
            "pinned_resolved_calls": {
                f"0x{source:X}": {
                    "target_segment": target[0], "target_offset": target[1]
                }
                for source, target in sorted(EXPECTED_CODE1_RESOLVED_CALLS.items())
            },
        },
        "a5_jump_table_entries": len(a5_targets),
        "loader_relocations": loader_record_count,
        "segment_relocations": sum(
            value for key, value in totals.items() if key.endswith("_relocations")
        ),
        "totals": dict(sorted(totals.items())),
        "segments": reports,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "PASS mac_relocations "
        f"segments={len(reports)} a5_entries={len(a5_targets)} "
        "relocations="
        f"{sum(v for k, v in totals.items() if k.endswith('_relocations')) + loader_record_count} "
        "absolute_calls="
        f"{sum(v for k, v in totals.items() if k.endswith('_jsr') or k.endswith('_jmp')) + sum(code1_calls.values())}"
    )


if __name__ == "__main__":
    main()
