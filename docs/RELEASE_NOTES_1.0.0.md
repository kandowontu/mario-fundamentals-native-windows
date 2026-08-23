# Mario's FUNdamentals native Windows port 1.0.0

This is the first complete native Windows preservation release of the Macintosh version 1.1 game.
It is a 64-bit Win32 executable and does not require the original application, disk image, an
emulator, loose assets, or a runtime installer.

## Included

- Original BrainStorm and Stepping Stone startup, voiced Mario title introduction, animated easel
  menu, and all five title sequences.
- Playable native Checkers, Go Fish, Dominoes, Backgammon, and Yacht engines with source-traced
  rules, AI, dialogue, animation, audio, replay, and result behavior.
- Original help pages, About and Credits panels, menus, preferences, voices, effects, and music.
- All 1,707 source Macintosh resources embedded in the executable.
- Fixes for frame-to-frame layout shimmer, black repaint flicker, layered-scene duplication,
  easel/menu alignment, complete music/effect routing, background audio lifetime, and the slow MIDI
  initialization stall between the publisher card and title intro.

## Verification

- Size: `16,635,711` bytes
- SHA-256: `09871AAE96170BBE53251E4334D4A87D31C4D0890F815B5CA14E0AB7E14313E5`
- Hidden regression result:
  `PASS assets=1707 pak=180 frames=3166 movies=467 commands=8586 midi_events=6968 sounds=313 games=5`
- PE audit: AMD64 Windows GUI binary with only Windows system/API-set imports
- Isolation audit: passes from an otherwise empty directory containing only the executable

Download `MarioFundamentals.exe` and, if desired, verify it against `SHA256SUMS.txt`:

```powershell
Get-FileHash .\MarioFundamentals.exe -Algorithm SHA256
```

Windows 10 or later is recommended.

## Rights notice

This is an independent preservation project and is not affiliated with or endorsed by Nintendo,
Interplay, BrainStorm, Presage, or Stepping Stone. The repository license covers only newly
authored port source and tooling; it does not grant rights in the original game data or release
executable. See `CREDITS.md` and `LICENSE.md` in the source repository.
