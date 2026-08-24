# Mario's Game Gallery / FUNdamentals native Windows collection 2.0.0

> Draft status: this release was withdrawn from public access after DOS rendering defects were
> found during user QA. No public `v2.0.0` release or Git tag currently exists. The draft must not
> be republished until its replacement artifact has completed presentation review.

Version 2.0.0 adds the complete embedded DOS 1.0 edition alongside the existing Macintosh 1.1
port. The executable opens with a native edition chooser; neither route requires the original
media, executable, emulator, installer, sound driver, or loose asset files.

## Corrective QA after withdrawal

- Macintosh Escape now shares the title controller's board-click completion route and installs the
  terminal hand/easel pose before the menu, matching CODE 12 `$1E14`/`$217A`.
- Macintosh title-stage mouse-down now follows CODE 12's completion callback: clicking the live
  board skips the remaining voice/animation and lands on the fully revealed menu.
- The live Macintosh menu now gives its right-hand layer exclusively to the selected-game actor;
  the completed title hand is no longer composited beneath it as a second glove/object.
- The no-window presentation sweeps now emit 200 exact 512×384 Macintosh frames and 210 exact
  320×200 DOS frames. They cover both Macintosh title/board mouse-down completion paths, all five
  source-specific selected-game intro input routes, DOS menu/help contexts, all intros/openings,
  Backgammon setup reveal, Dominoes drag/endings, Checkers endings, Go Fish hand transfer/victory,
  Yacht scoring/selection/victory, and both Yacht roll/settle timelines.
- The DOS source's eight-pixel `File / Options / Help` bar is restored exactly over main-menu and
  gameplay frames, with context-specific commands and the Pak 8000/8001 two-page instruction UI.
- Dominoes now follows CODE 14's exact first/later deal and highest-double dialogue tables. The
  corrected eight-way route eliminates the Macintosh post-deal abort and replaces four unrelated
  opening lines in both editions.
- Runtime status copy now stays within the original Pak font's supported character set; Unicode
  en/em dashes can no longer appear as question-mark glyphs during Backgammon, Dominoes, or Go Fish.
- The release gate now regenerates all Macintosh CODE and DOS resident/FBOV traceability ledgers.
  The supporting DOS discovery ledger no longer leaves 1,923 heuristic candidates generically
  pending: 77 overlay rows defer to the exact export/prologue ledger and 1,846 resident rows are
  explicitly dispositioned to the replaced platform/runtime/media family.
- The local release gate now compares eleven representative native frames against independently
  captured original Macintosh/DOS output. The checks cover the DOS main menu and every game's
  stable board, table, scorecard, or chrome geometry without conflating random game states.
- The Yacht idle cup is suppressed by the same controller guard in both editions throughout the
  roll movie and all sequential die-settle passes, with separate Macintosh and DOS regressions.
- DOS `Ply` motion pairs retain the source vertical/horizontal layout even though DOS `MuV` and
  `Img` geometry uses conventional x/y fields. The runtime now treats those dialects separately;
  all five game introductions move on their authored axes, the Yacht crosses the water instead
  of dropping offscreen, and the dice cup shakes vertically.
- Yacht speech now draws the fixed torso beneath the live head and uses the complete neutral Mario
  only between lines. Its stationary cup is hidden during the animated roll, removing both the
  shirt-over-jaw layering error and the duplicate cup.
- DOS MuV/Img geometry now uses its proven conventional x/y field order instead of Macintosh
  QuickDraw ordering. All 1,213 same-ID Img/Pak records are exact, eliminating clipped cels,
  displaced board-flip layers, and incomplete talking heads.
- Movie 1125's framed-picture base remains over the latent menu until the flip begins; four
  headless timeline samples verify that the reveal stays centered and the labels appear only as
  authored.
- DOS painting no longer erases the viewport to black before composing each timer frame. Only the
  letterbox is cleared, after the next logical frame is complete.
- Expanded hidden visual output covers every startup/title stage, four flip timestamps, all five
  menu gestures, 21 timestamps for every game introduction, eight opening states for every game,
  first-use/replay panels, and the deterministic interaction/result states listed above.

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

No 2.0 artifact is currently published. A replacement checksum and reproducibility record will be
inserted only after the corrected dual-edition presentation has been explicitly accepted. The
release gate still requires an AMD64 PE32+ Windows GUI binary, Windows system dependencies only,
identical clean builds, an empty-directory silent self-test, and exactly one embedded copy of each deterministic
asset pack.

## Credits and rights

The original DOS 1.0 and Macintosh 1.1 production teams are credited separately in
[`CREDITS.md`](../CREDITS.md). The repository's native source/tooling license does not cover
Nintendo characters, original artwork, audio, fonts, other game data, or release executables that
contain those materials. This independent preservation project is not affiliated with or endorsed
by the original rights holders.
