#!/usr/bin/env python3
"""Decode the three relocation streams used by the game's custom 68K loader."""

from __future__ import annotations

import struct
from dataclasses import dataclass


RELOCATION_KINDS = (
    "a5_relative",
    "main_segment_relative",
    "self_segment_relative",
)


def be32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def signed_payload(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return value - (1 << bits) if value & sign else value


@dataclass(frozen=True)
class RelocationStream:
    kind: str
    offsets: tuple[int, ...]
    encoded_bytes: int
    absolute_resets: int
    negative_deltas: int


@dataclass(frozen=True)
class SegmentRelocations:
    code_stop: int
    streams: tuple[RelocationStream, ...]

    @property
    def by_offset(self) -> dict[int, str]:
        return {
            offset: stream.kind
            for stream in self.streams
            for offset in stream.offsets
        }


@dataclass(frozen=True)
class LoaderTargetRelocations:
    """One three-stream relocation group consumed by CODE 1 $286."""

    target: str
    streams: tuple[RelocationStream, ...]

    @property
    def by_offset(self) -> dict[int, str]:
        return {
            offset: stream.kind
            for stream in self.streams
            for offset in stream.offsets
        }


@dataclass(frozen=True)
class LoaderRelocations:
    """The A5-world and CODE-1 groups appended to the DATA resource."""

    stream_start: int
    targets: tuple[LoaderTargetRelocations, ...]

    def for_target(self, target: str) -> LoaderTargetRelocations:
        for item in self.targets:
            if item.target == target:
                return item
        raise KeyError(target)


def decode_stream(
    data: bytes, position: int, count: int, kind: str
) -> tuple[RelocationStream, int]:
    """Mirror CODE 1 $212's signed delta/absolute relocation decoder."""
    start = position
    cursor = 0
    offsets: list[int] = []
    absolute_resets = 0
    negative_deltas = 0
    for _ in range(count):
        if position >= len(data):
            raise ValueError(f"{kind} relocation stream is truncated")
        first = data[position]
        if first & 0x80:
            position += 1
            delta = signed_payload(first & 0x7F, 7) * 2
            cursor += delta
            negative_deltas += delta < 0
        elif first & 0x40:
            if position + 2 > len(data):
                raise ValueError(f"{kind} word relocation is truncated")
            encoded = struct.unpack_from(">H", data, position)[0]
            position += 2
            delta = signed_payload(encoded & 0x3FFF, 14) * 2
            cursor += delta
            negative_deltas += delta < 0
        else:
            if position + 4 > len(data):
                raise ValueError(f"{kind} long relocation is truncated")
            encoded = be32(data, position)
            position += 4
            cursor = signed_payload(encoded & 0x3FFFFFFF, 30) * 2
            absolute_resets += 1
        offsets.append(cursor)
    return (
        RelocationStream(
            kind=kind,
            offsets=tuple(offsets),
            encoded_bytes=position - start,
            absolute_resets=absolute_resets,
            negative_deltas=negative_deltas,
        ),
        position,
    )


def parse_segment_relocations(data: bytes) -> SegmentRelocations:
    if len(data) < 24:
        raise ValueError("loadable CODE resource is too small for its header and streams")
    code_stop = be32(data, 8)
    if code_stop < 12 or code_stop + 12 > len(data):
        raise ValueError(f"invalid CODE relocation start 0x{code_stop:X}")

    position = code_stop
    streams: list[RelocationStream] = []
    seen: dict[int, str] = {}
    for kind in RELOCATION_KINDS:
        if position + 4 > len(data):
            raise ValueError(f"missing {kind} relocation count")
        count = be32(data, position)
        position += 4
        stream, position = decode_stream(data, position, count, kind)
        for offset in stream.offsets:
            if offset < 12 or offset + 4 > code_stop or offset & 1:
                raise ValueError(
                    f"{kind} relocation 0x{offset:X} is outside aligned segment code"
                )
            if offset in seen:
                raise ValueError(
                    f"relocation 0x{offset:X} occurs in both {seen[offset]} and {kind}"
                )
            seen[offset] = kind
        streams.append(stream)

    if position != len(data):
        raise ValueError(
            f"CODE resource has {len(data) - position} bytes after its relocation streams"
        )
    return SegmentRelocations(code_stop=code_stop, streams=tuple(streams))


def parse_loader_relocations(
    data_resource: bytes,
    stream_start: int,
    *,
    a5_lower_offset: int,
    a5_upper_offset: int,
    code1_size: int,
) -> LoaderRelocations:
    """Decode the six DATA-tail streams used to relocate A5 memory and CODE 1.

    CODE 1 $6C and $74 call the three-base dispatcher twice.  The first call
    patches the decoded A5 world and the second patches CODE 1 itself.  Each
    call consumes A5-, main-segment-, and self-segment-relative streams in
    that order; the second call is why raw absolute literals in CODE 1 cannot
    be interpreted before this DATA tail has been decoded.
    """
    if stream_start < 4 or stream_start + 24 > len(data_resource):
        raise ValueError(f"invalid DATA relocation start 0x{stream_start:X}")
    if a5_lower_offset >= a5_upper_offset:
        raise ValueError("invalid decoded A5-world bounds")
    if code1_size < 8:
        raise ValueError("CODE 1 is too small for loader relocation validation")

    position = stream_start
    targets: list[LoaderTargetRelocations] = []
    for target, lower, upper in (
        ("a5_world", a5_lower_offset, a5_upper_offset),
        ("code1", 4, code1_size),
    ):
        streams: list[RelocationStream] = []
        seen: dict[int, str] = {}
        for kind in RELOCATION_KINDS:
            if position + 4 > len(data_resource):
                raise ValueError(f"missing {target} {kind} relocation count")
            count = be32(data_resource, position)
            position += 4
            stream, position = decode_stream(data_resource, position, count, kind)
            for offset in stream.offsets:
                if offset < lower or offset + 4 > upper or offset & 1:
                    raise ValueError(
                        f"{target} {kind} relocation 0x{offset:X} is outside its "
                        "aligned target image"
                    )
                if offset in seen:
                    raise ValueError(
                        f"{target} relocation 0x{offset:X} occurs in both "
                        f"{seen[offset]} and {kind}"
                    )
                seen[offset] = kind
            streams.append(stream)
        targets.append(LoaderTargetRelocations(target=target, streams=tuple(streams)))

    if position != len(data_resource):
        raise ValueError(
            f"DATA resource has {len(data_resource) - position} bytes after its loader streams"
        )
    return LoaderRelocations(stream_start=stream_start, targets=tuple(targets))
