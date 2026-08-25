# Reverse-engineering report

## Source preservation

### Macintosh 1.1

The input is a 24 MiB bootable classic Macintosh HFS image containing System 7 and `Mario's FUNdamentals 1.1`. The application has Macintosh type `APPL`, creator `ZarK`, an empty data fork, and a 7,270,326-byte resource fork.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Original `MarioFundamentals.img` | 25,165,824 | `2F898FEC2605D9855D67DF69F6E90BD4D97BFF7E5199163A3462BC2DBA64F0A7` |
| Application resource fork | 7,270,326 | `86D3D6FEA35CF62DEDDC1B0480CDAE7217BA87EC0CEDAF51F1CA524FFAF8CAC1` |
| Extracted MacBinary | 7,270,528 | `C8C36225436C4D0ED404C8D0F6BF4B1B61706554AB6926A9BD4C6E98463B5155` |
| Embedded native asset pack | 7,277,380 | `5EE5E1A844F167FBC0D5FEA8D99DD22AB268360F03E5B3DB419C1C20EB1D8B6E` |

The source image is never used by build or runtime code. Extraction tools operate on a disposable copy. The final source SHA-256 was rechecked after extraction.

The supplied file is byte-for-byte the Internet Archive item's emulator-start image, not a
different download: Archive.org lists `MarioFundamentals.img` as 25,165,824 bytes with MD5
`EEAD810A2DF2FD4B6ED62D52F363F74C` and SHA-1
`4423EDDDE3BE15D80EF2822B2DB861BF094FB2F4`; the local image has the same size and both hashes.

Live comparison against the Internet Archive emulator established the startup presentation order
as BrainStorm logo, fade/black gap, Stepping Stone logo, fade/black gap, then the voiced Mario title
stage and easel menu. Character and name choices are first-use game/session state, not startup
screens. Reference captures and logical-resolution comparisons are retained under `work/references`
and `work/qa`.

### DOS 1.0

The second supplied source is an AO486 installation disk backed by a fixed VHD, accompanied by its
CD-ROM CHD. The installed `README.TXT` identifies `MARIO'S GAME GALLERY - DOS ver. 1.0 2/95`.
The installed game core and the matching CD files are:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Fixed VHD `marios game gallery.vhd` | 8,389,120 | `092C319B1EE2FBA79F12648AE1757953A6C7A2A837B5E38C7418169FC434085A` |
| CD CHD `marios game gallery.chd` | 221,698,560 | `A31033A28F3B1BC4744D36B90FD4B6867EBC236E8A7F754235E6D1A5881EB466` |
| `MARIO.EXE` | 402,576 | `D722F8B08E02C53020B1428A224A2D4EA4FAB4A6B3E79FC9D294613C2AE70877` |
| `MARIO.PRD` | 43,560 | `9AFFFF6B76AFDC49675E5ADB424E997B6F6E53B7D6D6330B870EC622F5D5871E` |
| `MARIO.PRS` | 6,131,939 | `8C1C4D52C5EDC9199F68DECF7A1C8D7A3E913D405DA301712B379D5B3DBAEB82` |
| `MARPREFS.DAT` | 44 | `C95E712B6A920611FEEFE6EEB45B5113123D205C15E91ABE548ECCDD799A7817` |
| Embedded DOS native asset pack | 6,118,104 | `A59AE03A7C0E050067BD10C54FAB1CBD2B3A137F56C387BE2FA9925425184D54` |

The VHD and CHD were inventoried read-only. Their 65- and 197-file manifests are retained as
`work/audit/dos-vhd-manifest.json` and `work/audit/dos-cd-manifest.json`. The release does not embed
or execute the VHD, CHD, DOS executable, PRD, PRS, drivers, installer, setup program, or preferences
file.

## Macintosh resource fork

`tools/rip_resources.py` parses the resource map directly and emits every resource plus `work/rip/manifest.json`. The result is 1,707 resources across 46 types. No resource type was filtered from the native pack.

The deterministic pack format is:

1. Eight-byte `MARIOFPK` signature.
2. Little-endian version, entry count, and 20-byte entry size.
3. Sorted entries containing four-byte Macintosh type, signed ID, attributes, offset, and length.
4. Four-byte-aligned, byte-identical resource payloads.

## DOS PRD/PRS resources

`MARIO.PRD` begins its version-6 directory at offset `0x80`; its 1,806 fixed 24-byte records start
at `0xB0`. Each record supplies a little-endian payload offset, four-byte type, ID, length,
metadata, and flags. The offset points past a 28-byte header in `MARIO.PRS`, which independently
repeats the type, ID, name, reserved word, and complete record length.

`tools/rip_dos_prs.py` validates both copies, rejects duplicate type/ID keys, requires the first
header at `0x30`, proves every later record begins exactly where the preceding payload ends, and
requires the final payload to end at the final PRS byte. It extracts 1,806 resources across seven
types: 4 DIB, 177 Img, 574 MuV, 187 Pak, 574 Ply, 278 SND, and 12 XMI. The native pack uses the same
`MARIOFPK` structure as the Macintosh pack, with short DOS type names space-padded to four bytes.
`tools/verify_dos_preservation.py` rechecks the original PRD/PRS records, every extracted hash, all
pack metadata and alignment, and the single byte-identical embedded occurrence in the PE.

## DOS MZ and FBOV overlays

`MARIO.EXE` is a 16-bit Borland C++ 1991 large-memory-model MZ program linked with TLINK 5.0. Its
declared resident image is 262,544 bytes: a 13,312-byte header, 249,232 image bytes, and 2,839 MZ
relocations. An `FBOV` payload at file offset `0x40190` contains 140,016 bytes. Its 133-entry segment
table starts at file offset `0x34B80`; 31 entries are real code overlays containing 132,146 code
bytes plus their fixup records.

`tools/analyze_dos_exe.py` validates every MZ relocation target, resident segment record, original
overlay stub, code/fixup bound, and content hash, then writes an FBOV-merged analysis image and all
31 exact overlay/fixup blobs under ignored `work/disassembly/dos`. The original five-byte
`INT 3F` stubs expose 505 exact overlay exports. A second Capstone pass adds compiler-prologue and
near-call evidence to create the conservative 591-candidate overlay ledger documented in
[`FUNCTION_AUDIT.md`](FUNCTION_AUDIT.md).

Resource/control-flow landmarks establish the publisher controller in overlay 20, live title/menu
in 21, eight-state title controller in 25, and the five game core/intro families. Overlay 25 fixes
the 15/2/5 source-count sequence around SND 5012, 5000, and 5001; movie 12091; SND 5011; full
title/menu music 130 versus the smaller low-memory fallback 150; Pak 708/1001 title bases; actors
709/710/1010/1014/1100/1020; and movie 1125 board flip. Overlay 21 maps the source menu order C/G/D/B/Y to native indices Checkers, Go Fish,
Dominoes, Backgammon, and Yacht and fixes movies 1111–1115 at x=242 with y=79/79/77/82/78.

## DOS media dialect

The DOS edition shares Presage's span image concept but not the Macintosh outer byte order:

- Pak sheet flags/count/tag/offset tables are little-endian; individual frame origin, dimensions,
  and extended span counts remain big-endian.
- DIB 1000 is a complete 1×1 indexed Windows BMP whose 256 BGR entries supply the DOS palette.
- MuV, Ply, and Img fields are little-endian. Thirty-two 1,024-byte MuV records and fourteen Ply
  records contain nonsemantic bytes after their declared data, which are preserved but ignored by
  the bounded parser.
- SND uses a little-endian `<encoding=3, sample-count, sample-rate>` header followed by unsigned
  8-bit mono PCM.
- XMI uses the standard `FORM`/`XDIR`/`CAT`/`XMID`/`EVNT` container and Miles' fixed 120 Hz delta
  timing rather than applying its embedded tempo meta values as SMF tempo changes.

The independent converter decodes all 187 Pak sheets/3,633 frames, all 574 movies/10,614 commands,
all 278 sounds/4,279,281 sample bytes, and all 12 XMI tracks/12,253 native events. Exactly 573 movie
image sources resolve. Movie 10001 is the sole source-orphaned visual timeline; it is preserved and
reported rather than supplied with invented art.

## 68k CODE and DATA

The application contains CODE 0 plus 20 loadable Motorola 68020 big-endian segments. CODE 0 declares an A5 world with 32,248 bytes below A5, 8,384 bytes above A5, and the `_LoadSeg` jump-table stub.

CODE 1 contains the startup/runtime and the custom DATA decompressor. `tools/decode_data.py` reproduces its bitstream exactly:

- Declared decoded size: 22,989 bytes.
- Compressed resource size: 23,163 bytes.
- A5 materialized interval: `-19663..8380`.
- Materialized bytes: 19,432.
- Flattened audit image: 28,039 bytes.
- Three streams: below-A5 data, an empty middle stream, and above-A5 data.
- Zero-runs use `(mask + 1)` bytes; arbitrary-byte runs use `(mask + 2)` bytes.

Decoded A5 globals confirm the resource-type slots for `Img `, `MuV `, `Ply `, and `Pak `.

CODE 1 `$352C` is also the exact common random-range scaler. It sign-extends `_Random`'s returned
word, adds `0x7fff`, multiplies the zero-extended word limit with signed `MULS.L`, and divides the
signed product by 65,536 with truncation toward zero. The rare `_Random == -32768` case therefore
maps to bucket zero, not 65535. Seed 201098413 reaches that edge on its next draw; the native
self-test fixes it as a deterministic vector alongside the ordinary range and shuffle vectors.

CODE 16's exported movie-controller dispatcher bounds its word argument against the initialized
record count before indexing eight-byte `{text pointer, movie ID, sound ID}` records. CODE 12's menu
controller supplies 23 records at A5 `$-38EA`. Its randomized third idle branch sends either index
21 (a decoded `movie=0, sound=0` record) or 12061 (rejected as out of range), proving that branch is
an intentional audiovisual no-op rather than an unripped asset.

The disassembler now treats the non-CODE-1 header's longword at offset 8 as the absolute end of code, not a length. This avoids interpreting relocation/export metadata as instructions. It also decodes every unloaded A5 jump-table stub from CODE 0/DATA. The union of segment starts, 788 `LINK` prologues, direct internal `BSR`/`JSR` targets, and all 218 exported targets contains 1,299 structural routine entries; export recovery added 42 targets that the earlier prologue/direct-call scan could not see. See `work/disassembly/function_inventory.csv` and the zero-unaccounted disposition ledger at `work/audit/function-traceability.csv`.

## Proprietary Pak graphics

CODE 10 performs the outer decompression; CODE 3 renders the inner spans.

Outer stream:

- Big-endian 32-bit decoded length.
- Control bytes consumed least-significant-bit first.
- Control bit 0: one literal byte.
- Control bit 1: big-endian 16-bit LZSS token.
- Distance = `(token & 0x0fff) + 1`.
- Length = `(token >> 12) + 3`.

Inner stream:

- Flags/type word (`0x0002` or `0x8002`).
- Frame count, tag, then big-endian frame offsets.
- High flag adds signed frame origins.
- Span opcode bit 7 starts a new row; low five bits hold a short count or select a 16-bit count.
- Operations are repeat-group/end, transparent skip, color fill, and literal pixels.

The native `PakSheet` decoder and the independent Python decoder both decode all 180 sheets and all 3,166 frames without error using color table 1000.

The game's ordinary dynamic interface text is not a Macintosh system font. CODE 15 `$1C8`
maps each character through `ASCII - $20` (clamping unsupported values to frame `$1F`), measures it
through CODE 3 `$5C0`, and draws it through CODE 3 `$5B8`. A space advances by Pak frame `$49`'s
width, and player-name fields are truncated to eleven displayed characters. The recovered font
families are Pak 223/224/225 for large yellow/black/white glyphs and Pak 226/227/228 for small
yellow/black/white glyphs. The native `Canvas::pakText` path uses those shipped frames directly for
the bottom status strip, scoreboards, player names, Yacht scores, and the name editor. Deterministic
regressions pin representative source metrics (`MARIO` = 74 pixels, `MY FRIEND` = 117,
small `PLAYER` = 68, and small `10 20` = 39) and the eleven-character display limit.

Go Fish's small red card multiplicities are another authored bitmap path, not text. CODE 17
`$3416/$349A/$3588/$36EC` selects Pak 5006 frames 0–3 for counts 1–4 and places each frame at the
same origin as its Pak 5005 rank card. Native deal and live-hand rendering use those exact 12x14
frames; their combined raster is pinned by the executable self-test. Pak 5007's thirteen small
held-question actors use the same rank indices. Independent source frames now verify rank 5 Luigi
before a successful transfer and rank 10 Yoshi on Mario's immediate extra question afterward.

## Movie resources

The animation system uses three coordinated resource types:

- `MuV `: 44-byte movie header with mode, flags, origin, dimensions, duration, time scale, tick duration, and record counts.
- `Ply `: 16-byte commands with opcodes for markers, image/base-image layers, offsets, sounds, and end.
- `Img `: 12-byte placement/source-rectangle records.

The Macintosh `MuV ` origin/extents and first two `Img ` placement words follow QuickDraw's native
vertical-then-horizontal ordering. CODE 18 confirms the Yacht title actors at horizontal anchors
`-149` and `-208`: after decoding that ordering, movie 6100's upper boat ends at y=240 and movie
6150's lower hull begins at y=240. The same correction places the Macintosh movie 1125 final
menu-wipe cel at `(26,135)`, exactly over the matching pixels in Pak 1001.

CODE 14 registers all three Macintosh Dominoes title actors against one shared stage point, with
movie 3004 four pixels to the right. Inputs `(18,220)`, `(18,220)`, and `(22,220)` make the terminal
3002/3003/3004 bounds `(400,270)-(477,323)`, `(169,283)-(400,323)`, and
`(133,283)-(173,308)`: five joined dominoes followed by Yoshi. Treating each movie's complementary
horizontal origin as another caller offset scattered those layers and left only one partial domino.

The Yacht gameplay cast has a separate source registration from its title boats. Dialogue movies
use input `(15,-1)` (movie 11411 frame 3 is `(228,18)-(336,123)`) over Pak 6012 at `(26,0)`; both
computer-roll entry paths register full-body movie 6021 at the same `(15,-1)` input, whose wide
source-time-240 bounds are `(99,18)-(337,212)`. At source time zero the movie activates base frame
0 and overlay frame 1 simultaneously. Their shared destination is `(217,18)` on Macintosh and
`(119,9)` on DOS; drawing raw frame 0 alone drops Mario's left glove.
Pak 6010 frame 12's stationary cup is
`(218,129)-(296,228)`, and movie 6020 input `(0,-9)` gives its first live frame those identical
bounds. The controller suppresses the stationary frame from mouse-down through the movie and all
five settle passes, so the contact frame neither jumps nor doubles. These Macintosh values do not
replace the separately recovered DOS placements.

The DOS records deliberately differ: `MuV ` uses `x, y, width, height`, while `Img ` uses
`x, y, left, top, right, bottom`. This is proven across all 1,213 same-ID DOS Img/Pak pairs: every
decoded source rectangle exactly equals its Pak frame dimensions under conventional DOS ordering,
whereas 1,169 rectangles exceed their frames if QuickDraw ordering is incorrectly applied. Overlay
25 places movie 1125 at object coordinates `(46,56)`; after its `(1,1)` movie origin, the native
instance input `(45,55)` gives a base bound of `(46,56)-(151,187)` and final-cel bound of
`(55,77)-(139,153)`, centered within the authored easel. The movie's persistent base cel conceals
Pak 1001's already-complete menu until the flip timeline begins.

The DOS game-intro overlays expose a second registration distinction. Overlay 1 writes movie 4999's
actor point as `(-100,126)`, overlay 7 writes both Checkers points as `(-93,144)`, and overlay 17
writes the Go Fish points as `(0,120)` and `(0,90)`. Those control paths register the actors against
their MuV bounds before playback; the native direct renderer therefore pre-resolves the MuV origins
instead of applying them a second time. At the independently captured source instants this places
Pak 4999 frame 2 at `(108,126)`, Pak 2801/2800 frames 7/6 at `(24,145)` and `(119,144)`, and Pak
5101/5102 frame 10 at `(234,121)` and `(94,115)`. Dominoes overlay 13 and Yacht overlay 30 use their
movie origins directly and retain their separately proven inputs. Five complete-frame reference
checks now enforce all of these distinctions.

Timeline opcode 7 dispatches authored sound events. Native host playback treats a movie's
time-zero cue as its tracked speech line and later cues as concurrent effects, so multi-hit
animations do not cancel their preceding effects or extend speech-gated controllers. Movie 1111
contains seven cues at timeline times 120, 180, 420, 480, 540, 600, and 660. CODE 12
`$23C8-$2424` loads it for the title, writes `duration-1` to the control's time field, and enables
that terminal open-hand cel. It is therefore not advanced—and those cues do not fire—while Mario
speaks. The live Checkers menu-selection controller later owns the same movie and its authored cues.

The CODE 12 `$1F74` title controller is preserved as an eight-state sequence. In particular, its
five-count pause begins only after sound 5001 finishes; movie 12091 then owns the talking-title
stage, and sound 5011 plus the 1.5-second movie 1125 wipe run together after that movie completes.
This ordering avoids folding voice duration into the authored pause or holding movie 1125's first
title-picture frame over the live menu.

The controller constructs Pak 709 and 710 before the opening fifteen-count title hold, but Pak 710
remains under its black cast during that first stable frame. The retained run shows both Mario and
the complete board/easel outline as silhouettes. The native renderer therefore applies Pak 710's
alpha as an opaque black mask until the greeting begins; drawing its source colors at this point
would expose the finished board artwork before the authored reveal.

The same controller's mouse-down branch at `$1DDC` and Escape branch at `$1E14` clear the live-stage
flag and post callback `$2E0`; its natural completion at `$217A` posts that identical callback. The
native shell therefore routes either input through one completion path that stops the active voice,
installs movie 1111's terminal hand/easel cel, and enters the fully revealed menu. The preceding
publisher-card controller remains independent. A board-coordinate mouse-down is exercised by the
silent QA route and must hash-identically to a live-title skip, proving that an in-progress board
reveal cannot remain active or move backward after the click.

The selected-game title actors do not share one universal input rule. Backgammon CODE 11
`$24/$2A` routes both key-down and mouse-down directly to the `$1C4` completion post, and Yacht
CODE 18 `$20/$26` routes both to the equivalent post at `$228`. Dominoes CODE 14 `$32` instead
sends mouse-down through one `$9BE` controller advance while its `$370` key-down branch only
consumes the event. Checkers CODE 16 `$456/$7F4` and Go Fish CODE 17 `$3B4/$276` perform
game-local housekeeping/input tests without posting the title completion. The native port therefore
immediately skips only Backgammon and Yacht, advances Dominoes by one controller tick on a click,
and leaves Checkers and Go Fish title playback unchanged. Escape is not repurposed as a return-to-menu
shortcut while any selected-game title is active.

The preceding publisher controller at `$1B0A`/`$1B74` explicitly loads `$1C` (28) controller
counts for the Stepping Stone card.  At the same 100 ms cadence demonstrated by the title
controller's 15/2/5-count pauses, this is a 2.8-second hold; the original high-cadence startup
capture independently brackets its fade at the same point.

CODE 12's menu event handler posts its game destination at A5 `$-6F5C` instead of launching
directly. Controller `$1400` waits for the active `$D36` selection movie or idle actor to finish,
then starts tracked sound 5010 and performs the transition. Mouse clicks and Return therefore queue
the destination, while further hover and keyboard selection changes are ignored once it is posted.

The selected actor's resting point is not a shared early frame. CODE 12 `$ABC` assigns multipliers
10, 13, 10, 10, and 8 to movies 1111–1115; `$C34` multiplies each by the movie tick duration 60,
giving exact Checkers/Go Fish/Dominoes/Backgammon/Yacht source times of
`600/780/600/600/480`. Controller `$D36-$100C` hides the old selected-label control, advances the
outgoing movie from that hold to its common time-zero neutral hand, then moves the pointer one row
per pass in the signed target direction; it does not choose a shorter circular route. After the
target pointer holds for three post-incremented passes, the target label becomes visible and the
incoming movie advances from zero to its own hold. `$E8E` and `$F3E` resample a changed desired row
at the two pre-incoming boundaries and restart from the actual working selection, while state 5
finishes an already-started incoming actor before accepting another transition. Native menu QA
covers the immediate pressed-control composition, every intermediate pointer row, both retarget
boundaries, neutral interval, settled hashes, and exact actor holds; freezing every actor at an
arbitrary universal time or replacing a running actor on each hover would contradict the source.

Movies at IDs 10000 and above select the image sheet at the containing 1000 boundary. The catalog
contains 467 movies, 125 image sheets, 1,380 image records, and 8,586 commands. The native `Movie`
class parses every timeline and renders all 466 source-resolvable timelines. The remaining resource,
movie 11001, is a source-orphaned legacy artifact: CODE 3 floors it to base image table 11000, which
contains 11 records, while its timeline requests indices 0–54. Its separate `Img ` 11001 has no
matching `Pak ` and cannot be selected by the shipped loader, and neither the decoded A5 world nor
any CODE load site references movie 11001. The byte-identical resources remain embedded for
preservation, but manufacturing replacement frames would not reproduce executable source behavior.

CODE 3 `$62A` also establishes the exact compositor rule: it locates the commands active at the
requested source time and draws every active opcode 3–6 image in timeline order; it does not replay
expired images. An exhaustive interval audit finds continuous visual coverage in 461 of the 466
source-resolvable movies. The five gaps belong to authored title/game-stage actors entering or
leaving their scene. The shared Pak 10000/11000/12000 talking-head cels are complete images (not
mouth-only deltas), so the native per-frame redraw follows the original rule without retaining
historical head pixels.

## Audio

All 313 `snd ` resources use Sound Manager format 1 or 2 command lists with uncompressed 8-bit mono
`SoundHeader` payloads. `Audio::makeWave` wraps these bytes as RIFF/WAVE in memory for concurrent
WinMM `waveOut` voices. CODE 12 starts the publisher cues directly: sound 8038 accompanies the
BrainStorm transition and sound 8042 accompanies Stepping Stone. Its main-menu `$1032` controller
also routes sound 5004 with actor 500's Pak-1011 shoe cycle and sound 5006 when actor 1500 is hidden
for movie 1101's bow-tie spin; `$12F8` schedules the separate silent actor-471 blink overlay.

CODE 1 `$B22` tests the Sound Manager enable flag and the current direct channel before returning
busy; `$CAA` submits a priority-`$8000` effect to that scheduler. CODE 12 `$EE4-$F00` uses this pair
for snd 5003 after a pointer row changes. The sample contains 1,152 frames at 11,025 Hz, which rounds
up to a 105 ms busy lifetime, longer than three 33 ms controller passes. The native audio layer now
tracks that lifetime separately from speech and unions the two states for the source query, so
non-adjacent menu travel cannot pile up one click per row while the preceding click is still audible.
The complete caller scan contains fifteen sites: nine in CODE 12 (`$EE4`, `$1118`,
`$1522/$1552/$1582`, `$203C/$2064/$208A/$2132`), five in CODE 14
(`$E42/$F2A`, `$1174`, `$1528`, `$19C6`), and one in CODE 17 (`$139A`). Their native counterparts
respectively cover menu UI/launch/title, Dominoes deal/result/reset/move selection, and the
standalone Go Fish 26015 response gate; `docs/AUDIO_AUDIT.md` records the complete mapping.

All 11 `Midi` resources are standard format-0, one-track files with a 480-tick division. The native
sequencer handles variable-length deltas, running status, tempo meta-events, channel messages, and
looping, then sends short messages to WinMM. The audit parses 6,968 channel events. The recovered
`SONG` mapping is menu 900; gameplay 910/904/912/906/908; and corresponding player-win music
911/905/913/907/909 for Backgammon, Dominoes, Checkers, Go Fish, and Yacht. Music enable/disable
preserves this requested track instead of resuming a guessed numeric neighbor. WinMM MIDI-device
opening is prewarmed asynchronously and requested tracks remain queued until it completes, avoiding
a machine-dependent UI-thread stall at the publisher-to-title transition.

The 467 movie timelines contain 666 authored sound events. Five reference absent source sounds
23019-23023; the shipped resource fork contains none of them. Movie 11093 instead contains valid
sound 8046 ("Yoshi or Koopa?"), which the native first-game chooser uses. Direct gameplay calls are
also mapped to their recovered resource IDs: Backgammon selection/movement/roll effects, Dominoes
deal/place/draw/error effects, Go Fish deal/request/draw effects, Checkers move/wipe effects, and
Yacht roll/settle/select/score effects. The full mapping and verification boundary are recorded in
[`AUDIO_AUDIT.md`](AUDIO_AUDIT.md).

## PICT resources

PICT 128 (title) and 129 (credits) convert directly. CODE 5 draws PICT 128 at its exact 447x215
size, then overlays text. Its port-relative values recover center x=265, initial baseline 65,
Times-14 line height plus two pixels, a 240-pixel divider after the fourth line, and center x=75
for the lower copyright/version block. `_RGBForeColor` receives source yellow and 30% gray A5
constants. The disk's System 7 suitcases contain the exact Times-14, Geneva-9, and Monaco-12
one-bit `NFNT` strikes; `tools/extract_source_fonts.py` verifies and extracts them by resource ID
and hash. The native renderer draws those glyph masks directly, including QuickDraw's one-pixel
synthetic bold smear, and reproduces the literal `v. ` prefix plus `vers` 1's short `1.1` string.
The complete 512x384 About raster is pinned in the executable self-test.

PICT 400–409 are help/instruction documents that mix QuickDraw text/vector commands with PackBits rectangles. General-purpose raster converters rejected this mix, but Deark 1.7.3 parses all ten and extracts 58 embedded raster components. The full vector/text resources remain byte-identical in the embedded pack. The nine help pages actually presented by the game were also captured from their source QuickDraw rendering and embedded as exact 486x350 modal panels for native F1 navigation.

## Gameplay findings

The ongoing control-flow audit records user-visible behavior by CODE-segment offset so native
implementations remain evidence-backed rather than inferred from modern versions of the games.

- Go Fish CODE 17 `$151A` contains two 13-entry rank-suffix movie tables. The question builder at
  `$402C` calls `Random(4)`: one branch in four plays movie 11546 ("Do you have any") followed by
  suffix table two; the other three branches uniformly choose movie 11575, 11576, 11545, or 11557
  and use suffix table one. The native question builder reproduces that table and probability split.
- Go Fish's `$188` movie-index table and `$190` shuffled selector expose the rest of its authored
  speech routing. Opening indexes 78, 79, 80, and 26 select movies 11600, 11603, 11003, and 11526,
  followed by fixed index 29 (movie 11529, "I'm-a go first"). Separate shuffled pools drive
  thinking, praise, success, fishing, turn, idle, frustration, and win responses. The thinking
  record (indexes 43/44) is present but `$11F8` guards it with a nonzero result from `Random(1)`, so
  the shipped path never selects or plays it; the native controller preserves the RNG advance and
  leaves that pool untouched. The failed Mario request also uses standalone sound 26015 before its
  fishing animation. The reachable pools and their queue order are reproduced natively instead of
  substituting generic lines.
- Go Fish CODE 17 `$4616` numbers the 52-card deck as four consecutive cards per character rank.
  `$467A` then performs 300 pairs of `Random(5200) / 100` index draws and swaps. Positions 0–6 are
  dealt to the player, 7–13 to Mario, and 14 onward become the forward draw pile. The native deck
  now preserves that exact numbering, random-call count, swap sequence, deal partition, and draw
  direction; a seed-one 14-card deal vector is part of the executable regression suite.
- Go Fish does not compact and recenter a vector of unique ranks. CODE 17 `$371A-$3852` creates
  thirteen persistent ten-byte rank records containing count, rank, active state, and a QuickDraw
  point. Pak 5005 is 54x76. The seven primary records use the rotated source positions
  `(104,281)`, `(163,281)`, `(222,281)`, `(281,281)`, `(340,281)`, `(399,281)`, `(45,281)`;
  records 7–12 form the overflow row at y=200. `$30F6/$316E/$3862` merge later opening duplicates
  into first occurrences and settle the surviving records. Later transfers and books clear their
  record without recentering the other ranks, and `$308A` adds a new distinct rank to the first
  inactive record. Native rendering and hit testing now use those records directly. Deterministic
  tests cover merging, four-card removal, first-free reuse, overflow coordinates, exact 54x76
  boundaries, and holes; muted captures cover both the grouped opening hand and the post-transfer
  gap seen in the source trace.
- Go Fish's neutral head cannot be drawn from Pak 5090 frame zero alone. That image is movie 5090's
  base layer and deliberately leaves transparent eye/eyebrow holes for its subsequent animation
  layers. The complete neutral head is Pak 11000 frame zero, placed at Macintosh `(202,18)` or DOS
  `(126,9)` over the fixed Pak 5300 torso. Native idle and question-card-transfer captures hash the
  face region in both editions so the page background cannot leak through it again.
- DOS Pak 5001 leaves the score captions blank: the original resident strings at executable file
  offsets `0x39905`, `0x3990B`, and `0x39911` are `MARIO`, `CARDS`, and `BOOKS`. The retained
  320x200 source frame fixes their Pak-223 raster origins at `(15,19)`, `(15,44)`, and `(15,35)`;
  the right captions repeat at x=230, with numeric columns at x=60 and x=275. The player name is
  left-aligned at `(230,19)`. Two native scoreboard hashes plus a narrow independent-source
  comparison now gate those runtime layers separately from the broad random-gameplay comparison.
- Go Fish's strategy routine at `$484A` runs with difficulty word two. `$48FA` calls the range
  helper with the adjacent memory-count word, which `$4616` clears and never increments; this is a
  `Random(0)` that always yields slot zero but still advances the QuickDraw seed. It then consults
  remembered ranks requested by the player, skips ranks in a 30-entry Mario request history, scans
  held cards cyclically from `$47AE`'s random card, and falls back to a random held rank only after
  history exhausts the choices. It records the preliminary selection before retrying the previous
  rank up to five times, then removes the final rank from player-question memory. The native port
  now preserves the discarded zero-range call as well as every conditional held-card draw. The end
  test at `$4DCA` activates as soon as the deck is empty and compares book counts even when both
  hands still contain cards. Both behaviors have executable regression coverage.
- Go Fish's `$1B88` idle controller is armed after 120 source ticks. Its persistent parity counter
  begins with the `$CD4` lazy selector (indexes 61/62, movies 11561/11562), then alternates with the
  `$C66` five-entry visual selector `{5094,5091,5090,999,999}`. Visual movies are placed at source
  origin `(19,196)`; movie 5090 has no timeline sound, while the two nonexistent movie-999 entries
  simply restore the normal cursor and start another 120-tick wait. The formerly inferred `5300`
  is a CODE 14 subroutine address, not a `snd ` resource. Both selector cursors and the parity counter
  survive an in-session replay, matching the module globals.
- Go Fish's game-over state machine at `$1FB4` maps `$4DCA` result two to the two-line player-win
  pool and result three to the four-line Mario-win pool; a tie selects neither. After its outcome
  line drains, only result two enters `$209E`'s card celebration. Pak 5211 supplies the seven
  Y-O-U-W-I-N-! faces, which are placed at `(161,200)`, `(220,200)`, `(279,200)`, `(135,281)`,
  `(194,281)`, `(253,281)`, and `(313,281)`. `$2424` then starts seven movie-5210 card flips at
  those positions, waits forty controller ticks, and reaches index 71 (`MuV ` 11571, “How about
  another game with me?”). Mario-win and tie results instead jump to state 30 after a two-tick
  pause and deliberately skip both the card display and replay question. The native controller now
  keeps the Play Again dialog out of all three paths until their source sequence is complete.
- Yacht CODE 18 `$23EA` dispatches score categories. Its category routines establish Yacht = 50
  (`$2508`), Big Straight = 30 (`$253A`), Little Straight = 25 for any four consecutive values
  (`$259C`), Four of a Kind = dice sum (`$2622`), Full House = dice sum (`$2670`), Choice = dice sum
  (`$2702`), and upper categories = matching-dice sum (`$271C`). These differ from common modern
  Yahtzee scoring and are covered by native regression tests.
- Yacht's `$19E2` game-over controller calls `$24C4`, whose result one is a tie, result two a player
  win, and result three a Mario win. The corresponding speech is fixed index 42 (`MuV ` 11442), an
  equal choice between indices 44/45 (11444/11445), or an equal choice between indices 39/43
  (11439/11443). This corrects the former native mapping to unrelated movies 11448/11452.
  After the player line, `$1B16` reveals dice 1–5 one per controller pass and starts movie 6022
  immediately after the fifth, a 3.5-second
  source celebration whose timeline fires sounds 5062, 5061, and 5008. Tie and Mario-win paths
  instead wait nine controller ticks. All three then play index 53 (`MuV ` 11453), the replay
  question, pause two ticks, and clear the twelve score rows twice each through `$1D52`, with the
  source click effect on even rows, before a final nine-tick pause. The native Play Again dialog is
  now gated on completion of that full state machine.
- Yacht's die loop at `$1978` draws below 30000 and divides by 5000 before adding one. Its player
  and Mario result-pool choices at `$1A66`/`$1A8E` draw below 2000 and divide by 1000. These exact
  bucket calculations now replace generic six-way/two-way library distributions.
- Yacht's host record array resolves all 74 source dialogue indexes, including cross-game movies
  11709–11711 and 11724–11729. `$318C` seeds the four-joke cursor with a below-4000 draw when the
  module loads. `$1018`/`$10EC` wait 80 source ticks, choose an ordinary line for three of four
  below-4000 buckets, and otherwise increment that cursor through the Pizza, Jamaica, Giovanni,
  and Yucca five-part tables at A5 `$-1E4`; every part has a five-tick gap. `$12A0` then chooses
  38/37 while rolls remain or 12/37 after the third roll with an equal below-2000 draw.
- Yacht's `$190` records at A5 `$-1A4`, `$-18C`, `$-174`, and `$-15C` are lazy shuffled selectors,
  not raw strings. They contain indexes 30/29/29/31 (computer thinking), 21/24/25 (Mario worried),
  48/46/47 (praise), and 51/52 (friendly praise). `$15C8` enters the thinking selector for two of
  three below-3000 buckets after every Mario roll, including the final one. `$16FE` draws below
  4000 only for a
  noninitial adviser reroll with at least two white dice—including an all-five reroll—and speaks
  index 34 in its first quarter. Filled
  score lines use `$836`'s equal index-13/14 choice; qualifying player scores use `$11EE`'s equal
  praise/worried selector choice only while the player's running total is ahead.

- Yacht's score-line input is the twelve-frame Pak 6011 controller created at `$2F22`, not the
  similarly shaped Mario scorecard on the left.  Its 96x20 player controls begin at logical
  `(378,54)` and advance by twenty pixels.  `$30EE` likewise stores displayed score positions at
  `60 + 20*row`.  Native hit-testing now uses those exact right-card rectangles and score rendering
  keeps the same twenty-pixel pitch instead of drifting upward by one pixel per row.

- Yacht rounds are owned by the player controller, not Mario's score routine. `$126E` advances the
  round only after the player's reaction speech and dice clear; Mario therefore opens each round,
  both scorecards receive all twelve entries, and the twelfth player score triggers `$19E2`.
  Mario's `$176A` path speaks its lead-in and category name first, pauses two source ticks, commits
  the score and effect 5003 at `$1848`, removes the green markers, waits nine ticks, and only then
  clears the dice for the player handoff.

- Yacht's dice are two distinct Pak 6010 control lanes. `$2B42` places flag-zero white dice at
  `(154,261)`, `(195,261)`, `(236,261)`, `(277,261)`, and `(318,261)`; `$2C40` places flag-one red
  dice at the same x coordinates and y 284. `$2A8E` removes only flag-zero dice before a roll, so
  red means held and white means reroll even though the shipped index-16 wording says to select
  dice “to roll again.” The source permits all five white dice to be rerolled, but player state
  `$113C` rejects an all-red selection because `$227C` finds no white die; only that invalid path
  plays dialogue index 16. A normal completed roll does not. Its settling loop
  skips all held flags in the same controller pass and reveals one regenerated white die per pass
  while red held dice stay visible. `$215C` also
  draws the remaining-roll markers from frames 13/14: Mario's green markers at `(147+27*i,182)`
  and the player's yellow markers at `(409+33*i,328)`; each marker is removed when its roll starts.
  Mario's `$1560` selection controller applies adviser changes in die-index order, playing 5028
  and waiting five source ticks after every actual white/red transition. Only after those changes
  and optional index-34 speech does movie 6021 perform the hand gesture for the next pipe roll.
  Computer state `$D84` uses that 6021 gesture before the first roll of every round as well as
  rerolls, so its authored 5044 cue and direct effect 5010 always precede pipe movie 6020/5018.
  Red/white state remains visible through Mario's score announcement and is cleared at handoff.

- Dominoes CODE 14 `$2EFE` enumerates legal placements in hand order, left
  chain end before right, with each eight-byte record storing the hand index,
  side, matched pip, and newly exposed pip.  The computer path at `$1870`
  selects the first candidate with the smallest newly exposed pip.  It then
  chooses randomly between ends only when the selected tile fits both.  The
  native port uses this ordering directly and exercises low-exposure,
  reversed-orientation, hand-order tie, and no-move regressions.
- Dominoes does not use CODE 13's generic shuffle for its tiles. CODE 14 `$852` makes three full
  passes over the canonical `00,01,...,66` 28-record array, swapping every position with an
  independent `Random(28)` position for 84 calls. It then checks positions 14–27 for a doublet;
  if that dealt half has none, it moves the first doublet from positions 0–13 to a random position
  14–26. The deal at `$E4E` consumes the array backward, giving position 27 to the player, 26 to
  Mario, and alternating until both hold seven. `$DCA` and `$2E9A` make the deal and highest-double
  opening speech choices. The native engine reproduces that call order, partition, direction, and
  correction branch, with seed-one and correction-seed deck vectors in the executable self-test.
- The deal controller's `$DCA` first-round index 2/3 pair resolves to movies 10002/10003; later
  rounds select index 4/54, movies 10004/10065. After `$2E9A` compares the highest doublets,
  Mario-first selects index 68/5 (movies 10084/10005) at 75/25, while player-first selects index
  6/8 (movies 10006/10008) at 50/50. All eight routes and both asset dialects are release-tested.
- Dominoes' ordinary-turn dialogue is not a fixed "my turn/your turn" pair. `$169E` consumes a
  value below 100 and uses voiced index 10 only for values 70–99. `$1CAC` consumes below 11 before
  each Mario move or draw; its first eligible 0–5 result plays index 22 and consumes a second
  below-5 value for a 3–7-tick thinking pause. Later branches select move comments from hand-state
  dependent pools, retry after a draw when no line was selected, and suppress the previous source
  index. Each unsuccessful repeated draw still consumes the otherwise discarded below-11 value.
  When the boneyard is empty, `$1810` follows that selector draw with a 50/20/10/20 choice among
  indices 34/33/32/35. `$2112` consumes below 500 on every player handoff but voices indices
  8/9/11 only in the first 45 values before the player has drawn. `$23DA` voices index 66 for 69%
  of first draws and uses dynamic slot 1 otherwise; later draws consume no RNG. `$2D80` consumes
  below 11 for every non-winning player move, switches pools below three remaining tiles, and
  suppresses the last index. The native turn controller reproduces these calls and defers each
  handoff until active speech drains, with deterministic coverage of thresholds, discarded draws,
  thinking delay, and repeat rejection.
- Dominoes' `$1E10` idle controller starts at 120 ticks. `$1EBA` sends values 0–29 to index 72,
  30–49 to index 73, and 50–99 to an increment-before-use cycle through four five-part jokes.
  Joke parts have five controller ticks between them; completion installs `$1FF8`'s random
  60–309-tick wait. The twelve below-4 initialization swaps are retained because they advance the
  shared QuickDraw seed before the deck shuffle even though the shuffled selector is not read by
  gameplay.
- Dominoes' result controller begins at `$0F7E`. A blocked chain first chooses source index 29 or
  30, shows the two pip totals with index 48, restores neutral index 0, and selects the result from
  49/45/46 (Mario), 50/42/44 (player), or fixed 51 (tie), with the source's 15/15/30/15-tick
  pauses. A last-tile Mario win uses 47/45/46; a player win uses the six-line
  20/20/20/20/10/10 selector and then a first- or later-game four-line comment pool. Every path
  converges on the equal 60/61 replay selector. The native state machine and five branch
  regressions preserve these paths and gate the Play Again dialog until the replay line drains.
- Checkers CODE 16 stores the 32 playable squares in row-major order and gives every square four
  one-step and four two-step links ordered up-left, up-right, down-left, down-right (`$47F0`).
  `$3FBE` recursively enumerates complete jump paths before `$3DC4` makes a second board pass for
  ordinary moves; the forced-capture flag suppresses ordinary and partial-jump candidates. `$40DA`
  scores two points per captured man, two additional points per captured king, four for crowning,
  and 300 for eliminating the opposing pieces, then subtracts the best opposing reply through the
  configured depth. `$3DC4` keeps a higher score and replaces an equal score only when a 0..255
  random draw exceeds 127. The native engine now ports that generator, minimax recursion, scan
  order, late-endgame depth increase, and tie rule instead of its former one-ply heuristic.
- Checkers CODE 16 initializes a 91-slot host table whose first two slots are runtime-only; source
  dialogue index 2 is therefore `MuV ` 11002. The game-over controller at `$0E98` waits five ticks
  and sends an eliminated/stuck Mario loss through two distinct paths. `$0FB0` draws equally from
  indices 50–57, 43, and 45 for a player elimination win, then `$0FCE` clears one occupied square
  per controller pass with sound 5003. If Mario still has pieces but no legal move, `$1164` uses
  fixed index 49 (`MuV ` 11636, “I can't move anywhere”) and skips that wipe. A first Mario win at
  `$113C` uses index 48 (`MuV ` 11058, “I won”); later wins select 48/57/47/52/46 equally. All paths
  wait six ticks and `$1196` chooses replay index 59 or 58 (`MuV ` 11074/11073). The native ending
  controller covers all four branch variants, and the earlier 80-quiet-ply draw rule—absent from
  the shipped controller—has been removed. With Forced Jumps disabled, the source generator exposes
  both a partial capture and the completed multi-jump path rather than forcing continuation. The
  Play Again path resets the board in place so the first/later Mario-win counter remains live;
  Dominoes likewise advances its first/later-round counter instead of rebuilding the game object.
- Checkers' idle controller at `$27BA` starts with a 120-controller-tick target. `$2884` draws below
  100: values 51–99 select the CODE 13 shuffled 9000/9001/9002 full-body movie pool, while values
  0–50 make a second draw. That second draw selects dialogue index 73 below 30, index 74 from
  30–49, or advances cyclically through the four five-part joke records from 50 upward. Joke parts
  are separated by five controller ticks. A visual completion installs a 500-tick target; a line
  or complete joke uses `$2B62` to install `Random(250)+60`. The native state machine reproduces
  these branches, delays, shuffle calls, cycle order, and the source's explicit (+3,-2) visual
  adjustment. Four deterministic seeds cover every first-level branch and the hidden shuffle calls.
- Yacht's adviser begins at CODE 18 `$3596` and internally orders categories as Aces through Sixes,
  Four of a Kind, Full House, Little Straight, Big Straight, Yacht, then Choice. `$36F2` handles
  completed-category priorities. `$37A2` then tries triples, the first longest consecutive fragment,
  two pairs, the higher pair, and open upper faces in that exact order; `$3DB0` removes the first
  copies of duplicate straight values so the last source-order copy remains. Final scoring at
  `$3AA0` keeps the first category on equal adviser scores and can deliberately sacrifice the first
  open Aces/Deuces line when the best remaining positive fallback is category four or later. The
  native adviser now ports these order and edge rules directly, with regressions for duplicate
  straights, endpoint retention, low-line sacrifice, and Choice's 20-point cutoff.
- Backgammon CODE 11 does not score whole boards with numeric weights. Its move dispatcher at
  `$161A` calls an ordered family of selectors: bar entry at `$18D4`, point making and blot
  consolidation at `$1A90`, safe destinations at `$1C58`/`$1D1E`, hits at `$1BD0`, contact/fallback
  play at `$1D76`, reinforcement and doubles handling at `$1E90`, and bearing off at `$1FB6`.
  The precise non-home order is `$1A90`, `$1E90`, `$1C58`, `$1BD0`, `$1D76`; all-home play omits
  `$1C58` and begins with `$1FB6`. Thus a safe rear move can intentionally outrank an available hit.
  The controller around `$136A` calls `$161A` again after every individual move with the updated
  board and remaining dice; it never prefilters candidate first moves through a maximum-length turn
  generator. Point scans and larger-die-first tests are preserved after mapping the source's reversed
  numbering onto the native board. This also preserves `$1FB6`'s unusual last-checker behavior: a
  high oversized roll is used immediately even when moving with the low die first would consume both
  dice. Native self-tests cover bar hits and own-blot entry, point making, safe/fallback play, doubles,
  exact bearing from a stack, ordinary blot hits, and that final-checker edge.
- Backgammon does not begin directly on a fully painted board. `$DD8` spends two controller passes
  installing and presenting the game, then lazily shuffles A5-`$3B52`'s indices `[3,4,0]` and starts
  one of `MuV ` 11603 (“Let's play!”), 11604 (“Good luck!”), or 11600 (“Nice to see you again!”)
  on the third pass. In the Macintosh CODE 11 table, `$EFE`'s fixed host-table index 58 resolves to
  movie 11618 (“Let's roll to see who goes first”). `$F2C` tests the old value of its two-count
  word, so values 2, 1, and 0 occupy three more passes; `$F50` consumes a separate setup-initialize
  pass, and the following pass enters `$F78`. The native startup regression proves that entire order,
  the lazy shuffle's exact seed advance, and the absence of prematurely visible checkers.

  DOS overlay 0 has a different 59-record table: indices 0-57 map in order to movies 11600-11657,
  while index 58 is movie 11093 (“Do you want to play as a Yoshi, or as a Koopa?”). The overlay calls
  index 58 at `$0C45` for the first character chooser and separately calls index 18, movie 11618, at
  `$2BC9` during the opening-roll controller. `tools/verify_dos_dialogue_tables.py` pins the complete
  table bytes and both call sites so the two edition-specific indices cannot be conflated.

  The DOS first-use controller constructs the selected game before this call. The question therefore
  runs over the live game's Mario/scoreboard/status composition, not a bare tiled page, and the Pak
  101 choice panel is revealed only after movie 11093 ends. Backgammon and Checkers use the centred
  host registration `(116,1)` in native DOS coordinates; Dominoes uses its score-portrait registration
  `(7,3)`. The subsequent name panel remains over the same preview for all five games. Native preview
  construction records and restores the shared random seed before creating the playable instance,
  preventing the first deal, board, or lazy dialogue selector from being consumed twice.
- Backgammon's initial board is not painted in one pass. `$F78` scans the eight nonzero six-byte
  records at A5-`$3ACE` in source-point order: `(1000,0,2)`, `(2000,5,5)`, `(2000,7,3)`,
  `(1000,11,5)`, `(2000,12,5)`, `(1000,16,3)`, `(1000,18,5)`, and `(2000,23,2)`.
  After the native `(point + 12) % 24` mapping these are points 12, 17, 19, 23, 0, 4, 6, and 11.
  `$1078` starts sound 5042 once for the new stack; state one then exposes exactly one checker on
  each following controller pass. A zero-count pass closes the record and the next pass starts the
  following stack. The native renderer now keeps a setup-only visibility array instead of showing
  the final logical board immediately. Its executable regression proves all 30 checker transitions,
  the eight stack-start passes `1,5,12,17,24,31,36,43`, and completion on pass 46.
  `$558A`/`$5756` then retain all one-to-fifteen checker actors on a point rather than collapsing
  counts above five. Their `$5A82`/`$59A6` geometry helpers use the two decoded 24-entry egg/shell
  coordinate tables, ten/eleven-pixel vertical spacing inside each group of five, the source's
  point-dependent horizontal lean, and a two-pixel sideways offset for the second and third groups.
  Native rendering and checker-motion endpoints now share those exact coordinates; a regression
  fixes all eight opening bases plus representative fifth, sixth, tenth, eleventh, and fifteenth
  actor positions on each board orientation.
  When no selector can consume a remaining die, `$1442` transfers through `$214A`; the latter draws
  without repetition from movie indices 13, 14, and 17 (`MuV ` 11613/11614/11617) before control
  returns to the player. The native turn controller now retains that three-line shuffled branch.
  On an ordinary completed Mario turn, `$15B4` draws a value below 200 and divides it by 100. The
  two quotients select `MuV ` 11611 ("It's your turn") and 11616 ("Your roll") equally. The dice
  controller similarly retains its authored below-600/100 and below-1200/200 bucket calculations.
  `$1320` is the live non-double Mario-roll branch after `$21E6` has begun the player's first turn.
  A5-`$3B3A` contains indices 32, 31, and 30, so it draws without repetition from `MuV `
  11632/11631/11630: “Ooo! You've got Mario thinking now!”, “Hmm...”, and “How about this
  move!” The praise indices 5, 7, and 9 (`MuV ` 11605/11607/11609) instead belong to A5-`$3B22`
  and `$121C`'s necessarily unreachable branch: `$11A6` requests a random value below 100 and
  immediately divides it by 100, so its state can never become -2. The native controller preserves
  that distinction rather than routing the unreachable praise pool into ordinary Mario rolls.
  The game-over controller uses two equal-probability pools: `$3BB6` selects 11642/11646
  ("Congratulations"/"You won") for a player win, while `$3B88` selects 11643/11644
  ("Mario wins this time"/"That was a lot of fun") for Mario's win. Both converge at `$3CF0` on
  movie 11640, the voiced request to play another game. The paths do not converge immediately:
  after the player announcement, `$3C2C` starts movies 4022 and 4023 simultaneously at offsets
  `(0,15)` and `(414,47)`, halves both timeline tick intervals, and lets 4022 fire sound 5074 at
  timeline time 420. Mario's-win path instead installs a twenty-controller-tick pause. The paired
  movies or Mario delay are followed by the shared two-tick pause, replay question, and final
  two-tick settling state before cleanup. The native controller and executable regression preserve
  both branches and withhold the Play Again dialog until this sequence is complete.
  `$2588` is a one-shot idle controller started after a 200-count wait. Its initial value below 200,
  divided by 100, chooses equally between a full-body movie and a spoken line. `$2606` maps a value
  below 300 to nonrepeating movies 9000/9001/9002. `$26AC` independently maps the same buckets to
  nonrepeating indices 38, 39, and 37: "Hey, what's taking so long," "You still want to play," and
  "Mamma mia." Completion restores the 200-count wait. There is no Backgammon knock-knock branch;
  the native controller removes that earlier false reconstruction and has deterministic coverage
  for both paths and the rejection loop. The opening, `$1320`, and `$214A` selector arrays likewise
  remain unshuffled until their first CODE 13 cursor wrap instead of consuming RNG calls during
  reset.

## Shared and late-loaded compatibility segments

- CODE 2 contains no hidden game engine. Its first export samples an inclusive line between two
  points into a relocatable counted Point array; the remaining exports allocate, index, count, and
  dispose counted Long arrays. Native animation paths and game collections use typed vectors with
  the same inclusive endpoint behavior.
- CODE 5 builds the source PICT-backed title/credits/about windows, lays out centered styled text,
  reads the short version string from a `vers` resource, starts sounds 5057 and 5072 for PICT 128
  and 129 respectively, and owns the picture-window cleanup path. The native path uses PICT 128's
  exact 447x215 extent, recovered QuickDraw coordinates and A5 colors, source System 7 bitmap
  strikes, the literal `v. 1.1` label, and a whole-panel raster regression. PICT 129, the return
  controller, and PE window lifetime replace only the platform window-management calls.
- CODE 6 is the common controller behind Pak 100/101/105/106. It manages the source name-entry,
  Yoshi/Koopa, New Game, and Play Again panels; keyboard shortcuts and Return handling; field
  drawing and cursor state; and a two-level event-handler override stack. The native `App` owns the
  same modal states directly and keeps automated QA modes silent.
- CODE 8 contains the bundled music/sound runtime, not game rules. It initializes `MPLR`/`MDRV`,
  loads `SONG`/`Midi`/Sound Manager resources, maintains timer and channel queues, handles note and
  controller traffic, and disposes driver state. CODE 13 supplies its song-volume and current-track
  control plus generic shuffle, rectangle, update-region, and delay helpers. `Audio`, `Movie` sound
  events, typed shuffle pools, `Canvas`, and the Win32 event loop are their native replacements.
- The shared selector at CODE 1 `$352C` calls QuickDraw `_Random`, adds 32767 to its signed word,
  multiplies by the caller's unsigned 16-bit limit, and returns the product's high word. The
  underlying source seed recurrence is `randSeed = randSeed * 16807 mod 2147483647`, with the
  signed low word returned to the caller. CODE 13 `$2AC` uses that selector in a descending
  Fisher–Yates swap, while `$6CC` advances the initial seed `TickCount & 0x3ff` times. Native
  `SourceRandom` ports all four details; deterministic sequence, range, and shuffle vectors run in
  every release self-test. All five games now share it instead of C++ library-specific random
  distributions or `std::shuffle`.
- CODE 15 negotiates color-device depth, enumerates graphics devices, centers windows and modal
  dialogs, lays out the Pak-backed centered text described above through CODE 3 `$5B8`/`$5C0`, and
  presents warnings when a requested mode is not available. The native renderer always owns a 32-bit buffer and uses monitor-aware placement,
  avoiding changes to the user's Windows display mode.
- CODE 4 is a single 40-byte `GetPixBaseAddr` compatibility wrapper. On older QuickDraw it follows
  the PixMap handle and returns the classic `baseAddr` field at offset two; on newer systems it
  sends selector `0x00040017` through the `_QDExtensions` trap. Native `Canvas` objects own their
  32-bit DIB storage directly, so neither relocatable PixMap handles nor that OS-version branch is
  needed.
- CODE 7 manages the original Preferences-folder resource file. It creates or opens the file,
  loads `mPRF` 128, migrates records older than version four, and writes it back on shutdown. The
  394-byte version-four record stores Animated Pieces at offset 2, Hide Background at offset 4,
  Music at offset 6, Sound at offset 8, and a Pascal player name at offset `0x8A`; names longer than
  18 characters are rejected, while CODE 6's live editor caps entered names at 15. The native port
  applies the live 15-character cap and maps those persistent choices to per-user Windows Registry
  values. Forced Jumps and Yoshi/Koopa choice are intentionally not added to this
  persisted set because the source record does not contain them.
- CODE 20 is not game logic. Its first two exports implement the classic `SysEnvirons`/`Gestalt`
  compatibility layer (including the `vers`, `mach`, `sysv`, `proc`, `fpu `, `qd  `, `kbd `,
  `atlk`, `mmu `, `ram `, and `lram` selectors). The remaining exports wrap File Manager parameter
  blocks, `STR#` lookup, and old/new `FindFolder` behavior. Native Win32 loading, resource lookup,
  filesystem paths, and typed process state replace those OS-version branches.
- CODE 21 is the compiler's string utility segment: null-terminated length/copy/append, C-to-Pascal
  and Pascal-to-C conversion, and recursive signed integer formatting for an arbitrary radix. The
  port uses bounded C++ strings and native number formatting instead of carrying these routines.
- CODE 22 chooses the main or largest-intersection graphics device, converts port bounds between
  local/global coordinates, clamps a window to minimum/maximum dimensions, moves it onto the chosen
  display, and scales rectangles with QuickDraw fixed-point helpers. `updateViewport`, Win32 window
  placement, and DPI-aware nearest-neighbour presentation are the native equivalents.
