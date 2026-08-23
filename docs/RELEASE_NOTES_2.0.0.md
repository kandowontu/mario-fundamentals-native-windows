# Mario's Game Gallery / FUNdamentals native Windows collection 2.0.0

Version 2.0.0 adds the complete embedded DOS 1.0 edition alongside the existing Macintosh 1.1
port. The executable opens with a native edition chooser; neither route requires the original
media, executable, emulator, installer, sound driver, or loose asset files.

## Highlights

- Native 320×200 Mario's Game Gallery DOS 1.0 shell: publisher and credits pages, voiced title,
  board flip, animated easel menu, game intros, character/name panels, all five games, and original
  reset/replay panels.
- Exact preservation of 1,806 DOS PRD/PRS resources in a deterministic embedded pack, alongside all
  1,707 Macintosh resources.
- DOS Pak/DIB/MuV/Ply/Img/SND/XMI runtime support, including 3,633 frames, 574 movies, 10,614
  commands, 278 sampled sounds, and 12 fixed-120 Hz XMIDI tracks.
- Structural disassembly records for the 16-bit Borland MZ/FBOV executable: 2,839 relocations,
  31 overlays, 505 exact export targets, and a confidence-labeled 591-row overlay ledger.
- Dual-edition behavioral regressions plus byte-preservation, PE/dependency, embedded-pack,
  empty-directory, and reproducible-build checks.

## Release artifact

- File: `MarioFundamentals.exe`
- Architecture: native AMD64 PE32+ Windows GUI
- Size: `24,972,149` bytes
- SHA-256: `7680FF8029DF57718824DAA7790A88F0FD69C186DE9D13B300A877801119147C`
- External dependencies: Windows system DLLs/API-set contracts only; no compiler redistributable
- Self-test:
  `PASS mac_assets=1707 mac_pak=180 mac_frames=3166 mac_movies=467 mac_commands=8586 mac_midi_events=6968 mac_sounds=313 games=5 dos_assets=1806 dos_pak=187 dos_frames=3633 dos_movies=574 dos_commands=10614 dos_xmi_events=12253 dos_sounds=278`

Two clean builds produced identical byte counts and SHA-256 hashes. The same silent self-test passed
from an empty working directory, and both deterministic asset packs occur exactly once in the PE.

## Credits and rights

The original DOS 1.0 and Macintosh 1.1 production teams are credited separately in
[`CREDITS.md`](../CREDITS.md). The repository's native source/tooling license does not cover
Nintendo characters, original artwork, audio, fonts, other game data, or release executables that
contain those materials. This independent preservation project is not affiliated with or endorsed
by the original rights holders.
