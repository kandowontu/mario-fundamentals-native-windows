#!/usr/bin/env python3
"""Convert standard Macintosh sound, MIDI, PICT, and icon resources."""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import wave
from pathlib import Path

from PIL import Image


def parse_sound(resource: bytes) -> tuple[int, bytes, dict[str, int]]:
    if len(resource) < 36:
        raise ValueError("sound resource is too short")
    sound_format = struct.unpack_from(">H", resource, 0)[0]
    if sound_format == 1:
        data_format_count = struct.unpack_from(">H", resource, 2)[0]
        command_count_offset = 4 + data_format_count * 6
    elif sound_format == 2:
        ref_count = struct.unpack_from(">H", resource, 2)[0]
        if ref_count != 0:
            raise ValueError(f"unsupported format 2 reference count: {ref_count}")
        command_count_offset = 4
    else:
        raise ValueError(f"unsupported sound resource format: {sound_format}")

    command_count = struct.unpack_from(">H", resource, command_count_offset)[0]
    if command_count != 1:
        raise ValueError(f"unsupported sound command count: {command_count}")
    command, param1, header_offset = struct.unpack_from(
        ">HHI", resource, command_count_offset + 2
    )
    if command not in (0x8050, 0x8051) or param1 != 0:
        raise ValueError(f"unsupported sound command 0x{command:04X}/0x{param1:04X}")

    sample_pointer, sample_count, fixed_rate, loop_start, loop_end = struct.unpack_from(
        ">IIIII", resource, header_offset
    )
    encode, base_frequency = struct.unpack_from(">BB", resource, header_offset + 20)
    if sample_pointer != 0 or encode != 0:
        raise ValueError(
            f"unsupported SoundHeader: sample_pointer={sample_pointer}, encode={encode}"
        )
    sample_offset = header_offset + 22
    samples = resource[sample_offset : sample_offset + sample_count]
    if len(samples) != sample_count:
        raise ValueError("truncated sample data")
    sample_rate = fixed_rate >> 16
    return sample_rate, samples, {
        "sample_count": sample_count,
        "sample_rate": sample_rate,
        "loop_start": loop_start,
        "loop_end": loop_end,
        "base_frequency": base_frequency,
    }


def convert_sounds(rip: Path, output: Path) -> list[dict[str, object]]:
    sound_output = output / "sounds"
    sound_output.mkdir(parents=True, exist_ok=True)
    result = []
    for resource_path in sorted((rip / "resources" / "snd_20").glob("*.bin")):
        resource_id = int(resource_path.stem)
        sample_rate, samples, metadata = parse_sound(resource_path.read_bytes())
        output_path = sound_output / f"{resource_id:05d}.wav"
        with wave.open(str(output_path), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(1)
            wav.setframerate(sample_rate)
            wav.writeframes(samples)
        result.append({"id": resource_id, "path": output_path.as_posix(), **metadata})
    return result


def convert_midi(rip: Path, output: Path) -> list[dict[str, object]]:
    midi_output = output / "midi"
    midi_output.mkdir(parents=True, exist_ok=True)
    result = []
    for resource_path in sorted((rip / "resources" / "Midi").glob("*.bin")):
        resource_id = int(resource_path.stem)
        output_path = midi_output / f"{resource_id:05d}.mid"
        shutil.copyfile(resource_path, output_path)
        result.append({"id": resource_id, "path": output_path.as_posix()})
    return result


def convert_pictures(rip: Path, output: Path, ffmpeg: str) -> list[dict[str, object]]:
    pict_output = output / "pictures"
    pict_output.mkdir(parents=True, exist_ok=True)
    result = []
    for resource_path in sorted((rip / "resources" / "PICT").glob("*.bin")):
        resource_id = int(resource_path.stem)
        output_path = pict_output / f"{resource_id:05d}.png"
        output_path.unlink(missing_ok=True)
        process = subprocess.run(
            [
                ffmpeg,
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-f",
                "image2",
                "-c:v",
                "qdraw",
                "-i",
                str(resource_path),
                "-frames:v",
                "1",
                "-update",
                "1",
                str(output_path),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if process.returncode or not output_path.is_file():
            result.append(
                {
                    "id": resource_id,
                    "error": process.stderr.strip()
                    or "ffmpeg returned success without producing an image",
                }
            )
            continue
        try:
            with Image.open(output_path) as image:
                image.load()
                result.append(
                    {
                        "id": resource_id,
                        "path": output_path.as_posix(),
                        "width": image.width,
                        "height": image.height,
                        "mode": image.mode,
                    }
                )
        except Exception as error:
            output_path.unlink(missing_ok=True)
            result.append({"id": resource_id, "error": str(error)})
    return result


def convert_icns(rip: Path, output: Path) -> list[dict[str, object]]:
    icon_output = output / "icons"
    icon_output.mkdir(parents=True, exist_ok=True)
    result = []
    for resource_path in sorted((rip / "resources" / "icns").glob("*.bin")):
        resource_id = int(resource_path.stem)
        try:
            with Image.open(resource_path) as image:
                image.load()
                output_path = icon_output / f"{resource_id:05d}.png"
                image.convert("RGBA").save(output_path)
                result.append(
                    {
                        "id": resource_id,
                        "path": output_path.as_posix(),
                        "width": image.width,
                        "height": image.height,
                    }
                )
        except Exception as error:
            result.append({"id": resource_id, "error": str(error)})
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("rip", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    manifest = {
        "sounds": convert_sounds(args.rip, args.output),
        "midi": convert_midi(args.rip, args.output),
        "pictures": convert_pictures(args.rip, args.output, args.ffmpeg),
        "icons": convert_icns(args.rip, args.output),
    }
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Converted {len(manifest['sounds'])} sounds")
    print(f"Copied {len(manifest['midi'])} MIDI sequences")
    print(
        "Converted "
        f"{sum('path' in item for item in manifest['pictures'])} PICT resources "
        f"({sum('error' in item for item in manifest['pictures'])} preserved raw)"
    )
    print(f"Converted {sum('path' in item for item in manifest['icons'])} icon resources")


if __name__ == "__main__":
    main()
