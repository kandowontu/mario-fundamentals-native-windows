# Mario's Game Gallery / FUNdamentals native Windows collection 2.0.1

> Release status: this source-registration parity update completed the full dual-edition release
> gate and was accepted for publication on 2026-08-30. Its SHA-256 is
> `773EF30CA1C0506D34A26D7D6E70C39ECDDEF50D9695084A5A16E2411CC7A51F`.

Version 2.0.1 is a focused visual-parity update for the self-contained Macintosh 1.1 and DOS 1.0
native Windows ports. It replaces remaining scaled or inferred coordinates with recovered
source-authored geometry.

## Corrections

- Checkers uses exact edition-specific square centers, piece-cel anchors, portraits, and static
  actor origins.
- DOS Go Fish uses the source hand slots, victory-letter positions, deck, neutral head/torso
  registrations, and its shorter opening-motion distances.
- DOS Backgammon uses the recovered 24-point shell and egg tables with the original stacking
  slopes rather than scaled Macintosh geometry.
- DOS Dominoes uses the source opening-chain lane and corrected static actor registrations.
- DOS Yacht uses the source die, held-die, and remaining-roll marker coordinates.

## Verification

- The complete hidden semantic, input, asset-preservation, audio, and fullscreen test suite passes.
- All 246 Macintosh and 237 DOS deterministic presentation frames are content-pinned.
- All 53 independent visual-reference comparisons pass.
- The staged executable and an isolated clean-room rebuild are byte-identical.

## Artifact

`MarioFundamentals.exe` is a self-contained AMD64 Windows GUI executable. It embeds both original
resource collections and does not require the original executable, disk image, emulator, installer,
or loose runtime assets.
