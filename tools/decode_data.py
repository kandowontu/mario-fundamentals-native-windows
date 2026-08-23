#!/usr/bin/env python3
"""Reproduce the CODE 1 loader's three-stream DATA decompressor."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


def decode_streams(source: bytes) -> tuple[dict[int, int], list[dict[str, int]], int]:
    position = 0
    memory: dict[int, int] = {}
    sections = []

    for section_index in range(3):
        destination = struct.unpack_from(">i", source, position)[0]
        position += 4
        start_destination = destination
        start_source = position

        def write(value: int) -> None:
            nonlocal destination
            memory[destination] = value & 0xFF
            destination += 1

        while True:
            command = source[position]
            position += 1
            if command & 0x80:
                count = (command & 0x7F) + 1
                for value in source[position : position + count]:
                    write(value)
                position += count
            elif command & 0x40:
                destination += (command & 0x3F) + 1
            elif command & 0x20:
                # The 68k loop writes once before DBGE-style repeat testing,
                # so the encoded value expands to mask + 2 bytes.
                count = (command & 0x1F) + 2
                value = source[position]
                position += 1
                for _ in range(count):
                    write(value)
            elif command & 0x10:
                # Unlike the arbitrary-byte fill above, this path does not
                # pre-increment D3 before entering the shared DBGE-style loop.
                count = (command & 0x0F) + 1
                for _ in range(count):
                    write(0xFF)
            elif command == 0:
                break
            elif command == 1:
                write(0xFF)
                write(0xFF)
                write(source[position])
                write(source[position + 1])
                position += 2
            elif command == 2:
                destination += 4
                write(0xFF)
                for value in source[position : position + 3]:
                    write(value)
                position += 3
            elif command == 3:
                write(0xA9)
                write(0xF0)
                destination += 2
                write(source[position])
                write(source[position + 1])
                position += 2
                destination += 1
                write(source[position])
                position += 1
            elif command == 4:
                write(0xA9)
                write(0xF0)
                destination += 1
                for value in source[position : position + 3]:
                    write(value)
                position += 3
                destination += 1
                write(source[position])
                position += 1
            else:
                raise ValueError(f"unknown opcode 0x{command:02X} at source offset 0x{position - 1:X}")

        sections.append(
            {
                "index": section_index,
                "source_start": start_source,
                "source_end": position,
                "destination_start": start_destination,
                "destination_end": destination,
            }
        )

    return memory, sections, position


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("data_resource", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    resource = args.data_resource.read_bytes()
    declared_size = struct.unpack_from(">I", resource, 0)[0]
    memory, sections, consumed = decode_streams(resource[4:])
    lower = min(memory)
    upper = max(memory) + 1
    flat = bytearray(upper - lower)
    for address, value in memory.items():
        flat[address - lower] = value

    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "a5_world.bin").write_bytes(flat)
    metadata = {
        "declared_size": declared_size,
        "compressed_size": len(resource) - 4,
        "compressed_bytes_consumed": consumed,
        "a5_lower_offset": lower,
        "a5_upper_offset": upper,
        "materialized_bytes": len(memory),
        "flat_size": len(flat),
        "sections": sections,
    }
    (args.output / "a5_world.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
