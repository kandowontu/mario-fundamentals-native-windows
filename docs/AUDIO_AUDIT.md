# Music and sound audit

This audit maps both shipped editions' audio resources and recovered call sites to the native port.
It covers sampled speech/effects, movie-timeline cues, MIDI/XMIDI playback, and the DOS title
controller. The Windows executable reads embedded copies and needs neither source medium at
runtime.

## Macintosh 1.1 resource coverage

- All 313 `snd ` resources decode successfully as classic Sound Manager format 1 or 2 command
  lists. Playback converts their 8-bit mono payloads to in-memory RIFF/WAVE data and supports
  concurrent effects alongside tracked speech.
- All 11 `Midi` resources parse successfully. The self-test validates 6,968 channel events,
  variable-length deltas, running status, tempo changes, channel messages, and loop handling.
- All 467 movie timelines parse successfully. They contain 666 opcode-7 sound events. The native
  movie host plays time-zero speech as the tracked voice and delayed cues as concurrent effects.
- The only unresolved movie cue references in the source catalog are `snd ` 23019-23023, referenced
  by movies 11087 and 11089-11092. Those five sounds are not present in the shipped resource fork.
  Movie 11093 contains the valid replacement question cue, `snd ` 8046 ("Yoshi or Koopa?"), and is
  the source path used by the native first-game character prompt.

The release self-test checks all of these counts, validates every sampled sound and MIDI file, and
asserts that the dangling-cue set is exactly `{23019, 23020, 23021, 23022, 23023}`.

CODE 1 `$B22` is the direct-sound channel busy query used before guarded UI effects; `$CAA` queues
the effect and keeps that channel occupied for the sample's integer-ceiling duration. The native
audio layer tracks direct-effect lifetime separately from speech lifetime, then unions both for the
source busy query because tracked `$A18` speech and priority `$CAA` effects occupy the same original
Sound Manager channel. Movie voices can remain concurrent, but a controller using `$B22` cannot
stack the same effect every 33 ms while the preceding sample is still draining. A silent executable
regression pins the 105 ms busy window of the 1,152-sample/11,025 Hz `snd ` 5003 menu click.

The instruction scan finds exactly fifteen `$B22` callers. All fifteen are dispositioned here; no
other CODE segment calls the query.

| Original caller(s) | Source purpose | Native controller and evidence |
| --- | --- | --- |
| CODE 12 `$EE4` | Guard menu-pointer effect 5003 | One-row menu traversal calls `Audio::directSoundBusy`; the 105 ms no-stacking regression exercises the route. |
| CODE 12 `$1118` | Guard shoe-idle effect 5004 | The actor-500 idle controller starts 5004 only when the shared direct channel is free. |
| CODE 12 `$1522/$1552/$1582` | Drain menu actors, sound 5010, and the ten-count launch hold | `App::tryStartPendingMenuGame` waits the selection/idle actors, starts 5010 once, preserves the ten-count delay, then waits the shared channel before entering the game intro. |
| CODE 12 `$203C/$2064/$208A/$2132` | Gate the 5012/5000/5001/5011 title states and final handoff | `App::tickIntro` uses `directSoundBusy` at all four corresponding state boundaries; the hidden startup sweep covers every state. |
| CODE 14 `$E42/$F2A` | Pace seven opening Dominoes pairs and drain the seventh cue | `DominoesGame::tick` exposes exactly one player/computer pair after each free-channel check, plays 5044 seven times, preserves `$EFE`'s two controller passes between later pairs, and waits once more before the opening move. A semantic regression asserts visible counts 1 through 7 and exact silent pass indices `0,3,6,9,12,15,18` in both editions. |
| CODE 14 `$1174` | Drain the blocked-result cue before speech | `DominoesGame::tickOutcome` uses the shared direct-channel query before choosing the result line. |
| CODE 14 `$1528` | Drain chain-reset effect 5023 before the three-pass replay hold | `OutcomePhase::ChainResetWait` preserves both the sound gate and the subsequent controller delay. |
| CODE 14 `$19C6` | Suppress selection effect 5043 when Mario moves while the channel is occupied | Both opening and ordinary Mario tile selection use the guarded effect route; the move itself is never incorrectly blocked. |
| CODE 17 `$139A` | Drain standalone Go Fish sound 26015 before the queued response movies | `GoFishGame::beginAfterDirectSpeech` and `tick` use the actual channel state, not a guessed WAV-duration timer. |

## DOS 1.0 resource coverage

- All 278 `SND` resources decode from their six-byte little-endian header into unsigned 8-bit mono
  PCM. Their payloads contain 4,279,281 sample bytes.
- All 12 `XMI` resources parse from the `FORM`/`XDIR`/`CAT`/`XMID`/`EVNT` structure into 12,253
  native MIDI events, including generated note-offs. No shipped note-on uses zero velocity and no
  track contains an internal XMIDI loop controller; source-requested whole-track looping is used.
- All 574 `MuV`/`Ply` timelines parse. Their 10,614 commands contain 743 opcode-7 sound references
  covering 308 unique IDs. Of those, 650 cue occurrences resolve to 245 shipped `SND` resources;
  93 occurrences cover 63 IDs absent from the original PRS, not lost during extraction. They are
  recorded exactly in `work/audit/dos-decoded-media-manifest.json` and retain the source runtime's
  missing-resource no-op behavior.
- The release self-test decodes every `SND`, checks all 12 track event counts and exact durations,
  asserts the exact 743/650/93 cue inventory and 63-ID absent set, and pins all direct
  publisher/title/menu/gameplay IDs used by the native DOS shell and shared game controllers.
- The recovered Backgammon and Checkers host tables route the first DOS Yoshi/Koopa chooser through
  movie 11093, whose time-zero SND 8046 asks the authored question. The DOS native chooser now
  renders and advances that host timeline over the live game preview before revealing the choice
  panel; the table call sites, visual registrations, and valid cue are release-gated.

Miles XMIDI advances its delta units at a fixed 120 Hz. The native parser therefore models 60 PPQN
at a forced 500,000 microseconds per quarter note and ignores the SMF-like tempo meta values stored
in these files, as the original XMIDI driver does. This behavior was independently checked against
[ScummVM's XMIDI parser](https://github.com/scummvm/scummvm/blob/master/audio/midiparser_xmidi.cpp).
Applying those values as ordinary Standard MIDI File tempos makes several tracks 20–40 percent too
slow.

| DOS purpose | `XMI` | Events | Native duration (ms) |
|---|---:|---:|---:|
| Main title/menu | 130 | 4,370 | 104,201 |
| Dominoes gameplay | 134 | 540 | 5,451 |
| Dominoes player win | 135 | 272 | 3,309 |
| Go Fish gameplay | 136 | 397 | 6,734 |
| Go Fish player win | 137 | 950 | 8,667 |
| Yacht gameplay | 138 | 435 | 6,701 |
| Yacht player win | 139 | 216 | 4,376 |
| Backgammon gameplay | 140 | 674 | 9,901 |
| Backgammon player win | 141 | 400 | 7,601 |
| Checkers gameplay | 142 | 946 | 7,634 |
| Checkers player win | 143 | 601 | 4,209 |
| Low-memory title/menu fallback | 150 | 2,452 | 59,034 |

DOS overlay 20 identifies the two publisher calls (`SND` 8039 and 8042). Overlay 25's eight-state
title controller fixes the subsequent sequence: 15 source ticks, 5012, two ticks, 5000, then 5001,
five ticks, movie 12091, 5011, and XMI 130/150 routing. The executable compares available memory
with 11,000 bytes before choosing full title/menu track 130 or the smaller fallback 150; a modern
native process takes the full-track branch while both resources remain decoded and regression
checked. The native controller waits for each tracked sample before applying the post-sound
countdown, preventing either an overlap or a false loading pause. Movie opcode-7 cues are advanced
during the five game-introduction timelines as well as normal gameplay.

## Macintosh 1.1 music routing

The source `SONG` table is not numerically sequential by game. The exact mapping is:

| Purpose | `SONG` | `Midi` |
| --- | ---: | ---: |
| Main menu/title | 130 | 900 |
| Dominoes gameplay | 134 | 904 |
| Dominoes player win | 135 | 905 |
| Go Fish gameplay | 136 | 906 |
| Go Fish player win | 137 | 907 |
| Yacht gameplay | 138 | 908 |
| Yacht player win | 139 | 909 |
| Backgammon gameplay | 140 | 910 |
| Backgammon player win | 141 | 911 |
| Checkers gameplay | 142 | 912 |
| Checkers player win | 143 | 913 |

The native catalog uses these exact IDs. Music is started with the game title sequence, switches to
the corresponding win song only on a player victory, and returns to the primary song on Play Again.
Turning Music off preserves the requested song so turning it back on resumes the correct menu,
gameplay, or victory track rather than guessing a replacement.

`midiOutOpen` is never called synchronously by the window thread. The port prewarms the Windows MIDI
mapper in the background and queues a requested sequence until that handle is ready. This removes
the machine-dependent temporary softlock that could occur at the menu-music handoff after the
Stepping Stone voice cue; a timed executable regression guards both foreground calls.

## Startup, title, and common UI

| Source action | Native routing |
| --- | --- |
| BrainStorm logo | `snd ` 8038 at the CODE 12 logo transition |
| Stepping Stone logo | `snd ` 8042 at the CODE 12 logo transition |
| Title speech sequence | tracked sounds 5012, 5000, 5001, 8056, and 5011 |
| Checkers menu-selection hand animation | all seven authored cues in movie 1111 at times 120, 180, 420, 480, 540, 600, and 660; CODE 12 pins the spoken title hand to `duration-1`, so these do not fire during the title voice |
| C/G/D/B/Y menu-selection movies | exact authored cue streams from movies 1111-1115: common 5009/5013 gesture cues plus 5017 motion, 5014, or 5028 dice cues as authored |
| Change menu selection | direct effect 5003 at `$EE4-$F00` after each one-row pointer step only when CODE 1 `$B22` reports the direct channel free; its 105 ms busy window suppresses overlapping clicks during the 33 ms traversal, in addition to the outgoing/incoming movies' authored cues |
| Start selected game | tracked sound 5010 after the current selection or idle actor finishes, followed by CODE 12's ten-count hold and shared-channel drain before the game-intro controller starts |
| PICT 128 title/about presentation | tracked sound 5057 from CODE 5 `$2E` |
| PICT 129 credits presentation | tracked sound 5072 from CODE 5 `$324` |
| Mario right-shoe idle | direct effect 5004 on CODE 12 `$1032` actor-500 show cycles and its terminal cue; Pak 1011 is composited at source point `(313,288)` |
| Mario bow-tie idle | direct effect 5006 when CODE 12 `$1032` hides actor 1500 and starts the 1.8-second movie 1101 spin at `(270,174)`; the static Pak 1100 bow tie is restored when it ends |
| Mario blink idle | no sound call; CODE 12 `$12F8` briefly shows actor 471/Pak 1014 at `(283,79)` independently of the sound-bearing idle controller |
| Valid name character | effect 9201 |
| Invalid/full/empty edit | effect 9202 |
| Delete character | effect 9203 |
| Modal OK/Cancel/Yes/No and character choice | effect 9204 |

The third `$1032` random branch posts either operation 21 or the word 12061 through CODE 16's
shared 23-entry movie controller. The decoded A5 table record for operation 21 contains movie ID 0
and sound ID 0, so the normal animated-pieces path deliberately does nothing. The controller rejects
12061 at its `index >= 23` range check. Neither path starts a movie, sampled sound, or music track;
there is no missing third menu-idle sound route.

## Direct gameplay effects

Movie-authored sounds are handled automatically, including multi-cue dice, movement, speech, and
celebration timelines. The remaining direct CODE calls are routed as follows:

An instruction-level scan of immediate resource IDs passed through the shared CODE `$A18` and
`$CAA` sound entry points finds 29 unique direct IDs. Every one now has a native route; CODE 6's
9203 delete-key cue is stored through its modal state table instead of either immediate-call form
and is routed as well.

| Game | Recovered direct sound routes |
| --- | --- |
| Backgammon | 5042 once at the start of each of `$F78`'s eight progressively painted checker stacks; 5024 roll-control press; movie 4020's ten authored 5069 rattles; 5019 roll settle; 5053 checker selection; 5054 invalid destination; 5072 bar entry; 5034 checker movement for both players; 5010 hit response; movie 4022/4023's authored player-win cues |
| Dominoes | 5044 for each of seven opening deals; 5003 on every player draw and Mario's repeated-draw decision; 5043 tile selection; 5042 tile transit; delayed 5017 placement commit; 9202 at the fourteen-bone hand limit; 5024 empty-boneyard cue; 5023 chain re-layout and player-result reset; blocked-result 5034 for Mario or 5057 for player/tie |
| Checkers | 5003 for checker movement and the source-order player-win board wipe; remaining host speech/effects come from the authored movie timelines |
| Go Fish | tracked 5032 through CODE 1 `$A18` for each of seven opening deals; concurrent 5010 requested-card motion; 5013 draws, both forced empty-hand refill entries, and player-win card transit; standalone 26015 on the failed Mario request; all question, response, refill (movie 11538), idle, and outcome speech from their source movies |
| Yacht | 5018 pipe/dice roll start; 5019 for each settling white die; 5028 for every player or sequential Mario white/red die-retention toggle; 5010 hand gesture plus movie 6021's authored 5044 cue before every Mario roll; 5003 score entry and scorecard clear |

Backgammon's recovered startup and ordinary-roll speech remains movie-authored, not a replacement
direct sound call. Executable cue checks bind 11600/11603/11604 to 6019/6023/6024, fixed roll prompt
11618 to 25000, and `$1320`'s live 11630/11631/11632 thinking pool to 6033/6037/6078.

Go Fish CODE 17 `$7DA` calls `$A18`, so each opening 5032 is a tracked sound rather than a
concurrent effect. The silent controller regression records the requested tracked resource at all
seven old-value deal instants; physical output remains disabled during verification.

Yacht dialogue movie 11416 is routed only from `$113C`'s invalid all-red/zero-white reroll path.
Normal first and second rolls do not play it; their later prompts come from the 80-tick idle controller.
Mario's score-entry effect 5003 occurs after his lead-in and category-name clips plus `$176A`'s
two-tick pause, not before the announcement.

An earlier Go Fish implementation attempted to play `snd ` 5300 during idle movie 5090. That ID does
not exist: `$5300` is a CODE 14 subroutine address, not a sound resource. The invalid playback call
has been removed; movie 5090 itself has no opcode-7 cue.

## Verification boundary

The audit verifies resource integrity and native routing without opening an audible game window.
`MarioFundamentals.exe --self-test` disables sampled and MIDI output before constructing game state,
then exercises the asset, movie, audio, rules, dialogue, and outcome regressions.
