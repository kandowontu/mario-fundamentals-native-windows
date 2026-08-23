#!/usr/bin/env python3
"""Extract the exact System 7 font resources used by CODE 5's About panel."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from machfs import Volume
from macresources import parse_file


SOURCE_SHA256 = "2f898fec2605d9855d67df69f6e90bd4d97bff7e5199163a3462bc2dba64f0a7"
SFNT_FACES = (
    ("Times", "Times Bold", 30457, "times-bold.ttf", 62212,
     "60654a5748d966ea1528400a95718c01c38029500d3c4506c9fe75f5622b5394"),
    ("Geneva", "Geneva", 11527, "geneva.ttf", 54684,
     "8dea419507846909d5dc1788cbf233bf7c7fab6e7a829e4a7b1e04cba85f5113"),
    ("Monaco", "Monaco", 16704, "monaco.ttf", 49816,
     "f7fca74ddfc6b70859242cd6e365ed029be9d6c8d6421503cf5b8fb22119683e"),
)

# CODE 5 selects Times 14 with QuickDraw's synthetic bold face, Geneva 9,
# and Monaco 12.  These exact screen-font strikes are present in the source
# System 7 suitcases, so the native port can reproduce their one-bit glyphs
# rather than asking Windows to rasterize the scalable faces differently.
BITMAP_STRIKES = (
    ("Times", 21606, "times-14.nfnt", 4124,
     "c6da9cf88e5b0cf28ef6a70aae2d3d7d64611c6fee5d91067f34fd5913e00e9f"),
    ("Geneva", 3160, "geneva-9.nfnt", 2152,
     "b2bbba4a4cd1e78320c6a4256bd5d08e01326349804905a1e5d577521f03c754"),
    ("Monaco", 18819, "monaco-12.nfnt", 2464,
     "4593cb0d3cc92a5c8acb8a0c2c9d7ba5b59731a12387944795ca00ad1801b9dd"),
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    image = args.image.read_bytes()
    if digest(image) != SOURCE_SHA256:
        raise SystemExit("FAIL source disk image hash does not match the audited image")
    volume = Volume()
    volume.read(image)
    font_folder = volume["System Folder"]["Fonts"]
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = []
    for suitcase, resource_name, resource_id, filename, size, expected_hash in SFNT_FACES:
        resource = next(
            item
            for item in parse_file(font_folder[suitcase].rsrc)
            if item.type == b"sfnt" and item.id == resource_id and item.name == resource_name
        )
        data = bytes(resource)
        if len(data) != size or digest(data) != expected_hash or data[:4] != b"\0\1\0\0":
            raise SystemExit(f"FAIL source font changed: {resource_name}")
        (args.output / filename).write_bytes(data)
        manifest.append(
            {
                "suitcase": suitcase,
                "resource_type": "sfnt",
                "resource_id": resource_id,
                "resource_name": resource_name,
                "file": filename,
                "bytes": size,
                "sha256": expected_hash.upper(),
            }
        )
    for suitcase, resource_id, filename, size, expected_hash in BITMAP_STRIKES:
        resource = next(
            item
            for item in parse_file(font_folder[suitcase].rsrc)
            if item.type == b"NFNT" and item.id == resource_id
        )
        data = bytes(resource)
        if len(data) != size or digest(data) != expected_hash:
            raise SystemExit(f"FAIL source font changed: {suitcase} NFNT {resource_id}")
        (args.output / filename).write_bytes(data)
        manifest.append(
            {
                "suitcase": suitcase,
                "resource_type": "NFNT",
                "resource_id": resource_id,
                "resource_name": None,
                "file": filename,
                "bytes": size,
                "sha256": expected_hash.upper(),
            }
        )
    (args.output / "manifest.json").write_text(
        json.dumps({"source_image_sha256": SOURCE_SHA256.upper(), "fonts": manifest}, indent=2)
        + "\n",
        encoding="utf-8",
    )
    print(f"PASS source System 7 fonts={len(manifest)} bytes={sum(x['bytes'] for x in manifest)}")


if __name__ == "__main__":
    main()
