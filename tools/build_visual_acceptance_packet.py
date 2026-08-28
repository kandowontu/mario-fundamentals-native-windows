#!/usr/bin/env python3
"""Build a compact human-review packet from the pinned dual-edition QA corpus.

The release gate proves deterministic behavior and compares representative
states with retained vanilla captures.  Publication still requires a human
visual pass.  This tool turns the complete frame corpus into lossless contact
sheets with links back to every full-resolution BMP; it does not replace or
relax any automated verifier.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw


@dataclass(frozen=True)
class Sheet:
    edition: str
    slug: str
    title: str
    files: tuple[Path, ...]
    columns: int


def corpus_digest(files: Iterable[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(files, key=lambda item: item.name):
        payload = path.read_bytes()
        digest.update(path.name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(len(payload).to_bytes(8, "big"))
        digest.update(hashlib.sha256(payload).digest())
    return digest.hexdigest().upper()


def require_pinned_corpus(directory: Path, expected: dict[str, object]) -> tuple[Path, ...]:
    files = tuple(sorted(directory.glob("*.bmp"), key=lambda item: item.name))
    expected_count = int(expected["frames"])
    if len(files) != expected_count:
        raise SystemExit(f"{directory}: found {len(files)} BMPs; expected {expected_count}")
    actual = corpus_digest(files)
    wanted = str(expected["corpus_sha256"]).upper()
    if actual != wanted:
        raise SystemExit(f"{directory}: corpus SHA-256 {actual} != {wanted}")
    return files


def matching(directory: Path, *patterns: str) -> tuple[Path, ...]:
    found: set[Path] = set()
    for pattern in patterns:
        found.update(directory.glob(pattern))
    return tuple(sorted(found, key=lambda item: item.name))


def render_sheet(sheet: Sheet, output: Path) -> tuple[int, int]:
    logical = (512, 384) if sheet.edition == "macintosh" else (320, 200)
    thumbnail = (256, 192) if sheet.edition == "macintosh" else (320, 200)
    label_height = 18
    rows = (len(sheet.files) + sheet.columns - 1) // sheet.columns
    canvas = Image.new(
        "RGB",
        (sheet.columns * thumbnail[0], rows * (thumbnail[1] + label_height)),
        (30, 30, 30),
    )
    draw = ImageDraw.Draw(canvas)
    for index, path in enumerate(sheet.files):
        frame = Image.open(path).convert("RGB")
        if frame.size != logical:
            raise SystemExit(f"{path}: {frame.size} != {logical}")
        frame = frame.resize(thumbnail, Image.Resampling.NEAREST)
        left = (index % sheet.columns) * thumbnail[0]
        top = (index // sheet.columns) * (thumbnail[1] + label_height)
        canvas.paste(frame, (left, top))
        draw.text((left + 3, top + thumbnail[1] + 2), path.stem, fill="white")
    canvas.save(output, format="PNG", optimize=True)
    return canvas.size


def link_list(files: tuple[Path, ...], packet_root: Path) -> str:
    return " ".join(
        f'<a href="{html.escape(Path(os.path.relpath(path, packet_root)).as_posix())}">'
        f"{html.escape(path.stem)}</a>"
        for path in files
    )


def main() -> None:
    project_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mac-directory", type=Path, default=project_root / "work" / "qa" / "mac")
    parser.add_argument("--dos-directory", type=Path, default=project_root / "work" / "qa" / "dos")
    parser.add_argument(
        "--output-directory",
        type=Path,
        default=project_root / "work" / "qa" / "visual-acceptance",
    )
    parser.add_argument(
        "--baseline",
        type=Path,
        default=project_root / "tools" / "qa-frame-baseline.json",
    )
    args = parser.parse_args()

    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))["editions"]
    mac_dir = args.mac_directory.resolve()
    dos_dir = args.dos_directory.resolve()
    require_pinned_corpus(mac_dir, baseline["macintosh"])
    require_pinned_corpus(dos_dir, baseline["dos"])

    output = args.output_directory.resolve()
    output.mkdir(parents=True, exist_ok=True)

    mac_intro = matching(mac_dir, "11-intro-*.bmp")
    dos_intro = matching(dos_dir, "07-intro-*.bmp")
    mac_opening = matching(mac_dir, "2?-opening-*.bmp")
    dos_opening = matching(dos_dir, "1?-opening-*.bmp")
    sheets = (
        Sheet(
            "macintosh", "mac-shell-menu", "Macintosh startup, title, board reveal, and menu",
            matching(mac_dir, "0*.bmp", "10*.bmp", "12*.bmp", "13*.bmp", "14*.bmp"), 4,
        ),
        Sheet(
            "dos", "dos-shell-menu", "DOS startup, title, board reveal, menu, and modal shell",
            matching(dos_dir, "00*.bmp", "01*.bmp", "02*.bmp", "03*.bmp", "04*.bmp",
                     "05*.bmp", "06*.bmp", "12-checkers-*.bmp", "15*.bmp"), 4,
        ),
        Sheet("macintosh", "mac-intros-a", "Macintosh game intros: samples 00–06", tuple(
            path for path in mac_intro if 0 <= int(path.stem.rsplit("-", 1)[1]) <= 6), 7),
        Sheet("macintosh", "mac-intros-b", "Macintosh game intros: samples 07–13", tuple(
            path for path in mac_intro if 7 <= int(path.stem.rsplit("-", 1)[1]) <= 13), 7),
        Sheet("macintosh", "mac-intros-c", "Macintosh game intros: samples 14–20", tuple(
            path for path in mac_intro if 14 <= int(path.stem.rsplit("-", 1)[1]) <= 20), 7),
        Sheet("dos", "dos-intros-a", "DOS game intros: samples 00–06", tuple(
            path for path in dos_intro if 0 <= int(path.stem.rsplit("-", 1)[1]) <= 6), 7),
        Sheet("dos", "dos-intros-b", "DOS game intros: samples 07–13", tuple(
            path for path in dos_intro if 7 <= int(path.stem.rsplit("-", 1)[1]) <= 13), 7),
        Sheet("dos", "dos-intros-c", "DOS game intros: samples 14–20", tuple(
            path for path in dos_intro if 14 <= int(path.stem.rsplit("-", 1)[1]) <= 20), 7),
        Sheet(
            "macintosh", "mac-intro-input", "Macintosh selected-game intro input outcomes",
            matching(mac_dir, "11-input-game-intro-*.bmp"), 5,
        ),
        Sheet("macintosh", "mac-openings", "Macintosh live opening-controller states", mac_opening, 8),
        Sheet("dos", "dos-openings", "DOS live opening-controller states", dos_opening, 8),
        Sheet(
            "macintosh", "mac-gameplay", "Macintosh gameplay, outcome, and replay states",
            matching(mac_dir, "31*.bmp", "32*.bmp", "33*.bmp", "34*.bmp", "35*.bmp"), 4,
        ),
        Sheet(
            "dos", "dos-gameplay", "DOS gameplay, outcome, and replay states",
            matching(dos_dir, "31*.bmp", "32*.bmp", "33*.bmp", "34*.bmp", "35*.bmp"), 4,
        ),
        Sheet(
            "macintosh", "mac-yacht", "Macintosh Yacht actor, cup, dice, and score states",
            matching(mac_dir, "30-yacht*.bmp", "36-yacht*.bmp"), 4,
        ),
        Sheet(
            "dos", "dos-yacht", "DOS Yacht actor, cup, dice, and score states",
            matching(dos_dir, "30-yacht*.bmp", "36-yacht*.bmp"), 4,
        ),
    )

    sheet_records = []
    for sheet in sheets:
        if not sheet.files:
            raise SystemExit(f"review sheet {sheet.slug} has no frames")
        filename = f"{sheet.slug}.png"
        width, height = render_sheet(sheet, output / filename)
        sheet_records.append((sheet, filename, width, height))

    candidate = project_root / "dist" / "MarioFundamentals.exe"
    candidate_hash = hashlib.sha256(candidate.read_bytes()).hexdigest().upper() if candidate.is_file() else "missing"
    candidate_bytes = candidate.stat().st_size if candidate.is_file() else 0
    sections = []
    for sheet, filename, width, height in sheet_records:
        links = link_list(sheet.files, output)
        sections.append(
            f'<section id="{sheet.slug}"><label class="check"><input type="checkbox" '
            f'data-key="{sheet.slug}"> visually accepted</label><h2>{html.escape(sheet.title)}</h2>'
            f'<a href="{filename}"><img src="{filename}" width="{width}" height="{height}" '
            f'alt="{html.escape(sheet.title)}"></a><details><summary>Full-resolution frames '
            f'({len(sheet.files)})</summary><nav>{links}</nav></details></section>'
        )

    page = f"""<!doctype html>
<meta charset="utf-8">
<title>Mario native port visual acceptance</title>
<style>
body {{ margin: 0 auto; max-width: 1500px; padding: 24px; background: #181818; color: #eee;
       font: 16px/1.45 system-ui, sans-serif; }}
h1, h2 {{ color: #ffd900; }} code {{ color: #9ee7ff; }}
section {{ margin: 28px 0 52px; border-top: 2px solid #555; padding-top: 20px; }}
img {{ max-width: 100%; height: auto; image-rendering: pixelated; border: 1px solid #777; }}
.check {{ float: right; padding: 8px 12px; background: #303030; border-radius: 6px; }}
nav {{ display: flex; flex-wrap: wrap; gap: 8px 14px; margin-top: 12px; }}
a {{ color: #8fd3ff; }} .warning {{ padding: 12px; border: 1px solid #f4c542; background: #332b08; }}
</style>
<h1>Mario's Game Gallery / FUNdamentals visual acceptance</h1>
<p class="warning"><strong>Unreleased QA candidate.</strong> This packet is the human review layer;
it does not publish either withdrawn GitHub draft. Click any sheet or frame for its lossless pixels.</p>
<p>Candidate: <a href="../../../dist/MarioFundamentals.exe">MarioFundamentals.exe</a><br>
Bytes: <code>{candidate_bytes}</code><br>SHA-256: <code>{candidate_hash}</code><br>
Pinned corpus: <code>243 Macintosh + 234 DOS = 477 frames</code>.</p>
<p>Required focus: startup colors/audio handoff; centered/concealed board reveal; stable menus;
all five intro paths; progressive deals/setup; solid Mario head/torso/hands; and Yacht's cup-free
pre-roll followed by exactly one large animated cup. The in-board Yacht “Good luck / I go first”
opening intentionally remains input-locked, while its selected-game title is click-skippable.</p>
{''.join(sections)}
<script>
for (const box of document.querySelectorAll('input[data-key]')) {{
  box.checked = localStorage.getItem('mf-accept-' + box.dataset.key) === '1';
  box.addEventListener('change', () => localStorage.setItem(
    'mf-accept-' + box.dataset.key, box.checked ? '1' : '0'));
}}
</script>
"""
    (output / "index.html").write_text(page, encoding="utf-8")
    print(f"PASS visual_acceptance_packet sheets={len(sheets)} output={output / 'index.html'}")


if __name__ == "__main__":
    main()
