#!/usr/bin/env python3
"""Statically verify the native release's PE shape and external dependencies."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


WINDOWS_DLLS = {
    "ADVAPI32.DLL",
    "COMCTL32.DLL",
    "GDI32.DLL",
    "KERNEL32.DLL",
    "USER32.DLL",
    "WINMM.DLL",
}
FORBIDDEN_RUNTIME_DLLS = {
    "LIBGCC_S_SEH-1.DLL",
    "LIBGCC_S_DW2-1.DLL",
    "LIBSTDC++-6.DLL",
    "LIBWINPTHREAD-1.DLL",
    "MSVCP140.DLL",
    "VCRUNTIME140.DLL",
    "VCRUNTIME140_1.DLL",
}
FORBIDDEN_ARTIFACT_TEXT = (
    "MarioFundamentals.img",
    "MarioFundamentals.pack",
    "MarioGameGallery.pack",
    "MARIO.PRD",
    "MARIO.PRS",
    "C:\\Users\\kando",
    "/Users/kando",
)


def fail(message: str) -> None:
    raise SystemExit(f"FAIL {message}")


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def c_string(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if end < 0:
        fail("unterminated PE import name")
    return data[offset:end].decode("ascii")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--asset-pack", action="append", default=[], type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    data = args.executable.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        fail("release is not a DOS/PE executable")
    pe = u32(data, 0x3C)
    if pe + 24 > len(data) or data[pe : pe + 4] != b"PE\0\0":
        fail("release has an invalid PE signature")
    machine = u16(data, pe + 4)
    section_count = u16(data, pe + 6)
    optional_size = u16(data, pe + 20)
    optional = pe + 24
    if machine != 0x8664 or u16(data, optional) != 0x20B:
        fail("release is not a native x86-64 PE32+ executable")
    if u16(data, optional + 68) != 2:
        fail("release is not linked as a Windows GUI application")
    if u32(data, optional + 108) < 16:
        fail("release PE data-directory table is incomplete")

    section_table = optional + optional_size
    sections: list[tuple[int, int, int, int]] = []
    for index in range(section_count):
        raw = section_table + index * 40
        if raw + 40 > len(data):
            fail("release PE section table is truncated")
        virtual_size = u32(data, raw + 8)
        virtual_address = u32(data, raw + 12)
        raw_size = u32(data, raw + 16)
        raw_offset = u32(data, raw + 20)
        sections.append((virtual_address, virtual_size, raw_offset, raw_size))

    def rva_to_offset(rva: int) -> int:
        for virtual_address, virtual_size, raw_offset, raw_size in sections:
            span = max(virtual_size, raw_size)
            if virtual_address <= rva < virtual_address + span:
                result = raw_offset + (rva - virtual_address)
                if result >= len(data):
                    fail(f"RVA 0x{rva:X} maps beyond the executable")
                return result
        fail(f"RVA 0x{rva:X} does not map to a PE section")
        raise AssertionError

    import_rva = u32(data, optional + 112 + 8)
    import_size = u32(data, optional + 112 + 12)
    if not import_rva or import_size < 20:
        fail("release has no normal PE import directory")
    imports: list[str] = []
    descriptor = rva_to_offset(import_rva)
    while True:
        if descriptor + 20 > len(data):
            fail("PE import descriptor table is truncated")
        values = struct.unpack_from("<IIIII", data, descriptor)
        if values == (0, 0, 0, 0, 0):
            break
        imports.append(c_string(data, rva_to_offset(values[3])).upper())
        descriptor += 20
    if len(imports) != len(set(imports)):
        fail("release imports a DLL more than once")

    forbidden = sorted(set(imports) & FORBIDDEN_RUNTIME_DLLS)
    if forbidden:
        fail("release depends on redistributable compiler runtimes: " + ", ".join(forbidden))
    unexpected = sorted(
        dll
        for dll in imports
        if dll not in WINDOWS_DLLS and not dll.startswith("API-MS-WIN-CRT-")
    )
    if unexpected:
        fail("release has non-Windows DLL dependencies: " + ", ".join(unexpected))

    delay_rva = u32(data, optional + 112 + 13 * 8)
    delay_size = u32(data, optional + 112 + 13 * 8 + 4)
    if delay_rva or delay_size:
        fail("release has an unaudited delay-import directory")
    found_strings = [
        value
        for value in FORBIDDEN_ARTIFACT_TEXT
        if value.encode("ascii") in data or value.encode("utf-16le") in data
    ]
    if found_strings:
        fail("release contains build/source artifact paths: " + ", ".join(found_strings))

    embedded_asset_packs = []
    for path in args.asset_pack:
        pack = path.read_bytes()
        offset = data.find(pack)
        if offset < 0 or data.find(pack, offset + 1) >= 0:
            fail(f"release does not contain exactly one byte-identical copy of {path.name}")
        embedded_asset_packs.append(
            {
                "name": path.name,
                "bytes": len(pack),
                "sha256": hashlib.sha256(pack).hexdigest().upper(),
                "executable_offset": offset,
                "exact_occurrences_in_executable": 1,
            }
        )

    report = {
        "status": "PASS",
        "executable": str(args.executable),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest().upper(),
        "pe": {
            "machine": "AMD64",
            "format": "PE32+",
            "subsystem": "Windows GUI",
            "sections": section_count,
            "delay_imports": 0,
        },
        "imports": imports,
        "dependency_classification": "Windows system DLLs/API-set contracts only",
        "redistributable_compiler_runtime_dlls": [],
        "source_artifact_strings": [],
        "embedded_asset_packs": embedded_asset_packs,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        f"PASS amd64-gui imports={len(imports)} compiler_runtime_dlls=0 "
        f"asset_packs={len(embedded_asset_packs)} "
        f"sha256={report['sha256']}"
    )


if __name__ == "__main__":
    main()
