#!/usr/bin/env python3
"""Extract the original HFS application and every resource without mutation."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

from machfs import Volume
from macresources import parse_file


GAME_FOLDER = "Mario's FUNdamentals 1.1"
GAME_FILE = "Mario's FUNdamentals 1.1"


def sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest()


def safe_component(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._")
    return value[:80] or "unnamed"


def type_directory(type_code: bytes) -> str:
    return "".join(
        chr(byte) if chr(byte).isalnum() or chr(byte) in "_-" else f"_{byte:02X}"
        for byte in type_code
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    image_bytes = args.image.read_bytes()
    volume = Volume()
    volume.read(image_bytes)

    game_folder = volume[GAME_FOLDER]
    game_file = game_folder[GAME_FILE]
    resources = list(parse_file(game_file.rsrc))

    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "forks").mkdir(exist_ok=True)
    (args.output / "resources").mkdir(exist_ok=True)
    (args.output / "documents").mkdir(exist_ok=True)

    (args.output / "forks" / "MarioFundamentals.data").write_bytes(game_file.data)
    (args.output / "forks" / "MarioFundamentals.rsrc").write_bytes(game_file.rsrc)

    for document_name in (
        "Read Me - Mario's FUNdamentals",
        "What To Do If The Game Crashes",
    ):
        document = game_folder[document_name]
        text = bytes(document.data).decode("mac_roman").replace("\r", "\n")
        (args.output / "documents" / f"{safe_component(document_name)}.txt").write_text(
            text, encoding="utf-8", newline="\n"
        )

    manifest_resources = []
    counts: Counter[str] = Counter()
    total_sizes: defaultdict[str, int] = defaultdict(int)

    for resource in resources:
        display_type = resource.type.decode("mac_roman")
        directory_name = type_directory(resource.type)
        resource_dir = args.output / "resources" / directory_name
        resource_dir.mkdir(exist_ok=True)

        suffix = f"_{safe_component(resource.name)}" if resource.name else ""
        filename = f"{resource.id:+06d}{suffix}.bin"
        relative_path = Path("resources") / directory_name / filename
        resource_data = bytes(resource)
        (args.output / relative_path).write_bytes(resource_data)

        counts[display_type] += 1
        total_sizes[display_type] += len(resource_data)
        manifest_resources.append(
            {
                "type": display_type,
                "type_hex": resource.type.hex().upper(),
                "id": resource.id,
                "name": resource.name,
                "attributes": resource.attribs,
                "size": len(resource_data),
                "sha256": sha256(resource_data),
                "path": relative_path.as_posix(),
            }
        )

    manifest = {
        "source": {
            "path": str(args.image.resolve()),
            "size": len(image_bytes),
            "sha256": sha256(image_bytes),
            "volume_name": volume.name,
        },
        "application": {
            "folder": GAME_FOLDER,
            "file": GAME_FILE,
            "type": game_file.type.decode("mac_roman"),
            "creator": game_file.creator.decode("mac_roman"),
            "data_fork_size": len(game_file.data),
            "data_fork_sha256": sha256(game_file.data),
            "resource_fork_size": len(game_file.rsrc),
            "resource_fork_sha256": sha256(game_file.rsrc),
            "resource_count": len(resources),
        },
        "types": [
            {"type": key, "count": counts[key], "bytes": total_sizes[key]}
            for key in counts
        ],
        "resources": manifest_resources,
    }
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print(f"Extracted {len(resources)} resources from {len(game_file.rsrc)} bytes")
    for item in manifest["types"]:
        print(f"{item['type']!r:8} {item['count']:4} {item['bytes']:9} bytes")


if __name__ == "__main__":
    main()
