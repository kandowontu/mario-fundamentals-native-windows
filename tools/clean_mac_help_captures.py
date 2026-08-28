#!/usr/bin/env python3
"""Remove emulator-pointer artifacts from the retained Macintosh Help captures.

The nine pages remain source-rendered QuickDraw captures tied to PICT 400-409.
Several original reference screenshots accidentally captured the host pointer:
three over otherwise uniform title/chrome pixels and four over the disabled
Next button.  This script performs only those deterministic repairs and refuses
unknown inputs so the cleanup cannot silently alter replacement artwork.
"""

from __future__ import annotations

import argparse
import hashlib
from collections import Counter
from pathlib import Path

SOURCE_SHA256 = {
    "backgammon-1.bmp": "DF562FE7E91AC0ED79925E9F01BD93BCF76722D814A630CDB847111632C097F7",
    "backgammon-2.bmp": "762FCEBD98F061F99F883BADDC6C789DA3875A900B378D63169048535AF9B27F",
    "checkers-1.bmp": "F4EE88BD96E2317EDD6AB81153BB57FB22BD83D76E1433A7E86369582D1889D7",
    "dominoes-1.bmp": "D34BFCA149AFC0134AE199B9EA4F6829A4D68A352F847AAE67446C0B436C4311",
    "dominoes-2.bmp": "4C403A4E8C861BFDAE48D26FF0CF57935B6EA180EEBE45C037EF588CB4B918EE",
    "gofish-1.bmp": "5FD4A3A6F5B9A5E7ECE7425ECAC69F234BE95F797B562DB186FDA354D197E6FC",
    "gofish-2.bmp": "C92F1DCB8A156E6B9A06873B31CCC3BCF8A371AD2C755196B768AD55B3C353A0",
    "yacht-1.bmp": "CD973B373015E7445FD300EE4C94D600820CDA2927BA69726FF94473C9A00FEA",
    "yacht-2.bmp": "E37FE4B68C1695C173646CFD9D66CE90D4918E925B12959531EEB537BEE8076F",
}

CLEAN_SHA256 = {
    "backgammon-1.bmp": "DF562FE7E91AC0ED79925E9F01BD93BCF76722D814A630CDB847111632C097F7",
    "backgammon-2.bmp": "7F6B3F112CC7108DC6E3F6DF22F1922571EF9C308C8DDC12998D6406DC1E6EE7",
    "checkers-1.bmp": "F4EE88BD96E2317EDD6AB81153BB57FB22BD83D76E1433A7E86369582D1889D7",
    "dominoes-1.bmp": "D6B08D708FDDD3258F10462D4EFE800B1D774E3087CDCD16B3A47D2DE3C641FB",
    "dominoes-2.bmp": "7472BE7B9FF65B0B351BF42A36F7277E4BF7D4015E1192C5DA04411AFD4C6769",
    "gofish-1.bmp": "67AC1F4E346011F18E66E68C13AFDB4B32127D7DC5E7100B52BC9E11B2534981",
    "gofish-2.bmp": "BDDDB031A330D0F4759C0B0C2B8AF71FC644D043E7BD4BBCCD3AB315D29593DB",
    "yacht-1.bmp": "37E8F3C1EEE77A7766EBF4E8CFF07D62124754F367E5042BFD59A657C674C173",
    "yacht-2.bmp": "1FA38048828048491EBFD07AE09020901F79B1F9B3688F3CBAB84100A7AB718E",
}

TOP_POINTER_REPAIRS = {
    # Inclusive-exclusive rectangles.  Each contains only the pointer and the
    # title chrome/background it obscured; no authored glyph enters the box.
    "dominoes-1.bmp": (129, 5, 162, 23),
    "gofish-1.bmp": (129, 9, 162, 45),
    "yacht-1.bmp": (129, 37, 162, 55),
}

PAGE_TWO_WITH_POINTER = (
    "backgammon-2.bmp",
    "dominoes-2.bmp",
    "gofish-2.bmp",
    "yacht-2.bmp",
)

# The clean disabled-Next control in the one-page Checkers help panel is the
# same source chrome/control state used by the four terminal pages.
DISABLED_NEXT_RECT = (290, 309, 363, 342)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def row_background(image, y: int) -> tuple[int, int, int]:
    # The content field or horizontal chrome line occupies almost the entire
    # row, so its modal RGB value is the exact captured background at that y.
    return Counter(image.getpixel((x, y)) for x in range(20, 470)).most_common(1)[0][0]


def repair(help_root: Path, check_only: bool) -> None:
    actual = {name: digest(help_root / name) for name in SOURCE_SHA256}
    if actual == CLEAN_SHA256:
        print("PASS Macintosh Help captures already clean")
        return
    if check_only:
        changed = [name for name in CLEAN_SHA256 if actual[name] != CLEAN_SHA256[name]]
        raise SystemExit("Macintosh Help cleanup verification failed: " + ", ".join(changed))
    if actual != SOURCE_SHA256:
        changed = [name for name in SOURCE_SHA256 if actual[name] != SOURCE_SHA256[name]]
        raise SystemExit("refusing unrecognized Macintosh Help inputs: " + ", ".join(changed))

    from PIL import Image

    images = {
        name: Image.open(help_root / name).convert("RGB")
        for name in SOURCE_SHA256
    }

    for name, (left, top, right, bottom) in TOP_POINTER_REPAIRS.items():
        image = images[name]
        for y in range(top, bottom):
            background = row_background(image, y)
            for x in range(left, right):
                image.putpixel((x, y), background)

    clean_next = images["checkers-1.bmp"].crop(DISABLED_NEXT_RECT)
    for name in PAGE_TWO_WITH_POINTER:
        images[name].paste(clean_next, DISABLED_NEXT_RECT[:2])

    for name in (*TOP_POINTER_REPAIRS, *PAGE_TWO_WITH_POINTER):
        images[name].save(help_root / name, format="BMP")

    for name in SOURCE_SHA256:
        print(f"{name} {digest(help_root / name)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "help_root",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "assets" / "help",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the cleaned hashes without modifying any asset",
    )
    args = parser.parse_args()
    repair(args.help_root.resolve(), args.check)


if __name__ == "__main__":
    main()
