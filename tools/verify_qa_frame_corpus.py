#!/usr/bin/env python3
"""Fail closed if any deterministic dual-edition QA frame changes.

Independent vanilla screenshots establish fidelity for representative source
states. This verifier complements them by content-pinning every deterministic
frame that the native presentation sweeps produce, including all sampled game
intro instants and random-dependent opening/outcome states.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


def fail(message: str) -> None:
    raise ValueError(message)


def inspect_bmp(path: Path) -> tuple[bytes, int, int]:
    payload = path.read_bytes()
    if len(payload) < 54 or payload[:2] != b"BM":
        fail(f"{path}: not a Windows BMP")
    declared_bytes = struct.unpack_from("<I", payload, 2)[0]
    dib_bytes = struct.unpack_from("<I", payload, 14)[0]
    width, signed_height = struct.unpack_from("<ii", payload, 18)
    planes, bits_per_pixel = struct.unpack_from("<HH", payload, 26)
    compression = struct.unpack_from("<I", payload, 30)[0]
    if declared_bytes != len(payload):
        fail(f"{path}: header size {declared_bytes} != actual size {len(payload)}")
    if dib_bytes != 40 or planes != 1 or bits_per_pixel != 32 or compression != 0:
        fail(
            f"{path}: expected 40-byte uncompressed 32-bit BMP, got "
            f"DIB={dib_bytes} planes={planes} bpp={bits_per_pixel} compression={compression}"
        )
    if width <= 0 or signed_height == 0:
        fail(f"{path}: invalid dimensions {width}x{signed_height}")
    return payload, width, abs(signed_height)


def audit_edition(name: str, directory: Path, expected: dict[str, object]) -> tuple[dict, list[str]]:
    errors: list[str] = []
    frames = sorted(directory.glob("*.bmp"), key=lambda path: path.name)
    expected_frames = int(expected["frames"])
    expected_width = int(expected["width"])
    expected_height = int(expected["height"])
    expected_bytes = int(expected["bmp_bytes"])
    if len(frames) != expected_frames:
        errors.append(f"{name}: found {len(frames)} frames; expected {expected_frames}")

    corpus = hashlib.sha256()
    records = []
    for path in frames:
        try:
            payload, width, height = inspect_bmp(path)
        except (OSError, ValueError) as error:
            errors.append(str(error))
            continue
        digest = hashlib.sha256(payload).digest()
        corpus.update(path.name.encode("utf-8"))
        corpus.update(b"\0")
        corpus.update(len(payload).to_bytes(8, "big"))
        corpus.update(digest)
        if len(payload) != expected_bytes:
            errors.append(f"{name}:{path.name}: {len(payload)} bytes; expected {expected_bytes}")
        if (width, height) != (expected_width, expected_height):
            errors.append(
                f"{name}:{path.name}: {width}x{height}; "
                f"expected {expected_width}x{expected_height}"
            )
        records.append(
            {
                "file": path.name,
                "bytes": len(payload),
                "width": width,
                "height": height,
                "sha256": digest.hex().upper(),
            }
        )

    actual_digest = corpus.hexdigest().upper()
    expected_digest = str(expected["corpus_sha256"]).upper()
    if actual_digest != expected_digest:
        errors.append(f"{name}: corpus SHA-256 {actual_digest} != {expected_digest}")
    return (
        {
            "status": "FAIL" if errors else "PASS",
            "directory": str(directory),
            "frames": len(frames),
            "logical_size": [expected_width, expected_height],
            "corpus_sha256": actual_digest,
            "expected_corpus_sha256": expected_digest,
            "files": records,
        },
        errors,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("--mac-directory", type=Path, required=True)
    parser.add_argument("--dos-directory", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()

    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    if baseline.get("format") != "mario-fundamentals-qa-frame-baseline-v1":
        raise SystemExit("unsupported QA frame baseline format")
    editions = baseline.get("editions")
    if not isinstance(editions, dict) or set(editions) != {"macintosh", "dos"}:
        raise SystemExit("QA frame baseline must contain exactly Macintosh and DOS")

    mac_report, mac_errors = audit_edition(
        "macintosh", args.mac_directory, editions["macintosh"]
    )
    dos_report, dos_errors = audit_edition("dos", args.dos_directory, editions["dos"])
    errors = mac_errors + dos_errors
    report = {
        "status": "FAIL" if errors else "PASS",
        "format": "mario-fundamentals-qa-frame-corpus-audit-v1",
        "baseline": str(args.baseline),
        "digest_method": baseline.get("digest_method"),
        "editions": {"macintosh": mac_report, "dos": dos_report},
        "errors": errors,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    if errors:
        print(f"FAIL qa_frame_corpus errors={len(errors)}")
        for error in errors:
            print(f"ERROR {error}")
        return 1
    print(
        "PASS qa_frame_corpus "
        f"macintosh={mac_report['frames']}:{mac_report['corpus_sha256']} "
        f"dos={dos_report['frames']}:{dos_report['corpus_sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
