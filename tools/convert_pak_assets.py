#!/usr/bin/env python3
"""Decode BrainStorm's compressed ``Pak `` bitmap resources.

The two layers implemented here are direct ports of the routines at CODE 10
offset 0x1A4 (resource LZSS) and CODE 3 offset 0x1C5A (span stream blitter).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from PIL import Image


def decode_resource(resource: bytes) -> bytes:
    """Expand the outer LZSS wrapper used only for ``Pak `` resources."""
    if len(resource) < 5:
        raise ValueError("resource is too short")
    expected = struct.unpack_from(">I", resource, 0)[0]
    source = 4
    output = bytearray()
    while source < len(resource) and len(output) < expected:
        flags = resource[source]
        source += 1
        for _ in range(8):
            if source >= len(resource) or len(output) >= expected:
                break
            if flags & 1:
                if source + 2 > len(resource):
                    raise ValueError("truncated LZSS back-reference")
                token = struct.unpack_from(">H", resource, source)[0]
                source += 2
                distance = (token & 0x0FFF) + 1
                count = (token >> 12) + 3
                if distance > len(output):
                    raise ValueError(
                        f"invalid LZSS distance {distance} at source 0x{source:X}"
                    )
                for _ in range(count):
                    output.append(output[-distance])
            else:
                output.append(resource[source])
                source += 1
            flags >>= 1
    if len(output) != expected:
        raise ValueError(f"expanded to {len(output)} bytes, expected {expected}")
    return bytes(output)


def decode_frame(
    stream: bytes, width: int, height: int
) -> tuple[bytes, bytes, int, int]:
    """Interpret the type-2 span stream used by CODE 3's bitmap blitter."""
    pixels = bytearray(width * height)
    alpha = bytearray(width * height)
    source = 0
    x = 0
    y = 0
    written = 0
    # Each entry is (body source offset, remaining replay count).  The 68k
    # routine intentionally executes the body once plus this replay count.
    repeats: list[list[int]] = []

    def ensure_horizontal(count: int) -> None:
        if y < 0 or y >= height or x + count > width:
            raise ValueError(
                f"span escapes {width}x{height} frame at ({x},{y}), count {count}"
            )

    while True:
        if source >= len(stream):
            raise ValueError("span stream has no terminator")
        command = stream[source]
        source += 1
        if command & 0x80:
            y += 1
            x = 0

        count = command & 0x1F
        if count == 0:
            if source + 2 > len(stream):
                raise ValueError("truncated extended span count")
            count = struct.unpack_from(">H", stream, source)[0]
            source += 2

        operation = (command >> 5) & 3
        if operation == 0:
            if count > 1:
                # CODE 3 decrements the encoded count before pushing it.
                # The body is entered once immediately, then replayed until
                # this remaining count becomes negative: exactly `count`
                # executions in total.
                repeats.append([source, count - 1])
                continue
            if not repeats:
                break
            repeats[-1][1] -= 1
            if repeats[-1][1] >= 0:
                source = repeats[-1][0]
            else:
                repeats.pop()
            continue

        if operation == 1:  # transparent skip
            ensure_horizontal(count)
            x += count
            continue

        if operation == 2:  # repeated palette index; count one terminates
            if count == 1:
                break
            if source >= len(stream):
                raise ValueError("truncated repeated span")
            value = stream[source]
            source += 1
            ensure_horizontal(count)
            start = y * width + x
            pixels[start : start + count] = bytes([value]) * count
            alpha[start : start + count] = b"\xFF" * count
            x += count
            written += count
            continue

        if source + count > len(stream):  # literal palette indices
            raise ValueError("truncated literal span")
        ensure_horizontal(count)
        start = y * width + x
        pixels[start : start + count] = stream[source : source + count]
        alpha[start : start + count] = b"\xFF" * count
        source += count
        x += count
        written += count

    return bytes(pixels), bytes(alpha), source, written


def read_palette(resource: bytes) -> list[tuple[int, int, int]]:
    if len(resource) < 8:
        raise ValueError("clut resource is too short")
    last_index = struct.unpack_from(">H", resource, 6)[0]
    colors = [(0, 0, 0)] * 256
    position = 8
    for sequential_index in range(last_index + 1):
        if position + 8 > len(resource):
            raise ValueError("truncated clut entry")
        _value, red, green, blue = struct.unpack_from(">HHHH", resource, position)
        position += 8
        # These device color tables use sentinel/zero ColorSpec values; the
        # entry order, as consumed by the indexed framebuffer, is canonical.
        colors[sequential_index] = (red >> 8, green >> 8, blue >> 8)
    return colors


def save_rgba(
    path: Path,
    pixels: bytes,
    alpha: bytes,
    width: int,
    height: int,
    palette: list[tuple[int, int, int]],
) -> None:
    rgba = bytearray(width * height * 4)
    for offset, palette_index in enumerate(pixels):
        red, green, blue = palette[palette_index]
        target = offset * 4
        rgba[target : target + 4] = bytes((red, green, blue, alpha[offset]))
    image = Image.frombytes("RGBA", (width, height), bytes(rgba))
    image.save(path, optimize=True)


def convert_pak(
    resource_path: Path,
    output_root: Path,
    palettes: dict[int, list[tuple[int, int, int]]],
    default_palette: int,
) -> dict[str, object]:
    resource = resource_path.read_bytes()
    unpacked = decode_resource(resource)
    flags, frame_count, tag = struct.unpack_from(">HHH", unpacked, 0)
    encoding = flags & 0x7FFF
    has_origin = bool(flags & 0x8000)
    if encoding != 2:
        raise ValueError(f"unsupported Pak encoding 0x{encoding:04X}")
    table_end = 6 + frame_count * 4
    if table_end > len(unpacked):
        raise ValueError("truncated frame-offset table")
    offsets = [
        struct.unpack_from(">I", unpacked, 6 + index * 4)[0]
        for index in range(frame_count)
    ]
    if any(offset < table_end or offset >= len(unpacked) for offset in offsets):
        raise ValueError("invalid frame offset")

    resource_id = int(resource_path.stem)
    resource_output = output_root / f"{resource_id:05d}"
    resource_output.mkdir(parents=True, exist_ok=True)
    frames = []
    # Palette 1000 is the game's main 256-color table.  Keeping this argument
    # explicit makes alternate renders reproducible while palette ownership is
    # cross-referenced against the movie metadata.
    palette = palettes[default_palette]
    for index, offset in enumerate(offsets):
        end = offsets[index + 1] if index + 1 < len(offsets) else len(unpacked)
        position = offset
        origin_x = origin_y = 0
        if has_origin:
            origin_x, origin_y = struct.unpack_from(">hh", unpacked, position)
            position += 4
        if position + 4 > end:
            raise ValueError(f"truncated frame {index} header")
        width, height = struct.unpack_from(">HH", unpacked, position)
        position += 4
        if width == 0 or height == 0:
            raise ValueError(f"invalid frame {index} dimensions {width}x{height}")
        pixels, alpha, consumed, written = decode_frame(
            unpacked[position:end], width, height
        )
        output_path = resource_output / f"{index:03d}.png"
        save_rgba(output_path, pixels, alpha, width, height, palette)
        frames.append(
            {
                "index": index,
                "offset": offset,
                "origin_x": origin_x,
                "origin_y": origin_y,
                "width": width,
                "height": height,
                "stream_bytes_consumed": consumed,
                "opaque_pixels": written,
                "path": output_path.as_posix(),
            }
        )
    return {
        "id": resource_id,
        "compressed_size": len(resource),
        "unpacked_size": len(unpacked),
        "compressed_sha256": hashlib.sha256(resource).hexdigest(),
        "unpacked_sha256": hashlib.sha256(unpacked).hexdigest(),
        "flags": flags,
        "encoding": encoding,
        "has_origin": has_origin,
        "tag": tag,
        "palette_id": default_palette,
        "frames": frames,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pak_directory", type=Path)
    parser.add_argument("clut_directory", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--palette", type=int, default=1000)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    palettes = {
        int(path.stem): read_palette(path.read_bytes())
        for path in args.clut_directory.glob("*.bin")
    }
    if args.palette not in palettes:
        raise ValueError(f"palette {args.palette} is not present")

    resources = []
    errors = []
    for resource_path in sorted(args.pak_directory.glob("*.bin")):
        try:
            resources.append(
                convert_pak(
                    resource_path, args.output / "resources", palettes, args.palette
                )
            )
        except Exception as error:
            errors.append({"path": resource_path.as_posix(), "error": str(error)})
    manifest = {
        "format": "BrainStorm Pak resource",
        "palette_ids": sorted(palettes),
        "default_palette_id": args.palette,
        "resources": resources,
        "errors": errors,
    }
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(
        f"Decoded {len(resources)} Pak resources / "
        f"{sum(len(item['frames']) for item in resources)} frames; "
        f"{len(errors)} errors"
    )


if __name__ == "__main__":
    main()
