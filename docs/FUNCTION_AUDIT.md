# Function and subsystem audit

## Meaning of “ported”

The port does not translate and execute original 68k or 8086 instructions. It audits both original
executables structurally, recovers their data formats and user-visible behavior, then implements
native equivalents with C++ objects and Win32 services. This avoids carrying an emulator or either
original executable into the release.

The inventory is intentionally conservative:

- 788 entries have a compiler-style `LINK` stack-frame prologue.
- The union of segment starts, `LINK` prologues, direct internal targets, and recovered exported
  jump-table targets contains 1,299 structural entries.
- Original segment metadata declares 218 exported jump-table entries. All 218 resolve to ledger
  rows; 42 export-only targets were invisible to the earlier prologue/direct-call scan.
- 905 A-line trap invocations across 214 trap words are cataloged.

Direct-call discovery cannot prove that every computed jump-table target is a separate source-level function. `work/disassembly/function_inventory.csv` therefore labels entries as structural candidates, not recovered original symbol names. The complete disposition ledger is `work/audit/function-traceability.csv`: 78 rows have explicit control-flow landmarks, 211 other exported rows have segment-family mappings, 1,010 other structural rows have segment-family mappings, and zero rows are unaccounted for. A semantic/platform replacement may absorb several compiler-generated 68k entries; this is traceability, not a claim of instruction-for-instruction translation.

## Macintosh 1.1 segment inventory

| CODE | Code bytes | Export metadata | LINK funcs | Identified entries | System mapping | Confidence |
|---:|---:|---:|---:|---:|---|---|
| 1 | 18,524 | 1 | 148 | 233 | Application runtime and Macintosh Toolbox glue | High |
| 2 | 560 | 5 | 6 | 6 | Line-interpolation point lists and counted arrays | High |
| 3 | 7,678 | 47 | 63 | 83 | Pak/movie/image resource engine | High |
| 4 | 40 | 1 | 1 | 1 | PixMap base-address compatibility (`GetPixBaseAddr`) | High |
| 5 | 1,208 | 3 | 8 | 10 | PICT title/credits windows and version-text layout | High |
| 6 | 4,874 | 20 | 27 | 44 | Pak-backed modal panels, text entry, and event-handler chaining | High |
| 7 | 894 | 3 | 7 | 7 | Preferences resource creation, migration, load, and save | High |
| 8 | 12,488 | 26 | 117 | 126 | Bundled MIDI/Sound Manager driver, sequencer, timers, and channel control | High |
| 10 | 3,640 | 18 | 21 | 44 | Pak decompression and shared asset/UI services | High |
| 11 | 24,528 | 4 | 62 | 120 | Backgammon | High |
| 12 | 9,658 | 3 | 40 | 72 | Main shell, startup, menu, Mario host | High |
| 13 | 1,802 | 25 | 15 | 31 | Song control, shuffle, geometry, window-update, and delay helpers | High |
| 14 | 23,654 | 4 | 42 | 94 | Dominoes | High |
| 15 | 1,708 | 6 | 12 | 16 | Display-depth negotiation, dialog placement, and text layout | High |
| 16 | 31,128 | 13 | 65 | 121 | Checkers | High |
| 17 | 19,966 | 5 | 65 | 132 | Go Fish | High |
| 18 | 16,456 | 2 | 59 | 114 | Yacht | High |
| 20 | 1,832 | 19 | 11 | 22 | System capability, File Manager, and FindFolder compatibility | High |
| 21 | 516 | 7 | 9 | 12 | C/Pascal string conversion and integer radix formatting | High |
| 22 | 1,180 | 6 | 10 | 11 | Multi-monitor selection, window placement, and QuickDraw geometry | High |

## DOS 1.0 MZ/FBOV inventory

`MARIO.EXE` is a 402,576-byte Borland C++ 1991 large-memory-model MZ executable linked by TLINK
5.0. `tools/analyze_dos_exe.py` validates its 262,544-byte resident MZ image, 13,312-byte header,
2,839 relocation records, 133-entry segment table, and 140,016-byte FBOV/VROOMM payload. All 31
real overlay code records and their fixup blocks are bounds-checked and individually hashed.

The exact DOS overlay ledger is `work/audit/dos-overlay-function-traceability.csv`:

- 31 overlays and all 132,146 overlay code bytes are accounted for.
- 505 entry targets come directly from the executable's original five-byte `INT 3F` export stubs.
- 567 Borland `push bp` / `mov bp,sp` prologues and 435 in-range near-call targets provide
  independent structural evidence.
- Their union contains 591 unique entry candidates: 505 exact export targets, 62 additional
  high-confidence compiler prologues, and 24 additional heuristic near-call targets.
- Every row maps to a native subsystem. The mapping contains 81 Backgammon, 42 Checkers,
  47 Dominoes, 65 Go Fish, 81 Yacht, 81 shell, and 194 media/runtime candidates.

The confidence labels matter. Export-stub targets are exact original entry points; compiler
prologues are high-confidence structural entries; near-call-only targets remain heuristic. The
candidate spans end at the next discovered entry and are not claimed to be recovered source-level
function sizes or symbols. A separate radare2 discovery ledger records 1,923 resident/overlay
candidates. Its 77 overlay rows defer to the exact ledger above, while all 1,846 resident rows map
to the native DOS platform/runtime/media replacement family. It has zero pending or unaccounted
candidates but remains supporting evidence because segmented far control flow makes its boundaries
less reliable than the original INT 3F exports and Borland prologues.

Confirmed overlay families include:

| Overlay | Bytes | Evidence-backed role |
|---:|---:|---|
| 0 / 1 | 21,725 / 704 | Backgammon core and Backgammon introduction (`XMI` 140, Pak 4000) |
| 3 / 4 / 5 / 7 | 6,092 / 7,142 / 2,731 / 860 | Checkers engine, search/UI, and introduction (`XMI` 142, Pak 2998) |
| 12 / 13 | 21,531 / 958 | Dominoes engine and introduction (`XMI` 134, Pak 3000) |
| 17 / 18 | 691 / 15,685 | Go Fish introduction (`XMI` 136, Pak 5000) and engine |
| 20 | 1,004 | Interplay/Presage startup (`Pak` 500/501, `SND` 8039/8042) |
| 21 | 6,081 | Live title/menu, C/G/D/B/Y selection, movies 1111–1115 |
| 25 | 2,291 | Eight-state title timing/audio controller and board flip 1125 |
| 28 / 30 | 12,101 / 834 | Yacht engine and introduction (`XMI` 138, Pak 6000) |

Overlays not assigned by confirmed resource/control-flow landmarks are explicitly marked
`dominant_immediate_resource_family` or `shared_or_supporting_overlay` in the ledger rather than
being presented as fully named routines.

## Native replacement matrix

| Original subsystem/function family | Evidence | Native replacement |
|---|---|---|
| DOS MZ/FBOV loading | MZ header/relocations, 133-entry segment table, 31 overlay records and fixups | Normal PE loading and typed native state; neither `MARIO.EXE` nor an overlay loader is embedded or executed |
| DOS PRD/PRS resource services | 1,806 directory records, paired PRS headers, complete payload chain | Dialect-aware `AssetStore` over the second embedded `MARIOFPK` RCDATA; exact type/ID/flags/payload preservation |
| DOS Pak/DIB/movie runtime | 187 Pak, 4 DIB, 574 each MuV/Ply, 177 Img resources | Dialect-aware Pak sheet/parser, DIB palette, little-endian movie parser with DOS-conventional x/y geometry distinct from Macintosh QuickDraw ordering, exact 1,213-record Img/Pak geometry gate, shared native compositor, 320×200 `Canvas` |
| DOS SND/XMI runtime | 278 SND, 12 XMI; overlay 20/25 resource calls | In-memory WAVE playback and fixed-120 Hz XMIDI sequencing; no original DOS driver, setup program, or sound-card configuration files |
| DOS startup/title/menu | Overlays 20/21/25, resident `IDM_*` command strings, Pak 8000/8001, and immediate resource/timing constants | Native Interplay, Presage, credits, dim/live title, 5012/5000/5001/12091/5011 sequence, board flip, source menu geometry/movies/shortcuts/game dispatch, the exact eight-pixel `File / Options / Help` raster over main/game frames, context-specific File/Options commands, and two-page instructions composed from the source help chrome, font, and gameplay sprites |
| Edition choice | New native collection shell | A small Win32 boot selector starts either independent embedded edition; `--edition=mac` and `--edition=dos` remain deterministic direct-launch switches |
| Segment Manager, A5 world, CODE/DATA loading | CODE 0/1 and decoded DATA | Normal PE loading, typed C++ state, no A5 emulation |
| Resource Manager calls | Resource traps and type globals | `AssetStore` over embedded `MARIOFPK` RCDATA |
| Ptr/Handle allocation and block moves | OS traps in shared segments | `std::vector`, `std::array`, RAII objects |
| Interpolated point paths and counted Long arrays | CODE 2 | Typed `std::vector<Point>` animation paths and typed game-state containers |
| QuickDraw windows, bitmaps, text, primitives | Toolbox traps and CODE 1/3/5/15 | 32-bit top-down DIB `Canvas`, deterministic nearest-neighbour presentation and GDI upload; source Pak 223–228 glyph rendering with exact CODE 15 character mapping, space advance, alignment, clipping, and eleven-character displayed-name limit; CODE 5's About panel uses the exact System 7 Times-14/Geneva-9/Monaco-12 `NFNT` strikes and QuickDraw baselines/colors instead of a Windows font |
| QuickDraw pseudorandom generation, range scaling, and shuffles | `_Random`; CODE 1 `$352C`; CODE 13 `$2AC`/`$6CC`; game-local swap loops | `SourceRandom` ports the 16807/mod-2147483647 seed recurrence, signed-low-word result, `$352C`'s signed 32-bit multiply and divide-by-65,536 (including the `-32768`/bucket-zero edge), 60 Hz startup stirring, the guarded shared descending Fisher–Yates routine, and source-specific repeated-swap deals |
| PixMap base-address compatibility | CODE 4 classic-field/`QDExtensions` branches | Direct access to owned 32-bit DIB pixel storage; no relocatable PixMap handles |
| Pak resource load/decompression | CODE 10 `0x1e`, `0xe2`, `0x1a4` | `PakSheet` outer LZSS and inner span decoder |
| Pak span blitter | CODE 3 `0x1c5a` | Alpha-aware `Canvas::sprite`/`spriteRegion` |
| Movie resource load and timeline engine | CODE 3 `$596`/`$62A` and `MuV `/`Ply `/`Img ` catalogs | Native `Movie` parser and source-exact active-interval compositor (expired images are not replayed), time loop, time-zero speech routing, and concurrent delayed sound-event playback; all 466 resolvable timelines and their visual-coverage intervals are audited |
| Sound Manager sampled playback | `snd ` resources, CODE 1 `$B22`/`$CAA`, and sound traps | All 313 resources validated; in-memory WAVE construction; concurrent WinMM `waveOut` voices; all 666 movie cue events audited; direct startup and per-game effect calls mapped; distinct speech/direct-effect lifetimes union for the shared source busy query. All fifteen `$B22` callers are dispositioned, including snd 5003's 105 ms suppression across 33 ms menu-pointer steps, title/menu launch waits, progressive Dominoes deal/result/reset gates, and Go Fish's standalone-26015 continuation. |
| MIDI driver/song playback | CODE 8/13 and `Midi`, `SONG`, `MDRV`, `MPLR` | All 11 SMFs and 6,968 events validated; exact non-sequential menu/game/player-win `SONG` mapping; per-channel state; correct-track resume across the independent Music toggle; WinMM MIDI output |
| Pak-backed modal panels and name editing | CODE 6, Pak 100/101/105/106/227; DOS overlay 0/Checkers host tables | Native name entry, Yoshi/Koopa selection, New Game confirmation, and Play Again controllers with source keyboard shortcuts, source 15-character stored-name and eleven-character displayed-name limits, source Pak 227 edit glyphs, exact 9201/9202/9203/9204 edit/modal effects, and DOS movie 11093/SND 8046 first-choice question routing. DOS preserves the live per-game preview beneath the animated question, delays Pak 101 until that movie ends, retains the preview beneath every name panel, and rolls back preview RNG before the playable instance. Eleven full-frame hashes cover the three talking questions, three terminal choices, and all five name prompts. |
| Classic event loop and menu dispatch | CODE 1/6 and MBAR/MENU resources | Unicode Win32 message loop; exact main-shell MBAR 128 versus game MBAR 129-133 scope; exact Quit/Q versus Exit Game/Q routing through Ctrl+Q; source Options MENU 300/301/303/302/304/305 selection while a game title is pending or active; name-field clipboard operations; Change Name; independent Sound/Music; compact Hide Background mode; Animated Pieces; and Checkers Forced Jumps |
| Preferences resource file | CODE 7 and `mPRF` 128 | Per-user Windows Registry values for the source record's player name, Sound, Music, Hide Background, and Animated Pieces fields; no shipped sidecar or original resource fork is required |
| Startup/menu/Mario host | CODE 12 | BrainStorm/Stepping Stone screens with direct sounds 8038/8042, source-timed fades/gaps, voiced title stage, `$1DDC` mouse-down and `$1E14` Escape routes to the same `$2E0` completion callback used by natural termination at `$217A`, movie 1111 pinned to `duration-1` as the title's open hand and advanced with its seven synchronized effects only by the live Checkers menu-selection controller, mutually exclusive title/menu right-hand layers, source C/G/D/B/Y selection order with movies 1111-1115, `$ABC`'s exact 10/13/10/10/8 hold multipliers (600/780/600/600/480 source ticks), and `$D36-$100C`'s immediate pressed control, hidden-label interval, outgoing hold-to-neutral retraction, signed one-row pointer traversal, three-pass delay, `$E8E/$F3E` retarget boundaries, incoming zero-to-hold reveal, and `$EE4`'s CODE 1 `$B22` direct-sound gate so 5003 cannot overlap itself across rows, plus keyboard/mouse routing, `$1400`'s posted launch destination that waits for the current selection/idle actor, plays tracked sound 5010 once, preserves its ten-count hold, and drains the shared channel through `$1522/$1552/$1582`, actor-500/Pak-1011 shoe taps with sound 5004, actor-1500/movie-1101 bow-tie spins with sound 5006, the independent actor-471 blink scheduler, session-scoped name/character choices using movie 11093's valid sound 8046, preview-state RNG rollback so first-use prompts do not double-consume the shared QuickDraw stream, original reset/replay dialogs, source idle-conversation timing support, and in-session Play Again resets that preserve later-round speech state |
| PICT title/credits/about presentation | CODE 5, PICT 128/129, `vers` resources, System 7 font suitcases | PICT 128 is drawn at its exact 447x215 size; the six centered yellow credit lines, 240-pixel divider, gray copyright, and `v. 1.1` label use recovered coordinates and source bitmap glyphs; PICT 129, modal return behavior, and direct sounds 5057/5072 are retained. The complete About raster is hash-regressed in `--self-test` |
| Display-depth negotiation and modal placement | CODE 15/22 | 32-bit DIB rendering, monitor-aware Win32 placement, source-size modal composition, and no destructive host display-mode switch |
| Backgammon rules/state/AI | CODE 11 and DOS overlays 0/1 | Native engine with source board geometry, egg/shell identities, `$DD8`'s two startup passes, A5-`$3B52`'s lazy `[3,4,0]` greeting selector, fixed 11618 roll prompt, old-value post-prompt delay, and `$F78`'s exact eight-record/46-pass one-checker-at-a-time setup reveal synchronized to one 5042 cue per stack. The DOS table difference is explicit: overlay 0 index 58 is the 11093 character question and its later index 18 is 11618; the full 59-record table and both call sites are byte-pinned. `$558A`/`$5756` preserve all fifteen possible checker actors on a point; rendering and motion use `$5A82`/`$59A6`'s decoded 24-point egg/shell bases, ten/eleven-pixel in-group spacing, point-dependent lean, and two-pixel offsets between groups of five instead of a five-sprite numeric-count shortcut. It includes the two-die tumbling movie, source 600/100 and 1200/200 dice buckets, async Mario moves, animated pieces, hit/double/bar speech, the live `$1320` `[32,31,30]` post-roll thinking pool (while `$121C`'s `[5,7,9]` praise pool remains unreachable), the lazy `$214A` three-line stalled-turn pool, the `$15B4` equal "It's your turn"/"Your roll" handoff choice, the `$3B88`/`$3BB6` equal-probability outcome pools, simultaneous half-tick 4022/4023 player-victory movies with timed sound 5074, the `$3CF0` replay prompt and source controller pauses, and the exact `$2588` 200-tick idle controller: an equal choice between nonrepeating 9000/9001/9002 full-body movies and the nonrepeating `$26AC` 11638/11639/11637 voice pool (including "Mamma mia"). The recovered `$161A` ordered AI selector handles bar entry, point making, consolidation, safe moves before hits outside home, contact fallback, and bearing off. The controller re-enters `$161A` after each die with the updated board; it does not apply a modern maximum-length turn prefilter. Source-order regressions cover startup ordering and dialogue pools, every initial checker transition and stack boundary, exact first-through-fifteenth checker geometry, idle branch/rejection draws, bar hits/merges, point making, safety-vs-hit ordering, fallback, doubles, exact bearing off, high-die oversized final-checker bearing off, and both complete outcome branches. |
| Dominoes rules/state/AI | CODE 13/14 and DOS overlays 12/13 | Native engine with the exact `$852` three-pass/84-call tile swap, dealt-half doublet guarantee, descending alternating 27/26… hand deal, `$DCA`'s first/later 10002/10003 and 10004/10065 deal pools, `$2E9A`'s 10084/10005 Mario-first and 10006/10008 player-first pools, and `$E42/$F2A`'s seven shared-channel-gated 5044 deal passes that expose exactly one player/computer pair at a time before the highest-double opening. `$E1C` preloads the first pass and `$EFE` restores the two controller passes between later pairs. Macintosh `$5160`'s fourteen `(6+32i,289)-(36+32i,351)` hand records and the DOS overlay's sixteen `(3+17i,154)-(19+17i,187)` records are shared by rendering and hit testing, including bones drawn after the opening seven. At capacity minus one, Macintosh `$232E/$5744` and DOS `$5367` scan for either endpoint and swap a matching boneyard record into the next-draw slot, avoiding an unplayable 14/16-bone softlock. It preserves centred chain layout and `$276C` mouse-down/drag/drop placement resolved against the dropped chain endpoint. `$27E0`–`$2A50` retains Chebyshev endpoint proximity, the 150-pixel short-chain radius, half-separation/60-pixel wrapped clamp, right-end ties, and pip disambiguation when ends overlap. The full dialogue, idle, strategy, blocked/last-tile, score/tie, reset, and replay controllers retain their audited source calls and pools. Exact deck/deal/opening, drawn-record visibility/hitability, capacity correction, drag boundaries, dialogue, idle, replay, and all five outcome paths run in self-test; 128 seeded public-input matches per dialect additionally require 28-tile uniqueness/conservation, legal chain adjacency, player/Mario plays and draws, and live result-controller completion. |
| Checkers rules/state/AI | CODE 15/16 | Native engine with optional forced captures, partial-jump choices when that option is disabled, animated moves, asynchronous Mario turns, multiple jumps, original Pak 2501 Yoshi/Koopa/king pieces, and score portraits. The `$27BA` idle controller preserves its 120-tick opening, 49/51 visual/dialogue split, lazily shuffled 9000/9001/9002 full-body pool, two impatient lines, ordered four-joke cycle with five-tick inter-line pauses, and distinct 500-tick or random 60–309-tick follow-up delays. The recovered CODE 16 full-path minimax selector replaces the former heuristic. The `$0E98` result controller distinguishes a player elimination win, Mario's no-legal-move loss, and first/later Mario wins; it uses the exact ten/five-entry speech pools, source-order piece wipe, five/six-tick pauses, `$1196` replay selector, and preserves Mario's win counter across Play Again. The non-source 80-ply draw was removed. Idle RNG, strategy, replay-state, voice-cue, and all four ending-path regressions run in the release self-test. Four additional seeded matches per dialect start from the twelve-versus-twelve board, select and land every player move through the public two-click hit path, retain the 330 ms move actor and Mario's delayed plan, and must reach the live result controller with legal playable-square occupancy and monotonically non-increasing piece counts after every pass. |
| Go Fish rules/state/AI | CODE 17 / DOS overlay 18 | Native engine with the source greeting pool and CODE 17 `$756-$8B2`'s complete opening controller: old-value 5/1/1/10-count pauses, seven separately exposed cards, tracked snd 5032 on every deal, one `$30F6` duplicate merge at a time, `$3862` re-entry after every move, `$3022`'s fourteen-pixel movement pass divisor, and fixed movie 11529 before Mario's first question. It retains four-cards-per-rank numbering, the `$467A` 300-pair 5200/100 swap deck, contiguous 0–6/7–13 deal and forward draw order, and `$371A-$3852`'s thirteen persistent rank records: rotated primary coordinates, overflow row, animated opening consolidation, stable later holes, first-inactive-record reuse, and exact 54x76 click rectangles. It also preserves small held question cards, synchronized prefix/rank questions, exact 1-in-4 uncommon-prefix branch, shuffled turn/result/outcome dialogue pools, requested-card extra turns with 5010/5013 motion effects, forced empty-hand refills, and deck-empty book-count termination. Standalone sound 26015 advances through CODE 17 `$139A`'s actual shared-channel completion query before its queued response movies. DOS dynamically restores the `MARIO`, player-name, `BOOKS`, and `CARDS` captions omitted from its Pak 5001 background, with source-aligned value columns. The fixed torso is composed beneath live head movies, while complete Pak 11000 supplies the neutral face; movie 5090's transparent base is never substituted for that complete actor. `$11F8` preserves the source's `Random(1)` advance and its necessarily unreachable 11543/11544 thinking branch. The recovered `$484A` Mario selector preserves the otherwise-discarded `Random(0)` seed advance, player-question memory, 30-entry request history, cyclic held-card scan, preliminary history write, five-try repeat avoidance, and card-count-times-100 index draws. `$1B88`'s idle controller uses a 120-source-tick wait and alternates—starting with speech—between the shuffled 11561/11562 pool and the lazy-shuffled 5094/5091/5090/999/999 visual pool; movie 5090 has no sound cue (`$5300` is a CODE 14 address), and both missing-999 entries intentionally do nothing. Selector order and the alternation counter persist across Play Again. The result controller preserves the branch-specific outcome pools; a player win deals Pak 5211's seven “YOU WIN!” faces, switches to SONG 137/Midi 907, flips them away with seven copies of movie 5210 at the source coordinates, and alone reaches movie 11571's replay question. Deck, opening phase/motion order, hand layout, dialogue/RNG, strategy, resource timeline, voice cues, scoreboard raster, neutral-face raster, and all three outcome paths run in the release self-test. |
| Yacht rules/state/AI | CODE 18 | Native engine with source scorecard order and scoring values, `$2F22`'s twelve 96x20 right-player score controls and twenty-pixel score placement, exact index-11/18 Mario-first opening plus `$CF8`'s optional index-20 branch, source-registered Macintosh dialogue head/torso actors and a two-cel-composed movie-6021 idle actor, both pre-roll movie-6021 gestures at `(15,-1)` (wide bounds `(99,18)-(337,212)`), and pipe-roll movie 6020 whose contact frame exactly overlays Pak 6010's `(218,129)-(296,228)` stationary frame. The stationary cup is suppressed through the complete live roll movie and all five sequential settle passes in both editions; executable ownership checks cover every controller pass and every movie tick has at most one live large-cup cel. The two small cup-shaped controls remain intentionally visible as source remaining-roll markers. The controller retains exact 30000/5000 die buckets, `$2B42`/`$2C40`'s exact white-reroll/red-held lanes, same-pass held-die skipping, progressive white-die settling, legal all-five rerolls, and `$215C`'s markers decremented at roll start. The recovered `$3596` adviser includes its internal category order, immediate-bank priorities, triple/straight/pair retention, duplicate handling, final-category ties, and low-line sacrifice rule. All thirteen direct `$352C` sites are mapped: module-load joke seeding, opening, filled-line feedback, two player-idle gates, idle-line choice, score reaction, computer thinking, conditional reroll speech, five dice, and both outcome pools. `$190`'s four thinking/reaction selectors use their original lazy shuffles. The `$12A0`/`$142E` idle controller waits 80 source ticks, uses its 75/25 prompt/joke split, varies the prompt with rolls remaining, and cycles the recovered Pizza/Jamaica/Giovanni/Yucca five-part conversations with five-tick gaps. `$19E2` preserves source indices 42, 44/45, and 39/43 for tie/player/Mario outcomes, the player-only 6022 celebration, common index-53 replay question, and 24-tick scorecard clear. Input geometry, dialogue/RNG, strategy, actor/cup-layer presentation, and all three result branches have executable regression coverage. |

Go Fish card groups draw Pak 5006 frames zero through three over Pak 5005 at the recovered card
origin, reproducing the authored red one-through-four corner numerals instead of system-font text.
The empty-hand controller is covered in both directions as well: Mario continues after refilling a
player whom his request emptied, while a player who completes a book on a successful request receives
movie 11538's replacement card and retains the earned extra turn. The latter live regression proves
the replacement rank can immediately be clicked instead of leaving an empty-hand softlock.
Eight seeded full matches in each asset dialect also drive the public click path from the recovered
opening deal through the real result controller. They reject any duplicated/lost live card, any
52-card/book conservation error, any divergence between the player's cards and the thirteen source
display records, or any input-dead turn that cannot finish within the controller ceiling.

The Yacht controller audit additionally covers `$1560`'s adviser-driven white/red transitions:
Mario changes one die at a time with effect 5028 and five-tick gaps, retains the resulting lane
state through his score announcement, runs the thinking selector after the final roll too, and
finishes selection/optional speech before movie 6021 performs the next-roll hand gesture.
The same 6021 plus direct-5010 pre-roll sequence now runs before Mario's initial roll in every round,
not only before adviser rerolls.
Player state `$113C` permits an all-five-white reroll, rejects only an all-red/zero-white reroll,
and routes dialogue 11416 solely through that error path rather than after successful rolls.
Round progression is now tied to player state `$126E`, so Mario opens all twelve rounds and both
scorecards finish with twelve entries. Mario's `$176A` scoring sequence defers its score write and
effect 5003 until both announcement clips and the two-tick pause complete, then preserves the
source nine-tick post-score handoff delay. These transitions have an executable lifecycle regression.
The player-win outcome regression also requires `$1B16`'s five-pass 1–5 die reveal before movie 6022.
Eight seeded full matches in each asset dialect now drive those controllers continuously from the
Good Luck/I Go First opening through all twelve rounds and the real result sequence. They use the
public roll button, white/red die hit records, and right-scorecard lines; require all 24 category
records to be committed exactly once before the source wipe; and reject invalid dice, score,
turn-stage, cup-ownership, or nonterminating states after every controller pass.

## Fidelity boundaries

- The game rules are native semantic equivalents with source-derived probability tables, the
  original QuickDraw random sequence/range/shuffle behavior, recovered AI/scoring branches, and
  executable coverage for the audited dialogue, idle, replay, and result controllers. They do not
  execute translated 68k instructions. Macintosh cooperative scheduling is represented by the
  native 33 ms controller timer, so host-dependent presentation may differ by one timer quantum.
- The DOS edition reuses those audited semantic game controllers where the editions share rules,
  but loads the DOS-native backgrounds, sprites, movies, voices, effects, XMI, coordinates, hit
  targets, publisher/title/menu controller, game introductions, name/character panels, and
  reset/replay dialogs. The same strategy, drag/boundary, dialogue, outcome, replay, and lifecycle
  regressions are rerun against the DOS asset dialect. This is a native behavioral port, not an
  instruction-for-instruction 8086 recompilation.
- Both publisher screens, startup/title speech, animated C/G/D/B/Y main-menu selection, first-use session prompts, five
  title sequences, synchronized in-game talking heads, low-frequency idle prompts/jokes, and the
  original New Game/Play Again dialogs are restored. Source selectors retain their lazy-shuffle and
  cross-round state where the original module globals did.
- The silent presentation gate regenerates 231 Macintosh and 232 DOS logical-resolution frames,
  covering both Macintosh title mouse-down routes, all five source-specific selected-game intro
  mouse routes (two finishes, one single-tick advance, and two ignored inputs),
  every menu selection, 21 samples for every game intro, eight opening states for every game,
  deterministic Backgammon setup, Dominoes full-hand/drag/outcome, Checkers outcome, Go Fish
  hand/question/transfer/victory, and Yacht score/computer-dice/selection/victory/stationary-cup
  states, both Yacht roll/settle sequences, ten samples of the reroll gesture in each edition, and
  DOS menu/help context screens. Fifty-two independent original-output comparisons include
  publisher states, the black-cast title silhouette and speech poses, four menu-transition instants
  plus the exact settled Go Fish actor,
  complete source-timed frames for seven Macintosh intro instants and all five DOS intros,
  first-use panels, Backgammon's setup reveal, the DOS Go Fish scoreboard captions and three Go Fish hand/question states,
  one-pixel-sensitive Dominoes portrait registration, and focused Macintosh Yacht
  actor/dice/marker/gesture/cup checks. A separate 225-file inventory gates every retained source
  capture as compared, redundant, external, incomplete repaint, stochastic, or supplemental.
- CODE 12 `$1032`'s two sound-bearing random menu idles and `$12F8`'s independent blink are restored.
  Its third randomized branch is also closed: decoded CODE 16 dispatch and A5 table data prove that
  operation 21 is a zero-movie/zero-sound record and 12061 fails the controller's 23-entry range
  check, so both halves intentionally leave the menu unchanged.
- The nine authored PICT help pages used by the original are rendered through the source QuickDraw environment, embedded as exact 486x350 modal panels, and navigated with their original page chrome and buttons.
- All 466 source-resolvable movies are supported. The byte-identical movie/`Img ` 11001 pair is
  retained as a proven source-orphaned artifact: the shipped CODE 3 loader can select only base
  table 11000 (11 images), while that timeline requests 55 and has no load-site reference.
- All CODE segments are now assigned an evidence-backed subsystem. Segments 4, 7, and 20–22 are
  PixMap, preferences, system/file, string-formatting, and display-placement compatibility
  utilities rather than missing game engines.

These boundaries distinguish intentional native platform replacements from missing source behavior.
