#!/usr/bin/env python3
"""Account for every retained vanilla reference capture.

This complements pixel comparison: stable source frames must be tied to an
automated comparison, while duplicate sequence frames, host/pre-launch chrome,
incomplete QuickDraw/browser repaints, and stochastic gameplay alternates must
have an explicit non-comparison disposition. An unknown file fails the gate.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from verify_visual_references import DOS_SAMPLES, MAC_SAMPLES


EXPECTED_CAPTURE_COUNTS = {"macintosh": 213, "dos": 12}


@dataclass(frozen=True)
class Rule:
    pattern: re.Pattern[str]
    category: str
    reason: str
    represented_by: tuple[str, ...] = ()


def rule(pattern: str, category: str, reason: str,
         *represented_by: str) -> Rule:
    return Rule(re.compile(pattern), category, reason, represented_by)


MAC_NON_COMPARISON_RULES = (
    rule(
        r"original-menu-hover-(?:checkers|gofish|dominoes|backgammon|yacht)\.png",
        "invalid_browser_clip",
        "browser clip coordinates were device-scaled and padded; the canonical full-page Go Fish hover capture is normalized and compared instead",
        "original-menu-hover-gofish-full.png",
    ),
    rule(
        r"cold-0[0-9]\.png",
        "external_prelaunch",
        "Internet Archive/browser launch chrome before the emulated game surface is initialized",
    ),
    rule(
        r"cold-(?:1[0-9]|20|21|3[2-7])\.png",
        "host_repaint_or_blank",
        "blank or partially repainted browser/emulator launch surface, not a stable authored frame",
        "hi-20.png", "hi-34.png",
    ),
    rule(
        r"cold-(?:2[2-9]|30|31|3[8-9]|4[0-9])\.png",
        "redundant_sequence_frame",
        "lower-resolution duplicate publisher-card sequence represented by canonical full-surface samples",
        "hi-20.png", "hi-28.png", "hi-34.png", "hi-52.png",
    ),
    rule(
        r"frame-[0-9]{2}\.png",
        "external_prelaunch",
        "Mac Finder/application-launch capture outside the native game's authored client surface",
    ),
    rule(
        r"hi-0[0-8]\.png",
        "external_prelaunch",
        "Mac Finder/application-launch capture before authored publisher presentation",
    ),
    rule(
        r"hi-(?:09|1[0-9]|2[9]|3[0-3]|5[3-9])\.png",
        "host_repaint_or_blank",
        "blank host initialization or inter-card transition without authored visual content",
        "hi-20.png", "hi-34.png",
    ),
    rule(
        r"hi-(?:2[0-8]|3[4-9]|4[0-9]|5[0-2])\.png",
        "redundant_sequence_frame",
        "adjacent publisher-card frame represented by the stable and fade comparison samples",
        "hi-20.png", "hi-28.png", "hi-34.png", "hi-52.png",
    ),
    rule(
        r"run-0[0-5]\.png",
        "redundant_sequence_frame",
        "byte-identical title silhouette hold represented by run-00",
        "run-00.png",
    ),
    rule(
        r"run-(?:0[6-9]|1[0-9]|2[0-4]|26|28|29)\.png",
        "redundant_sequence_frame",
        "adjacent stable title speech frame represented by greeting/talking comparisons and movie QA",
        "run-06.png", "run-23.png",
    ),
    rule(
        r"run-(?:25|27|30|31)\.png",
        "incomplete_quickdraw_repaint",
        "partial title actor repaint exposed by browser capture timing; native double buffering intentionally prevents it",
        "run-06.png", "run-23.png",
    ),
    rule(
        r"original-checkers-game\.png",
        "incomplete_quickdraw_repaint",
        "partially drawn character modal; the stable terminal modal is compared independently",
        "original-checkers-intro-3s.png",
    ),
    rule(
        r"original-(?:dominoes-flow-3|gofish-trace-6|yacht-trace-4)\.png",
        "incomplete_quickdraw_repaint",
        "mostly black partial actor/update capture; reproducing it would reintroduce the source flicker defect",
    ),
    rule(
        r"original-dominoes-(?:flow-2|trace-[0-2])\.png",
        "redundant_sequence_frame",
        "adjacent Dominoes menu/intro stage represented by selected-menu and terminal-intro comparisons plus all source-timed QA frames",
        "original-dominoes-flow-1.png", "original-dominoes-trace-3.png",
    ),
    rule(
        r"original-dominoes-(?:late|trace-[5-9])\.png",
        "stochastic_gameplay_alternate",
        "random dealt/placed Dominoes state; board geometry is compared and rules, turn, drag, placement, outcome, and replay paths are executable regressions",
        "original-dominoes-trace-4.png",
    ),
    rule(
        r"original-gofish-trace-4\.png",
        "stochastic_gameplay_alternate",
        "random opening deal; deal cadence/card visibility is executable-tested and stable grouped/question/transfer states are compared",
        "original-gofish-trace-5.png", "original-gofish-trace-7.png",
        "original-gofish-trace-8.png",
    ),
)

DOS_NON_COMPARISON_RULES = (
    rule(
        r"go-fish-mac\.png",
        "supplemental_cross_edition",
        "MarioWiki Macintosh screenshot stored beside the DOS article set; higher-resolution vanilla Mac captures drive the automated Go Fish comparisons",
        "original-gofish-trace-3.png", "original-gofish-trace-5.png",
        "original-gofish-trace-7.png", "original-gofish-trace-8.png",
    ),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def inventory(
    edition: str,
    directory: Path,
    samples: tuple,
    rules: tuple[Rule, ...],
) -> tuple[list[dict[str, object]], list[str]]:
    compared: dict[str, list[str]] = defaultdict(list)
    for sample in samples:
        compared[sample.reference].append(sample.name)

    records: list[dict[str, object]] = []
    errors: list[str] = []
    paths = sorted(directory.glob("*.png"))
    if not paths:
        errors.append(f"{edition}: no PNG references in {directory}")
        return records, errors
    if len(paths) != EXPECTED_CAPTURE_COUNTS[edition]:
        errors.append(
            f"{edition}: expected {EXPECTED_CAPTURE_COUNTS[edition]} retained captures, "
            f"found {len(paths)}"
        )

    for path in paths:
        with Image.open(path) as image:
            size = [image.width, image.height]
        record: dict[str, object] = {
            "edition": edition,
            "file": path.name,
            "size": size,
            "sha256": sha256(path),
        }
        if path.name in compared:
            record.update(
                category="automated_comparison",
                reason="direct input to tolerant independent-source pixel comparison",
                comparison_cases=compared[path.name],
            )
        else:
            matches = [candidate for candidate in rules
                       if candidate.pattern.fullmatch(path.name)]
            if len(matches) != 1:
                errors.append(
                    f"{edition}:{path.name}: expected one disposition rule, found {len(matches)}"
                )
                record.update(category="unaccounted", reason="no unique disposition")
            else:
                matched = matches[0]
                record.update(
                    category=matched.category,
                    reason=matched.reason,
                    represented_by=list(matched.represented_by),
                )
        records.append(record)

    present = {path.name for path in paths}
    for reference in compared:
        if reference not in present:
            errors.append(f"{edition}: compared reference is missing: {reference}")
    return records, errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mac_reference_directory", type=Path)
    parser.add_argument("--dos-reference-directory", type=Path)
    parser.add_argument("--json-output", type=Path, required=True)
    args = parser.parse_args()

    records, errors = inventory(
        "macintosh", args.mac_reference_directory, MAC_SAMPLES,
        MAC_NON_COMPARISON_RULES,
    )
    if args.dos_reference_directory:
        dos_records, dos_errors = inventory(
            "dos", args.dos_reference_directory, DOS_SAMPLES,
            DOS_NON_COMPARISON_RULES,
        )
        records.extend(dos_records)
        errors.extend(dos_errors)

    categories = Counter(str(record["category"]) for record in records)
    editions = Counter(str(record["edition"]) for record in records)
    digest_groups: dict[str, list[str]] = defaultdict(list)
    for record in records:
        digest_groups[str(record["sha256"])].append(
            f"{record['edition']}:{record['file']}"
        )
    duplicate_groups = [files for files in digest_groups.values() if len(files) > 1]

    report = {
        "status": "FAIL" if errors else "PASS",
        "editions": dict(sorted(editions.items())),
        "categories": dict(sorted(categories.items())),
        "duplicate_byte_groups": duplicate_groups,
        "errors": errors,
        "captures": records,
    }
    args.json_output.parent.mkdir(parents=True, exist_ok=True)
    args.json_output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    category_summary = " ".join(
        f"{name}={count}" for name, count in sorted(categories.items())
    )
    if errors:
        print(f"FAIL visual_reference_inventory={len(records)} {category_summary}")
        for error in errors:
            print(f"ERROR {error}")
        return 1
    print(
        f"PASS visual_reference_inventory={len(records)} "
        f"duplicate_byte_groups={len(duplicate_groups)} {category_summary}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
