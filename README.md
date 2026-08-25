# Mario's Game Gallery / FUNdamentals — native Windows preservation collection

[![Release](https://img.shields.io/github/v/release/kandowontu/mario-fundamentals-native-windows?display_name=tag)](https://github.com/kandowontu/mario-fundamentals-native-windows/releases/latest)

Latest public release: [v1.0.0](https://github.com/kandowontu/mario-fundamentals-native-windows/releases/tag/v1.0.0)

The dual-edition `v2.0.0` candidate is withdrawn and remains unpublished while DOS presentation
QA continues. There is currently no endorsed public 2.0 executable or tag.

This repository contains clean native Win32 compatibility ports of Mario's Game Gallery DOS 1.0
and Mario's FUNdamentals Macintosh 1.1. On boot, one self-contained executable offers either
edition. It embeds both complete source resource collections and needs no original disk image,
original executable, emulator, installer, DOS driver, or loose asset file at runtime.

## Dual-edition release

- A native boot selector launches either the 320×200 DOS 1.0 presentation or the 512×384
  Macintosh 1.1 presentation. `--edition=dos` and `--edition=mac` bypass it for deterministic use.
- The DOS route includes the Interplay and Presage cards, original production-credits page, dim and
  live title stages, voiced Mario sequence, board flip, animated C/G/D/B/Y easel menu, five game
  introductions, source character/name panels, all five games, and original reset/replay panels.
- All 1,806 DOS resources are preserved from the exact PRD/PRS chain: 187 Pak sheets/3,633 frames,
  574 movies/10,614 commands, 278 SND resources, and 12 XMI tracks. The DOS asset dialect is decoded
  directly; no screenshot reconstruction or replacement voice/music library is used.
- The original 16-bit Borland MZ/FBOV executable is not shipped or executed. Its 2,839 relocations,
  133 segment records, 31 overlays, 505 exact export stubs, and 591 conservative overlay entry
  candidates are independently cataloged and mapped to native subsystems.
- Shared games use the recovered native rule/AI/dialogue/outcome controllers with edition-specific
  source art, animation, audio, coordinates, hit targets, and shell behavior. Every behavioral
  regression runs against both asset dialects.

## Macintosh 1.1 fidelity rebuild

- The original BrainStorm and Stepping Stone publisher screens, black/fade timing, title stage,
  voiced Mario introduction, original easel menu, and all five animated game title sequences run
  in their source order. The live menu also restores CODE 12's C/G/D/B/Y selection order and
  corresponding 1111-1115 easel-object animations, red active label, matching pointer arm, and
  held selection pose, with arrow keys, letter shortcuts, and Return. Its randomized Mario host
  now includes the source shoe-tap/5004, bow-tie-spin/5006, and independent blink controllers;
  the remaining randomized branch is the source's proven zero-movie/zero-sound no-op.
- Name and Yoshi/Koopa selection occur when the first game needs them; Mario finishes the source
  character-choice speech before its modal choice panel is revealed. The player name and the
  source's four persistent options now survive later launches through per-user Windows settings,
  while the character choice remains session-scoped like the original record format. The temporary
  board shown beneath these first-use prompts rolls back its preview random state before the real
  game starts, preventing the shared QuickDraw sequence from being consumed twice.
- In-game Mario hosts now use the original synchronized `MuV `/`Ply ` mouth timelines and embedded
  voice cues; the original New Game and Play Again resource dialogs are restored. Accepting Play
  Again now resets the board in the existing source-style session, preserving later-round dialogue
  state instead of silently constructing a new game.
- Dynamic game labels, player names, score values, prompts, and the name editor now use the
  original Pak 223–228 bitmap glyphs through the recovered CODE 3/15 character-width and placement
  rules. They no longer substitute a scalable Windows font for the source interface lettering.
- CODE 5's About panel now draws PICT 128 at its exact 447x215 size and uses the disk's exact
  System 7 Times-14, Geneva-9, and Monaco-12 bitmap strikes at the recovered QuickDraw baselines,
  colors, divider position, copyright position, and literal `v. 1.1` label. Its complete raster is
  regression-tested; no generic Windows font remains in the renderer.
- All five native game engines are playable. Their source turn pacing now includes asynchronous
  Mario moves, voiced multi-part dialogue, visible dice/deal/reroll sequences, multiple jumps,
  result announcements, source idle prompts and knock-knock conversations, and the original
  end-of-game flow. The recovered control-flow, RNG, dialogue, strategy, replay, and result branches
  are covered by deterministic executable regressions rather than represented as
  instruction-by-instruction translations.
  - Yacht uses the recovered original scoring values (25-point Little Straight, 30-point Big
    Straight, and dice-sum Full House/Four of a Kind) plus CODE 18's exact adviser category order,
    straight/pair retention, duplicate handling, and final-line sacrifice rules. Its ending now uses
    the recovered tie/player/Mario result pools, the player-only movie-6022 celebration with timed
    effects, the shared replay question, and the source's progressive scorecard wipe. Its complete
    source dialogue RNG is also restored: Good Luck/I Go First opening, optional first-turn line,
    shuffled thinking and score-reaction pools, conditional reroll speech, 80-tick idle prompts,
    and the original Pizza/Jamaica/Giovanni/Yucca knock-knock cycle.
    Its dice controller uses the original white-reroll/red-held lanes, sequential Mario selection
    effects, remaining-roll markers, pre-roll hand gesture, player-owned twelve-round lifecycle,
    delayed Mario score commits, and progressive five-die player-win reveal.
- Go Fish now follows CODE 17's recovered greeting/"I'm-a go first" opening, shuffled dialogue
  pools, its four-cards-per-rank numbering with source Pak 5006 corner numerals, 300-pair source
  deck swap, contiguous source deal,
  persistent thirteen-record hand layout (including the rotated seven-card opening order, fixed
  54x76 hit targets, overflow row, duplicate consolidation, gap preservation, and first-free-record
  reuse),
  rank-suffix tables and 1-in-4 `Do you have any...` branch, remembered player questions,
  nonrepeating Mario request history (including the source's discarded `Random(0)` seed advance),
  forced-refill paths, requested-card extra turns, and deck-empty book-count outcome rule. Its
  120-tick idle controller alternates the two impatient lines with the original 5094/5091/5090
  visual pool and its two intentional no-op entries, retaining selector state across replay. Its
  player-win ending now deals the source Pak 5211
  “YOU WIN!” card faces, flips them away together with seven synchronized copies of movie 5210,
  and asks for another game; Mario-win and tie branches retain their intentionally shorter endings.
- Backgammon opens with CODE 11's two warm-up passes, lazy `[3,4,0]` greeting selector (movies
  11603/11604/11600), and fixed movie 11618, “Let's roll to see who goes first.” It then paints its
  eight starting stacks in exact source-record order, starting sound 5042 once per stack and
  exposing one checker on every following controller pass. The board keeps all fifteen possible
  checker actors per point and uses CODE 11's decoded 24-point egg/shell bases, point-dependent
  lean, and two-pixel offsets between groups of five for both drawing and movement, instead of
  collapsing larger stacks into a numeric counter. Ordinary non-double Mario rolls use
  `$1320`'s recovered `[32,31,30]` thinking pool; the `[5,7,9]` praise branch belongs to unreachable
  `$121C` and is not substituted into live play. The game then uses CODE 11's recovered ordered move
  selector—bar-entry hits, point making, blot
  consolidation, safe moves, contact play, and source-order bearing off—instead of a substitute
  numerical board evaluator. As in the original controller, the selector is re-entered after every
  individual die rather than prefiltering turns through modern maximum-move rules. Its complete
  game-over controller is also restored: source outcome lines, the simultaneous 4022/4023
  player-victory movies and timed effect, branch-specific pauses, Mario's replay question, and only
  then the original Play Again dialog. Its idle controller now alternates by source draw between
  the three nonrepeating full-body Mario movies and three nonrepeating voiced prompts; the invented
  Backgammon knock-knock path has been removed.
- Dominoes uses CODE 14's exact three-pass 84-call tile swap, dealt-half doublet guarantee,
  backward alternating deal, deal/opening speech draws, hand-order candidate enumeration, and
  smallest-exposed-pip selection, including the original random side choice when a tile fits both
  chain ends. Player placement restores the original drag controller, including its dynamic
  endpoint radius, right-side distance ties, and pip-based choice when a wrapped chain's ends
  overlap. Its recovered turn controller restores probabilistic Mario/player handoffs, the
  one-time thinking line and delay, repeated-draw RNG consumption, four-way pass comments,
  first/later player-draw behavior, move-comment repeat suppression, and the exact timed idle
  prompts/four-joke cycle. Its complete last-tile and blocked-game result controller uses the
  source's weighted speech pools, score/tie branches, delays, and two replay questions.
- Checkers uses CODE 16's recovered full-path move generator and depth-limited minimax scoring,
  including mandatory multi-jumps, optional partial jumps when Forced Jumps is disabled,
  king/crowning weights, source board-scan order, and its 50% equal-score replacement rule. Its
  ending now distinguishes eliminating Mario from leaving him
  unable to move, uses the ten-line player-win and first/later Mario-win pools, clears the board in
  source order after an elimination, and reaches the original two-line replay pool before the
  Play Again dialog. Its source-timed idle controller restores the three full-body animations, two
  impatient lines, ordered four-joke cycle, inter-line pauses, and exact random follow-up delays.
  The former invented 80-quiet-move draw shortcut has been removed.
- Original game backgrounds, characters, cards, dice, pieces, scorecards, voices, effects, and music
  are decoded from the supplied image rather than replaced with lookalike art.
- The original QuickDraw pseudorandom generator, CODE 1 range scaler, CODE 13 startup stirring and
  descending shuffle, plus game-local swap routines drive dice rolls, deals, AI ties, and dialogue
  selectors; deterministic source vectors are checked by the release self-test.
- All 1,707 original Macintosh resources embedded in one deterministic archive.
- Runtime decoders for all 180 proprietary `Pak ` resources (3,166 frames).
- Runtime support for 467 `MuV `/`Ply `/`Img ` movies (8,586 commands), including layered images, offsets, timing, and sound events.
- Runtime conversion/playback for all 313 classic Mac `snd ` resources, including concurrent voices;
  all 666 movie sound events and direct startup/gameplay effect routes are audited.
- In-memory WinMM sequencing for all 11 standard MIDI resources with the recovered non-sequential
  `SONG` routing for menu, gameplay, and player-win tracks—no temporary files. The Windows MIDI
  mapper is prewarmed asynchronously, so a slow software-synth initialization cannot freeze the
  startup transition between the publisher card and Mario's title sequence.
- Original title, credits, icon, startup, menu, title-sequence, and gameplay art in the native UI.
- All nine source-authored help pages and the game-specific File/Edit/Options menu functions are
  restored, together with the PICT 128 About and PICT 129 Credits presentations and their original
  jingles, including name-field clipboard editing, Change Name, independent Sound/Music,
  Hide Background, Animated Pieces, and Checkers Forced Jumps.
- The recovered CODE 7 `mPRF` behavior is ported without a required sidecar file: Sound, Music,
  Hide Background, Animated Pieces, and the source editor's 15-character player name are persisted
  under the current Windows user. Hidden QA runs deliberately do not read or overwrite those values
  and forcibly mute voices, effects, and MIDI.
- DPI-scaled output uses a deterministic nearest-neighbour presentation buffer, preventing the
  one-pixel frame-to-frame shimmer produced by fractional GDI stretching. A persistent full-client
  back buffer is committed in one blit, so Windows repaints cannot expose the black clear or a
  partially rendered frame.
- No translated 68k code is executed by the port.

The executable is a self-contained native fidelity target for both supplied editions. It
semantically replaces the original 68k/8086 code and Macintosh/DOS platform services; it does not
embed or run either original application executable. See
[the function audit](docs/FUNCTION_AUDIT.md) for the evidence and coverage statement.

The supplied `MarioFundamentals.img` is the exact emulator-start image from the linked Internet
Archive item: its 25,165,824-byte size, MD5 `EEAD810A2DF2FD4B6ED62D52F363F74C`, and SHA-1
`4423EDDDE3BE15D80EF2822B2DB861BF094FB2F4` all match the Archive.org metadata. Its independently
recorded SHA-256 is `2F898FEC2605D9855D67DF69F6E90BD4D97BFF7E5199163A3462BC2DBA64F0A7`.

The DOS source set is pinned independently: the supplied fixed VHD is 8,389,120 bytes with SHA-256
`092C319B1EE2FBA79F12648AE1757953A6C7A2A837B5E38C7418169FC434085A`; its CD CHD is
221,698,560 bytes with SHA-256
`A31033A28F3B1BC4744D36B90FD4B6867EBC236E8A7F754235E6D1A5881EB466`. The matching game core is
MARIO.EXE/PRD/PRS with hashes recorded in
[the reverse-engineering report](docs/REVERSE_ENGINEERING.md).

## Build and run

Requirements:

- Windows 10 or later
- CMake 3.24+
- Ninja
- A MinGW-w64 C++20 compiler

From PowerShell:

```powershell
.\tools\build_release.ps1
.\dist\MarioFundamentals.exe
```

The release script configures a static C++ runtime build, compiles the resources, runs the
executable's full hidden/silent self-test, verifies the PE architecture and dependency set, proves
both deterministic asset packs occur exactly once, proves all 1,707 Macintosh and 1,806 DOS ripped
resources against their extraction manifests when those local audit records are present, reruns the
self-test from an empty working directory, regenerates 202 Macintosh and 210 DOS no-window
presentation frames at the exact 512×384 and 320×200 logical sizes, compares 34 independent
original-output references (publisher/title states, stable layouts, source-timed game intros, the
post-intro Backgammon chooser, exact Dominoes portrait registration, and focused Macintosh Yacht
actor/cup checks) when the unshipped local captures are present, and copies the result to `dist`.

The dual-edition artifact produced here is an unreleased QA candidate. Its build-specific checksum
is written to `dist/SHA256SUMS.txt`; no candidate should be redistributed as a 2.0 release until the
corrected dual-edition presentation audit is explicitly accepted. The hidden self-test runs from an
otherwise empty directory containing only the executable, proving that no disk image, original
application, DOS support file, loose asset, or generated sidecar is required at runtime.

To run only the executable-level audit:

```powershell
Start-Process .\build\MarioFundamentals.exe -ArgumentList '--self-test' -WindowStyle Hidden -Wait
```

To regenerate the silent logical-resolution presentation sweeps without creating a window or
opening an audio device:

```powershell
Start-Process .\build\MarioFundamentals.exe -ArgumentList '--render-mac-qa' -WindowStyle Hidden -Wait
Start-Process .\build\MarioFundamentals.exe -ArgumentList '--render-dos-qa' -WindowStyle Hidden -Wait
```

The expected report begins:

```text
PASS mac_assets=1707 mac_pak=180 mac_frames=3166 mac_movies=467 mac_commands=8586 mac_midi_events=6968 mac_sounds=313 games=5 dos_assets=1806 dos_pak=187 dos_frames=3633 dos_movies=574 dos_commands=10614 dos_xmi_events=12253 dos_sounds=278
```

## Controls

- Boot selector: choose Macintosh 1.1 or DOS 1.0; closing it exits without starting either game.
- Mouse: select games, pieces, cards, dice, score lines, and buttons.
- Main menu: arrow keys cycle in source C/G/D/B/Y order; those letters select directly; `Enter`
  starts the selected game.
- `S`: toggle sound and voices.
- `M`: toggle music independently.
- `H`: hide/show the backing presentation by switching to the original-size compact game window.
- `F1`: show rules for the current game.
- `F11` or `Alt+Enter`: toggle fullscreen in either edition.
- `Escape`: return to the main menu; from the menu, exit.
- `Ctrl+Q`: source File-menu shortcut; exit the current game, or quit from the main shell.
- Credits: click anywhere to return.

The DOS edition retains its source-sized UI and primary keyboard routes: C/G/D/B/Y select games,
arrows cycle the easel, Return/Space starts, `N` opens the original New Game confirmation, and
`S`/`M` toggle sound/music. Escape skips the active startup stage, returns from a game to the menu,
or exits from the menu. Its source 320x200 `File / Options / Help` strip, context-specific game
commands, and two-page Pak-backed instruction screens are also restored without shifting the game
artwork.

## Preservation records

- [Reverse-engineering report](docs/REVERSE_ENGINEERING.md)
- [Function and subsystem audit](docs/FUNCTION_AUDIT.md)
- [Music and sound audit](docs/AUDIO_AUDIT.md)
- [Fidelity and release-withdrawal matrix](docs/QA_MATRIX.md)
- [Asset catalog](docs/ASSET_CATALOG.md)
- [Version 2.0.0 release notes](docs/RELEASE_NOTES_2.0.0.md)
- Machine-readable disassembly, decoded DATA image, overlay records, movie catalogs, converted
  media, and visual QA captures are generated under ignored `work/` directories. The Macintosh
  1,299-row routine disposition ledger is `work/audit/function-traceability.csv`; the exact DOS
  591-row overlay ledger is `work/audit/dos-overlay-function-traceability.csv`. Separate Macintosh,
  DOS, and PE reports are produced by the release pipeline.

## Credits and legal status

The native port was created by [kandowontu](https://github.com/kandowontu), with engineering
assistance from OpenAI Codex. Both editions' production credits—including Charles Martinet as the
voice of Mario—are separately preserved in [CREDITS.md](CREDITS.md).

The repository's original native-port source and tooling are available under the terms in
[LICENSE.md](LICENSE.md). That license does **not** cover Nintendo characters, original artwork,
audio, fonts, other game data, or release executables containing that data. Those materials remain
the property of their respective rights holders. This independent preservation project is not
affiliated with or endorsed by Nintendo, Interplay, BrainStorm, Presage, or Stepping Stone.
