# Fidelity and release-withdrawal matrix

This matrix turns the reported defects and release requirements into explicit evidence. `Automated
pass` means the current hidden gate proves the named invariant; it does not authorize publication.
Both historical release entries remain withdrawn until the replacement artifact is visually accepted.

| Requirement or reported defect | Current evidence | State |
| --- | --- | --- |
| Self-contained native Windows executable | `tools/verify_release.py` requires AMD64 GUI PE, Windows system imports only, no compiler runtime DLLs, and exactly one embedded copy of each Macintosh/DOS pack. The isolated self-test runs from a directory containing only the EXE. | Automated pass |
| Correct Macintosh and DOS source media/assets | Preservation verification hashes all 1,707 Macintosh and 1,806 DOS resources against the extraction manifests; `docs/ASSET_CATALOG.md` records source-media and pack hashes. | Automated pass |
| Macintosh BrainStorm/Stepping Stone startup, title speech, and easel menu | `src/app.cpp::tickIntro`, movie 1111/12091/1125 decoding, the eight startup captures, four reveal samples, and `docs/AUDIO_AUDIT.md`'s direct/cued sound map cover the complete path. CODE 12's terminal open-hand cel is hash- and source-reference-tested separately from movie 1111's menu-selection effects. | Automated pass |
| DOS Interplay/Presage colors, credits, title speech, and menu | `src/dos_app.cpp`, six startup captures, four reveal samples, source palette tests, and overlays 20/21/25 in `work/disassembly/dos` cover the path. | Automated pass |
| Temporary softlock after “Proudly Present” | MIDI output is prewarmed asynchronously and sequence requests queue until the mapper is ready; the isolated hidden startup/self-test completes. See `docs/AUDIO_AUDIT.md`. | Automated pass |
| No background Mario/audio process after exit | Both window destroy paths stop voices and music and terminate their message loops; every gate run ends with no `MarioFundamentals` process. | Automated pass |
| Frame-to-frame menu/gameplay shifting | `Canvas::present` performs one explicit nearest-neighbour logical-to-client map before a 1:1 upload, eliminating DPI-dependent `StretchBlt` phase changes. Every QA frame is pinned to 512×384 or 320×200. | Automated pass |
| Port scenes retain original whole-frame geometry | Fifty-two independent comparisons cover Macintosh publisher/title states, the black-cast board silhouette and open right hand, four menu-transition instants plus the exact settled Go Fish actor, stable gameplay geometry in both editions, seven Macintosh and all five DOS game-intro instants, first-use character/name panels, Backgammon setup, DOS Go Fish scoreboard captions plus Go Fish question/transfer states, both Dominoes score-portrait registrations, and focused Yacht dice/marker/actor/hand/gesture/cup regions. A 225-file inventory rejects unaccounted retained captures. `docs/VISUAL_REFERENCE_AUDIT.md` records the method and measured margins. | Automated pass |
| Black repaint flicker | Both shells suppress erase-background; DOS composes the complete logical frame before touching the window DC and clears only the letterbox. | Automated pass |
| Board flip centered and menu concealed before reveal | Macintosh/DOS dialect-specific MuV/Img ordering is independently validated; the Macintosh title begins with Pak 710 under its black cast, and movie 1125's persistent base frame covers the latent menu. The source silhouette is independently compared and four timeline frames per edition exercise start through terminal reveal. | Automated pass |
| Clicking the DOS board cannot rewind the reveal | `DosApp::introSkipTarget` advances DimTitle/TalkingTitle to MenuReveal and MenuReveal to the completed menu; `03g/03h` captures exercise the live skip route. | Automated pass |
| Macintosh title/easel click skips like vanilla | CODE 12 `$1DDC`, `$1E14`, and `$217A` converge on the same completion post. `08/09` and `09a/09b` exercise live and board-flip mouse routes. | Automated pass |
| Macintosh selected-game intro input matches vanilla | CODE 11/14/16/17/18 prove two immediate finishes, one Dominoes single-tick advance, and two ignored mouse inputs; key routing is separately exercised without leaking a Windows `WM_CHAR`. | Automated pass |
| No duplicate/missing title hand or menu actor | Title and selected-game right-hand layers are mutually exclusive; all five exact selection-hold hashes plus pressed, outgoing, neutral, one-row pointer traversal, retarget, incoming, and settled transition frames are enforced in the Mac sweep. | Automated pass |
| Mario head/torso compositing is solid during speech | The fixed torso renders before the live head and the complete neutral actor is used only between lines. Go Fish additionally uses complete Pak 11000 rather than movie 5090's transparent base layer. Game intros/openings and outcome captures exercise these layers in both editions. | Automated pass |
| Yacht cup does not duplicate | CODE 18's edition-neutral stationary-cup guard is ported through the roll movie and all five settle passes. Every movie tick is asserted to contain at most one large-cup cel, and seven roll/settle frames are generated for both editions. The two small cup-shaped objects in vanilla are intentional remaining-roll markers, not duplicated roll cups. | Automated pass |
| Macintosh Yacht reroll actor stays registered | Both CODE 18 pre-roll paths use the source `(15,-1)` movie-6021 registration; its wide frame has exact `(99,18)-(337,212)` bounds and an independent edge comparison covers the full-body gesture. | Automated pass |
| Backgammon setup reveal is aligned and complete | CODE 11's eight stack records and all 46 controller passes have semantic regressions; visual QA renders 0, 1, 5, 10, 15, and all 30 revealed checkers in both dialects. | Automated pass |
| Dominoes deals, accepts a legal drag, and reaches all endings | Opening/deal timing, exact deck, boneyard, geometry, and drag/drop regressions run in both dialects; QA renders a legal drag plus player, Mario, and blocked-tie result states. Independent edge comparisons pin the score portrait to Macintosh `(11,11)` and DOS `(7,15)`, below the menu bar. | Automated pass |
| Checkers intro/gameplay and endings | Full-path strategy, capture rules, replay state, idle behavior, and four ending branches are regression-tested; all four result presentations render in both dialects. | Automated pass |
| Go Fish deals all cards progressively and remains playable | The seven-card deal/group timeline, stable hand-slot transfer, click rectangles, strategy, dialogue, and three outcomes are regression-tested; grouped hand, source Luigi question, Yoshi post-transfer question, and 0/3/7-letter victory frames render in both dialects. | Automated pass |
| Yacht scoring, selection, turn order, and endings | Score/category/input, adviser, roll order, dialogue, cup, and all outcome branches have executable regressions; scorecard, the source five-dice/two-marker computer state, held-dice selection, victory, and roll/settle states render in both dialects. | Automated pass |
| Music, speech, and effects are complete | `docs/AUDIO_AUDIT.md` maps startup, menu, intro, and per-game direct calls plus every movie cue; the decoder verifies 313 Mac sounds/6,968 MIDI events and 278 DOS sounds/12,253 XMI events. | Automated pass |
| Every recoverable original code entry is dispositioned | The Macintosh ledger maps 1,299 structural CODE entries with zero unaccounted. The DOS primary overlay ledger maps 591 exact/high/heuristic entries, including all 505 original INT 3F exports; the supporting radare2 ledger maps 77 overlay candidates to that exact ledger and 1,846 conservative resident candidates to the replaced DOS runtime/media family, with zero pending or unaccounted rows. | Automated pass |
| Alt+Enter fullscreen works in both editions | `tools/test_fullscreen.ps1` exercises fullscreen entry and exact windowed restoration for each edition without changing the Windows display mode. | Automated pass |
| Repository credits/rights are accurate | `CREDITS.md`, source/tooling license boundaries, source-media provenance, and the withdrawn-draft notice are present. Original game data is explicitly excluded from the source license. | Present |
| No premature public release | GitHub `v1.0.0` and `v2.0.0` are both withdrawn drafts with no public downloadable release artifact. | Enforced |

## Current gate

`tools/build_release.ps1` must pass all semantic, asset, audiovisual, presentation, independent
visual-reference inventory/comparison, fullscreen, function-traceability, preservation,
PE/dependency, and isolated-runtime checks. It now requires exactly 230 fresh
Macintosh frames and 231 fresh DOS frames at their native logical dimensions. Any missing, stale,
wrong-sized, or corrupt frame fails the build.

The remaining publication condition is explicit visual acceptance of the corrected candidate. Until
then, passing this matrix produces only an unreleased QA artifact.
