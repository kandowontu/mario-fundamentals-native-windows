# Mario's Game Gallery / FUNdamentals native Windows collection 2.0.0

> Draft status: this release was withdrawn from public access after DOS rendering defects were
> found during user QA. No public `v2.0.0` release or Git tag currently exists. The draft must not
> be republished until its replacement artifact has completed presentation review.

Version 2.0.0 adds the complete embedded DOS 1.0 edition alongside the existing Macintosh 1.1
port. The executable opens with a native edition chooser; neither route requires the original
media, executable, emulator, installer, sound driver, or loose asset files.

## Corrective QA after withdrawal

- DOS selected-game input now follows the exact FBOV overlay tables rather than a shared shell
  default. Any key completes all five titles; mouse-down completes Backgammon, Dominoes, Go Fish,
  and Yacht, while Checkers deliberately ignores it. Escape completes the current title instead
  of jumping backward to the menu. The release gate pins every table/target and sends real hidden
  window messages through all eleven routes.
- Macintosh Escape now shares the title controller's board-click completion route and installs the
  terminal hand/easel pose before the menu, matching CODE 12 `$1E14`/`$217A`.
- Macintosh title-stage mouse-down now follows CODE 12's completion callback: clicking the live
  board skips the remaining voice/animation and lands on the fully revealed menu. A hidden
  integration probe sends actual `WM_LBUTTONDOWN` input through the packaged window for both the
  talking title and partial board wipe and requires the identical final-menu raster.
- Macintosh selected-game title input now has the same fail-closed proof as the DOS route. The
  release gate hashes and decodes CODE 11/14/16/17/18, then sends real hidden-window mouse/key
  messages through all ten source-specific routes: four completion inputs, Dominoes' one-tick
  mouse advance, and five ignored inputs. A printable finishing key is also prohibited from
  leaking its paired Windows `WM_CHAR` into the first-use name field.
- Yacht's two Macintosh mouse contexts are now explicitly audited rather than conflated. CODE 18's
  first export lets a click finish the Yacht-on-the-water title; after the scorecard board appears,
  export 2 sends event 6 to `$6F0`'s gameplay-control dispatcher, so the vanilla "Good luck" /
  "I go first" board opening remains deliberately non-skippable. The native self-test clicks the
  live roll control during that lock and requires the opening state to remain unchanged.
- The first stable Macintosh title frame now keeps Pak 710 under the source black cast. The easel
  artwork appears only when the greeting begins instead of being exposed during the silhouette hold.
- CODE 12's title control now pins movie 1111 at `duration-1`, preserving Mario's open right hand
  throughout his spoken introduction. The checker-stack cels and seven effects belong only to the
  live Checkers menu-selection animation.
- The live Macintosh menu now gives its right-hand layer exclusively to the selected-game actor;
  the completed title hand is no longer composited beneath it as a second glove/object.
- CODE 12's menu controller now uses the five disassembled selection hold times
  `(600,780,600,600,480)` and plays each outgoing actor from its hold to the terminal neutral hand
  before advancing the incoming actor from zero to its own hold. `$E8E/$F3E` retargeting, one-row
  C/G/D/B/Y pointer traversal, the hidden-label interval, three-pass target delay, and immediate
  pressed-control feedback are preserved as well. This replaces the former universal 180-tick
  shortcut and direct actor swap.
- CODE 1 `$B22`/`$CAA` direct-sound arbitration is now retained separately from tracked speech.
  The 1,152-sample snd 5003 menu click occupies the source channel for 105 ms, preventing adjacent
  33 ms pointer steps from launching several overlapping copies during a long selection move.
- CODE 1 `$A18` now retains `$99C/$A44`'s stop-and-replace behavior instead of overlapping prior
  samples. The release gate scans all 65 absolute sample calls and requires the source split of 45
  tracked `$A18` requests and 20 `$CAA` direct effects, including all ten table/register arguments.
  Go Fish card responses, Yacht score reactions, and Backgammon post-move dialogue are separated
  from their preceding tracked cue rather than canceling it in the same native event.
- The complete fifteen-caller `$B22` audit now covers the remaining title and menu-launch waits,
  Dominoes deal/result/reset/selection states, and Go Fish's standalone 26015 continuation. The
  native query unions tracked speech and direct effects just like the original shared channel.
- Dominoes now exposes one player/computer pair after each of CODE 14 `$E42`'s seven free-channel
  checks, retains `$EFE`'s two controller passes between later pairs, and drains the seventh 5044
  cue at `$F2A` before the opening move. The prior fixed-delay path that made the other bones pop
  into existence has been removed in both editions.
- Dominoes now completes CODE 14 `$14C6` after either kind of player win. Effect 5023 drains before
  SONG/XMI 135 begins; movie 3900 then runs at the recovered Macintosh `(231,-13)` or DOS
  `(-2,-17)` registration with its six 5069 cues, followed by the source three-pass hold. The
  blocked-hand player branch can no longer skip the result actor/music, and the last-tile branch
  can no longer switch music before its preceding speech has finished.
- Go Fish now runs CODE 17 `$756-$8B2`'s opening state machine instead of one four-second delay.
  The seven cards appear on their source passes with tracked 5032 cues; `$30F6` merges one duplicate
  at a time; `$3862` settles one surviving card per re-entry; and `$316E/$3022` keep each moving card
  visible for the source distance-derived pass count before movie 11529 and Mario's first turn.
- Go Fish no longer softlocks when a successful player request completes a book and empties the
  hand. Movie 11538 and draw effect 5013 now deal the source replacement card after the success line
  while preserving the earned extra turn; the same helper retains Mario's distinct continue-after-
  refill route.
- Go Fish release verification now completes eight deterministic live-controller matches in both
  editions through actual card clicks. It checks all 52 cards and every persistent hand record after
  each transition, so a lost card, duplicate card, broken hit target, or input-dead turn blocks the
  candidate rather than passing through isolated opening/outcome fixtures.
- Checkers release verification now completes four deterministic matches in both editions through
  actual piece-selection and destination clicks. The test retains animated movement and Mario's
  delayed multi-step turns while checking playable-square occupancy, captures, crowning, selection
  records, and monotonic piece counts until the real result/replay sequence finishes.
- Dominoes now exposes every post-deal hand record: fourteen at Macintosh CODE 14 `$5160`'s exact
  rectangles and sixteen in DOS overlay 12. Drawn bones no longer disappear or lose their hit
  targets, DOS uses its own hand cap, and `$5744`/DOS `$5367` forces the final permitted draw to
  match a chain endpoint. The release test completes 128 matches per dialect through real draw and
  drag/drop input while preserving all 28 unique bones and every chain link.
- The no-window presentation sweeps now emit 232 exact 512×384 Macintosh frames and 233 exact
  320×200 DOS frames. They cover both Macintosh title/board mouse-down completion paths, all five
  source-specific selected-game intro input routes, DOS menu/help contexts, all intros/openings,
  Backgammon setup reveal, Dominoes full-hand/drag/endings, Checkers endings, Go Fish
  hand/question/transfer/victory, Yacht scoring/computer-dice/selection/victory, both Yacht
  roll/settle timelines, and the complete movie-6021 reroll-gesture cycle in both editions.
- The DOS source's eight-pixel `File / Options / Help` bar is restored exactly over main-menu and
  gameplay frames, with context-specific commands and the Pak 8000/8001 two-page instruction UI.
- Dominoes now follows CODE 14's exact first/later deal and highest-double dialogue tables. The
  corrected eight-way route eliminates the Macintosh post-deal abort and replaces four unrelated
  opening lines in both editions.
- Runtime status copy now stays within the original Pak font's supported character set; Unicode
  en/em dashes can no longer appear as question-mark glyphs during Backgammon, Dominoes, or Go Fish.
- The release gate now regenerates all Macintosh CODE and DOS resident/FBOV traceability ledgers.
  Its supporting DOS discovery pass decodes every one of the 2,900 FBOV fixups and 2,839 MZ
  relocations into exact segment dependencies. Of 1,846 resident candidates, 1,750 now carry their
  propagated shell/media/game caller families, 76 are separated as validated resident overlay
  thunks, and 20 retain an explicit compiler/system-or-indirect disposition because no structural
  path reaches them. The 77 appended-overlay rows still defer to the exact export/prologue ledger;
  generic, pending, unknown, or unaccounted dispositions fail the gate.
- The local release gate now compares 52 representative native frames/regions against independently
  captured original Macintosh/DOS output. Coverage includes publisher/title states, four menu
  transition instants plus the exact settled Go Fish actor, stable game geometry, seven Macintosh and all five DOS source-timed intro instants,
  complete post-intro first-use panels, Backgammon's setup reveal, DOS Go Fish scoreboard
  captions, Go Fish grouped/question/transfer hands, exact Dominoes portrait registration, the
  title's open hand, and Yacht actor/hand/dice/marker/gesture/cup regions. A separate inventory
  accounts for all 225 retained captures
  and fails on any unknown file.
- All 465 hidden presentation frames are now independently reproducible and content-pinned as two
  complete ordered corpora after source-reference comparison. Macintosh live play keeps its
  TickCount-derived QuickDraw seed; only the no-window QA renderer resets to the fixed regression
  seed, so random openings and outcomes cannot evade the gate by changing between builds.
- The Dominoes score portrait now uses its independently captured edition-specific origin:
  Macintosh `(11,11)` and DOS `(7,15)`, safely below the DOS menu bar.
- DOS Backgammon, Checkers, and Go Fish intro actors now resolve the registered points supplied by
  their overlays before applying MuV bounds. This removes the second registration that displaced
  Backgammon by `(101,18)`, both Checkers actors by their separate origins, and both Go Fish actors
  below their original swimming lanes.
- Backgammon's second checker click now follows CODE 11 `$8CC-$91A`: it cancels when the selected
  checker itself is clicked, otherwise it validates the destination before considering any new
  selection. Legal moves onto friendly stacks no longer reselect the destination and stall the
  turn. Eight complete public-input matches per edition now gate rolls, moves, hits, bar entry,
  bearing off, checker conservation, Mario's delayed turns, and result completion.
- The Yacht idle cup is suppressed by the same controller guard in both editions throughout the
  roll movie and all sequential die-settle passes, with separate Macintosh and DOS regressions.
  The renderer now chooses one exclusive visual owner—movie 6020 or Pak 6010's stationary cel—and
  the regression executes the entire live movie-to-five-die-settle transition while checking every
  controller pass. All 960 Macintosh and 1,200 DOS source instants prove exactly one animated large
  cup cel; vanilla's small cup-shaped counters remain because they are authored remaining-roll markers.
- Yacht now has eight full public-input matches per edition in the release gate. Each one performs
  all player rolls, holds and releases dice through their source hit records, fills the right
  scorecard, lets Mario's recovered adviser fill the left scorecard, reaches round twelve, and
  drains the native result sequence while enforcing cup, dice, score, and turn-state invariants.
- Play Again now has explicit dual-edition state-ownership coverage for all five games. Per-round
  boards, hands, scores, request histories, animations, and outcome state reset, while each game's
  source-defined later-round counters, shuffled dialogue cursors, idle cycles, player identity, and
  options remain attached to the existing session. Later Backgammon full matches use this live
  replay path instead of reconstructing a fresh engine.
- DOS `Ply` motion pairs retain the source vertical/horizontal layout even though DOS `MuV` and
  `Img` geometry uses conventional x/y fields. The runtime now treats those dialects separately;
  all five game introductions move on their authored axes, the Yacht crosses the water instead
  of dropping offscreen, and the dice cup shakes vertically.
- Yacht speech now draws the fixed torso beneath the live head and uses the complete neutral Mario
  only between lines. Its stationary cup is hidden during the animated roll, removing both the
  shirt-over-jaw layering error and the duplicate cup.
- The neutral Yacht actor now composes both simultaneous time-zero cels from movie 6021 instead of
  drawing its incomplete raw base frame. Mario's left glove is solid in Macintosh and DOS gameplay.
- Both Macintosh computer-roll entry paths now register the full-body movie-6021 gesture at CODE
  18's source `(15,-1)` anchor instead of the displaced DOS-era coordinate. The wide gesture is
  independently compared with vanilla and its exact bounds are self-tested.
- Go Fish now uses complete Pak 11000 for neutral Mario. Pak 5090 frame zero is only movie 5090's
  transparent base layer, so using it alone exposed the page through his eyes; idle and question-
  card-transfer face regions are now hash-regressed in both editions.
- DOS Go Fish now draws overlay 18's `MARIO`, player-name, `BOOKS`, and `CARDS` text at the source
  Pak-223 coordinates instead of leaving blank score panels with displaced numbers. Both panels
  have exact regional hashes and a focused vanilla screenshot comparison.
- Go Fish's source-matched question QA now verifies the Luigi request before transfer and the Yoshi
  request afterward, including Pak 5007's rank-specific held card and the 7-to-8 card-count change.
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
- Exact Macintosh custom-loader records: all 3,149 DATA/CODE relocations, 2,600 resolved absolute
  calls, a 4,091-site global direct-call graph, all 218 A5 exports, and a conservative 936-row
  zero-unaccounted disposition ledger.
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
