# Changelog

All notable native-port changes are documented here.

## [2.0.0] - 2026-08-23

### Added

- Native boot selector for the Macintosh 1.1 and DOS 1.0 editions.
- Self-contained DOS port of Mario's Game Gallery with its publisher/credits sequence, voiced
  title controller, easel menu, all five game introductions, character/name panels, gameplay,
  reset/replay dialogs, music, voices, and effects.
- Exact DOS PRD/PRS extractor, little-endian DOS media decoders, deterministic embedded asset
  pack, MZ/FBOV overlay analyzer, and routine traceability ledgers.
- DOS XMI playback at Miles XMIDI's fixed 120 Hz timing and executable regressions for every
  shipped track duration.
- Headless DOS presentation captures and dual-edition gameplay, asset, audio, dependency,
  preservation, and empty-directory release verification.

### Changed

- Shared game controllers now render and hit-test against each edition's native 512×384 or
  320×200 coordinate system while retaining the recovered source rules and outcomes.
- The release verifier now requires exactly one byte-identical embedded copy of both source asset
  packs and rejects DOS source/build paths in the finished PE.

## [1.0.0] - 2026-08-23

### Added

- Self-contained 64-bit native Windows executable with all 1,707 source resources embedded.
- Native implementations of Checkers, Go Fish, Dominoes, Backgammon, and Yacht.
- Source-order publisher cards, title sequence, voiced Mario introduction, easel menu, game
  intros, help, About, Credits, menus, options, replay flow, and persistent preferences.
- Runtime decoders for 180 Pak resources, 467 movies, 313 sounds, and 11 MIDI resources.
- Source-traced game rules, AI, RNG, dialogue, turn pacing, idle behavior, and result sequences.
- Deterministic hidden/silent executable regression suite and PE/preservation verification tools.
- Flicker-free integer-scaled presentation and asynchronous MIDI-device prewarming.

[1.0.0]: https://github.com/kandowontu/mario-fundamentals-native-windows/releases/tag/v1.0.0
[2.0.0]: https://github.com/kandowontu/mario-fundamentals-native-windows/releases/tag/v2.0.0
