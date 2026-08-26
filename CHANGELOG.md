# Changelog

All notable native-port changes are documented here.

## [2.0.0] - Unreleased

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
- Silent, no-window Macintosh presentation output covering startup, title skip, four board-flip
  positions, every menu pose, 21 points across each game intro, eight opening states per game, and
  stationary/contact cup plus seven Yacht roll/settle states. The release gate regenerates and
  validates all 232 Macintosh and 233 DOS frames at each edition's exact logical dimensions.
- A fail-closed 225-file vanilla-capture inventory and 52 independent pixel/edge comparisons,
  including the title silhouette/open hand, four menu selections, Go Fish question/transfer, and
  Yacht dice/marker/composed-hand states.
- Eight deterministic full Yacht matches per edition, driven through the public roll, die-control,
  and score-line click paths with continuous dice, scorecard, turn, cup-ownership, and completion
  invariants.
- Dual-edition Play Again regressions for all five games, separating reset board/deck/score/history
  data from preserved source module state such as round counters, shuffled dialogue cursors, idle
  cycles, character identity, and options.
- A fail-closed Macintosh selected-game intro-input audit which pins CODE 11/14/16/17/18 payload
  hashes, event-5/event-6 branch targets, and the distinct completion, single-tick, and ignored
  routes. A hidden packaged-window probe sends all five mouse and all five key paths through the
  real Win32 dispatcher and checks printable-key `WM_CHAR` suppression.
- Exact DOS resident dependency classification from all 2,900 FBOV fixups and 2,839 MZ
  relocations. The supporting ledger now distinguishes 1,750 graph-reached resident candidates,
  76 validated overlay-stub thunks, and 20 explicitly conservative compiler/system-or-indirect
  candidates instead of assigning all 1,846 rows to one generic runtime bucket.

### Changed

- Shared game controllers now render and hit-test against each edition's native 512×384 or
  320×200 coordinate system while retaining the recovered source rules and outcomes.
- The release verifier now requires exactly one byte-identical embedded copy of both source asset
  packs and rejects DOS source/build paths in the finished PE.

### Fixed

- Restored the five original DOS selected-game intro input handlers from FBOV overlays
  1/7/13/17/30. Event 28 key-down completes every intro; event 45 mouse-down completes Backgammon,
  Dominoes, Go Fish, and Yacht and is intentionally absent from Checkers. Escape now follows the
  same completion post instead of returning backward to the menu. A binary-table audit and hidden
  packaged-window input probe enforce the routes and prevent a printable skip key's Windows
  `WM_CHAR` from leaking into the following name field.
- Restored CODE 11 `$856-$91A` Backgammon click precedence. Once a checker is selected, a legal
  friendly-stack destination now performs the move instead of reselecting that point; a repeat
  click on the selected checker cancels it. Eight complete public-input matches per edition now
  enforce checker conservation, hits, bar entry, bearing off, and result-controller completion.
- Restored CODE 1 `$B22`/`$CAA` direct-sound arbitration. The 105 ms Macintosh menu click now keeps
  the source channel busy across adjacent 33 ms pointer steps instead of launching overlapping
  copies during a multi-row selection change.
- Restored CODE 1 `$A18`'s tracked replacement semantics across UI and game controllers instead of
  treating those calls as overlapping effects. A fail-closed disassembly audit now pins all 65
  absolute calls: 45 `$A18`, 20 `$CAA`, ten dynamic arguments, and both immediate resource sets.
  Go Fish card responses, Yacht score reactions, and Backgammon post-move/hit dialogue now enter
  their later controller states only after the preceding tracked cue drains.
- Dispositioned all fifteen original `$B22` call sites and restored their shared speech/effect
  waits across the Macintosh title, menu launch, Dominoes, and Go Fish controllers.
- Replaced Dominoes' fixed aggregate opening delay with CODE 14's seven channel-gated 5044 deal
  passes and two-pass inter-pair cadence, revealing exactly one player/computer pair at a time in
  both editions.
- Restored CODE 14 `$14C6`'s common player-result sequence for both last-tile and blocked-hand
  Dominoes wins: drain effect 5023, switch to SONG/XMI 135, play authored movie 3900 at each
  edition's recovered registration, hold three controller passes, then ask to play again. Exact
  outcome-music and Play Again restoration checks now cover every result branch in both editions.
- Restored Dominoes' complete drawn-hand controller: all fourteen Macintosh and sixteen DOS source
  records now render and accept drag input, DOS no longer inherits the Macintosh hand cap, and
  CODE 14 `$5744`/DOS overlay `$5367` moves an endpoint-matching bone into the last legal draw slot
  so a full hand cannot softlock. The release test now completes 128 real-input matches per edition
  with exact 28-tile and chain-adjacency invariants.
- Added a hidden packaged-window regression that sends `WM_LBUTTONDOWN` at the Macintosh easel
  during both the live title and board wipe and requires the identical completed-menu pose.
- Replaced Go Fish's guessed standalone-26015 timer with CODE 17 `$139A`'s real direct-channel
  completion gate.
- Ported CODE 12's complete Macintosh menu-selection controller: immediate pressed feedback,
  outgoing hold-to-neutral retraction, signed one-row C/G/D/B/Y pointer traversal, hidden-label
  interval, three-pass delay, retarget boundaries, and exact incoming hold times.
- Pinned Macintosh title movie 1111 to CODE 12's `duration-1` open-hand cel while Mario speaks;
  checker-stack frames and their effects now run only under the live menu-selection controller.
- Composed both time-zero cels of Yacht movie 6021 for idle Mario in both editions, restoring his
  left glove without reintroducing the separate dialogue torso/head or duplicate-cup layers.
- Restored the DOS Go Fish scoreboards' runtime `MARIO`, player-name, `BOOKS`, and `CARDS`
  captions and corrected both value columns to their source pixels. A narrow independent-source
  comparison and two native regional hashes now prevent the blank-caption regression.
- Kept the Macintosh easel under its source black cast during the initial title silhouette hold;
  the finished board artwork is no longer revealed before Mario's greeting.
- Corrected Go Fish's post-transfer QA actor from the Luigi question card to the source Yoshi card,
  and added the preceding Luigi-question state as an independent comparison.

- Moved the Dominoes score portrait to the independently captured vanilla position in each
  edition: `(11,11)` on Macintosh and `(7,15)` below the DOS menu bar.
- Routed Escape during the live Macintosh title through CODE 12's same completion path as a board
  click, preserving movie 1111's terminal hand/easel pose instead of entering the menu mid-cel.
- Corrected all CODE 14 Dominoes opening routes: first-round deal speech now uses movies
  10002/10003, later rounds use 10004/10065, Mario-first uses 10084/10005, and player-first uses
  10006/10008. This removes a Macintosh abort caused by the former DOS-only movie request.
- Restored the Macintosh title controller's source mouse-down behavior: clicking the live
  title/board sequence now stops its current voice and enters the completed menu immediately.
- Kept CODE 18's two Macintosh click contexts distinct: the Yacht-on-the-water title accepts a
  mouse-down completion, while clicks after the scorecard board appears enter `$6F0`'s locked
  gameplay-control dispatcher and do not skip the vanilla "Good luck" / "I go first" opening.
- Applied the Yacht stationary-cup suppression to the shared Macintosh/DOS controller and added
  per-edition full-path regressions covering every animated-shake and sequential-die-settle pass.
  The renderer now gives the animated movie and stationary Pak cel one exclusive visual-owner path;
  all 960 Macintosh and 1,200 DOS movie instants must contain exactly one active cup cel.
- Re-registered the Macintosh Yacht dialogue head, torso, idle actor, stationary cup, and roll
  movie against the preserved vanilla capture. The stationary and roll-contact cups now occupy the
  same `(218,129)-(296,228)` rectangle, with an exact regional hash preventing a jump or duplicate.
- Added a validation-only GitHub Actions workflow for every `main` push and pull request. It runs
  the complete packaged Windows gate and reproducible build but has read-only permissions and no
  artifact-upload or release step; withdrawn releases cannot be republished by CI.
- Restored CODE 14's shared Macintosh Dominoes title registration, joining all five dominoes and
  Yoshi instead of leaving a single scattered partial tile.
- Decoded DOS `Ply` motion payloads in their retained vertical/horizontal order instead of the
  `x/y` order used by DOS `MuV` and `Img` records. This restores the intended motion axis across
  every translated movie: Yacht now sails horizontally, game-intro actors cross their stages,
  and the Yacht dice cup shakes vertically rather than sliding sideways.
- Composited Yacht dialogue as a torso underlay followed by the live head, using the complete
  neutral Mario only while speech is idle. The stationary cup is now suppressed throughout the
  roll movie, eliminating the doubled cup and preventing Mario's jaw from being painted behind
  his shirt.
- Ended DOS game introductions when their source actors complete instead of holding blank or
  trailing cels for the Macintosh-only two-second tableau delay; corrected the Dominoes parade's
  eight-pixel stage registration.
- Repainted every intermediate Go Fish opening deal so all seven cards visibly arrive one at a
  time instead of the first card being followed by a fully consolidated hand.
- Restored the DOS Dominoes boneyard button hitbox and mouse capture, making draws and drag-to-table
  moves work at the rendered 320x200 controls even when the pointer crosses the window edge.
- Routed `Alt+Enter` through Windows system-key handling and a shared borderless-fullscreen
  controller for both editions; `F11` now follows the same path in both shells.
- Made DOS startup skipping monotonic: clicks or Escape on the live title advance into the board
  reveal, and skipping an in-progress reveal completes the filled board instead of rewinding to
  the dim title shadow.
- Decoded the Interplay and Presage publisher cards through their dedicated DOS `Intrply_clut`
  and `Presage_clut` color tables instead of the general game palette.
- Restored the title screen's terminal open-hand cel and placed all five red menu selections on
  their exact DOS button rows, eliminating the extra lower button produced by accumulated spacing.
- Registered every DOS speech head and full-body host movie against its game's original actor
  position. Mario no longer appears over a scorecard, duplicates his head, or loses the neutral
  torso between speech cels.
- Registered Yacht's neutral, talking-head, and full-body gesture assets at their source actor
  positions rather than scaling their already-authored DOS stage coordinates a second time.
- Restored each DOS game-introduction movie to its recovered stage registration and stopped
  applying the Dominoes controller baseline a second time, which had moved its entire parade below
  the framebuffer.
- Expanded silent DOS presentation QA to sample 21 points across every game introduction and eight
  timed frames across every opening conversation, in addition to the startup/menu captures.
- Corrected the DOS-specific conventional `x/y`, `width/height`, and rectangle field order instead
  of applying the Macintosh QuickDraw vertical-first order. All 1,213 same-ID DOS Img/Pak records
  now match exactly, fixing clipped talking heads and displaced movie layers throughout the port.
- Kept movie 1125's title-picture base cel over the latent menu until the board flip starts, so the
  completed menu is not exposed before its authored reveal.
- Removed the full-client black erase from every DOS timer repaint. Frames are now fully composed
  before presentation, with only the letterbox cleared, eliminating the black flash between frames.
- Expanded hidden DOS QA output to cover the live/concealed title, talking head, four board-flip
  stages, all five menu gestures, and start/middle/end frames for every game introduction.

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
