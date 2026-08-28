#!/usr/bin/env python3
"""Compare native Macintosh QA frames with independent vanilla captures.

The Macintosh reference set was captured from the original image in the
Internet Archive vMac emulator; the DOS set contains independent native-size
screenshots.  The browser scales the Macintosh 512x384 game surface, so the
verifier removes the stable emulator/window chrome and resamples that surface.
Static cases compare scene regions whose geometry is not controlled by random
cards, pieces, speech mouths, or the mouse cursor. Independently captured
dynamic intro instants and source-matched Yacht actor/cup regions cover the
registered foreground actors that those static regions intentionally omit.

This is deliberately a tolerant structural check, not a claim that a browser
screenshot can be byte-identical to the native renderer.  It independently
catches shifted scorecards, boards, status bars, tiled backgrounds, and other
whole-scene composition regressions that self-generated screenshots cannot.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter, ImageStat


MAC_LOGICAL_SIZE = (512, 384)
MAC_CAPTURE_SIZE = (2048, 1280)
# Stable interior of the emulated 512x384 game surface in the local captures.
MAC_CAPTURE_CROP = (341, 128, 1707, 1152)
# The retained title-sequence run was captured in a smaller browser viewport.
MAC_RUN_CAPTURE_SIZE = (1265, 712)
MAC_RUN_CAPTURE_CROP = (370, 157, 882, 541)
MAC_SOURCE_URL = "https://archive.org/details/mario-fundamentals"
DOS_LOGICAL_SIZE = (320, 200)
DOS_CAPTURE_CROP = (0, 0, 320, 200)
DOS_SOURCE_URL = "https://www.mariowiki.com/Mario%27s_Game_Gallery"


@dataclass(frozen=True)
class Sample:
    name: str
    reference: str
    qa_frame: str
    regions: tuple[tuple[int, int, int, int], ...]
    maximum_rmse: float
    capture_size: tuple[int, int] | None = None
    capture_crop: tuple[int, int, int, int] | None = None
    edge_only: bool = False


MAC_SAMPLES = (
    # Independently captured publisher/title states protect the palette, stage
    # composition, Mario's complete title pose, and talking animation.
    Sample(
        "brainstorm",
        "hi-20.png",
        "00-brainstorm.bmp",
        ((0, 0, 512, 384),),
        1.5,
    ),
    Sample(
        "brainstorm-fade",
        "hi-28.png",
        "01-brainstorm-fade.bmp",
        ((0, 0, 512, 384),),
        1.0,
    ),
    Sample(
        "stepping-stone",
        "hi-34.png",
        "02-stepping-stone.bmp",
        ((0, 0, 512, 384),),
        2.0,
    ),
    Sample(
        "stepping-stone-fade",
        "hi-52.png",
        "03-stepping-stone-fade.bmp",
        ((0, 0, 512, 384),),
        2.0,
    ),
    Sample(
        "title-silhouette",
        "run-00.png",
        "04-title-silhouette.bmp",
        ((0, 0, 512, 384),),
        5.0,
        MAC_RUN_CAPTURE_SIZE,
        MAC_RUN_CAPTURE_CROP,
    ),
    Sample(
        "title-greeting",
        "run-06.png",
        "05-title-greeting.bmp",
        ((0, 0, 512, 384),),
        10.0,
        MAC_RUN_CAPTURE_SIZE,
        MAC_RUN_CAPTURE_CROP,
    ),
    Sample(
        "title-talking",
        "run-23.png",
        "06-title-talking.bmp",
        ((0, 0, 512, 384),),
        10.0,
        MAC_RUN_CAPTURE_SIZE,
        MAC_RUN_CAPTURE_CROP,
    ),
    # CODE 12 pins movie 1111 at duration-1 during the title sequence. This
    # focused region rejects the checker-stack cels that previously replaced
    # Mario's open right hand while he spoke.
    Sample(
        "title-open-right-hand",
        "run-06.png",
        "05-title-greeting.bmp",
        ((400, 135, 485, 220),),
        32.0,
        MAC_RUN_CAPTURE_SIZE,
        MAC_RUN_CAPTURE_CROP,
    ),
    # These click/hover captures cover one settled pose plus three exact
    # outgoing-pose transition instants.  CODE 12 changes the red label first,
    # retracts the prior hand/object actor for 300 ms, and only then advances
    # the incoming actor to its selection-specific resting time.
    Sample(
        "menu-backgammon-selected",
        "original-dominoes-flow-0.png",
        "10-menu-selection-4.bmp",
        ((0, 0, 512, 384),),
        4.0,
    ),
    Sample(
        "menu-dominoes-transition-outgoing",
        "original-dominoes-flow-1.png",
        "10a-transition-backgammon-to-dominoes.bmp",
        ((0, 0, 512, 384),),
        4.0,
    ),
    Sample(
        "menu-go-fish-transition-outgoing",
        "original-gofish-trace-0.png",
        "10b-transition-dominoes-to-go-fish-outgoing.bmp",
        ((0, 0, 512, 384),),
        4.0,
    ),
    Sample(
        "menu-yacht-transition-outgoing",
        "original-yacht-trace-0.png",
        "10e-transition-go-fish-to-yacht.bmp",
        ((0, 0, 512, 384),),
        5.0,
    ),
    Sample(
        "menu-go-fish-settled",
        "original-menu-hover-gofish-full.png",
        "10-menu-selection-2.bmp",
        ((0, 0, 512, 384),),
        12.0,
        MAC_RUN_CAPTURE_SIZE,
        MAC_RUN_CAPTURE_CROP,
    ),
    Sample(
        "menu-go-fish-settled-hand-object",
        "original-menu-hover-gofish-full.png",
        "10-menu-selection-2.bmp",
        ((385, 125, 480, 230),),
        30.0,
        MAC_RUN_CAPTURE_SIZE,
        MAC_RUN_CAPTURE_CROP,
    ),
    # Whole-frame dynamic captures catch source actor registration and staging
    # errors that board/chrome-only comparisons deliberately exclude.
    Sample(
        "dominoes-intro",
        "original-dominoes-trace-3.png",
        "11-intro-1-15.bmp",
        ((0, 0, 512, 384),),
        15.0,
    ),
    Sample(
        "checkers-intro",
        "original-checkers-initial.png",
        "11-intro-2-02.bmp",
        ((0, 0, 512, 384),),
        12.5,
    ),
    Sample(
        "go-fish-intro-early",
        "original-gofish-trace-1.png",
        "11-intro-3-04.bmp",
        ((0, 0, 512, 384),),
        6.0,
    ),
    Sample(
        "go-fish-intro-late",
        "original-gofish-trace-2.png",
        "11-intro-3-19.bmp",
        ((0, 0, 512, 384),),
        5.0,
    ),
    Sample(
        "yacht-intro-early",
        "original-yacht-trace-1.png",
        "11-intro-4-00.bmp",
        ((0, 0, 512, 384),),
        13.0,
    ),
    Sample(
        "yacht-intro-middle",
        "original-yacht-trace-2.png",
        "11-intro-4-08.bmp",
        ((0, 0, 512, 384),),
        14.0,
    ),
    Sample(
        "yacht-intro-late",
        "original-yacht-trace-3.png",
        "11-intro-4-16.bmp",
        ((0, 0, 512, 384),),
        7.0,
    ),
    Sample(
        "backgammon-board",
        "original-backgammon-board.png",
        "31-backgammon-setup-30.bmp",
        ((0, 0, 165, 165), (347, 0, 512, 165), (0, 165, 512, 384)),
        16.0,
    ),
    Sample(
        "backgammon-character-choice",
        "original-backgammon-after-intro.png",
        "12-backgammon-character-choice.bmp",
        ((0, 0, 512, 384),),
        13.0,
    ),
    Sample(
        "backgammon-setup-reveal",
        "original-backgammon-name.png",
        "31-backgammon-setup-10.bmp",
        ((0, 0, 512, 384),),
        15.0,
    ),
    Sample(
        "dominoes-table",
        "original-dominoes-trace-4.png",
        "21-opening-0.bmp",
        ((100, 0, 512, 330),),
        4.0,
    ),
    Sample(
        "dominoes-score-portrait-registration",
        "original-dominoes-trace-4.png",
        "21-opening-0.bmp",
        ((0, 0, 110, 115),),
        38.0,
        edge_only=True,
    ),
    Sample(
        "checkers-chrome",
        "original-checkers-game-started.png",
        "22-opening-8.bmp",
        ((0, 0, 180, 165), (332, 0, 512, 165)),
        14.0,
    ),
    Sample(
        "checkers-character-choice",
        "original-checkers-intro-3s.png",
        "12-character-choice-2.bmp",
        ((0, 0, 512, 384),),
        12.0,
    ),
    Sample(
        "checkers-name-prompt",
        "original-checkers-yoshi-board.png",
        "13-name-prompt-2.bmp",
        ((0, 0, 512, 384),),
        16.0,
    ),
    Sample(
        "go-fish-table",
        "original-gofish-trace-3.png",
        "35-gofish-victory-0.bmp",
        ((0, 0, 180, 135), (332, 0, 512, 135), (0, 180, 512, 340)),
        16.0,
    ),
    Sample(
        "go-fish-grouped-hand",
        "original-gofish-trace-5.png",
        "35-gofish-hand.bmp",
        ((0, 0, 512, 384),),
        22.0,
    ),
    Sample(
        "go-fish-luigi-question",
        "original-gofish-trace-7.png",
        "35-gofish-question.bmp",
        ((0, 0, 512, 384),),
        14.0,
    ),
    Sample(
        "go-fish-question-transfer",
        "original-gofish-trace-8.png",
        "35-gofish-hand-transfer.bmp",
        ((0, 0, 512, 384),),
        15.0,
    ),
    Sample(
        "yacht-scorecards",
        "original-yacht-trace-5.png",
        "24-opening-32.bmp",
        ((0, 0, 145, 325), (367, 0, 512, 325)),
        10.0,
    ),
    Sample(
        "yacht-center-actor",
        "original-yacht-trace-5.png",
        "24-opening-32.bmp",
        ((145, 0, 367, 220),),
        30.0,
    ),
    Sample(
        "yacht-computer-dice-and-markers",
        "original-yacht-trace-7.png",
        "36-yacht-computer-dice.bmp",
        ((0, 0, 512, 384),),
        12.0,
    ),
    # Movie 6021's time-zero overlay completes the idle actor's left glove;
    # comparing the hand region prevents a raw base-cel substitution.
    Sample(
        "yacht-idle-actor-hands",
        "original-yacht-trace-7.png",
        "36-yacht-computer-dice.bmp",
        ((210, 120, 300, 200),),
        40.0,
    ),
    Sample(
        "yacht-reroll-gesture-registration",
        "original-yacht-trace-8.png",
        "36-yacht-reroll-gesture-180.bmp",
        ((90, 0, 345, 220),),
        16.0,
        edge_only=True,
    ),
    Sample(
        "yacht-roll-contact-cup",
        "original-yacht-trace-6.png",
        "30-yacht-roll-0.bmp",
        ((0, 0, 512, 384),),
        6.0,
    ),
)


DOS_SAMPLES = (
    Sample(
        "menu",
        "menu.png",
        "04-menu.bmp",
        ((0, 0, 320, 200),),
        11.0,
    ),
    Sample(
        "backgammon-chrome",
        "backgammon.png",
        "31-backgammon-setup-30.bmp",
        ((0, 0, 110, 92), (210, 0, 320, 92)),
        34.0,
    ),
    Sample(
        "dominoes-table",
        "dominoes.png",
        "11-opening-8.bmp",
        ((100, 95, 315, 145),),
        3.0,
    ),
    Sample(
        "dominoes-score-portrait-registration",
        "dominoes.png",
        "11-opening-128.bmp",
        ((0, 8, 70, 75),),
        60.0,
        edge_only=True,
    ),
    Sample(
        "checkers-chrome",
        "checkers.png",
        "12-opening-16.bmp",
        ((0, 0, 115, 68), (205, 0, 320, 68)),
        27.0,
    ),
    Sample(
        "go-fish-table",
        "go-fish.png",
        "35-gofish-hand.bmp",
        ((0, 0, 110, 80), (210, 0, 320, 80), (0, 80, 320, 145)),
        28.0,
    ),
    # Overlay 18 draws these captions and values dynamically in the DOS
    # edition; Pak 5001 does not contain them. Keep narrow regions around the
    # fixed labels so a broad board/chrome tolerance cannot hide their loss.
    Sample(
        "go-fish-scoreboard-labels",
        "go-fish.png",
        "35-gofish-hand.bmp",
        ((14, 18, 72, 52), (229, 34, 289, 52)),
        3.0,
    ),
    Sample(
        "yacht-scorecards",
        "yacht-gameplay.png",
        "14-opening-128.bmp",
        ((0, 0, 105, 180), (215, 0, 320, 180)),
        12.0,
    ),
    # Dynamic intro samples use independently captured source instants. These
    # whole-frame comparisons catch registered-actor offsets that static board
    # and scorecard regions deliberately exclude.
    Sample(
        "backgammon-intro",
        "backgammon-intro.png",
        "07-intro-0-06.bmp",
        ((0, 0, 320, 200),),
        2.0,
    ),
    Sample(
        "dominoes-intro",
        "dominoes-intro.png",
        "07-intro-1-04.bmp",
        ((0, 0, 320, 200),),
        5.5,
    ),
    Sample(
        "checkers-intro",
        "checkers-intro.png",
        "07-intro-2-19.bmp",
        ((0, 0, 320, 200),),
        5.0,
    ),
    Sample(
        "go-fish-intro",
        "go-fish-intro.png",
        "07-intro-3-12.bmp",
        ((0, 0, 320, 200),),
        2.5,
    ),
    Sample(
        "yacht-intro",
        "yacht-intro.png",
        "07-intro-4-01.bmp",
        ((0, 0, 320, 200),),
        19.0,
    ),
)


def load_reference(
    path: Path,
    capture_size: tuple[int, int],
    capture_crop: tuple[int, int, int, int],
    logical_size: tuple[int, int],
) -> Image.Image:
    with Image.open(path) as image:
        if image.size != capture_size:
            raise ValueError(
                f"{path}: expected {capture_size[0]}x{capture_size[1]} capture, "
                f"found {image.width}x{image.height}"
            )
        return image.convert("RGB").crop(capture_crop).resize(
            logical_size, Image.Resampling.BILINEAR
        )


def load_qa_frame(path: Path, logical_size: tuple[int, int]) -> Image.Image:
    with Image.open(path) as image:
        if image.size != logical_size:
            raise ValueError(
                f"{path}: expected {logical_size[0]}x{logical_size[1]} frame, "
                f"found {image.width}x{image.height}"
            )
        return image.convert("RGB")


def structural_rmse(
    reference: Image.Image,
    candidate: Image.Image,
    regions: tuple[tuple[int, int, int, int], ...],
    blur_radius: float,
    edge_only: bool,
) -> float:
    squared_rms = 0.0
    channel_count = 0
    for region in regions:
        expected = reference.crop(region).filter(ImageFilter.GaussianBlur(blur_radius))
        actual = candidate.crop(region).filter(ImageFilter.GaussianBlur(blur_radius))
        if edge_only:
            expected = expected.convert("L").filter(ImageFilter.FIND_EDGES)
            actual = actual.convert("L").filter(ImageFilter.FIND_EDGES)
        expected = expected.resize((64, 64), Image.Resampling.BILINEAR)
        actual = actual.resize((64, 64), Image.Resampling.BILINEAR)
        rms = ImageStat.Stat(ImageChops.difference(expected, actual)).rms
        squared_rms += sum(value * value for value in rms)
        channel_count += len(rms)
    return math.sqrt(squared_rms / channel_count)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mac_reference_directory", type=Path)
    parser.add_argument("mac_qa_directory", type=Path)
    parser.add_argument("--dos-reference-directory", type=Path)
    parser.add_argument("--dos-qa-directory", type=Path)
    parser.add_argument("--json-output", type=Path, required=True)
    args = parser.parse_args()

    if bool(args.dos_reference_directory) != bool(args.dos_qa_directory):
        raise SystemExit("DOS reference and QA directories must be supplied together")

    groups = [
        (
            "macintosh",
            MAC_SAMPLES,
            args.mac_reference_directory,
            args.mac_qa_directory,
            MAC_CAPTURE_SIZE,
            MAC_CAPTURE_CROP,
            MAC_LOGICAL_SIZE,
            1.0,
        )
    ]
    if args.dos_reference_directory:
        groups.append(
            (
                "dos",
                DOS_SAMPLES,
                args.dos_reference_directory,
                args.dos_qa_directory,
                DOS_LOGICAL_SIZE,
                DOS_CAPTURE_CROP,
                DOS_LOGICAL_SIZE,
                0.5,
            )
        )

    cases: list[dict[str, object]] = []
    failed = False
    edition_counts: dict[str, int] = {}
    for (
        edition,
        samples,
        reference_directory,
        qa_directory,
        capture_size,
        capture_crop,
        logical_size,
        blur_radius,
    ) in groups:
        edition_counts[edition] = len(samples)
        for sample in samples:
            reference_path = reference_directory / sample.reference
            qa_path = qa_directory / sample.qa_frame
            if not reference_path.is_file():
                raise SystemExit(f"missing {edition} reference capture: {reference_path}")
            if not qa_path.is_file():
                raise SystemExit(f"missing {edition} native QA frame: {qa_path}")
            score = structural_rmse(
                load_reference(
                    reference_path,
                    sample.capture_size or capture_size,
                    sample.capture_crop or capture_crop,
                    logical_size,
                ),
                load_qa_frame(qa_path, logical_size),
                sample.regions,
                blur_radius,
                sample.edge_only,
            )
            passed = score <= sample.maximum_rmse
            failed |= not passed
            cases.append(
                {
                    "edition": edition,
                    "name": sample.name,
                    "reference": sample.reference,
                    "qa_frame": sample.qa_frame,
                    "regions": [list(region) for region in sample.regions],
                    "structural_rmse": round(score, 6),
                    "maximum_rmse": sample.maximum_rmse,
                    "comparison": "edge" if sample.edge_only else "rgb",
                    "status": "PASS" if passed else "FAIL",
                }
            )

    report = {
        "status": "FAIL" if failed else "PASS",
        "reference_sources": {
            "macintosh_vanilla_emulator": MAC_SOURCE_URL,
            "dos_independent_screenshots": DOS_SOURCE_URL,
        },
        "editions": edition_counts,
        "macintosh_reference_capture_size": list(MAC_CAPTURE_SIZE),
        "macintosh_reference_game_surface_crop": list(MAC_CAPTURE_CROP),
        "native_logical_sizes": {
            "macintosh": list(MAC_LOGICAL_SIZE),
            "dos": list(DOS_LOGICAL_SIZE),
        },
        "method": (
            "Gaussian-smoothed 64x64 regional RGB RMSE; the Macintosh browser capture is "
            "reduced to its stable game surface; random actor/card/piece/cursor regions are "
            "excluded from stable-layout cases; four Macintosh publisher states, three title "
            "states, four Macintosh menu-transition instants, a settled selection and its "
            "focused hand/object actor, seven source-timed Macintosh game intros, "
            "and five source-timed DOS game intros compare complete frames; a focused title-hand "
            "case pins the terminal open-hand cel; three first-use "
            "panel frames, a Backgammon setup reveal, three Go Fish hand/question states, and a "
            "Yacht dice/marker state are checked as complete frames; edge-only checks pin both "
            "Dominoes score portraits and the Macintosh Yacht reroll gesture; three Macintosh Yacht "
            "regions independently verify the idle actor, its composed hands, and the full-body "
            "gesture; the source-matched roll-contact cup is checked as a complete frame"
        ),
        "cases": cases,
    }
    args.json_output.parent.mkdir(parents=True, exist_ok=True)
    args.json_output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    summary = " ".join(
        f"{case['edition']}:{case['name']}="
        f"{case['structural_rmse']:.3f}/{case['maximum_rmse']:.1f}"
        for case in cases
    )
    if failed:
        print(f"FAIL visual_reference_cases={len(cases)} {summary}")
        return 1
    print(f"PASS visual_reference_cases={len(cases)} {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
