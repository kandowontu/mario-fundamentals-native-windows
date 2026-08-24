#!/usr/bin/env python3
"""Decode and audit Mario's Game Gallery 1.0 DOS media resources.

The DOS PRS stores little-endian resource tables, movie metadata, and
timelines. Unlike the Macintosh QuickDraw records, DOS MuV/Img geometry uses
conventional x/y and width/height field order. Pak sheet headers are
little-endian, while their per-frame bitmap records retain the original
big-endian Presage span representation. SND resources are six-byte
little-endian headers followed by unsigned 8-bit PCM.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path

from PIL import Image


MOVIE_OPCODES = {2, 3, 4, 5, 6, 7, 10}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def read_dib_palette(resource: bytes) -> list[tuple[int, int, int]]:
    if len(resource) < 54 or resource[:2] != b"BM":
        raise ValueError("palette resource is not a Windows BMP")
    file_bytes, pixel_offset = struct.unpack_from("<IxxxxI", resource, 2)
    info_bytes = struct.unpack_from("<I", resource, 14)[0]
    width, height, planes, bits = struct.unpack_from("<iiHH", resource, 18)
    if file_bytes != len(resource) or info_bytes != 40:
        raise ValueError("unsupported BMP palette header")
    if width != 1 or abs(height) != 1 or planes != 1 or bits != 8:
        raise ValueError("DOS palette resource is not the expected 1x1 indexed BMP")
    palette_offset = 14 + info_bytes
    if palette_offset + 256 * 4 > pixel_offset or pixel_offset > len(resource):
        raise ValueError("BMP palette table is truncated")
    return [
        (resource[offset + 2], resource[offset + 1], resource[offset])
        for offset in range(palette_offset, palette_offset + 256 * 4, 4)
    ]


def decode_span_frame(
    stream: bytes, width: int, height: int
) -> tuple[bytes, bytes, int, int]:
    pixels = bytearray(width * height)
    alpha = bytearray(width * height)
    position = 0
    x = 0
    y = 0
    written = 0
    repeats: list[list[int]] = []

    def check(count: int) -> None:
        if y < 0 or y >= height or x < 0 or x + count > width:
            raise ValueError(
                f"span escapes {width}x{height} frame at ({x},{y}), count {count}"
            )

    while True:
        if position >= len(stream):
            raise ValueError("span stream has no terminator")
        command = stream[position]
        position += 1
        if command & 0x80:
            y += 1
            x = 0
        count = command & 0x1F
        if count == 0:
            if position + 2 > len(stream):
                raise ValueError("truncated extended span count")
            # Per-frame Pak records retain Presage's big-endian bitmap format.
            count = struct.unpack_from(">H", stream, position)[0]
            position += 2
        operation = command >> 5 & 3
        if operation == 0:
            if count > 1:
                repeats.append([position, count - 1])
                continue
            if not repeats:
                break
            repeats[-1][1] -= 1
            if repeats[-1][1] >= 0:
                position = repeats[-1][0]
            else:
                repeats.pop()
            continue
        if operation == 1:
            check(count)
            x += count
            continue
        if operation == 2:
            if count == 1:
                break
            if position >= len(stream):
                raise ValueError("truncated fill span")
            value = stream[position]
            position += 1
            check(count)
            start = y * width + x
            pixels[start : start + count] = bytes([value]) * count
            alpha[start : start + count] = b"\xFF" * count
            x += count
            written += count
            continue
        if position + count > len(stream):
            raise ValueError("truncated literal span")
        check(count)
        start = y * width + x
        pixels[start : start + count] = stream[position : position + count]
        alpha[start : start + count] = b"\xFF" * count
        position += count
        x += count
        written += count
    return bytes(pixels), bytes(alpha), position, written


def save_rgba(
    path: Path,
    pixels: bytes,
    alpha: bytes,
    width: int,
    height: int,
    palette: list[tuple[int, int, int]],
) -> None:
    rgba = bytearray(width * height * 4)
    for index, palette_index in enumerate(pixels):
        red, green, blue = palette[palette_index]
        rgba[index * 4 : index * 4 + 4] = bytes((red, green, blue, alpha[index]))
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.frombytes("RGBA", (width, height), bytes(rgba)).save(path, optimize=True)


def decode_pak(
    path: Path, output: Path, palette: list[tuple[int, int, int]]
) -> dict[str, object]:
    resource = path.read_bytes()
    if len(resource) < 10:
        raise ValueError("Pak is too short")
    flags, frame_count, tag = struct.unpack_from("<HHH", resource, 0)
    if flags & 0x7FFF != 2:
        raise ValueError(f"unsupported Pak flags 0x{flags:04X}")
    table_end = 6 + frame_count * 4
    if table_end > len(resource):
        raise ValueError("Pak frame table is truncated")
    offsets = [struct.unpack_from("<I", resource, 6 + index * 4)[0] for index in range(frame_count)]
    if offsets != sorted(offsets) or len(set(offsets)) != len(offsets):
        raise ValueError("Pak frame offsets are not strictly increasing")
    if any(offset < table_end or offset >= len(resource) for offset in offsets):
        raise ValueError("Pak frame offset lies outside the resource")

    resource_id = int(path.stem)
    frames: list[dict[str, object]] = []
    for index, offset in enumerate(offsets):
        end = offsets[index + 1] if index + 1 < len(offsets) else len(resource)
        position = offset
        origin_x = origin_y = 0
        if flags & 0x8000:
            if position + 4 > end:
                raise ValueError(f"Pak frame {index} has a truncated origin")
            origin_x, origin_y = struct.unpack_from(">hh", resource, position)
            position += 4
        if position + 4 > end:
            raise ValueError(f"Pak frame {index} has a truncated size")
        width, height = struct.unpack_from(">HH", resource, position)
        position += 4
        if width == 0 or height == 0:
            raise ValueError(f"Pak frame {index} has invalid dimensions {width}x{height}")
        pixels, alpha, consumed, opaque = decode_span_frame(
            resource[position:end], width, height
        )
        trailing = resource[position + consumed : end]
        relative = Path(f"{resource_id:05d}") / f"{index:03d}.png"
        save_rgba(output / relative, pixels, alpha, width, height, palette)
        frames.append(
            {
                "index": index,
                "offset": offset,
                "bytes": end - offset,
                "origin_x": origin_x,
                "origin_y": origin_y,
                "width": width,
                "height": height,
                "opaque_pixels": opaque,
                "stream_bytes_consumed": consumed,
                "trailing_bytes": len(trailing),
                "trailing_sha256": sha256(trailing) if trailing else None,
                "rgba_sha256": sha256(
                    b"".join(
                        bytes((*palette[value], alpha[pixel]))
                        for pixel, value in enumerate(pixels)
                    )
                ),
                "path": relative.as_posix(),
            }
        )
    tiled_page: dict[str, object] | None = None
    if frame_count == 64 and all(
        frame["width"] == frames[0]["width"] and frame["height"] == frames[0]["height"]
        for frame in frames
    ):
        tile_width = int(frames[0]["width"])
        tile_height = int(frames[0]["height"])
        page = Image.new("RGBA", (tile_width * 8, tile_height * 8))
        for index, frame in enumerate(frames):
            with Image.open(output / str(frame["path"])) as tile:
                page.alpha_composite(
                    tile.convert("RGBA"),
                    ((index % 8) * tile_width, (index // 8) * tile_height),
                )
        page_relative = Path("pages") / f"{resource_id:05d}.png"
        page_path = output / page_relative
        page_path.parent.mkdir(parents=True, exist_ok=True)
        page.save(page_path, optimize=True)
        tiled_page = {
            "width": tile_width * 8,
            "height": tile_height * 8,
            "rgba_sha256": sha256(page.tobytes()),
            "path": page_relative.as_posix(),
        }

    return {
        "id": resource_id,
        "bytes": len(resource),
        "sha256": sha256(resource),
        "flags": f"0x{flags:04X}",
        "has_origin": bool(flags & 0x8000),
        "tag": tag,
        "frame_count": frame_count,
        "frames": frames,
        "tiled_page": tiled_page,
    }


def parse_img(path: Path) -> list[dict[str, int]]:
    resource = path.read_bytes()
    if len(resource) % 12:
        raise ValueError("Img resource is not a multiple of 12 bytes")
    records = []
    for offset in range(0, len(resource), 12):
        x, y, left, top, right, bottom = struct.unpack_from("<hhhhhh", resource, offset)
        if right < left or bottom < top:
            raise ValueError("Img source rectangle is inverted")
        records.append(
            {
                "index": offset // 12,
                "x": x,
                "y": y,
                "left": left,
                "top": top,
                "right": right,
                "bottom": bottom,
            }
        )
    return records


def parse_movie(
    path: Path,
    root: Path,
    image_tables: dict[int, list[dict[str, int]]],
    pak_frame_counts: dict[int, int],
) -> dict[str, object]:
    header = path.read_bytes()
    if len(header) < 44:
        raise ValueError("MuV header is shorter than 44 bytes")
    resource_id = int(path.stem)
    origin_x, origin_y = struct.unpack_from("<hh", header, 2)
    width, height = struct.unpack_from("<HH", header, 10)
    duration = struct.unpack_from("<I", header, 14)[0]
    time_scale, tick_duration, declared_command_count, declared_image_count = struct.unpack_from(
        "<HHHH", header, 18
    )
    timeline_path = root / "Ply" / f"{resource_id:05d}.ply"
    if not timeline_path.exists():
        raise ValueError("matching Ply resource is absent")
    timeline = timeline_path.read_bytes()
    if len(timeline) < declared_command_count * 16:
        raise ValueError("Ply timeline is shorter than its declared command table")
    command_count = declared_command_count
    timeline_tail = timeline[command_count * 16 :]
    image_sheet_id = resource_id if resource_id < 10000 else resource_id // 1000 * 1000
    images = image_tables.get(image_sheet_id)
    frame_count = pak_frame_counts.get(image_sheet_id)
    resolved = images is not None and frame_count is not None
    commands: list[dict[str, object]] = []
    unresolved_images: set[int] = set()
    opcode_counts: Counter[int] = Counter()
    for index in range(command_count):
        offset = index * 16
        opcode, flags, parameter, start, command_duration, first, second = struct.unpack_from(
            "<BBHIIhh", timeline, offset
        )
        if opcode not in MOVIE_OPCODES:
            raise ValueError(f"unknown Ply opcode {opcode}")
        opcode_counts[opcode] += 1
        if opcode in {3, 4, 5, 6}:
            if images is None or frame_count is None or parameter >= len(images) or parameter >= frame_count:
                unresolved_images.add(parameter)
                resolved = False
        # DOS MuV/Img records use x/y order, but Ply motion commands retain
        # the original vertical/horizontal pair. Normalize the audit manifest
        # to semantic x/y so it describes what the runtime renders.
        x, y = (second, first) if opcode in {5, 6} else (first, second)
        commands.append(
            {
                "index": index,
                "opcode": opcode,
                "flags": flags,
                "parameter": parameter,
                "start": start,
                "duration": command_duration,
                "x": x,
                "y": y,
            }
        )
    if not commands or commands[-1]["opcode"] != 10 or commands[-1]["start"] != duration:
        raise ValueError("Ply timeline has an invalid end marker")
    return {
        "id": resource_id,
        "bytes": len(header),
        "sha256": sha256(header),
        "header_sha256": sha256(header[:44]),
        "trailing_bytes": len(header) - 44,
        "trailing_sha256": sha256(header[44:]) if len(header) > 44 else None,
        "mode": struct.unpack_from("<H", header, 0)[0],
        "origin_x": origin_x,
        "origin_y": origin_y,
        "width": width,
        "height": height,
        "duration": duration,
        "time_scale": time_scale,
        "tick_duration": tick_duration,
        "command_count": command_count,
        "declared_command_count": declared_command_count,
        "timeline_trailing_bytes": len(timeline_tail),
        "timeline_trailing_sha256": sha256(timeline_tail) if timeline_tail else None,
        "declared_image_count": declared_image_count,
        "image_sheet_id": image_sheet_id,
        "resolved": resolved,
        "unresolved_images": sorted(unresolved_images),
        "opcode_counts": {str(key): value for key, value in sorted(opcode_counts.items())},
        "timeline_sha256": sha256(timeline),
        "commands": commands,
    }


def audit_sound(path: Path) -> dict[str, object]:
    resource = path.read_bytes()
    if len(resource) < 6:
        raise ValueError("SND resource is shorter than its header")
    encoding, sample_bytes, sample_rate = struct.unpack_from("<HHH", resource, 0)
    if encoding != 3:
        raise ValueError(f"unsupported SND encoding {encoding}")
    if sample_bytes != len(resource) - 6:
        raise ValueError("SND sample count does not match its payload")
    return {
        "id": int(path.stem),
        "bytes": len(resource),
        "sha256": sha256(resource),
        "encoding": encoding,
        "sample_bytes": sample_bytes,
        "sample_rate": sample_rate,
        "duration_milliseconds": round(sample_bytes * 1000 / sample_rate),
        "pcm_sha256": sha256(resource[6:]),
    }


def audit_xmi(path: Path) -> dict[str, object]:
    resource = path.read_bytes()
    if len(resource) < 32 or resource[:4] != b"FORM" or resource[8:12] != b"XDIR":
        raise ValueError("XMI lacks the expected FORM/XDIR container")
    declared = struct.unpack_from(">I", resource, 4)[0] + 8
    if declared > len(resource):
        raise ValueError("XMI top-level FORM is truncated")
    evnt_offset = resource.find(b"EVNT")
    if evnt_offset < 0 or evnt_offset + 8 > len(resource):
        raise ValueError("XMI contains no EVNT chunk")
    evnt_bytes = struct.unpack_from(">I", resource, evnt_offset + 4)[0]
    if evnt_offset + 8 + evnt_bytes > len(resource):
        raise ValueError("XMI EVNT chunk is truncated")
    events = resource[evnt_offset + 8 : evnt_offset + 8 + evnt_bytes]

    def variable(position: int) -> tuple[int, int]:
        value = 0
        for _ in range(4):
            if position >= len(events):
                raise ValueError("XMI variable integer is truncated")
            byte = events[position]
            position += 1
            value = value << 7 | byte & 0x7F
            if not byte & 0x80:
                return value, position
        raise ValueError("XMI variable integer is invalid")

    position = 0
    tick = 0
    maximum_tick = 0
    channel_events = 0
    generated_note_offs = 0
    note_on_zero_velocity = 0
    tempo_values: list[int] = []
    controller_counts: Counter[int] = Counter()
    while position < len(events):
        while position < len(events) and events[position] < 0x80:
            tick += events[position]
            position += 1
        if position >= len(events):
            break
        status = events[position]
        position += 1
        if status == 0xFF:
            if position >= len(events):
                raise ValueError("XMI meta event is truncated")
            meta_type = events[position]
            position += 1
            length, position = variable(position)
            if position + length > len(events):
                raise ValueError("XMI meta payload is truncated")
            if meta_type == 0x51 and length == 3:
                tempo_values.append(int.from_bytes(events[position : position + 3], "big"))
            position += length
            if meta_type == 0x2F:
                break
            continue
        if status in (0xF0, 0xF7):
            length, position = variable(position)
            position += length
            if position > len(events):
                raise ValueError("XMI system payload is truncated")
            continue
        if status < 0x80 or status >= 0xF0:
            raise ValueError(f"unsupported XMI status 0x{status:02X}")
        data_count = 1 if status & 0xE0 == 0xC0 else 2
        if position + data_count > len(events):
            raise ValueError("XMI channel event is truncated")
        data1 = events[position]
        data2 = events[position + 1] if data_count == 2 else 0
        position += data_count
        if data1 & 0x80 or data2 & 0x80:
            raise ValueError("XMI channel data byte is invalid")
        channel_events += 1
        maximum_tick = max(maximum_tick, tick)
        if status & 0xF0 == 0xB0:
            controller_counts[data1] += 1
        if status & 0xF0 == 0x90:
            duration, position = variable(position)
            maximum_tick = max(maximum_tick, tick + duration)
            if data2:
                generated_note_offs += 1
            else:
                note_on_zero_velocity += 1
    return {
        "id": int(path.stem),
        "bytes": len(resource),
        "sha256": sha256(resource),
        "form_declared_bytes": declared,
        "event_offset": evnt_offset + 8,
        "event_bytes": evnt_bytes,
        "event_sha256": sha256(events),
        "channel_events": channel_events,
        "generated_note_offs": generated_note_offs,
        "native_midi_event_count": channel_events + generated_note_offs,
        "note_on_zero_velocity": note_on_zero_velocity,
        "maximum_tick": maximum_tick,
        "duration_milliseconds_at_120hz": round(maximum_tick * 1000 / 120),
        "tempo_meta_values_ignored_by_xmidi": tempo_values,
        "controller_counts": {
            str(controller): count for controller, count in sorted(controller_counts.items())
        },
        "loop_start_events": controller_counts[0x74],
        "loop_break_events": controller_counts[0x75],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("resources", type=Path, help="output directory from rip_dos_prs.py")
    parser.add_argument("output", type=Path, help="decoded PNG output directory")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--palette", type=int, default=1000)
    args = parser.parse_args()

    dib_path = args.resources / "DIB" / f"{args.palette:05d}.dib"
    palette_bytes = dib_path.read_bytes()
    palette = read_dib_palette(palette_bytes)
    args.output.mkdir(parents=True, exist_ok=True)

    pak_resources = []
    for path in sorted((args.resources / "Pak").glob("*.pak")):
        try:
            pak_resources.append(decode_pak(path, args.output / "Pak", palette))
        except Exception as error:
            raise ValueError(f"{path}: {error}") from error
    pak_frame_counts = {int(item["id"]): int(item["frame_count"]) for item in pak_resources}
    pak_frames = {int(item["id"]): item["frames"] for item in pak_resources}
    image_tables = {
        int(path.stem): parse_img(path)
        for path in sorted((args.resources / "Img").glob("*.img"))
    }
    geometry_records = 0
    geometry_unpaired_ids: list[int] = []
    for resource_id, records in image_tables.items():
        frames = pak_frames.get(resource_id)
        if frames is None:
            # A small set of shipped Img tables is retained without a same-ID
            # Pak; movie resolution below records whether any runtime path can
            # use them. They cannot participate in the geometry proof.
            geometry_unpaired_ids.append(resource_id)
            continue
        if len(records) > len(frames):
            raise ValueError(f"Img {resource_id} exceeds its matching Pak frame table")
        for record, frame in zip(records, frames):
            source_width = int(record["right"]) - int(record["left"])
            source_height = int(record["bottom"]) - int(record["top"])
            if (
                int(record["left"]) != 0
                or int(record["top"]) != 0
                or source_width != int(frame["width"])
                or source_height != int(frame["height"])
            ):
                raise ValueError(
                    f"Img {resource_id} frame {record['index']} geometry "
                    f"{source_width}x{source_height} does not match Pak "
                    f"{frame['width']}x{frame['height']}"
                )
            geometry_records += 1
    movies = [
        parse_movie(path, args.resources, image_tables, pak_frame_counts)
        for path in sorted((args.resources / "MuV").glob("*.muv"))
    ]
    sounds = [audit_sound(path) for path in sorted((args.resources / "SND").glob("*.snd"))]
    music = [audit_xmi(path) for path in sorted((args.resources / "XMI").glob("*.xmi"))]
    unresolved = [item for item in movies if not item["resolved"]]
    sound_ids = {int(item["id"]) for item in sounds}
    movie_sound_references: Counter[int] = Counter(
        int(command["parameter"])
        for movie in movies
        for command in movie["commands"]
        if int(command["opcode"]) == 7
    )
    dangling_movie_sound_ids = sorted(set(movie_sound_references) - sound_ids)

    manifest = {
        "schema": 1,
        "format": "Mario's Game Gallery 1.0 DOS decoded media",
        "palette": {
            "id": args.palette,
            "path": str(dib_path),
            "bytes": len(palette_bytes),
            "sha256": sha256(palette_bytes),
            "rgb_sha256": sha256(b"".join(bytes(color) for color in palette)),
        },
        "pak": {
            "resource_count": len(pak_resources),
            "frame_count": sum(int(item["frame_count"]) for item in pak_resources),
            "resources": pak_resources,
        },
        "images": {
            "resource_count": len(image_tables),
            "record_count": sum(len(records) for records in image_tables.values()),
            "pak_geometry_exact_count": geometry_records,
            "pak_geometry_unpaired_ids": geometry_unpaired_ids,
            "resources": [
                {"id": resource_id, "records": records}
                for resource_id, records in sorted(image_tables.items())
            ],
        },
        "movies": {
            "resource_count": len(movies),
            "command_count": sum(int(item["command_count"]) for item in movies),
            "resolved_count": len(movies) - len(unresolved),
            "unresolved_count": len(unresolved),
            "unresolved_ids": [item["id"] for item in unresolved],
            "resources": movies,
        },
        "sounds": {
            "resource_count": len(sounds),
            "sample_bytes": sum(int(item["sample_bytes"]) for item in sounds),
            "movie_opcode7_reference_count": sum(movie_sound_references.values()),
            "movie_opcode7_unique_ids": len(movie_sound_references),
            "movie_opcode7_resolved_ids": len(set(movie_sound_references) & sound_ids),
            "movie_opcode7_source_dangling_ids": dangling_movie_sound_ids,
            "movie_opcode7_source_dangling_count": len(dangling_movie_sound_ids),
            "resources": sounds,
        },
        "music": {"resource_count": len(music), "resources": music},
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        "PASS "
        f"pak={len(pak_resources)} frames={manifest['pak']['frame_count']} "
        f"movies={len(movies)} resolved={len(movies) - len(unresolved)} "
        f"unresolved={len(unresolved)} sounds={len(sounds)} xmi={len(music)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
