#include "app.hpp"
#include "dos_app.hpp"
#include "launcher.hpp"
#include "about.hpp"
#include "audio_catalog.hpp"
#include "games/backgammon.hpp"
#include "games/checkers.hpp"
#include "games/dominoes.hpp"
#include "games/go_fish.hpp"
#include "games/yacht.hpp"
#include "menu_catalog.hpp"
#include "movie.hpp"
#include "resource_ids.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <utility>
#include <windows.h>

namespace {

int selfTest(HINSTANCE instance) {
    // Deterministic vectors from QuickDraw's documented seed recurrence and
    // the CODE 1 $352C scaler recovered from the shipped 68K instructions.
    mf::SourceRandom sourceSequence(1);
    constexpr std::array<std::uint32_t, 3> expectedSeeds{
        16807U, 282475249U, 1622650073U};
    constexpr std::array<std::int16_t, 3> expectedWords{
        16807, 15089, static_cast<std::int16_t>(-21287)};
    for (std::size_t index = 0; index < expectedSeeds.size(); ++index) {
        if (sourceSequence.next() != expectedWords[index] ||
            sourceSequence.seed() != expectedSeeds[index]) {
            throw std::runtime_error("QuickDraw random sequence does not match the source");
        }
    }
    mf::SourceRandom sourceRange(1);
    if (sourceRange.below(6) != 4 || sourceRange.below(6) != 4 ||
        sourceRange.below(6) != 1) {
        throw std::runtime_error("CODE 1 random-range scaling does not match the source");
    }
    // Seed 201098413 reaches the signed-low-word value -32768 on its next
    // QuickDraw draw. CODE 1 $352C's signed multiply/divide returns bucket
    // zero; unsigned widening used to produce the impossible index 65535.
    mf::SourceRandom sourceSignedMinimum(201098413U);
    if (sourceSignedMinimum.below(6) != 0 ||
        sourceSignedMinimum.seed() != 1869250560U) {
        throw std::runtime_error("CODE 1 signed-minimum range edge does not match the source");
    }
    mf::SourceRandom sourceShuffleRandom(1);
    std::vector<int> shuffled{0, 1, 2, 3, 4, 5};
    mf::sourceShuffle(shuffled, sourceShuffleRandom);
    if (shuffled != std::vector<int>{2, 5, 1, 0, 3, 4}) {
        throw std::runtime_error("CODE 13 source shuffle does not match its deterministic vector");
    }

    HBITMAP title = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_TITLE));
    HBITMAP credits = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_CREDITS));
    HBITMAP brainstorm = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_BRAINSTORM));
    HBITMAP brainstormFade = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_BRAINSTORM_FADE));
    HBITMAP steppingStone = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_STEPPING_STONE));
    HBITMAP steppingStoneFade = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_STEPPING_STONE_FADE));
    constexpr std::array<int, 9> helpIds{
        IDB_HELP_BACKGAMMON_1, IDB_HELP_BACKGAMMON_2,
        IDB_HELP_DOMINOES_1, IDB_HELP_DOMINOES_2, IDB_HELP_CHECKERS_1,
        IDB_HELP_GOFISH_1, IDB_HELP_GOFISH_2, IDB_HELP_YACHT_1, IDB_HELP_YACHT_2};
    std::array<HBITMAP, 9> helpBitmaps{};
    for (std::size_t index = 0; index < helpBitmaps.size(); ++index)
        helpBitmaps[index] = LoadBitmapW(instance, MAKEINTRESOURCEW(helpIds[index]));
    if (!title || !credits || !brainstorm || !brainstormFade || !steppingStone ||
        !steppingStoneFade || std::any_of(helpBitmaps.begin(), helpBitmaps.end(),
                                          [](HBITMAP bitmap) { return bitmap == nullptr; })) {
        throw std::runtime_error("embedded bitmap resources could not be loaded");
    }
    BITMAP titleInfo{};
    BITMAP creditsInfo{};
    BITMAP startupInfo{};
    GetObjectW(title, sizeof(titleInfo), &titleInfo);
    GetObjectW(credits, sizeof(creditsInfo), &creditsInfo);
    GetObjectW(brainstorm, sizeof(startupInfo), &startupInfo);
    DeleteObject(title);
    DeleteObject(credits);
    DeleteObject(brainstorm);
    DeleteObject(brainstormFade);
    DeleteObject(steppingStone);
    DeleteObject(steppingStoneFade);
    for (HBITMAP bitmap : helpBitmaps) {
        BITMAP helpInfo{};
        GetObjectW(bitmap, sizeof(helpInfo), &helpInfo);
        if (helpInfo.bmWidth != 486 || helpInfo.bmHeight != 350) {
            DeleteObject(bitmap);
            throw std::runtime_error("embedded source help page has the wrong dimensions");
        }
        DeleteObject(bitmap);
    }
    if (titleInfo.bmWidth != 447 || titleInfo.bmHeight != 215 ||
        creditsInfo.bmWidth != 512 || creditsInfo.bmHeight != 384 ||
        startupInfo.bmWidth != 512 || startupInfo.bmHeight != 384) {
        throw std::runtime_error("embedded bitmap dimensions do not match the source art");
    }

    mf::AssetStore assets(instance, IDR_ASSET_PACK);
    if (assets.count() != 1707) throw std::runtime_error("asset count is not 1,707");
    if (assets.ids("snd ").size() != 313 || assets.ids("Midi").size() != 11 ||
        assets.ids("MuV ").size() != 467 || assets.ids("Ply ").size() != 467 ||
        assets.ids("Img ").size() != 125) {
        throw std::runtime_error("embedded resource type counts do not match the audited image");
    }

    mf::GraphicsAssets graphics(assets);
    std::size_t decodedFrames = 0;
    const auto pakIds = assets.ids("Pak ");
    if (pakIds.size() != 180) throw std::runtime_error("Pak resource count is not 180");
    for (int id : pakIds) {
        mf::PakSheet sheet(assets.get("Pak ", id));
        for (int frame = 0; frame < sheet.frameCount(); ++frame) {
            const mf::Sprite decoded = sheet.decodeFrame(frame, graphics.palette());
            if (decoded.width <= 0 || decoded.height <= 0 ||
                decoded.colors.size() != decoded.alpha.size()) {
                throw std::runtime_error("Pak frame decode returned invalid dimensions");
            }
            ++decodedFrames;
        }
    }
    if (decodedFrames != 3166) throw std::runtime_error("Pak frame count is not 3,166");

    mf::Audio audio(assets);
    std::size_t midiEvents = 0;
    const auto midiIds = assets.ids("Midi");
    for (int id : midiIds) midiEvents += audio.midiEventCount(id);
    if (midiIds.size() != 11 || midiEvents == 0) {
        throw std::runtime_error("MIDI resources did not pass the native parser");
    }
    constexpr std::array<std::pair<int, int>, 11> songToMidi{{
        {130, 900}, {134, 904}, {135, 905}, {136, 906}, {137, 907}, {138, 908},
        {139, 909}, {140, 910}, {141, 911}, {142, 912}, {143, 913}}};
    if (assets.ids("SONG").size() != songToMidi.size()) {
        throw std::runtime_error("SONG resource count does not match the source");
    }
    for (const auto& [songId, midiId] : songToMidi) {
        if (mf::readBe16(assets.get("SONG", songId), 0) != midiId) {
            throw std::runtime_error("SONG-to-MIDI routing does not match the source");
        }
    }
    if (mf::audio_catalog::kMenuMusic != 900 ||
        mf::audio_catalog::kPrimaryGameMusic != std::array<int, 5>{910, 904, 912, 906, 908} ||
        mf::audio_catalog::kPlayerWinMusic != std::array<int, 5>{911, 905, 913, 907, 909} ||
        mf::audio_catalog::primaryGameMusic(false, -1) != -1 ||
        mf::audio_catalog::primaryGameMusic(false, 5) != -1 ||
        mf::audio_catalog::playerWinMusic(false, -1) != -1 ||
        mf::audio_catalog::playerWinMusic(false, 5) != -1) {
        throw std::runtime_error("native music routing table does not match the source SONG map");
    }
    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        if (mf::audio_catalog::primaryGameMusic(false, gameIndex) !=
                mf::audio_catalog::kPrimaryGameMusic[static_cast<std::size_t>(gameIndex)] ||
            mf::audio_catalog::playerWinMusic(false, gameIndex) !=
                mf::audio_catalog::kPlayerWinMusic[static_cast<std::size_t>(gameIndex)]) {
            throw std::runtime_error("Macintosh game music route selection changed");
        }
    }
    std::size_t soundBytes = 0;
    const auto soundIds = assets.ids("snd ");
    for (int id : soundIds) soundBytes += audio.soundWaveSize(id);
    if (soundIds.size() != 313 || soundBytes == 0) {
        throw std::runtime_error("sound resources did not pass the native parser");
    }
    {
        // The WinMM software synth may initialize slowly, but startup must
        // never wait for it on the window thread. No MIDI events are ticked or
        // emitted by this probe; destruction only closes the prewarmed handle.
        mf::Audio startupMusicProbe(assets);
        const auto prewarmStart = std::chrono::steady_clock::now();
        startupMusicProbe.prewarmMusicOutput();
        const auto prewarmElapsed = std::chrono::steady_clock::now() - prewarmStart;
        const auto requestStart = std::chrono::steady_clock::now();
        if (!startupMusicProbe.playMusic(mf::audio_catalog::kMenuMusic)) {
            throw std::runtime_error("menu music could not be queued during MIDI prewarm");
        }
        const auto requestElapsed = std::chrono::steady_clock::now() - requestStart;
        startupMusicProbe.stopMusic();
        if (prewarmElapsed > std::chrono::milliseconds(250) ||
            requestElapsed > std::chrono::milliseconds(250)) {
            throw std::runtime_error("menu MIDI initialization blocked the startup controller");
        }
    }
    // Exact immediate IDs recovered from every CODE $A18/$CAA call site.
    // 9203 is the one additional direct UI cue stored in CODE 6's state table.
    static constexpr std::array directSoundIds{
        5000, 5001, 5003, 5004, 5006, 5010, 5011, 5012, 5013, 5017,
        5018, 5019, 5023, 5024, 5028, 5032, 5034, 5042, 5043, 5044,
        5053, 5054, 5057, 5072, 8038, 8042, 9201, 9202, 9204};
    for (const int id : directSoundIds) {
        if (!assets.contains("snd ", id) || audio.soundWaveSize(id) == 0) {
            throw std::runtime_error("recovered direct sound inventory is absent or invalid");
        }
    }
    if (!assets.contains("snd ", 9203) || audio.soundWaveSize(9203) == 0) {
        throw std::runtime_error("CODE 6 delete-key sound is absent or invalid");
    }
    {
        mf::Audio directSoundGateProbe(assets);
        if (!directSoundGateProbe.sourceDirectSoundGateRegressionTest()) {
            throw std::runtime_error("CODE 1 direct-sound busy gate changed");
        }
    }
    if (mf::audio_catalog::kBrainstormSound != 8038 ||
        mf::audio_catalog::kSteppingStoneSound != 8042 ||
        mf::audio_catalog::kMenuSelectionSound != 5003 ||
        mf::audio_catalog::kMenuLaunchSound != 5010 ||
        mf::audio_catalog::kMenuFootSound != 5004 ||
        mf::audio_catalog::kMenuBowTieSound != 5006 ||
        mf::audio_catalog::kAboutSound != 5057 ||
        mf::audio_catalog::kCreditsSound != 5072 ||
        !assets.contains("snd ", mf::audio_catalog::kBrainstormSound) ||
        !assets.contains("snd ", mf::audio_catalog::kSteppingStoneSound) ||
        audio.soundWaveSize(mf::audio_catalog::kBrainstormSound) == 0 ||
        audio.soundWaveSize(mf::audio_catalog::kSteppingStoneSound) == 0 ||
        audio.soundWaveSize(mf::audio_catalog::kMenuSelectionSound) == 0 ||
        audio.soundWaveSize(mf::audio_catalog::kMenuLaunchSound) == 0 ||
        audio.soundWaveSize(mf::audio_catalog::kMenuFootSound) == 0 ||
        audio.soundWaveSize(mf::audio_catalog::kMenuBowTieSound) == 0 ||
        audio.soundWaveSize(mf::audio_catalog::kAboutSound) == 0 ||
        audio.soundWaveSize(mf::audio_catalog::kCreditsSound) == 0) {
        throw std::runtime_error("startup/menu direct sound resources are absent or invalid");
    }
    // Self-test constructs every game and therefore starts their opening host
    // movies.  Keep verification strictly silent even when launched manually.
    audio.setEnabled(false);
    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        audio.playMusic(mf::audio_catalog::playerWinMusic(false, gameIndex));
        if (audio.requestedMusicResourceId() !=
                mf::audio_catalog::kPlayerWinMusic[static_cast<std::size_t>(gameIndex)]) {
            throw std::runtime_error("Macintosh player-win music request was not retained");
        }
        audio.playMusic(mf::audio_catalog::primaryGameMusic(false, gameIndex));
        if (audio.requestedMusicResourceId() !=
                mf::audio_catalog::kPrimaryGameMusic[static_cast<std::size_t>(gameIndex)]) {
            throw std::runtime_error("Macintosh Play Again music request was not restored");
        }
    }
    mf::SourceFonts sourceFonts(instance);
    mf::SourceRandom random(0x4d415249U);
    mf::GameContext context{assets, graphics, audio, random, [] {}, L"PLAYER", true};
    mf::Canvas canvas(mf::kLogicalWidth, mf::kLogicalHeight);
    const auto& times14 = sourceFonts.font(mf::SourceFontFace::Times14);
    const auto& geneva9 = sourceFonts.font(mf::SourceFontFace::Geneva9);
    const auto& monaco12 = sourceFonts.font(mf::SourceFontFace::Monaco12);
    if (times14.ascent() != 12 || times14.descent() != 3 || times14.leading() != 0 ||
        times14.textWidth(L"Jon made the stuff work,", true) != 166 ||
        geneva9.ascent() != 10 || geneva9.descent() != 2 || geneva9.leading() != 0 ||
        geneva9.textWidth(L"\x00a9" L"1996 BrainStorm\x2122 Inc.") != 116 ||
        monaco12.ascent() != 12 || monaco12.descent() != 3 || monaco12.leading() != 1 ||
        monaco12.textWidth(L"v. 1.1") != 42) {
        throw std::runtime_error("CODE 5 System 7 NFNT metrics do not match the source");
    }
    HBITMAP aboutTitle = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_TITLE));
    if (!aboutTitle) throw std::runtime_error("PICT 128 About bitmap could not be reloaded");
    canvas.clear(mf::rgb(0, 0, 0));
    mf::renderSourceAbout(canvas, sourceFonts, aboutTitle, {32, 84});
    DeleteObject(aboutTitle);
    const std::uint64_t aboutRaster = canvas.pixelHash({0, 0, 512, 384});
    if (aboutRaster != 9429561953654105681ULL)
        throw std::runtime_error("CODE 5 About panel raster changed");
    if (graphics.sprite(223, 33).width != 17 || graphics.sprite(223, 33).height != 14 ||
        graphics.sprite(226, 33).width != 14 || graphics.sprite(226, 33).height != 10 ||
        graphics.sprite(5006, 0).width != 12 || graphics.sprite(5006, 0).height != 14 ||
        graphics.sprite(5006, 3).width != 12 || graphics.sprite(5006, 3).height != 14 ||
        canvas.pakTextWidth(graphics, L"MARIO", 223) != 74 ||
        canvas.pakTextWidth(graphics, L"MY FRIEND", 223) != 117 ||
        canvas.pakTextWidth(graphics, L"PLAYER", 226) != 68 ||
        canvas.pakTextWidth(graphics, L"10 20", 226) != 39 ||
        canvas.pakTextWidth(graphics, L"ABCDEFGHIJKLM", 226, 11) !=
            canvas.pakTextWidth(graphics, L"ABCDEFGHIJK", 226)) {
        throw std::runtime_error("CODE 3/15 Pak text metrics do not match the source font packs");
    }
    canvas.clear(mf::rgb(7, 11, 13));
    canvas.pakText(graphics, L"MARIO", 223, {4, 3, 100, 23},
                   DT_LEFT | DT_TOP | DT_SINGLELINE);
    const std::uint64_t largeTextRaster = canvas.pixelHash({0, 0, 104, 26});
    canvas.clear(mf::rgb(7, 11, 13));
    canvas.pakText(graphics, L"10 20", 226, {2, 5, 80, 25},
                   DT_LEFT | DT_TOP | DT_SINGLELINE);
    const std::uint64_t smallTextRaster = canvas.pixelHash({0, 0, 84, 28});
    if (largeTextRaster != 9879098201465093269ULL ||
        smallTextRaster != 17893612500376930038ULL) {
        throw std::runtime_error("CODE 3/15 Pak text rasters do not match the shipped glyphs");
    }
    canvas.clear(mf::rgb(7, 11, 13));
    for (int frame = 0; frame < 4; ++frame)
        canvas.sprite(graphics.sprite(5006, frame), 2 + frame * 13, 3, false);
    if (canvas.pixelHash({0, 0, 56, 20}) != 14772238942320179375ULL) {
        throw std::runtime_error("CODE 17 Pak 5006 card-count numerals changed");
    }
    std::size_t movieCommands = 0;
    std::size_t resolvedMovies = 0;
    std::size_t movieSoundCues = 0;
    std::vector<int> danglingMovieSoundIds;
    std::vector<int> moviesWithVisualGaps;
    const auto movieIds = assets.ids("MuV ");
    for (int id : movieIds) {
        mf::Movie movie(assets, id);
        movieCommands += movie.commandCount();
        const auto cues = movie.soundCues();
        movieSoundCues += cues.size();
        for (int soundId : cues) {
            if (!assets.contains("snd ", soundId)) danglingMovieSoundIds.push_back(soundId);
        }
        if (movie.resolved()) {
            ++resolvedMovies;
            canvas.clear(0);
            movie.render(canvas, graphics, 0);
            bool visualGap = false;
            for (std::uint32_t time = 0; time < movie.duration(); ++time) {
                if (movie.activeImageCount(time) == 0) {
                    visualGap = true;
                    break;
                }
            }
            if (visualGap) moviesWithVisualGaps.push_back(id);
        }
    }
    // The source's orphan movie 11001 requests 55 images from the 11-image base
    // table 11000 and has no load-site reference; all 466 source-resolvable movies
    // must still construct and render.
    if (movieIds.size() != 467 || resolvedMovies != 466 || movieCommands != 8586) {
        throw std::runtime_error("movie catalog does not match the audited resources");
    }
    if (moviesWithVisualGaps != std::vector<int>{2800, 2801, 3900, 5101, 5102} ||
        mf::Movie(assets, 1125).activeImageCount(300) != 2 ||
        mf::Movie(assets, 11003).activeImageCount(300) != 1) {
        throw std::runtime_error("CODE 3 active-interval movie compositing changed");
    }
    std::sort(danglingMovieSoundIds.begin(), danglingMovieSoundIds.end());
    danglingMovieSoundIds.erase(
        std::unique(danglingMovieSoundIds.begin(), danglingMovieSoundIds.end()),
        danglingMovieSoundIds.end());
    if (movieSoundCues != 666 ||
        danglingMovieSoundIds != std::vector<int>{23019, 23020, 23021, 23022, 23023}) {
        throw std::runtime_error("movie sound-cue catalog does not match the audited source");
    }
    const mf::Movie characterQuestion(assets, 11093);
    if (characterQuestion.soundsAtStart() != std::vector<int>{8046} ||
        !assets.contains("snd ", 8046)) {
        throw std::runtime_error("Yoshi/Koopa character question lost its valid source voice cue");
    }
    const mf::Movie goFishQuestion(assets, 11519);
    if (goFishQuestion.soundsAtStart() != std::vector<int>{26039}) {
        throw std::runtime_error("talking-head movie lost its time-zero voice cue");
    }
    const mf::Movie introHand(assets, 1111);
    if (introHand.duration() != 780 || introHand.timeScale() != 600 ||
        introHand.soundsBetween(0, introHand.duration()) !=
            std::vector<int>{5009, 5013, 5017, 5017, 5017, 5017, 5013}) {
        throw std::runtime_error("startup hand movie lost its source sound timeline");
    }
    if (mf::menu_catalog::kSourceSelectionToGameIndex !=
            std::array<int, 5>{2, 3, 1, 0, 4} ||
        mf::menu_catalog::kGameIndexToSourceSelection !=
            std::array<int, 5>{4, 3, 1, 2, 5} ||
        mf::menu_catalog::kSelectionMovies !=
            std::array<int, 5>{1111, 1112, 1113, 1114, 1115} ||
        mf::menu_catalog::kSelectionHoldSourceTimes !=
            std::array<std::uint32_t, 5>{600U, 780U, 600U, 600U, 480U} ||
        mf::menu_catalog::transitionDirection(2, 5) != 1 ||
        mf::menu_catalog::transitionDirection(5, 1) != -1 ||
        mf::menu_catalog::transitionDirection(3, 3) != 0 ||
        mf::menu_catalog::stepSelection(2, 1) != 3 ||
        mf::menu_catalog::stepSelection(5, 1) != 1 ||
        mf::menu_catalog::stepSelection(1, -1) != 5 ||
        mf::menu_catalog::stepSelection(3, 0) != 3) {
        throw std::runtime_error("CODE 12 C/G/D/B/Y menu-selection mapping changed");
    }
    if (mf::menu_catalog::fileQuitAction(-1, -1) !=
            mf::menu_catalog::FileQuitAction::QuitApplication ||
        mf::menu_catalog::fileQuitAction(-1, 0) !=
            mf::menu_catalog::FileQuitAction::ExitGame ||
        mf::menu_catalog::fileQuitAction(4, -1) !=
            mf::menu_catalog::FileQuitAction::ExitGame) {
        throw std::runtime_error("source MBAR File/Q routing changed");
    }
    if (mf::menu_catalog::kGameIndexToOptionsMenuId !=
            std::array<int, 5>{301, 303, 302, 304, 305} ||
        mf::menu_catalog::optionsMenuId(-1) != 300 ||
        mf::menu_catalog::optionsMenuId(
            mf::menu_catalog::gameContextIndex(-1, 0)) != 301 ||
        mf::menu_catalog::optionsMenuId(
            mf::menu_catalog::gameContextIndex(-1, 2)) != 302 ||
        mf::menu_catalog::optionsMenuId(
            mf::menu_catalog::gameContextIndex(3, -1)) != 304) {
        throw std::runtime_error("source MBAR game Options routing changed");
    }
    const std::array<std::vector<int>, 5> menuMovieCues{{
        {5009, 5013, 5017, 5017, 5017, 5017, 5013},
        {5009, 5013, 5014, 5013},
        {5009, 5013, 5017, 5013},
        {5009, 5013, 5017, 5017, 5017, 5013},
        {5009, 5013, 5028, 5028, 5013}}};
    for (int sourceSelection = 1; sourceSelection <= 5; ++sourceSelection) {
        const mf::Movie selectionMovie(assets, mf::menu_catalog::movie(sourceSelection));
        const mf::Rect selectionBounds = selectionMovie.visualBounds(397, 127);
        if (!selectionMovie.resolved() || selectionMovie.soundCues() !=
                menuMovieCues[static_cast<std::size_t>(sourceSelection - 1)] ||
            selectionBounds.left != 397 || selectionBounds.top != 127 ||
            selectionBounds.right != 497 || selectionBounds.bottom != 227 ||
            mf::menu_catalog::sourceSelection(
                mf::menu_catalog::gameIndex(sourceSelection)) != sourceSelection) {
            throw std::runtime_error("menu-selection movie/cue routing does not match CODE 12");
        }
    }
    const mf::Movie bowTieIdle(assets, 1101);
    if (!bowTieIdle.resolved() || bowTieIdle.duration() != 1080 ||
        bowTieIdle.timeScale() != 600 || !bowTieIdle.soundCues().empty()) {
        throw std::runtime_error("CODE 12 bow-tie idle movie no longer matches its controller");
    }
    const mf::Movie talkingTitle(assets, 12091);
    const mf::Movie menuReveal(assets, 1125);
    const mf::Rect menuRevealFinal = menuReveal.imageBounds(15, 12, 107);
    if (talkingTitle.duration() != 1380 || talkingTitle.timeScale() != 600 ||
        talkingTitle.soundsAtStart() != std::vector<int>{8056} ||
        !talkingTitle.soundsBetween(0, talkingTitle.duration()).empty() ||
        menuReveal.duration() != 900 || menuReveal.timeScale() != 600 ||
        !menuReveal.soundsAtStart().empty() ||
        !menuReveal.soundsBetween(0, menuReveal.duration()).empty() ||
        menuRevealFinal.left != 26 || menuRevealFinal.top != 135 ||
        menuRevealFinal.right != 197 || menuRevealFinal.bottom != 274) {
        throw std::runtime_error("startup title controller movies lost their source timelines");
    }
    constexpr std::array<std::pair<int, int>, 9> idleVoiceCues{{
        {10090, 6041}, {10091, 8011}, {11438, 6041},
        {11561, 6041}, {11562, 8011}, {11638, 6041},
        {11639, 8011}, {11700, 6041}, {11701, 8011}}};
    for (const auto& [movieId, soundId] : idleVoiceCues) {
        if (mf::Movie(assets, movieId).soundsAtStart() != std::vector<int>{soundId}) {
            throw std::runtime_error("idle-conversation movie lost its source voice cue");
        }
    }
    constexpr std::array<std::pair<int, int>, 21> backgammonVoiceCues{{
        {11600, 6019}, {11603, 6023}, {11604, 6024},
        {11605, 6063}, {11607, 6065}, {11609, 6068}, {11611, 6015},
        {11613, 6082}, {11614, 6083}, {11616, 6087}, {11617, 8030},
        {11618, 25000}, {11630, 6033}, {11631, 6037}, {11632, 6078},
        {11637, 6040}, {11640, 6047}, {11642, 6042}, {11643, 6044},
        {11644, 8025}, {11646, 8027}}};
    for (const auto& [movieId, soundId] : backgammonVoiceCues) {
        if (mf::Movie(assets, movieId).soundsAtStart() != std::vector<int>{soundId}) {
            throw std::runtime_error("Backgammon dialogue movie lost its source voice cue");
        }
    }
    const mf::Movie backgammonVictoryLeft(assets, 4022);
    const mf::Movie backgammonVictoryRight(assets, 4023);
    if (!backgammonVictoryLeft.resolved() || !backgammonVictoryRight.resolved() ||
        backgammonVictoryLeft.duration() != 2100 ||
        backgammonVictoryRight.duration() != 2100 ||
        backgammonVictoryLeft.timeScale() != 600 ||
        backgammonVictoryRight.timeScale() != 600 ||
        backgammonVictoryLeft.commandCount() != 30 ||
        backgammonVictoryRight.commandCount() != 29 ||
        backgammonVictoryLeft.soundsBetween(419, 420) != std::vector<int>{5074} ||
        !backgammonVictoryRight.soundsAtStart().empty() ||
        !backgammonVictoryRight.soundsBetween(0, backgammonVictoryRight.duration()).empty()) {
        throw std::runtime_error("Backgammon paired victory movies lost their source timeline");
    }
    const mf::Movie goFishVictoryFlip(assets, 5210);
    if (!goFishVictoryFlip.resolved() || goFishVictoryFlip.duration() != 240 ||
        goFishVictoryFlip.timeScale() != 600 || goFishVictoryFlip.commandCount() != 6 ||
        !goFishVictoryFlip.soundsAtStart().empty() ||
        !goFishVictoryFlip.soundsBetween(0, goFishVictoryFlip.duration()).empty()) {
        throw std::runtime_error("Go Fish victory-card movie lost its source timeline");
    }
    constexpr std::array<std::pair<int, int>, 7> goFishOutcomeVoiceCues{{
        {11565, 6045}, {11566, 8023}, {11567, 8024}, {11568, 8025},
        {11569, 8026}, {11570, 8027}, {11571, 6047}}};
    for (const auto& [movieId, soundId] : goFishOutcomeVoiceCues) {
        if (mf::Movie(assets, movieId).soundsAtStart() != std::vector<int>{soundId}) {
            throw std::runtime_error("Go Fish outcome movie lost its source voice cue");
        }
    }
    constexpr std::array<std::pair<int, int>, 25> dominoesOutcomeVoiceCues{{
        {10040, 6030}, {10041, 6073}, {10051, 6042}, {10052, 6043},
        {10053, 6071}, {10055, 8027}, {10056, 6044}, {10057, 6045},
        {10058, 8026}, {10059, 8022}, {10060, 8023}, {10061, 8024},
        {10062, 6046}, {10063, 6063}, {10064, 6064}, {10065, 6065},
        {10067, 6067}, {10068, 6068}, {10069, 6069}, {10071, 6070},
        {10072, 8025}, {10073, 6047}, {10074, 6050}, {10086, 8054},
        {10087, 8055}}};
    for (const auto& [movieId, soundId] : dominoesOutcomeVoiceCues) {
        if (mf::Movie(assets, movieId).soundsAtStart() != std::vector<int>{soundId}) {
            throw std::runtime_error("Dominoes outcome movie lost its source voice cue");
        }
    }
    constexpr std::array<std::pair<int, int>, 16> checkersOutcomeVoiceCues{{
        {11053, 6071}, {11055, 8027}, {11056, 6044}, {11057, 6045},
        {11058, 8026}, {11063, 6063}, {11064, 6064}, {11065, 6065},
        {11067, 6067}, {11068, 6068}, {11069, 6069}, {11071, 6070},
        {11072, 8025}, {11073, 6047}, {11074, 6050}, {11636, 8029}}};
    for (const auto& [movieId, soundId] : checkersOutcomeVoiceCues) {
        if (mf::Movie(assets, movieId).soundsAtStart() != std::vector<int>{soundId}) {
            throw std::runtime_error("Checkers outcome movie lost its source voice cue");
        }
    }
    const mf::Movie yachtVictory(assets, 6022);
    if (!yachtVictory.resolved() || yachtVictory.duration() != 2100 ||
        yachtVictory.timeScale() != 600 || yachtVictory.commandCount() != 36 ||
        yachtVictory.soundsBetween(119, 120) != std::vector<int>{5062} ||
        yachtVictory.soundsBetween(299, 300) != std::vector<int>{5061} ||
        yachtVictory.soundsBetween(779, 780) != std::vector<int>{5008}) {
        throw std::runtime_error("Yacht victory movie lost its source timeline");
    }
    const mf::Movie yachtIntroUpper(assets, 6100);
    const mf::Movie yachtIntroLower(assets, 6150);
    const mf::Movie yachtRollCup(assets, 6020);
    const mf::Movie yachtRerollGesture(assets, 6021);
    const mf::Movie dominoesIntroRight(assets, 3002);
    const mf::Movie dominoesIntroMiddle(assets, 3003);
    const mf::Movie dominoesIntroLeft(assets, 3004);
    const mf::Movie dominoesPlayerResult(assets, 3900);
    const mf::Rect dominoesRightFinal = dominoesIntroRight.activeVisualBounds(
        dominoesIntroRight.duration() - 1, 18, 220);
    const mf::Rect dominoesMiddleFinal = dominoesIntroMiddle.activeVisualBounds(
        dominoesIntroMiddle.duration() - 1, 18, 220);
    const mf::Rect dominoesLeftFinal = dominoesIntroLeft.activeVisualBounds(
        dominoesIntroLeft.duration() - 1, 22, 220);
    const std::vector<int> dominoesPlayerResultSounds = dominoesPlayerResult.soundCues();
    if (!dominoesPlayerResult.resolved() ||
        dominoesPlayerResult.duration() != 2100 ||
        dominoesPlayerResult.timeScale() != 600 ||
        dominoesPlayerResult.commandCount() != 43 ||
        dominoesPlayerResultSounds != std::vector<int>(6, 5069)) {
        throw std::runtime_error(
            "Macintosh Dominoes player-result movie audit changed: resolved=" +
            std::to_string(dominoesPlayerResult.resolved()) + " duration=" +
            std::to_string(dominoesPlayerResult.duration()) + " timescale=" +
            std::to_string(dominoesPlayerResult.timeScale()) + " commands=" +
            std::to_string(dominoesPlayerResult.commandCount()) + " sounds=" +
            std::to_string(dominoesPlayerResultSounds.size()));
    }
    const mf::Rect yachtUpperStart = yachtIntroUpper.imageBounds(0, -149, 0);
    const mf::Rect yachtLowerStart = yachtIntroLower.imageBounds(0, -208, 0);
    const mf::Rect yachtTalkingHead = mf::Movie(assets, 11411).imageBounds(3, 15, -1);
    const mf::Rect yachtRollStart = yachtRollCup.activeVisualBounds(0, 0, -9);
    const mf::Rect yachtGestureWide = yachtRerollGesture.activeVisualBounds(240, 15, -1);
    if (dominoesRightFinal.left != 400 || dominoesRightFinal.top != 270 ||
        dominoesRightFinal.right != 477 || dominoesRightFinal.bottom != 323 ||
        dominoesMiddleFinal.left != 169 || dominoesMiddleFinal.top != 283 ||
        dominoesMiddleFinal.right != 400 || dominoesMiddleFinal.bottom != 323 ||
        dominoesLeftFinal.left != 133 || dominoesLeftFinal.top != 283 ||
        dominoesLeftFinal.right != 173 || dominoesLeftFinal.bottom != 308 ||
        yachtUpperStart.left != -125 || yachtUpperStart.top != 108 ||
        yachtUpperStart.right != 183 || yachtUpperStart.bottom != 240 ||
        yachtLowerStart.left != -191 || yachtLowerStart.top != 240 ||
        yachtLowerStart.right != 106 || yachtLowerStart.bottom != 271 ||
        yachtTalkingHead.left != 228 || yachtTalkingHead.top != 18 ||
        yachtTalkingHead.right != 336 || yachtTalkingHead.bottom != 123 ||
        yachtRollStart.left != 218 || yachtRollStart.top != 129 ||
        yachtRollStart.right != 296 || yachtRollStart.bottom != 228 ||
        yachtGestureWide.left != 99 || yachtGestureWide.top != 18 ||
        yachtGestureWide.right != 337 || yachtGestureWide.bottom != 212) {
        throw std::runtime_error("Macintosh registered actor geometry regression");
    }
    for (std::uint32_t time = 0; time < yachtRollCup.duration(); ++time) {
        if (yachtRollCup.activeImageCount(time) > 1) {
            throw std::runtime_error("Macintosh Yacht roll movie duplicated its large cup layer");
        }
    }
    constexpr std::array<std::pair<int, int>, 6> yachtOutcomeVoiceCues{{
        {11439, 6045}, {11442, 6046}, {11443, 8023},
        {11444, 8024}, {11445, 8027}, {11453, 6047}}};
    for (const auto& [movieId, soundId] : yachtOutcomeVoiceCues) {
        if (mf::Movie(assets, movieId).soundsAtStart() != std::vector<int>{soundId}) {
            throw std::runtime_error("Yacht outcome movie lost its source voice cue");
        }
    }
    if (mf::yachtCategoryScore(7, {2, 2, 5, 5, 5}) != 19 ||
        mf::yachtCategoryScore(8, {3, 3, 3, 3, 6}) != 18 ||
        mf::yachtCategoryScore(9, {1, 2, 3, 4, 6}) != 25 ||
        mf::yachtCategoryScore(9, {2, 3, 4, 5, 5}) != 25 ||
        mf::yachtCategoryScore(10, {2, 3, 4, 5, 6}) != 30 ||
        mf::yachtCategoryScore(11, {4, 4, 4, 4, 4}) != 50) {
        throw std::runtime_error("Yacht source scoring table regression");
    }
    if (mf::yachtScoreCategoryAt({378, 54}) != 11 ||
        mf::yachtScoreCategoryAt({473, 73}) != 11 ||
        mf::yachtScoreCategoryAt({378, 74}) != 10 ||
        mf::yachtScoreCategoryAt({473, 293}) != 0 ||
        mf::yachtScoreCategoryAt({377, 54}) != -1 ||
        mf::yachtScoreCategoryAt({474, 54}) != -1 ||
        mf::yachtScoreCategoryAt({378, 53}) != -1 ||
        mf::yachtScoreCategoryAt({378, 294}) != -1) {
        throw std::runtime_error("Yacht source score-line hit geometry regression");
    }
    const mf::Rect firstWhiteDie = mf::yachtDieRect(0, false);
    const mf::Rect lastWhiteDie = mf::yachtDieRect(4, false);
    const mf::Rect firstRedDie = mf::yachtDieRect(0, true);
    if (firstWhiteDie.left != 154 || firstWhiteDie.top != 261 ||
        firstWhiteDie.right != 193 || firstWhiteDie.bottom != 299 ||
        lastWhiteDie.left != 318 || lastWhiteDie.top != 261 ||
        lastWhiteDie.right != 357 || lastWhiteDie.bottom != 299 ||
        firstRedDie.left != 154 || firstRedDie.top != 284 ||
        firstRedDie.right != 193 || firstRedDie.bottom != 322 ||
        !firstWhiteDie.contains({154, 261}) || !firstWhiteDie.contains({192, 298}) ||
        firstWhiteDie.contains({153, 261}) || firstWhiteDie.contains({193, 261}) ||
        firstWhiteDie.contains({154, 260}) || firstWhiteDie.contains({154, 299})) {
        throw std::runtime_error("Yacht source die-lane geometry regression");
    }
    if (mf::yachtRemainingRollMarkers(0, false) != 3 ||
        mf::yachtRemainingRollMarkers(0, true) != 2 ||
        mf::yachtRemainingRollMarkers(1, false) != 2 ||
        mf::yachtRemainingRollMarkers(2, true) != 0 ||
        mf::yachtRemainingRollMarkers(3, false) != 0 ||
        !mf::yachtComputerRerollSpeechEligible(
            1, std::array<bool, 5>{false, false, false, false, false}) ||
        !mf::yachtComputerRerollSpeechEligible(
            1, std::array<bool, 5>{false, false, true, true, true}) ||
        mf::yachtComputerRerollSpeechEligible(
            1, std::array<bool, 5>{false, true, true, true, true}) ||
        mf::yachtComputerRerollSpeechEligible(
            0, std::array<bool, 5>{false, false, false, false, false}) ||
        !mf::yachtHasWhiteDieToReroll(
            std::array<bool, 5>{true, true, false, true, true}) ||
        mf::yachtHasWhiteDieToReroll(
            std::array<bool, 5>{true, true, true, true, true})) {
        throw std::runtime_error("Yacht source remaining-roll controller regression");
    }
    std::array<int, 12> openYachtScores{};
    openYachtScores.fill(-1);
    auto yachtSacrificeScores = std::array<int, 12>{};
    yachtSacrificeScores.fill(0);
    yachtSacrificeScores[0] = yachtSacrificeScores[3] = yachtSacrificeScores[4] =
        yachtSacrificeScores[5] = yachtSacrificeScores[6] = -1;
    auto yachtChoiceScores = std::array<int, 12>{};
    yachtChoiceScores.fill(0);
    yachtChoiceScores[0] = yachtChoiceScores[6] = -1;
    if (mf::yachtComputerRerolls({6, 6, 6, 2, 4}, openYachtScores) !=
            std::array<bool, 5>{false, false, false, true, true} ||
        mf::yachtComputerHeldDice({6, 6, 6, 2, 4}, openYachtScores) !=
            std::array<bool, 5>{true, true, true, false, false} ||
        mf::yachtComputerRerolls({1, 2, 3, 4, 6}, openYachtScores) !=
            std::array<bool, 5>{false, false, false, false, true} ||
        mf::yachtComputerRerolls({1, 2, 2, 3, 4}, openYachtScores) !=
            std::array<bool, 5>{false, true, false, false, false} ||
        mf::yachtComputerRerolls({1, 2, 3, 5, 6}, openYachtScores) !=
            std::array<bool, 5>{false, false, false, false, true} ||
        mf::yachtComputerCategory({4, 4, 4, 2, 6}, openYachtScores) != 3 ||
        mf::yachtComputerCategory({4, 5, 6, 2, 2}, yachtSacrificeScores) != 0 ||
        mf::yachtComputerCategory({4, 5, 5, 6, 6}, yachtChoiceScores) != 6) {
        throw std::runtime_error("Yacht source strategy regression");
    }

    auto backgammon = std::make_unique<mf::BackgammonGame>(context);
    auto backgammonSetup = std::make_unique<mf::BackgammonGame>(context);
    auto backgammonStartup = std::make_unique<mf::BackgammonGame>(context);
    auto backgammonFullMatch = std::make_unique<mf::BackgammonGame>(context);
    auto backgammonReplay = std::make_unique<mf::BackgammonGame>(context);
    if (!backgammon->sourceStrategyRegressionTest() ||
        !mf::BackgammonGame::sourceIdleRegressionTest() ||
        !mf::BackgammonGame::sourceCheckerGeometryRegressionTest() ||
        !backgammon->sourceDialogueRegressionTest() ||
        !backgammonStartup->sourceStartupRegressionTest() ||
        !backgammonSetup->sourceSetupRevealRegressionTest()) {
        throw std::runtime_error("Backgammon source strategy regression");
    }
    if (!backgammonFullMatch->sourceFullMatchRegressionTest())
        throw std::runtime_error("Backgammon full-match controller regression");
    if (!backgammonReplay->sourceReplayRegressionTest())
        throw std::runtime_error("Backgammon replay-state regression");
    auto backgammonHumanOutcome = std::make_unique<mf::BackgammonGame>(context);
    auto backgammonMarioOutcome = std::make_unique<mf::BackgammonGame>(context);
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 0));
    if (!backgammonHumanOutcome->sourceOutcomeRegressionTest(true) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(false, 0))
        throw std::runtime_error("Backgammon player outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 0));
    if (!backgammonMarioOutcome->sourceOutcomeRegressionTest(false) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 0))
        throw std::runtime_error("Backgammon Mario outcome/music regression");
    auto dominoes = std::make_unique<mf::DominoesGame>(context);
    if (!dominoes->sourceStrategyRegressionTest() ||
        !dominoes->sourceOpeningRegressionTest() ||
        !dominoes->sourceDealPresentationRegressionTest() ||
        !mf::DominoesGame::sourceIdleRegressionTest() ||
        !mf::DominoesGame::sourceDialogueRegressionTest() ||
        !dominoes->sourceDragRegressionTest() ||
        !dominoes->sourceBoneyardHitboxRegressionTest()) {
        throw std::runtime_error("Dominoes source strategy regression");
    }
    auto dominoesFullMatch = std::make_unique<mf::DominoesGame>(context);
    if (!dominoesFullMatch->sourceFullMatchRegressionTest())
        throw std::runtime_error("Dominoes full-match controller regression");
    auto dominoesHumanOutcome = std::make_unique<mf::DominoesGame>(context);
    auto dominoesMarioOutcome = std::make_unique<mf::DominoesGame>(context);
    auto dominoesBlockedHumanOutcome = std::make_unique<mf::DominoesGame>(context);
    auto dominoesBlockedMarioOutcome = std::make_unique<mf::DominoesGame>(context);
    auto dominoesBlockedTieOutcome = std::make_unique<mf::DominoesGame>(context);
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 1));
    if (!dominoesHumanOutcome->sourceOutcomeRegressionTest(1, false) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(false, 1))
        throw std::runtime_error("Dominoes last-tile player outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 1));
    if (!dominoesMarioOutcome->sourceOutcomeRegressionTest(-1, false) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 1))
        throw std::runtime_error("Dominoes last-tile Mario outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 1));
    if (!dominoesBlockedHumanOutcome->sourceOutcomeRegressionTest(1, true) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(false, 1))
        throw std::runtime_error("Dominoes blocked-player outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 1));
    if (!dominoesBlockedMarioOutcome->sourceOutcomeRegressionTest(-1, true) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 1))
        throw std::runtime_error("Dominoes blocked-Mario outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 1));
    if (!dominoesBlockedTieOutcome->sourceOutcomeRegressionTest(2, true) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 1) ||
        !dominoes->sourceReplayRegressionTest())
        throw std::runtime_error("Dominoes blocked-tie/replay music regression");
    auto checkers = std::make_unique<mf::CheckersGame>(context);
    if (!checkers->sourceStrategyRegressionTest() ||
        !mf::CheckersGame::sourceIdleRegressionTest()) {
        throw std::runtime_error("Checkers source strategy regression");
    }
    auto checkersFullMatch = std::make_unique<mf::CheckersGame>(context);
    if (!checkersFullMatch->sourceFullMatchRegressionTest())
        throw std::runtime_error("Checkers full-match controller regression");
    auto checkersHumanEliminationOutcome = std::make_unique<mf::CheckersGame>(context);
    auto checkersHumanStuckOutcome = std::make_unique<mf::CheckersGame>(context);
    auto checkersFirstMarioOutcome = std::make_unique<mf::CheckersGame>(context);
    auto checkersLaterMarioOutcome = std::make_unique<mf::CheckersGame>(context);
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 2));
    if (!checkersHumanEliminationOutcome->sourceOutcomeRegressionTest(1) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(false, 2))
        throw std::runtime_error("Checkers elimination outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 2));
    if (!checkersHumanStuckOutcome->sourceOutcomeRegressionTest(2) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 2))
        throw std::runtime_error("Checkers no-legal-move outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 2));
    if (!checkersFirstMarioOutcome->sourceOutcomeRegressionTest(-1) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 2))
        throw std::runtime_error("Checkers first Mario outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 2));
    if (!checkersLaterMarioOutcome->sourceOutcomeRegressionTest(-2) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 2) ||
        !checkers->sourceReplayRegressionTest())
        throw std::runtime_error("Checkers later Mario/replay music regression");
    auto goFish = std::make_unique<mf::GoFishGame>(context);
    if (!goFish->sourceStrategyRegressionTest())
        throw std::runtime_error("Go Fish source strategy regression");
    if (!goFish->sourceDialogueRegressionTest())
        throw std::runtime_error("Go Fish source dialogue regression");
    if (!goFish->sourceHandSlotRegressionTest())
        throw std::runtime_error("Go Fish source hand-slot regression");
    if (!goFish->sourceOpeningDealRegressionTest())
        throw std::runtime_error("Go Fish source opening-controller regression");
    auto goFishRefill = std::make_unique<mf::GoFishGame>(context);
    if (!goFishRefill->sourceEmptyHandRefillRegressionTest())
        throw std::runtime_error("Go Fish source empty-hand refill regression");
    auto goFishFullMatch = std::make_unique<mf::GoFishGame>(context);
    if (!goFishFullMatch->sourceFullMatchRegressionTest())
        throw std::runtime_error("Go Fish full-match controller regression");
    auto goFishHumanOutcome = std::make_unique<mf::GoFishGame>(context);
    auto goFishMarioOutcome = std::make_unique<mf::GoFishGame>(context);
    auto goFishTieOutcome = std::make_unique<mf::GoFishGame>(context);
    auto goFishReplay = std::make_unique<mf::GoFishGame>(context);
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 3));
    if (!goFishHumanOutcome->sourceOutcomeRegressionTest(1) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(false, 3))
        throw std::runtime_error("Go Fish player outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 3));
    if (!goFishMarioOutcome->sourceOutcomeRegressionTest(-1) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 3))
        throw std::runtime_error("Go Fish Mario outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 3));
    if (!goFishTieOutcome->sourceOutcomeRegressionTest(2) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 3) ||
        !goFishReplay->sourceReplayRegressionTest())
        throw std::runtime_error("Go Fish tie/replay music regression");
    auto yachtHumanOutcome = std::make_unique<mf::YachtGame>(context);
    auto yachtMarioOutcome = std::make_unique<mf::YachtGame>(context);
    auto yachtTieOutcome = std::make_unique<mf::YachtGame>(context);
    auto yachtComputerSelection = std::make_unique<mf::YachtGame>(context);
    auto yachtTurnOrder = std::make_unique<mf::YachtGame>(context);
    auto yachtCupPresentation = std::make_unique<mf::YachtGame>(context);
    auto yachtFullMatch = std::make_unique<mf::YachtGame>(context);
    auto yachtReplay = std::make_unique<mf::YachtGame>(context);
    if (!mf::App::sourceIntroSkipRegressionTest() ||
        !mf::YachtGame::sourceDialogueRegressionTest() ||
        !yachtComputerSelection->sourceComputerSelectionRegressionTest() ||
        !yachtTurnOrder->sourceTurnOrderRegressionTest() ||
        !yachtCupPresentation->sourceCupPresentationRegressionTest() ||
        !yachtFullMatch->sourceFullMatchRegressionTest() ||
        !yachtReplay->sourceReplayRegressionTest()) {
        throw std::runtime_error("Yacht source outcome sequence regression");
    }
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 4));
    if (!yachtHumanOutcome->sourceOutcomeRegressionTest(1) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(false, 4))
        throw std::runtime_error("Yacht player outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 4));
    if (!yachtMarioOutcome->sourceOutcomeRegressionTest(-1) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 4))
        throw std::runtime_error("Yacht Mario outcome/music regression");
    audio.playMusic(mf::audio_catalog::primaryGameMusic(false, 4));
    if (!yachtTieOutcome->sourceOutcomeRegressionTest(2) ||
        audio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(false, 4))
        throw std::runtime_error("Yacht tie outcome/music regression");
    std::array<std::unique_ptr<mf::Game>, 5> games{
        std::move(backgammon),
        std::move(dominoes),
        std::move(checkers),
        std::move(goFish),
        std::make_unique<mf::YachtGame>(context)};
    for (const auto& game : games) game->render(canvas);
    games[0]->click({454, 205});
    games[1]->click({440, 260});
    games[2]->click({102, 254});
    games[2]->click({62, 214});
    games[3]->click({24, 320});
    games[4]->click({100, 235});
    games[4]->click({400, 80});
    for (const auto& game : games) game->render(canvas);

    // The DOS edition is embedded as a separate resource store because its
    // PRS uses different byte order, palette, sample, and music formats.  This
    // exhaustive decode remains silent: it never opens WinMM output devices.
    mf::AssetStore dosAssets(instance, IDR_DOS_ASSET_PACK, mf::AssetDialect::Dos);
    if (dosAssets.count() != 1806 || dosAssets.ids("Pak ").size() != 187 ||
        dosAssets.ids("MuV ").size() != 574 || dosAssets.ids("Ply ").size() != 574 ||
        dosAssets.ids("Img ").size() != 177 || dosAssets.ids("SND ").size() != 278 ||
        dosAssets.ids("XMI ").size() != 12 || dosAssets.ids("DIB ").size() != 4) {
        throw std::runtime_error("DOS embedded resource counts do not match the PRD/PRS audit");
    }
    mf::GraphicsAssets dosGraphics(dosAssets);
    std::size_t dosFrames = 0;
    for (int id : dosAssets.ids("Pak ")) {
        mf::PakSheet sheet(dosAssets.get("Pak ", id), mf::AssetDialect::Dos);
        for (int frame = 0; frame < sheet.frameCount(); ++frame) {
            const mf::Sprite decoded = sheet.decodeFrame(frame, dosGraphics.palette());
            if (decoded.width <= 0 || decoded.height <= 0 ||
                decoded.colors.size() != decoded.alpha.size()) {
                throw std::runtime_error("DOS Pak frame decode returned invalid dimensions");
            }
            ++dosFrames;
        }
    }
    if (dosFrames != 3633) throw std::runtime_error("DOS Pak frame count is not 3,633");
    std::size_t dosMovieCommands = 0;
    std::size_t dosResolvedMovies = 0;
    std::size_t dosMovieImageGeometryChecks = 0;
    std::size_t dosMovieSoundCues = 0;
    std::size_t dosPresentMovieSoundCues = 0;
    std::vector<int> dosUnresolvedMovies;
    std::vector<int> dosMissingMovieSoundIds;
    for (int id : dosAssets.ids("MuV ")) {
        mf::Movie movie(dosAssets, id);
        dosMovieCommands += movie.commandCount();
        const std::vector<int> soundCues = movie.soundCues();
        dosMovieSoundCues += soundCues.size();
        for (const int soundId : soundCues) {
            if (dosAssets.contains("SND ", soundId)) ++dosPresentMovieSoundCues;
            else dosMissingMovieSoundIds.push_back(soundId);
        }
        if (movie.resolved()) {
            ++dosResolvedMovies;
            for (std::size_t image = 0; image < movie.imageCount(); ++image) {
                const mf::Rect source = movie.imageBounds(image);
                const mf::Sprite& frame = dosGraphics.sprite(
                    movie.imageSheetId(), static_cast<int>(image));
                if (source.width() != frame.width || source.height() != frame.height) {
                    throw std::runtime_error(
                        "DOS Img geometry does not match its Pak frame dimensions");
                }
                ++dosMovieImageGeometryChecks;
            }
        } else {
            dosUnresolvedMovies.push_back(id);
        }
    }
    if (dosMovieCommands != 10614 || dosResolvedMovies != 573 ||
        dosMovieImageGeometryChecks == 0 || dosUnresolvedMovies != std::vector<int>{10001}) {
        throw std::runtime_error("DOS movie audit differs from the extracted timelines");
    }
    std::ranges::sort(dosMissingMovieSoundIds);
    dosMissingMovieSoundIds.erase(
        std::unique(dosMissingMovieSoundIds.begin(), dosMissingMovieSoundIds.end()),
        dosMissingMovieSoundIds.end());
    const std::vector<int> expectedDosMissingMovieSoundIds{
        8045, 8047, 8049, 8050, 8052,
        9008, 9009, 9010, 9011, 9012, 9013, 9014, 9015, 9016, 9017, 9018, 9019,
        9029, 9030, 9031, 9032, 9033, 9034, 9035, 9036, 9037, 9038, 9039, 9040,
        23019, 23020, 23021, 23022, 23023,
        24002, 24005, 24006, 24009,
        25013, 25014, 25015, 25019, 25020, 25021, 25022, 25023, 25024,
        25026, 25027, 25028, 25029,
        26014, 26018, 26019, 26029, 26030, 26032, 26050, 26051, 26053,
        27012, 27014, 27021,
    };
    if (dosMovieSoundCues != 743 || dosPresentMovieSoundCues != 650 ||
        dosMissingMovieSoundIds != expectedDosMissingMovieSoundIds) {
        throw std::runtime_error("DOS movie sound-cue inventory changed");
    }
    const mf::Movie dosDominoesPlayerResult(dosAssets, 3900);
    if (!dosDominoesPlayerResult.resolved() ||
        dosDominoesPlayerResult.duration() != 2100 ||
        dosDominoesPlayerResult.timeScale() != 600 ||
        dosDominoesPlayerResult.commandCount() != 42 ||
        dosDominoesPlayerResult.soundCues() != std::vector<int>(6, 5069)) {
        throw std::runtime_error("DOS Dominoes player-result movie audit changed");
    }
    const mf::Movie dosCharacterQuestion(dosAssets, 11093);
    const mf::Rect dosCentredCharacterQuestion =
        dosCharacterQuestion.activeVisualBounds(600, 116, 1);
    const mf::Rect dosPortraitCharacterQuestion =
        dosCharacterQuestion.activeVisualBounds(600, 7, 3);
    if (dosCharacterQuestion.soundsAtStart() != std::vector<int>{8046} ||
        !dosAssets.contains("SND ", 8046) ||
        dosCentredCharacterQuestion.left != 121 ||
        dosCentredCharacterQuestion.top != 9 ||
        dosCentredCharacterQuestion.right != 176 ||
        dosCentredCharacterQuestion.bottom != 61 ||
        dosPortraitCharacterQuestion.left != 12 ||
        dosPortraitCharacterQuestion.top != 11 ||
        dosPortraitCharacterQuestion.right != 67 ||
        dosPortraitCharacterQuestion.bottom != 63) {
        throw std::runtime_error("DOS character question lost its authored voice cue");
    }
    // DOS MuV/Img records are conventional x/y structures, not QuickDraw's
    // vertical-first records.  These exact bounds cover both the persistent
    // blank board and the final 1125 flip cel; swapping either axis reproduces
    // the clipped, off-center release-candidate rendering.
    const mf::Movie dosMenuReveal(dosAssets, 1125);
    const mf::Rect dosMenuBase = dosMenuReveal.imageBounds(0, 45, 55);
    const mf::Rect dosMenuFinal = dosMenuReveal.imageBounds(15, 45, 55);
    if (dosMenuBase.left != 46 || dosMenuBase.top != 56 ||
        dosMenuBase.right != 151 || dosMenuBase.bottom != 187 ||
        dosMenuFinal.left != 55 || dosMenuFinal.top != 77 ||
        dosMenuFinal.right != 139 || dosMenuFinal.bottom != 153) {
        throw std::runtime_error("DOS movie coordinate dialect regression");
    }
    // Ply motion words remain vertical/horizontal even though DOS MuV/Img
    // geometry was converted to x/y. The Yacht must travel horizontally and
    // its cup shake must travel vertically.
    const mf::Movie dosYachtIntro(dosAssets, 6100);
    const mf::Rect yachtStart = dosYachtIntro.activeVisualBounds(0, -90, 0);
    const mf::Rect yachtLater = dosYachtIntro.activeVisualBounds(1200, -90, 0);
    const mf::Movie dosYachtCup(dosAssets, 6020);
    const mf::Rect cupStart = dosYachtCup.activeVisualBounds(60, -26, 53);
    const mf::Rect cupShake = dosYachtCup.activeVisualBounds(120, -26, 53);
    if (yachtLater.left - yachtStart.left != 100 ||
        yachtLater.top != yachtStart.top ||
        cupShake.left != cupStart.left || cupShake.top - cupStart.top != 8) {
        throw std::runtime_error("DOS Ply motion-axis regression");
    }
    for (std::uint32_t time = 0; time < dosYachtCup.duration(); ++time) {
        if (dosYachtCup.activeImageCount(time) > 1) {
            throw std::runtime_error("DOS Yacht roll movie duplicated its large cup layer");
        }
    }
    // The intro overlays do not all pass the same kind of coordinate to the
    // movie player. Backgammon, Checkers, and Go Fish supply registered actor
    // points, so their MuV bounds origins must be resolved at the call site.
    // These source-time samples are also the exact foreground placements in
    // the independent vanilla DOS screenshots used by the visual gate.
    const mf::Rect dosBackgammonReference =
        mf::Movie(dosAssets, 4999).activeVisualBounds(1320, -201, 108);
    const mf::Rect dosCheckersLeftReference =
        mf::Movie(dosAssets, 2801).activeVisualBounds(4200, -96, 134);
    const mf::Rect dosCheckersRightReference =
        mf::Movie(dosAssets, 2800).activeVisualBounds(4200, -107, 130);
    const mf::Rect dosGoFishRightReference =
        mf::Movie(dosAssets, 5101).activeVisualBounds(1620, 0, 79);
    const mf::Rect dosGoFishLeftReference =
        mf::Movie(dosAssets, 5102).activeVisualBounds(1620, 0, 85);
    if (dosBackgammonReference.left != 108 || dosBackgammonReference.top != 126 ||
        dosCheckersLeftReference.left != 24 || dosCheckersLeftReference.top != 145 ||
        dosCheckersRightReference.left != 119 || dosCheckersRightReference.top != 144 ||
        dosGoFishRightReference.left != 234 || dosGoFishRightReference.top != 121 ||
        dosGoFishLeftReference.left != 94 || dosGoFishLeftReference.top != 115) {
        throw std::runtime_error("DOS intro registered-position regression");
    }
    if (!mf::DosApp::sourceIntroSkipRegressionTest() ||
        !mf::DosApp::sourceGameIntroCompletionRegressionTest())
        throw std::runtime_error("DOS title/menu skip progression regression");
    mf::Audio dosAudio(dosAssets);
    std::size_t dosMidiEvents = 0;
    for (int id : dosAssets.ids("XMI ")) dosMidiEvents += dosAudio.midiEventCount(id);
    constexpr std::array<std::pair<int, std::uint64_t>, 12> dosMusicDurations{{
        {130, 104201}, {134, 5451}, {135, 3309}, {136, 6734},
        {137, 8667}, {138, 6701}, {139, 4376}, {140, 9901},
        {141, 7601}, {142, 7634}, {143, 4209}, {150, 59034},
    }};
    for (const auto& [id, duration] : dosMusicDurations) {
        if (dosAudio.midiDurationMilliseconds(id) != duration)
            throw std::runtime_error("DOS XMI fixed-rate timing audit failed");
    }
    if (mf::audio_catalog::kDosMenuMusic != 130 ||
        mf::audio_catalog::kDosPrimaryGameMusic !=
            std::array<int, 5>{140, 134, 142, 136, 138} ||
        mf::audio_catalog::kDosPlayerWinMusic !=
            std::array<int, 5>{141, 135, 143, 137, 139} ||
        mf::audio_catalog::primaryGameMusic(true, -1) != -1 ||
        mf::audio_catalog::primaryGameMusic(true, 5) != -1 ||
        mf::audio_catalog::playerWinMusic(true, -1) != -1 ||
        mf::audio_catalog::playerWinMusic(true, 5) != -1) {
        throw std::runtime_error("DOS native music routing table changed");
    }
    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        if (mf::audio_catalog::primaryGameMusic(true, gameIndex) !=
                mf::audio_catalog::kDosPrimaryGameMusic[static_cast<std::size_t>(gameIndex)] ||
            mf::audio_catalog::playerWinMusic(true, gameIndex) !=
                mf::audio_catalog::kDosPlayerWinMusic[static_cast<std::size_t>(gameIndex)]) {
            throw std::runtime_error("DOS game music route selection changed");
        }
    }
    std::size_t dosSoundBytes = 0;
    for (int id : dosAssets.ids("SND ")) dosSoundBytes += dosAudio.soundWaveSize(id);
    if (dosMidiEvents == 0 || dosSoundBytes == 0) {
        throw std::runtime_error("DOS XMI/SND media did not pass the native parsers");
    }
    constexpr std::array<int, 28> dosDirectSoundIds{
        5000, 5001, 5003, 5010, 5011, 5012, 5013, 5017, 5018,
        5019, 5023, 5024, 5028, 5032, 5034, 5042, 5043, 5044,
        5053, 5054, 5057, 5072, 8039, 8042, 8046, 9202, 9204, 26015};
    for (int id : dosDirectSoundIds) {
        if (!dosAssets.contains("SND ", id) || dosAudio.soundWaveSize(id) <= 44)
            throw std::runtime_error("DOS native direct SFX/voice mapping is incomplete");
    }
    dosAudio.setEnabled(false);
    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        dosAudio.playMusic(mf::audio_catalog::playerWinMusic(true, gameIndex));
        if (dosAudio.requestedMusicResourceId() !=
                mf::audio_catalog::kDosPlayerWinMusic[static_cast<std::size_t>(gameIndex)]) {
            throw std::runtime_error("DOS player-win music request was not retained");
        }
        dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, gameIndex));
        if (dosAudio.requestedMusicResourceId() !=
                mf::audio_catalog::kDosPrimaryGameMusic[static_cast<std::size_t>(gameIndex)]) {
            throw std::runtime_error("DOS Play Again music request was not restored");
        }
    }
    mf::SourceRandom dosRandom(0x4d474731U);
    mf::GameContext dosContext{dosAssets, dosGraphics, dosAudio, dosRandom,
                               [] {}, L"PLAYER", true};
    auto dosBackgammon = std::make_unique<mf::BackgammonGame>(dosContext);
    auto dosBackgammonStartup = std::make_unique<mf::BackgammonGame>(dosContext);
    auto dosBackgammonSetup = std::make_unique<mf::BackgammonGame>(dosContext);
    auto dosBackgammonHumanOutcome = std::make_unique<mf::BackgammonGame>(dosContext);
    auto dosBackgammonMarioOutcome = std::make_unique<mf::BackgammonGame>(dosContext);
    auto dosBackgammonFullMatch = std::make_unique<mf::BackgammonGame>(dosContext);
    auto dosBackgammonReplay = std::make_unique<mf::BackgammonGame>(dosContext);
    if (!dosBackgammon->sourceStrategyRegressionTest() ||
        !dosBackgammon->sourceDialogueRegressionTest() ||
        !dosBackgammonStartup->sourceStartupRegressionTest() ||
        !dosBackgammonSetup->sourceSetupRevealRegressionTest()) {
        throw std::runtime_error("DOS Backgammon native behavior regression");
    }
    if (!dosBackgammonFullMatch->sourceFullMatchRegressionTest())
        throw std::runtime_error("DOS Backgammon full-match controller regression");
    if (!dosBackgammonReplay->sourceReplayRegressionTest())
        throw std::runtime_error("DOS Backgammon replay-state regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 0));
    if (!dosBackgammonHumanOutcome->sourceOutcomeRegressionTest(true) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(true, 0))
        throw std::runtime_error("DOS Backgammon player outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 0));
    if (!dosBackgammonMarioOutcome->sourceOutcomeRegressionTest(false) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 0))
        throw std::runtime_error("DOS Backgammon Mario outcome/music regression");
    auto dosDominoes = std::make_unique<mf::DominoesGame>(dosContext);
    auto dosDominoesHumanOutcome = std::make_unique<mf::DominoesGame>(dosContext);
    auto dosDominoesMarioOutcome = std::make_unique<mf::DominoesGame>(dosContext);
    auto dosDominoesBlockedHumanOutcome = std::make_unique<mf::DominoesGame>(dosContext);
    auto dosDominoesBlockedMarioOutcome = std::make_unique<mf::DominoesGame>(dosContext);
    auto dosDominoesBlockedTieOutcome = std::make_unique<mf::DominoesGame>(dosContext);
    if (!dosDominoes->sourceStrategyRegressionTest() ||
        !dosDominoes->sourceOpeningRegressionTest() ||
        !dosDominoes->sourceDealPresentationRegressionTest())
        throw std::runtime_error("DOS Dominoes strategy regression");
    auto dosDominoesFullMatch = std::make_unique<mf::DominoesGame>(dosContext);
    if (!dosDominoesFullMatch->sourceFullMatchRegressionTest())
        throw std::runtime_error("DOS Dominoes full-match controller regression");
    if (!dosDominoes->sourceDragRegressionTest() ||
        !dosDominoes->sourceBoneyardHitboxRegressionTest())
        throw std::runtime_error("DOS Dominoes drag regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 1));
    if (!dosDominoesHumanOutcome->sourceOutcomeRegressionTest(1, false) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(true, 1))
        throw std::runtime_error("DOS Dominoes last-tile player outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 1));
    if (!dosDominoesMarioOutcome->sourceOutcomeRegressionTest(-1, false) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 1))
        throw std::runtime_error("DOS Dominoes last-tile Mario outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 1));
    if (!dosDominoesBlockedHumanOutcome->sourceOutcomeRegressionTest(1, true) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(true, 1))
        throw std::runtime_error("DOS Dominoes blocked-player outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 1));
    if (!dosDominoesBlockedMarioOutcome->sourceOutcomeRegressionTest(-1, true) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 1))
        throw std::runtime_error("DOS Dominoes blocked-Mario outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 1));
    if (!dosDominoesBlockedTieOutcome->sourceOutcomeRegressionTest(2, true) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 1))
        throw std::runtime_error("DOS Dominoes blocked-tie outcome/music regression");
    if (!dosDominoes->sourceReplayRegressionTest())
        throw std::runtime_error("DOS Dominoes replay regression");
    auto dosCheckers = std::make_unique<mf::CheckersGame>(dosContext);
    auto dosCheckersHumanElimination = std::make_unique<mf::CheckersGame>(dosContext);
    auto dosCheckersHumanStuck = std::make_unique<mf::CheckersGame>(dosContext);
    auto dosCheckersFirstMario = std::make_unique<mf::CheckersGame>(dosContext);
    auto dosCheckersLaterMario = std::make_unique<mf::CheckersGame>(dosContext);
    if (!dosCheckers->sourceStrategyRegressionTest() ||
        !dosCheckers->sourceReplayRegressionTest()) {
        throw std::runtime_error("DOS Checkers native behavior regression");
    }
    auto dosCheckersFullMatch = std::make_unique<mf::CheckersGame>(dosContext);
    if (!dosCheckersFullMatch->sourceFullMatchRegressionTest())
        throw std::runtime_error("DOS Checkers full-match controller regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 2));
    if (!dosCheckersHumanElimination->sourceOutcomeRegressionTest(1) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(true, 2))
        throw std::runtime_error("DOS Checkers elimination outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 2));
    if (!dosCheckersHumanStuck->sourceOutcomeRegressionTest(2) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 2))
        throw std::runtime_error("DOS Checkers no-legal-move outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 2));
    if (!dosCheckersFirstMario->sourceOutcomeRegressionTest(-1) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 2))
        throw std::runtime_error("DOS Checkers first Mario outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 2));
    if (!dosCheckersLaterMario->sourceOutcomeRegressionTest(-2) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 2))
        throw std::runtime_error("DOS Checkers later Mario outcome/music regression");
    auto dosGoFish = std::make_unique<mf::GoFishGame>(dosContext);
    auto dosGoFishHumanOutcome = std::make_unique<mf::GoFishGame>(dosContext);
    auto dosGoFishMarioOutcome = std::make_unique<mf::GoFishGame>(dosContext);
    auto dosGoFishTieOutcome = std::make_unique<mf::GoFishGame>(dosContext);
    auto dosGoFishReplay = std::make_unique<mf::GoFishGame>(dosContext);
    if (!dosGoFish->sourceStrategyRegressionTest())
        throw std::runtime_error("DOS Go Fish strategy regression");
    if (!dosGoFish->sourceDialogueRegressionTest())
        throw std::runtime_error("DOS Go Fish dialogue regression");
    if (!dosGoFish->sourceHandSlotRegressionTest())
        throw std::runtime_error("DOS Go Fish hand-slot regression");
    if (!dosGoFish->sourceOpeningDealRegressionTest())
        throw std::runtime_error("DOS Go Fish opening-controller regression");
    auto dosGoFishRefill = std::make_unique<mf::GoFishGame>(dosContext);
    if (!dosGoFishRefill->sourceEmptyHandRefillRegressionTest())
        throw std::runtime_error("DOS Go Fish empty-hand refill regression");
    auto dosGoFishFullMatch = std::make_unique<mf::GoFishGame>(dosContext);
    if (!dosGoFishFullMatch->sourceFullMatchRegressionTest())
        throw std::runtime_error("DOS Go Fish full-match controller regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 3));
    if (!dosGoFishHumanOutcome->sourceOutcomeRegressionTest(1) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(true, 3))
        throw std::runtime_error("DOS Go Fish player outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 3));
    if (!dosGoFishMarioOutcome->sourceOutcomeRegressionTest(-1) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 3))
        throw std::runtime_error("DOS Go Fish Mario outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 3));
    if (!dosGoFishTieOutcome->sourceOutcomeRegressionTest(2) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 3))
        throw std::runtime_error("DOS Go Fish tie outcome/music regression");
    if (!dosGoFishReplay->sourceReplayRegressionTest())
        throw std::runtime_error("DOS Go Fish replay-state regression");
    auto dosYachtSelection = std::make_unique<mf::YachtGame>(dosContext);
    auto dosYachtTurnOrder = std::make_unique<mf::YachtGame>(dosContext);
    auto dosYachtHumanOutcome = std::make_unique<mf::YachtGame>(dosContext);
    auto dosYachtMarioOutcome = std::make_unique<mf::YachtGame>(dosContext);
    auto dosYachtTieOutcome = std::make_unique<mf::YachtGame>(dosContext);
    auto dosYachtCupPresentation = std::make_unique<mf::YachtGame>(dosContext);
    auto dosYachtFullMatch = std::make_unique<mf::YachtGame>(dosContext);
    auto dosYachtReplay = std::make_unique<mf::YachtGame>(dosContext);
    if (!dosYachtSelection->sourceComputerSelectionRegressionTest() ||
        !dosYachtTurnOrder->sourceTurnOrderRegressionTest() ||
        !dosYachtCupPresentation->sourceCupPresentationRegressionTest() ||
        !dosYachtFullMatch->sourceFullMatchRegressionTest() ||
        !dosYachtReplay->sourceReplayRegressionTest()) {
        throw std::runtime_error("DOS Yacht native behavior regression");
    }
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 4));
    if (!dosYachtHumanOutcome->sourceOutcomeRegressionTest(1) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::playerWinMusic(true, 4))
        throw std::runtime_error("DOS Yacht player outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 4));
    if (!dosYachtMarioOutcome->sourceOutcomeRegressionTest(-1) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 4))
        throw std::runtime_error("DOS Yacht Mario outcome/music regression");
    dosAudio.playMusic(mf::audio_catalog::primaryGameMusic(true, 4));
    if (!dosYachtTieOutcome->sourceOutcomeRegressionTest(2) ||
        dosAudio.requestedMusicResourceId() != mf::audio_catalog::primaryGameMusic(true, 4))
        throw std::runtime_error("DOS Yacht tie outcome/music regression");
    mf::Canvas dosCanvas(mf::kDosLogicalWidth, mf::kDosLogicalHeight);
    std::array<std::unique_ptr<mf::Game>, 5> dosGames{
        std::make_unique<mf::BackgammonGame>(dosContext),
        std::make_unique<mf::DominoesGame>(dosContext),
        std::make_unique<mf::CheckersGame>(dosContext),
        std::make_unique<mf::GoFishGame>(dosContext),
        std::make_unique<mf::YachtGame>(dosContext)};
    for (const auto& game : dosGames) {
        game->render(dosCanvas);
        (void)game->tick();
        game->render(dosCanvas);
    }

    std::fprintf(stdout,
                 "PASS mac_assets=1707 mac_pak=180 mac_frames=%zu mac_movies=467 "
                 "mac_commands=%zu mac_midi_events=%zu mac_sounds=313 games=5 "
                 "dos_assets=1806 dos_pak=187 dos_frames=%zu dos_movies=574 "
                 "dos_commands=%zu dos_xmi_events=%zu dos_sounds=278\n",
                 decodedFrames, movieCommands, midiEvents, dosFrames, dosMovieCommands,
                 dosMidiEvents);
    std::fflush(stdout);
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const wchar_t* commandLine = GetCommandLineW();
    const bool isSelfTest = std::wcsstr(commandLine, L"--self-test") != nullptr;
    if (isSelfTest) {
        try {
            return selfTest(instance);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL %s\n", error.what());
            std::fflush(stderr);
            return 1;
        }
    }
    if (std::wcsstr(commandLine, L"--render-dos-qa") != nullptr) {
        try {
            mf::DosApp app(instance);
            app.renderQaFrames(L"work\\qa\\dos");
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL %s\n", error.what());
            std::fflush(stderr);
            return 1;
        }
    }
    if (std::wcsstr(commandLine, L"--render-mac-qa") != nullptr) {
        try {
            mf::App app(instance);
            app.renderQaFrames(L"work\\qa\\mac");
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL %s\n", error.what());
            std::fflush(stderr);
            return 1;
        }
    }
    try {
        mf::GameEdition edition = mf::GameEdition::Cancel;
        if (std::wcsstr(commandLine, L"--edition=dos") ||
            std::wcsstr(commandLine, L"--qa-dos")) {
            edition = mf::GameEdition::Dos;
        } else if (std::wcsstr(commandLine, L"--edition=mac") ||
                   std::wcsstr(commandLine, L"--qa-")) {
            edition = mf::GameEdition::Macintosh;
        } else {
            edition = mf::chooseGameEdition(instance, showCommand);
        }
        if (edition == mf::GameEdition::Dos) {
            mf::DosApp app(instance);
            return app.run(showCommand);
        }
        if (edition == mf::GameEdition::Macintosh) {
            mf::App app(instance);
            return app.run(showCommand);
        }
        return 0;
    } catch (const std::exception& error) {
        const std::string text = std::string("The native Mario collection could not start:\n\n") +
                                 error.what();
        MessageBoxA(nullptr, text.c_str(), "Mario native collection", MB_OK | MB_ICONERROR);
        return 1;
    }
}
