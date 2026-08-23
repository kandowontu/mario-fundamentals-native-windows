#!/usr/bin/env python3
"""Normalize an uncompressed 24-bit BMP to the Win32 resource-safe v3 header."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def normalize(data: bytes) -> bytes:
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("input is not a BMP file")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    planes, bit_count = struct.unpack_from("<HH", data, 26)
    compression = struct.unpack_from("<I", data, 30)[0]
    if dib_size < 40 or planes != 1 or bit_count != 24 or compression != 0:
        raise ValueError("only uncompressed 24-bit BMP input is supported")
    if pixel_offset < 14 + dib_size or pixel_offset > len(data):
        raise ValueError("invalid BMP pixel offset")

    dib = bytearray(data[14:54])
    struct.pack_into("<I", dib, 0, 40)
    result = bytearray(b"BM")
    result.extend(b"\0" * 12)
    result.extend(dib)
    result.extend(data[pixel_offset:])
    struct.pack_into("<I", result, 2, len(result))
    struct.pack_into("<I", result, 10, 54)
    return bytes(result)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    converted = normalize(args.input.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(converted)
    print(f"Wrote {args.output} ({len(converted)} bytes)")


if __name__ == "__main__":
    main()
