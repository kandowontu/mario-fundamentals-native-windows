#!/usr/bin/env python3
"""Catalog the ``MuV ``, ``Ply ``, and ``Img `` animation resources."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


OPCODE_NAMES = {
    2: "marker",
    3: "image_base",
    4: "image",
    5: "image_offset",
    6: "image_offset_base",
    7: "sound",
    10: "end",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_movie_header(data: bytes) -> dict[str, object]:
    if len(data) < 44:
        raise ValueError("MuV resource is shorter than its 44-byte header")
    result: dict[str, object] = {
        "mode": data[0],
        "flags": data[1],
        "origin_y": struct.unpack_from(">h", data, 2)[0],
        "origin_x": struct.unpack_from(">h", data, 4)[0],
        "reserved_y": struct.unpack_from(">h", data, 6)[0],
        "reserved_x": struct.unpack_from(">h", data, 8)[0],
        "height": struct.unpack_from(">H", data, 10)[0],
        "width": struct.unpack_from(">H", data, 12)[0],
        "duration": struct.unpack_from(">I", data, 14)[0],
        "time_scale": struct.unpack_from(">H", data, 18)[0],
        "tick_duration": struct.unpack_from(">H", data, 20)[0],
        "command_count": struct.unpack_from(">H", data, 22)[0],
        "image_count": struct.unpack_from(">H", data, 24)[0],
        "color_depth": struct.unpack_from(">H", data, 26)[0],
        "runtime_seed_hex": data[28:44].hex(),
        "extra_payload_size": len(data) - 44,
        "extra_payload_sha256": sha256(data[44:]),
    }
    return result


def parse_image_records(data: bytes) -> list[dict[str, int]]:
    if len(data) % 12:
        raise ValueError("Img resource size is not a multiple of 12")
    records = []
    for index in range(len(data) // 12):
        values = struct.unpack_from(">hhhhhh", data, index * 12)
        y_offset, x_offset, top, left, bottom, right = values
        records.append(
            {
                "index": index,
                "x_offset": x_offset,
                "y_offset": y_offset,
                "source_top": top,
                "source_left": left,
                "source_bottom": bottom,
                "source_right": right,
                "source_width": right - left,
                "source_height": bottom - top,
            }
        )
    return records


def parse_commands(data: bytes, count: int) -> list[dict[str, object]]:
    required = count * 16
    if required > len(data):
        raise ValueError(f"Ply resource needs {required} bytes but has {len(data)}")
    commands = []
    for index in range(count):
        opcode, flags, parameter, start, duration, packed = struct.unpack_from(
            ">BBHIII", data, index * 16
        )
        if opcode not in OPCODE_NAMES:
            raise ValueError(f"unknown Ply opcode {opcode} at record {index}")
        command: dict[str, object] = {
            "index": index,
            "opcode": opcode,
            "operation": OPCODE_NAMES[opcode],
            "flags": flags,
            "parameter": parameter,
            "start": start,
            "duration": duration,
        }
        if opcode in (5, 6):
            command["offset_x"] = struct.unpack(">h", packed.to_bytes(4, "big")[:2])[0]
            command["offset_y"] = struct.unpack(">h", packed.to_bytes(4, "big")[2:])[0]
        elif packed:
            command["extra"] = packed
        commands.append(command)
    return commands


def resource_map(directory: Path) -> dict[int, Path]:
    return {int(path.stem): path for path in directory.glob("*.bin")}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("rip_resources", type=Path)
    parser.add_argument("pak_manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    movies = resource_map(args.rip_resources / "MuV_20")
    timelines = resource_map(args.rip_resources / "Ply_20")
    images = resource_map(args.rip_resources / "Img_20")
    pak_data = json.loads(args.pak_manifest.read_text(encoding="utf-8"))
    pak_by_id = {item["id"]: item for item in pak_data["resources"]}
    errors: list[str] = []
    warnings: list[str] = []

    if set(movies) != set(timelines):
        errors.append("MuV and Ply resource ID sets differ")

    image_sheets: dict[int, dict[str, object]] = {}
    for resource_id, path in sorted(images.items()):
        data = path.read_bytes()
        try:
            records = parse_image_records(data)
            pak = pak_by_id.get(resource_id)
            if pak is not None and len(records) != len(pak["frames"]):
                raise ValueError(
                    f"Img has {len(records)} records; Pak has {len(pak['frames'])} frames"
                )
            frame_errors = []
            if pak is not None:
                for record, frame in zip(records, pak["frames"]):
                    if record["source_width"] > frame["width"] or record[
                        "source_height"
                    ] > frame["height"]:
                        frame_errors.append(record["index"])
            if frame_errors:
                raise ValueError(f"source rectangles exceed Pak frames: {frame_errors}")
            if pak is None:
                warnings.append(
                    f"Img {resource_id} has no same-ID Pak resource (legacy/unused candidate)"
                )
            image_sheets[resource_id] = {
                "id": resource_id,
                "pak_id": resource_id if pak is not None else None,
                "sha256": sha256(data),
                "record_count": len(records),
                "records": records,
            }
        except Exception as error:
            errors.append(f"Img {resource_id}: {error}")

    movie_entries = []
    for resource_id, movie_path in sorted(movies.items()):
        try:
            movie_data = movie_path.read_bytes()
            timeline_data = timelines[resource_id].read_bytes()
            header = parse_movie_header(movie_data)
            commands = parse_commands(timeline_data, int(header["command_count"]))
            if commands[-1]["opcode"] != 10:
                raise ValueError("timeline does not end in opcode 10")
            if commands[-1]["start"] != header["duration"]:
                raise ValueError(
                    f"end time {commands[-1]['start']} != duration {header['duration']}"
                )

            sheet_id = resource_id if resource_id < 10000 else resource_id // 1000 * 1000
            sheet = image_sheets.get(sheet_id)
            if sheet is None:
                raise ValueError(f"image sheet {sheet_id} is absent")
            bad_frames = sorted(
                {
                    int(command["parameter"])
                    for command in commands
                    if int(command["opcode"]) in (3, 4, 5, 6)
                    and int(command["parameter"]) >= int(sheet["record_count"])
                }
            )
            resolved = not bad_frames
            if bad_frames:
                warnings.append(
                    f"movie {resource_id} references frames outside runtime sheet "
                    f"{sheet_id}: {bad_frames} (legacy/unused candidate)"
                )
            movie_entries.append(
                {
                    "id": resource_id,
                    "image_sheet_id": sheet_id,
                    "resolved": resolved,
                    "movie_sha256": sha256(movie_data),
                    "timeline_sha256": sha256(timeline_data),
                    "timeline_size": len(timeline_data),
                    "timeline_trailing_size": len(timeline_data)
                    - int(header["command_count"]) * 16,
                    **header,
                    "commands": commands,
                }
            )
        except Exception as error:
            errors.append(f"movie {resource_id}: {error}")

    output = {
        "format": "BrainStorm movie resources",
        "movie_count": len(movie_entries),
        "image_sheet_count": len(image_sheets),
        "movies": movie_entries,
        "image_sheets": list(image_sheets.values()),
        "errors": errors,
        "warnings": warnings,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(
        f"Cataloged {len(movie_entries)} movies and {len(image_sheets)} image sheets; "
        f"{len(errors)} errors, {len(warnings)} warnings"
    )
    if errors:
        for error in errors:
            print(error)


if __name__ == "__main__":
    main()
