#include "app.hpp"

#include "about.hpp"
#include "audio_catalog.hpp"
#include "menu_catalog.hpp"

#include "games/backgammon.hpp"
#include "games/checkers.hpp"
#include "games/dominoes.hpp"
#include "games/go_fish.hpp"
#include "games/yacht.hpp"

#include <commctrl.h>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <windowsx.h>

namespace mf {
namespace {

constexpr UINT kCommandNewGame = 1001;
constexpr UINT kCommandMainMenu = 1002;
constexpr UINT kCommandExit = 1003;
constexpr UINT kCommandChangeName = 1101;
constexpr UINT kCommandSound = 1102;
constexpr UINT kCommandMusic = 1103;
constexpr UINT kCommandHideBackground = 1104;
constexpr UINT kCommandAnimatedPieces = 1105;
constexpr UINT kCommandForcedJumps = 1106;
constexpr UINT kCommandUndo = 1301;
constexpr UINT kCommandCut = 1302;
constexpr UINT kCommandCopy = 1303;
constexpr UINT kCommandPaste = 1304;
constexpr UINT kCommandClear = 1305;
constexpr UINT kCommandHelpBackgammon = 1201;
constexpr UINT kCommandHelpDominoes = 1202;
constexpr UINT kCommandHelpCheckers = 1203;
constexpr UINT kCommandHelpGoFish = 1204;
constexpr UINT kCommandHelpYacht = 1205;
constexpr UINT kCommandAbout = 1209;
constexpr UINT kCommandCredits = 1210;

constexpr std::array<int, 5> kHelpBitmapBase{0, 2, 4, 5, 7};
constexpr std::array<int, 5> kHelpPageCount{2, 2, 1, 2, 2};
constexpr wchar_t kPreferencesKey[] = L"Software\\MarioFundamentalsNative";
// CODE 6 initializes its Pak-backed editor's character cap at $0f.
constexpr std::size_t kMaximumPlayerNameLength = 15;

constexpr bool macTitleIntroInputSkips(int phase) {
    // CODE 12 $1DDC handles mouse-down and $1E14 handles Escape while the
    // title-stage controller is active. Both post the same $2E0 completion
    // callback as its natural end at $217A.
    return phase >= 7;
}

enum class MacGameIntroMouseAction {
    Ignore,
    AdvanceOneTick,
    Finish,
};

constexpr MacGameIntroMouseAction macGameIntroMouseAction(int gameIndex) {
    // Backgammon CODE 11 $24/$2A routes key-down and mouse-down to the
    // completion post at $1C4. Yacht CODE 18 $20/$26 does the same at $228.
    // Dominoes CODE 14 $32 routes mouse-down through one $9BE controller
    // advance; Checkers CODE 16 $7F4 and Go Fish CODE 17 $276 leave their
    // title controllers locked/inactive and therefore ignore it.
    if (gameIndex == 0 || gameIndex == 4) return MacGameIntroMouseAction::Finish;
    if (gameIndex == 1) return MacGameIntroMouseAction::AdvanceOneTick;
    return MacGameIntroMouseAction::Ignore;
}

constexpr bool macGameIntroKeySkips(int gameIndex) {
    return gameIndex == 0 || gameIndex == 4;
}

}  // namespace

App::App(HINSTANCE instance)
    : instance_(instance), assets_(instance, IDR_ASSET_PACK), graphics_(assets_), audio_(assets_),
      sourceFonts_(instance),
      introHandMovie_(assets_, 1111), talkingHeadMovie_(assets_, 12091),
      menuRevealMovie_(assets_, 1125), menuSelectionHost_(assets_, graphics_, audio_),
      menuBowTieHost_(assets_, graphics_, audio_),
      gameCharacterHost_(assets_, graphics_, audio_),
      random_(1) {
    // CODE 13 $6CC advances QuickDraw Random TickCount & 0x3ff times. The
    // source TickCount is a 60 Hz boot-relative counter.
    const auto sourceTicks = static_cast<std::uint32_t>(GetTickCount64() * 60ULL / 1000ULL);
    random_.discard(sourceTicks & 0x3ffU);
    titleBitmap_ = LoadBitmapW(instance_, MAKEINTRESOURCEW(IDB_TITLE));
    creditsBitmap_ = LoadBitmapW(instance_, MAKEINTRESOURCEW(IDB_CREDITS));
    brainstormBitmap_ = LoadBitmapW(instance_, MAKEINTRESOURCEW(IDB_BRAINSTORM));
    brainstormFadeBitmap_ = LoadBitmapW(instance_, MAKEINTRESOURCEW(IDB_BRAINSTORM_FADE));
    steppingStoneBitmap_ = LoadBitmapW(instance_, MAKEINTRESOURCEW(IDB_STEPPING_STONE));
    steppingStoneFadeBitmap_ = LoadBitmapW(instance_, MAKEINTRESOURCEW(IDB_STEPPING_STONE_FADE));
    constexpr std::array<int, 9> helpIds{
        IDB_HELP_BACKGAMMON_1, IDB_HELP_BACKGAMMON_2,
        IDB_HELP_DOMINOES_1, IDB_HELP_DOMINOES_2, IDB_HELP_CHECKERS_1,
        IDB_HELP_GOFISH_1, IDB_HELP_GOFISH_2, IDB_HELP_YACHT_1, IDB_HELP_YACHT_2};
    for (std::size_t index = 0; index < helpBitmaps_.size(); ++index)
        helpBitmaps_[index] = LoadBitmapW(instance_, MAKEINTRESOURCEW(helpIds[index]));
    preferencesEnabled_ = std::wcsstr(GetCommandLineW(), L"--qa-") == nullptr &&
                          std::wcsstr(GetCommandLineW(), L"--render-mac-qa") == nullptr;
    if (preferencesEnabled_) loadPreferences();
    if (preferencesEnabled_ && audio_.musicEnabled()) audio_.prewarmMusicOutput();
}

App::~App() {
    if (preferencesEnabled_) savePreferences();
    if (paintBufferDc_ && paintBufferPreviousBitmap_)
        SelectObject(paintBufferDc_, paintBufferPreviousBitmap_);
    if (paintBufferBitmap_) DeleteObject(paintBufferBitmap_);
    if (paintBufferDc_) DeleteDC(paintBufferDc_);
    if (titleBitmap_) DeleteObject(titleBitmap_);
    if (creditsBitmap_) DeleteObject(creditsBitmap_);
    if (brainstormBitmap_) DeleteObject(brainstormBitmap_);
    if (brainstormFadeBitmap_) DeleteObject(brainstormFadeBitmap_);
    if (steppingStoneBitmap_) DeleteObject(steppingStoneBitmap_);
    if (steppingStoneFadeBitmap_) DeleteObject(steppingStoneFadeBitmap_);
    for (HBITMAP bitmap : helpBitmaps_) if (bitmap) DeleteObject(bitmap);
}

bool App::sourceIntroSkipRegressionTest() {
    return !macTitleIntroInputSkips(static_cast<int>(IntroPhase::StartupBlack)) &&
           !macTitleIntroInputSkips(static_cast<int>(IntroPhase::TitleGap)) &&
           macTitleIntroInputSkips(static_cast<int>(IntroPhase::Silhouette)) &&
           macTitleIntroInputSkips(static_cast<int>(IntroPhase::Greeting)) &&
           macTitleIntroInputSkips(static_cast<int>(IntroPhase::TalkingHead)) &&
           macTitleIntroInputSkips(static_cast<int>(IntroPhase::MenuReveal)) &&
           macGameIntroKeySkips(0) && !macGameIntroKeySkips(1) &&
           !macGameIntroKeySkips(2) && !macGameIntroKeySkips(3) &&
           macGameIntroKeySkips(4) &&
           macGameIntroMouseAction(0) == MacGameIntroMouseAction::Finish &&
           macGameIntroMouseAction(1) == MacGameIntroMouseAction::AdvanceOneTick &&
           macGameIntroMouseAction(2) == MacGameIntroMouseAction::Ignore &&
           macGameIntroMouseAction(3) == MacGameIntroMouseAction::Ignore &&
           macGameIntroMouseAction(4) == MacGameIntroMouseAction::Finish;
}

void App::renderQaFrames(std::wstring_view outputDirectory) {
    audio_.setEnabled(false);
    const std::filesystem::path root(outputDirectory);
    std::filesystem::create_directories(root);
    const auto save = [this, &root](std::wstring_view name) {
        render();
        canvas_.saveBmp((root / name).wstring());
    };

    constexpr std::array<std::pair<IntroPhase, std::wstring_view>, 8> startupFrames{{
        {IntroPhase::Brainstorm, L"00-brainstorm.bmp"},
        {IntroPhase::BrainstormFade, L"01-brainstorm-fade.bmp"},
        {IntroPhase::SteppingStone, L"02-stepping-stone.bmp"},
        {IntroPhase::SteppingStoneFade, L"03-stepping-stone-fade.bmp"},
        {IntroPhase::Silhouette, L"04-title-silhouette.bmp"},
        {IntroPhase::Greeting, L"05-title-greeting.bmp"},
        {IntroPhase::TalkingHead, L"06-title-talking.bmp"},
        {IntroPhase::MenuReveal, L"07-menu-reveal-start.bmp"},
    }};
    screen_ = Screen::Intro;
    for (const auto& [phase, name] : startupFrames) {
        introPhase_ = phase;
        introPhaseMilliseconds_ = phase == IntroPhase::TalkingHead ? 600U : 0U;
        save(name);
        if (phase == IntroPhase::Silhouette &&
            canvas_.pixelHash({0, 0, kLogicalWidth, kLogicalHeight}) !=
                0x1CAA563B9984B116ULL) {
            throw std::runtime_error(
                "Macintosh title board artwork appeared before the source reveal");
        }
        if (phase == IntroPhase::Greeting &&
            canvas_.pixelHash({400, 135, 485, 220}) != 0xA24FE4462B4A17E7ULL) {
            throw std::runtime_error(
                "Macintosh title hand is not the source terminal open-hand cel");
        }
    }

    constexpr std::array<std::pair<std::uint32_t, std::wstring_view>, 4> revealFrames{{
        {0, L"07a-menu-reveal-00.bmp"},
        {300, L"07b-menu-reveal-20.bmp"},
        {750, L"07c-menu-reveal-50.bmp"},
        {1499, L"07d-menu-reveal-final.bmp"},
    }};
    introPhase_ = IntroPhase::MenuReveal;
    for (const auto& [milliseconds, name] : revealFrames) {
        introPhaseMilliseconds_ = milliseconds;
        save(name);
    }

    // Exercise the actual mouse-down route through both a live title pose and
    // the board-flip phase. Both must install movie 1111's terminal
    // hand/easel cel before the menu frame.
    screen_ = Screen::Intro;
    introPhase_ = IntroPhase::TalkingHead;
    introPhaseMilliseconds_ = 600U;
    save(L"08-skip-before.bmp");
    click({256, 192});
    if (screen_ != Screen::Menu)
        throw std::runtime_error("Macintosh live-title mouse-down did not finish the intro");
    save(L"09-skip-menu.bmp");
    const std::uint64_t liveTitleSkipHash =
        canvas_.pixelHash({0, 0, kLogicalWidth, kLogicalHeight});

    screen_ = Screen::Intro;
    introPhase_ = IntroPhase::MenuReveal;
    introPhaseMilliseconds_ = 300U;
    save(L"09a-board-skip-before.bmp");
    // Use a point visibly inside the easel board, matching the user's actual
    // click route rather than relying on an arbitrary stage coordinate.
    click({150, 250});
    if (screen_ != Screen::Menu)
        throw std::runtime_error("Macintosh board-reveal mouse-down did not finish the intro");
    save(L"09b-board-skip-menu.bmp");
    if (canvas_.pixelHash({0, 0, kLogicalWidth, kLogicalHeight}) != liveTitleSkipHash)
        throw std::runtime_error("Macintosh board-reveal skip did not install the final menu pose");

    for (int sourceSelection = 1; sourceSelection <= 5; ++sourceSelection) {
        menuSourceSelection_ = sourceSelection;
        showSelectedMenuPose();
        save(L"10-menu-selection-" + std::to_wstring(sourceSelection) + L".bmp");
        if (sourceSelection == 1 &&
            canvas_.pixelHash({0, 0, kLogicalWidth, kLogicalHeight}) !=
                0xC8DC550B480F2920ULL) {
            throw std::runtime_error("Macintosh single-hand menu composition regression");
        }
    }

    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        beginGameIntro(gameIndex);
        for (int sample = 0; sample <= 20; ++sample) {
            gameIntroMilliseconds_ = gameIntroDurationMilliseconds_ *
                                     static_cast<std::uint32_t>(sample) / 20U;
            save(L"11-intro-" + std::to_wstring(gameIndex) + L"-" +
                 (sample < 10 ? L"0" : L"") + std::to_wstring(sample) + L".bmp");
        }
    }
    gameIntroMovies_.clear();

    // Preserve the first-use character/name transitions over their live game
    // pages. These catch prompt/board ordering, actor layering, and modal
    // registration instead of auditing those pieces apart.
    characterConfirmed_ = false;
    nameConfirmed_ = false;
    playerName_ = L"My friend";
    for (int gameIndex = 0; gameIndex <= 2; ++gameIndex) {
        beginGameCharacter(gameIndex);
        gameCharacterHost_.stop();
        save(gameIndex == 0 ? L"12-backgammon-character-choice.bmp" :
             L"12-character-choice-" + std::to_wstring(gameIndex) + L".bmp");
        game_.reset();
    }
    characterConfirmed_ = true;
    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        beginGameName(gameIndex);
        // Capture the source panel between insertion-point blinks.
        introPhaseMilliseconds_ = 500U;
        save(L"13-name-prompt-" + std::to_wstring(gameIndex) + L".bmp");
        game_.reset();
    }
    nameConfirmed_ = true;

    // Exercise each source mouse-down route with first-use prompts already
    // satisfied. Backgammon/Yacht finish, Dominoes advances one controller
    // tick, and Checkers/Go Fish retain the current title frame.
    characterConfirmed_ = true;
    nameConfirmed_ = true;
    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        beginGameIntro(gameIndex);
        gameIntroMilliseconds_ = gameIntroDurationMilliseconds_ / 2U;
        click({256, 192});
        const bool shouldFinish = gameIndex == 0 || gameIndex == 4;
        if ((screen_ != Screen::GameIntro) != shouldFinish)
            throw std::runtime_error("Macintosh game-intro mouse route regression");
        save(L"11-input-game-intro-" + std::to_wstring(gameIndex) + L".bmp");
    }
    game_.reset();

    // Exercise the real Windows key route as well. A printable Backgammon
    // skip must not leak its generated WM_CHAR into the name editor, while
    // Escape must leave a non-skippable Dominoes title active.
    nameConfirmed_ = false;
    playerName_ = L"My friend";
    beginGameIntro(0);
    handleMessage(window_, WM_KEYDOWN, 'A', 0);
    handleMessage(window_, WM_CHAR, 'A', 0);
    if (screen_ != Screen::GameName || playerName_ != L"My friend")
        throw std::runtime_error("Macintosh game-intro key/character route regression");
    nameConfirmed_ = true;
    beginGameIntro(1);
    handleMessage(window_, WM_KEYDOWN, VK_ESCAPE, 0);
    if (screen_ != Screen::GameIntro)
        throw std::runtime_error("Macintosh non-skippable game-intro Escape regression");
    gameIntroMovies_.clear();

    constexpr std::array<int, 8> openingTicks{0, 1, 8, 16, 32, 64, 96, 128};
    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        startGame(gameIndex);
        int elapsedTicks = 0;
        for (const int targetTicks : openingTicks) {
            while (elapsedTicks < targetTicks) {
                (void)game_->tick();
                ++elapsedTicks;
            }
            game_->render(canvas_);
            canvas_.saveBmp((root / (L"2" + std::to_wstring(gameIndex) +
                L"-opening-" + std::to_wstring(targetTicks) + L".bmp")).wstring());
        }
    }

    // Render deterministic interactive and outcome states, not only passive
    // openings. These setters exercise the same game renderers used by live
    // mouse/turn paths and expose layering, geometry, text, and sprite errors.
    startGame(0);
    if (auto* backgammon = dynamic_cast<BackgammonGame*>(game_.get())) {
        for (const int revealed : std::array{0, 1, 5, 10, 15, 30}) {
            backgammon->setQaSetupRevealPresentation(revealed);
            save(std::wstring(L"31-backgammon-setup-") + (revealed < 10 ? L"0" : L"") +
                 std::to_wstring(revealed) + L".bmp");
        }
    }

    startGame(1);
    if (auto* dominoes = dynamic_cast<DominoesGame*>(game_.get())) {
        dominoes->setQaDragPresentation();
        save(L"32-dominoes-drag.bmp");
    }
    constexpr std::array<std::pair<int, std::wstring_view>, 3> dominoOutcomes{{
        {1, L"human"}, {-1, L"mario"}, {2, L"tie"},
    }};
    for (const auto& [winner, label] : dominoOutcomes) {
        startGame(1);
        if (auto* dominoes = dynamic_cast<DominoesGame*>(game_.get())) {
            dominoes->setQaOutcomePresentation(winner, winner == 2);
            save(L"33-dominoes-outcome-" + std::wstring(label) + L".bmp");
        }
    }

    constexpr std::array<std::pair<int, std::wstring_view>, 4> checkerOutcomes{{
        {1, L"human-elimination"}, {2, L"human-stuck"},
        {-1, L"mario-first"}, {-2, L"mario-later"},
    }};
    for (const auto& [variant, label] : checkerOutcomes) {
        startGame(2);
        if (auto* checkers = dynamic_cast<CheckersGame*>(game_.get())) {
            checkers->setQaOutcomePresentation(variant);
            save(L"34-checkers-outcome-" + std::wstring(label) + L".bmp");
        }
    }

    startGame(3);
    if (auto* goFish = dynamic_cast<GoFishGame*>(game_.get())) {
        goFish->setQaHandSlotsPresentation(false);
        save(L"35-gofish-hand.bmp");
        if (canvas_.pixelHash({230, 35, 283, 75}) != 0x6F80822EB2A44F3DULL)
            throw std::runtime_error("Macintosh Go Fish idle head is not the solid source actor");
        goFish->setQaQuestionPresentation();
        save(L"35-gofish-question.bmp");
        goFish->setQaHandSlotsPresentation(true);
        save(L"35-gofish-hand-transfer.bmp");
        if (canvas_.pixelHash({230, 35, 283, 75}) != 0x6F80822EB2A44F3DULL)
            throw std::runtime_error("Macintosh Go Fish question card damaged the idle head");
    }
    for (const int letters : std::array{0, 3, 7}) {
        startGame(3);
        if (auto* goFish = dynamic_cast<GoFishGame*>(game_.get())) {
            goFish->setQaVictoryPresentation(letters);
            save(L"35-gofish-victory-" + std::to_wstring(letters) + L".bmp");
        }
    }

    startGame(4);
    if (auto* yacht = dynamic_cast<YachtGame*>(game_.get())) {
        yacht->setQaScorecardPresentation();
        save(L"36-yacht-scorecard.bmp");
        yacht->setQaComputerDicePresentation();
        save(L"36-yacht-computer-dice.bmp");
        if (canvas_.pixelHash({210, 120, 300, 200}) != 0xE0A86509681E5B97ULL)
            throw std::runtime_error("Macintosh Yacht idle actor lost its composed hand layer");
        yacht->setQaDiceSelectionPresentation();
        save(L"36-yacht-dice-selection.bmp");
        yacht->setQaVictoryPresentation();
        save(L"36-yacht-victory.bmp");
        for (std::uint32_t sourceTime = 0; sourceTime < 600; sourceTime += 60) {
            yacht->setQaRerollGesturePresentation(sourceTime);
            save(sourceTime == 240 ? L"36-yacht-reroll-gesture.bmp" :
                 L"36-yacht-reroll-gesture-" + std::to_wstring(sourceTime) + L".bmp");
        }
    }

    startGame(4);
    if (auto* yacht = dynamic_cast<YachtGame*>(game_.get())) {
        yacht->setQaStationaryCupPresentation();
        save(L"36-yacht-stationary-cup.bmp");
        const std::uint64_t stationaryCupHash = canvas_.pixelHash({218, 129, 296, 228});
        yacht->setQaRollPresentation();
        constexpr std::array<int, 7> rollTicks{0, 4, 8, 16, 32, 48, 56};
        int elapsedTicks = 0;
        for (const int targetTicks : rollTicks) {
            while (elapsedTicks < targetTicks) {
                (void)yacht->tick();
                ++elapsedTicks;
            }
            yacht->render(canvas_);
            canvas_.saveBmp((root / (L"30-yacht-roll-" +
                std::to_wstring(targetTicks) + L".bmp")).wstring());
            if (targetTicks == 0 &&
                canvas_.pixelHash({218, 129, 296, 228}) != stationaryCupHash) {
                throw std::runtime_error(
                    "Macintosh Yacht cup moved or duplicated on roll contact");
            }
        }
    }
}

void App::createWindow(int showCommand) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = L"MarioFundamentalsNativeWindow";
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass)) throw std::runtime_error("window class registration failed");
    // A 1.5x client fits a 1366x768-class desktop including normal window chrome.
    HMENU menuBar = CreateMenu();
    fileMenu_ = CreatePopupMenu();
    editMenu_ = CreatePopupMenu();
    optionsMenu_ = CreatePopupMenu();
    HMENU helpMenu = CreatePopupMenu();
    AppendMenuW(editMenu_, MF_STRING, kCommandUndo, L"&Undo\tCtrl+Z");
    AppendMenuW(editMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu_, MF_STRING, kCommandCut, L"Cu&t\tCtrl+X");
    AppendMenuW(editMenu_, MF_STRING, kCommandCopy, L"&Copy\tCtrl+C");
    AppendMenuW(editMenu_, MF_STRING, kCommandPaste, L"&Paste\tCtrl+V");
    AppendMenuW(editMenu_, MF_STRING, kCommandClear, L"C&lear");
    AppendMenuW(helpMenu, MF_STRING, kCommandHelpBackgammon, L"&Backgammon");
    AppendMenuW(helpMenu, MF_STRING, kCommandHelpCheckers, L"&Checkers");
    AppendMenuW(helpMenu, MF_STRING, kCommandHelpDominoes, L"&Dominoes");
    AppendMenuW(helpMenu, MF_STRING, kCommandHelpGoFish, L"&Go Fish");
    AppendMenuW(helpMenu, MF_STRING, kCommandHelpYacht, L"&Yacht");
    AppendMenuW(helpMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(helpMenu, MF_STRING, kCommandAbout, L"&About Mario's FUNdamentals");
    AppendMenuW(helpMenu, MF_STRING, kCommandCredits, L"&Credits");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu_), L"&File");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(editMenu_), L"&Edit");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(optionsMenu_), L"&Options");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"&Help");

    RECT bounds{0, 0, 768, 576};
    AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, TRUE, 0);
    window_ = CreateWindowExW(0, windowClass.lpszClassName, L"Mario's FUNdamentals",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              bounds.right - bounds.left, bounds.bottom - bounds.top,
                              nullptr, menuBar, instance_, this);
    if (!window_) {
        DestroyMenu(menuBar);
        throw std::runtime_error("window creation failed");
    }
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    updateFileMenu();
    updateEditMenu();
    updateOptionsMenu();
    SetTimer(window_, 1, 33, nullptr);
}

int App::run(int showCommand) {
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    createWindow(showCommand);
    // Automated capture and regression modes must never emit voices, effects, or MIDI.
    // They also bypass persisted preferences, so this does not affect normal launches.
    if (!preferencesEnabled_) audio_.setEnabled(false);
    if (restoreBackgroundHidden_) toggleBackground();
    if (std::wcsstr(GetCommandLineW(), L"--qa-about")) {
        screen_ = Screen::About;
        pictureReturnScreen_ = Screen::Menu;
        InvalidateRect(window_, nullptr, FALSE);
    } else if (std::wcsstr(GetCommandLineW(), L"--qa-credits")) {
        screen_ = Screen::Credits;
        pictureReturnScreen_ = Screen::Menu;
        InvalidateRect(window_, nullptr, FALSE);
    } else if (std::wcsstr(GetCommandLineW(), L"--qa-character")) {
        playerName_ = L"PLAYER";
        screen_ = Screen::Character;
        InvalidateRect(window_, nullptr, FALSE);
    } else if (const wchar_t* qaDialog = std::wcsstr(GetCommandLineW(), L"--qa-dialog=")) {
        playerName_ = L"PLAYER";
        startGame(0);
        dialog_ = std::wcstol(qaDialog + 12, nullptr, 10) == 106
                      ? Dialog::PlayAgain : Dialog::ConfirmReset;
    } else if (const wchar_t* qaIntro = std::wcsstr(GetCommandLineW(), L"--qa-intro=")) {
        playerName_ = L"PLAYER";
        beginGameIntro(static_cast<int>(std::wcstol(qaIntro + 11, nullptr, 10)));
    } else if (const wchar_t* qaGameCharacter =
                   std::wcsstr(GetCommandLineW(), L"--qa-game-character=")) {
        playerName_ = L"PLAYER";
        beginGameCharacter(static_cast<int>(std::wcstol(qaGameCharacter + 20, nullptr, 10)));
    } else if (const wchar_t* qaGameName =
                   std::wcsstr(GetCommandLineW(), L"--qa-game-name=")) {
        beginGameName(static_cast<int>(std::wcstol(qaGameName + 15, nullptr, 10)));
    } else if (const wchar_t* qaBackgammonSetup =
                   std::wcsstr(GetCommandLineW(), L"--qa-backgammon-setup=")) {
        playerName_ = L"PLAYER";
        startGame(0);
        if (auto* backgammon = dynamic_cast<BackgammonGame*>(game_.get())) {
            backgammon->setQaSetupRevealPresentation(
                static_cast<int>(std::wcstol(
                    qaBackgammonSetup + std::wcslen(L"--qa-backgammon-setup="), nullptr, 10)));
        }
    } else if (std::wcsstr(GetCommandLineW(), L"--qa-gofish-hand-transfer")) {
        playerName_ = L"PLAYER";
        startGame(3);
        if (auto* goFish = dynamic_cast<GoFishGame*>(game_.get())) {
            goFish->setQaHandSlotsPresentation(true);
        }
    } else if (std::wcsstr(GetCommandLineW(), L"--qa-gofish-hand")) {
        playerName_ = L"PLAYER";
        startGame(3);
        if (auto* goFish = dynamic_cast<GoFishGame*>(game_.get())) {
            goFish->setQaHandSlotsPresentation(false);
        }
    } else if (const wchar_t* qaGoFishVictory =
                   std::wcsstr(GetCommandLineW(), L"--qa-gofish-victory=")) {
        playerName_ = L"PLAYER";
        startGame(3);
        if (auto* goFish = dynamic_cast<GoFishGame*>(game_.get())) {
            goFish->setQaVictoryPresentation(
                static_cast<int>(std::wcstol(qaGoFishVictory + 20, nullptr, 10)));
        }
    } else if (std::wcsstr(GetCommandLineW(), L"--qa-dominoes-drag")) {
        playerName_ = L"PLAYER";
        startGame(1);
        if (auto* dominoes = dynamic_cast<DominoesGame*>(game_.get())) {
            dominoes->setQaDragPresentation();
        }
    } else if (const wchar_t* qaDominoesOutcome =
                   std::wcsstr(GetCommandLineW(), L"--qa-dominoes-outcome=")) {
        playerName_ = L"PLAYER";
        startGame(1);
        const int value = static_cast<int>(std::wcstol(qaDominoesOutcome + 22, nullptr, 10));
        if (auto* dominoes = dynamic_cast<DominoesGame*>(game_.get())) {
            dominoes->setQaOutcomePresentation(value == 12 ? 2 : value < 0 ? -1 : 1,
                                               value >= 10 || value <= -10);
        }
    } else if (const wchar_t* qaCheckersOutcome =
                   std::wcsstr(GetCommandLineW(), L"--qa-checkers-outcome=")) {
        playerName_ = L"PLAYER";
        startGame(2);
        if (auto* checkers = dynamic_cast<CheckersGame*>(game_.get())) {
            checkers->setQaOutcomePresentation(
                static_cast<int>(std::wcstol(qaCheckersOutcome + 22, nullptr, 10)));
        }
    } else if (std::wcsstr(GetCommandLineW(), L"--qa-yacht-dice-selection")) {
        playerName_ = L"PLAYER";
        startGame(4);
        if (auto* yacht = dynamic_cast<YachtGame*>(game_.get())) {
            yacht->setQaDiceSelectionPresentation();
        }
    } else if (std::wcsstr(GetCommandLineW(), L"--qa-yacht-scorecard")) {
        playerName_ = L"PLAYER";
        startGame(4);
        if (auto* yacht = dynamic_cast<YachtGame*>(game_.get())) {
            yacht->setQaScorecardPresentation();
        }
    } else if (std::wcsstr(GetCommandLineW(), L"--qa-yacht-victory")) {
        playerName_ = L"PLAYER";
        startGame(4);
        if (auto* yacht = dynamic_cast<YachtGame*>(game_.get())) {
            yacht->setQaVictoryPresentation();
        }
    } else if (const wchar_t* qaGame = std::wcsstr(GetCommandLineW(), L"--qa-game=")) {
        playerName_ = L"PLAYER";
        startGame(static_cast<int>(std::wcstol(qaGame + 10, nullptr, 10)));
    } else if (const wchar_t* qaMenuSelection =
                   std::wcsstr(GetCommandLineW(), L"--qa-menu-selection=")) {
        screen_ = Screen::Menu;
        showSelectedMenuPose();
        const int selection = static_cast<int>(std::wcstol(qaMenuSelection + 20, nullptr, 10));
        selectMenuSource(std::clamp(selection, 1, 5));
        InvalidateRect(window_, nullptr, FALSE);
    } else if (const wchar_t* qaMenuIdle =
                   std::wcsstr(GetCommandLineW(), L"--qa-menu-idle=")) {
        screen_ = Screen::Menu;
        showSelectedMenuPose();
        showMenuIdleQaPose(static_cast<int>(std::wcstol(qaMenuIdle + 15, nullptr, 10)));
        InvalidateRect(window_, nullptr, FALSE);
    } else if (std::wcsstr(GetCommandLineW(), L"--qa-menu")) {
        screen_ = Screen::Menu;
        showSelectedMenuPose();
        InvalidateRect(window_, nullptr, FALSE);
    } else if (const wchar_t* qaStartup = std::wcsstr(GetCommandLineW(), L"--qa-startup=")) {
        const long phase = std::wcstol(qaStartup + 13, nullptr, 10);
        if (phase >= static_cast<long>(IntroPhase::StartupBlack) &&
            phase <= static_cast<long>(IntroPhase::MenuReveal)) {
            introPhase_ = static_cast<IntroPhase>(phase);
        }
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK App::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app ? app->handleMessage(window, message, wParam, lParam)
               : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT App::handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (isFullscreenShortcut(message, wParam, lParam)) {
        toggleFullscreen();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: updateViewport(); InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paintState{}; HDC dc = BeginPaint(window, &paintState);
        render(); paint(dc); EndPaint(window, &paintState); return 0;
    }
    case WM_LBUTTONDOWN: {
        const Point logical = toLogical({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (screen_ == Screen::Game && dialog_ == Dialog::None && game_) {
            SetCapture(window);
            game_->mouseDown(logical);
        } else {
            click(logical);
        }
        InvalidateRect(window, nullptr, FALSE); return 0;
    }
    case WM_LBUTTONUP:
        if (screen_ == Screen::Game && dialog_ == Dialog::None && game_)
            game_->mouseUp(toLogical({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
        if (GetCapture() == window) ReleaseCapture();
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_CAPTURECHANGED:
        if (screen_ == Screen::Game && game_) game_->mouseCancel();
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_MOUSEMOVE:
        if (screen_ == Screen::Game && dialog_ == Dialog::None && game_ &&
            GetCapture() == window) {
            game_->mouseMove(toLogical({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
            InvalidateRect(window, nullptr, FALSE);
        } else if (screen_ == Screen::Menu && dialog_ == Dialog::None) {
            hoverMenu(toLogical({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
        }
        return 0;
    case WM_CHAR:
        if (suppressNextCharacter_) {
            suppressNextCharacter_ = false;
            return 0;
        }
        typeCharacter(static_cast<wchar_t>(wParam));
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kCommandNewGame:
            if (screen_ == Screen::Game && activeGameIndex_ >= 0) {
                dialog_ = Dialog::ConfirmReset;
            }
            break;
        case kCommandMainMenu:
            if (screen_ == Screen::GameIntro || screen_ == Screen::Game ||
                screen_ == Screen::Help || screen_ == Screen::About ||
                screen_ == Screen::Credits)
                returnToMenu();
            break;
        case kCommandExit: DestroyWindow(window); break;
        case kCommandChangeName: beginChangeName(); break;
        case kCommandSound: toggleSound(); break;
        case kCommandMusic: toggleMusic(); break;
        case kCommandHideBackground: toggleBackground(); break;
        case kCommandAnimatedPieces: toggleAnimatedPieces(); break;
        case kCommandForcedJumps: toggleForcedJumps(); break;
        case kCommandUndo:
            if (screen_ == Screen::Name || screen_ == Screen::GameName) playerName_ = nameBeforeEdit_;
            break;
        case kCommandCut: copyNameToClipboard(true); break;
        case kCommandCopy: copyNameToClipboard(false); break;
        case kCommandPaste: pasteNameFromClipboard(); break;
        case kCommandClear:
            if (screen_ == Screen::Name || screen_ == Screen::GameName) playerName_.clear();
            break;
        case kCommandHelpBackgammon: toggleHelp(0); break;
        case kCommandHelpDominoes: toggleHelp(1); break;
        case kCommandHelpCheckers: toggleHelp(2); break;
        case kCommandHelpGoFish: toggleHelp(3); break;
        case kCommandHelpYacht: toggleHelp(4); break;
        case kCommandAbout:
            if (screen_ != Screen::About && screen_ != Screen::Credits)
                pictureReturnScreen_ = screen_;
            screen_ = Screen::About;
            audio_.playSound(audio_catalog::kAboutSound);
            break;
        case kCommandCredits:
            if (screen_ != Screen::About && screen_ != Screen::Credits)
                pictureReturnScreen_ = screen_;
            screen_ = Screen::Credits;
            audio_.playSound(audio_catalog::kCreditsSound);
            break;
        default: break;
        }
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_INITMENUPOPUP:
        updateFileMenu();
        updateEditMenu();
        updateOptionsMenu();
        return 0;
    case WM_KEYDOWN:
        // A character message generated by an earlier key-down must never
        // leak through a later, unrelated key press.
        suppressNextCharacter_ = false;
        if (GetKeyState(VK_CONTROL) < 0 && wParam == 'Q') {
            if (menu_catalog::fileQuitAction(activeGameIndex_, pendingGameIndex_) ==
                menu_catalog::FileQuitAction::ExitGame) {
                returnToMenu();
            } else {
                DestroyWindow(window);
            }
            return 0;
        }
        if (screen_ == Screen::GameIntro && macGameIntroKeySkips(pendingGameIndex_)) {
            // The classic Mac event is one key-down. Windows follows a
            // printable key-down with WM_CHAR, which would otherwise type the
            // skip key into the first-use name panel after this transition.
            suppressNextCharacter_ = true;
            finishGameIntro(true);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (screen_ == Screen::Menu && dialog_ == Dialog::None && menuLaunchGameIndex_ < 0) {
            int selection = 0;
            if (wParam == VK_RETURN && GetKeyState(VK_MENU) >= 0) {
                startSelectedMenuGame();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_UP) {
                selection = menuSourceSelection_ == 1 ? 5 : menuSourceSelection_ - 1;
            } else if (wParam == VK_RIGHT || wParam == VK_DOWN) {
                selection = menuSourceSelection_ == 5 ? 1 : menuSourceSelection_ + 1;
            } else if (wParam == 'C') selection = 1;
            else if (wParam == 'G') selection = 2;
            else if (wParam == 'D') selection = 3;
            else if (wParam == 'B') selection = 4;
            else if (wParam == 'Y') selection = 5;
            if (selection != 0) {
                selectMenuSource(selection);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
        }
        if (wParam == VK_ESCAPE) {
            if (dialog_ != Dialog::None) dialog_ = Dialog::None;
            else if (screen_ == Screen::Intro &&
                     macTitleIntroInputSkips(static_cast<int>(introPhase_))) {
                skipTitleIntro();
            }
            else if (screen_ == Screen::Help) screen_ = helpReturnScreen_;
            else if (screen_ == Screen::About || screen_ == Screen::Credits) {
                audio_.stop();
                screen_ = pictureReturnScreen_;
            }
            else if (screen_ == Screen::GameName && changingName_) {
                changingName_ = false;
                playerName_ = nameBeforeEdit_;
                screen_ = nameReturnScreen_;
            }
            else if (screen_ == Screen::Menu) DestroyWindow(window);
            else if (screen_ == Screen::GameIntro) {}
            else returnToMenu();
        }
        else if (wParam == VK_F1) toggleHelp();
        else if ((screen_ == Screen::Name || screen_ == Screen::GameName) &&
                 GetKeyState(VK_CONTROL) < 0 && wParam == 'Z')
            playerName_ = nameBeforeEdit_;
        else if ((screen_ == Screen::Name || screen_ == Screen::GameName) &&
                 GetKeyState(VK_CONTROL) < 0 && wParam == 'X')
            copyNameToClipboard(true);
        else if ((screen_ == Screen::Name || screen_ == Screen::GameName) &&
                 GetKeyState(VK_CONTROL) < 0 && wParam == 'C')
            copyNameToClipboard(false);
        else if ((screen_ == Screen::Name || screen_ == Screen::GameName) &&
                 GetKeyState(VK_CONTROL) < 0 && wParam == 'V')
            pasteNameFromClipboard();
        else if (wParam == 'N' && GetKeyState(VK_CONTROL) < 0 &&
                 screen_ == Screen::Game && activeGameIndex_ >= 0)
            dialog_ = Dialog::ConfirmReset;
        else if (screen_ != Screen::Name && screen_ != Screen::GameName && wParam == 'S')
            toggleSound();
        else if (screen_ != Screen::Name && screen_ != Screen::GameName && wParam == 'M')
            toggleMusic();
        else if (screen_ != Screen::Name && screen_ != Screen::GameName && wParam == 'H')
            toggleBackground();
        else if (game_) game_->key(static_cast<unsigned>(wParam));
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_TIMER:
        audio_.tick(33);
        if (screen_ == Screen::Intro) {
            tickIntro(33);
            InvalidateRect(window, nullptr, FALSE);
        } else if (screen_ == Screen::Name) {
            introPhaseMilliseconds_ += 33;
            InvalidateRect(window, nullptr, FALSE);
        } else if (screen_ == Screen::GameName) {
            introPhaseMilliseconds_ += 33;
            InvalidateRect(window, nullptr, FALSE);
        } else if (screen_ == Screen::Menu) {
            bool repaint = menuSelectionHost_.tick();
            repaint = menuBowTieHost_.tick() || repaint;
            repaint = tickMenuIdleControllers() || repaint;
            repaint = tryStartPendingMenuGame() || repaint;
            if (repaint) InvalidateRect(window_, nullptr, FALSE);
        } else if (screen_ == Screen::GameIntro) {
            tickGameIntro(33);
            InvalidateRect(window, nullptr, FALSE);
        } else if (screen_ == Screen::GameCharacter) {
            if (gameCharacterHost_.tick()) InvalidateRect(window_, nullptr, FALSE);
        } else if (screen_ == Screen::Game && game_) {
            bool repaint = game_->tick();
            if (game_->finished() && dialog_ == Dialog::None) {
                gameFinishedMilliseconds_ += 33;
                if (gameFinishedMilliseconds_ >= game_->postFinishDelayMilliseconds() &&
                    !audio_.soundPlaying()) {
                    dialog_ = Dialog::PlayAgain;
                    repaint = true;
                }
            }
            if (repaint) InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_SYSCHAR:
        if (wParam == VK_RETURN) return 0;
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam); info->ptMinTrackSize = {560, 460}; return 0;
    }
    case WM_CLOSE:
        fullscreen_.restore(window);
        DestroyWindow(window);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

void App::render() {
    if (screen_ == Screen::Intro) renderIntro();
    else if (screen_ == Screen::Name) renderName();
    else if (screen_ == Screen::Character) renderCharacter();
    else if (screen_ == Screen::Menu) renderMenu();
    else if (screen_ == Screen::GameIntro) renderGameIntro();
    else if (screen_ == Screen::GameCharacter) renderGameCharacter();
    else if (screen_ == Screen::GameName) renderGameName();
    else if (screen_ == Screen::About) renderAbout();
    else if (screen_ == Screen::Credits) renderCredits();
    else if (screen_ == Screen::Help) renderHelp();
    else if (game_) game_->render(canvas_);
    if (screen_ == Screen::Game && dialog_ != Dialog::None) renderDialog();
}

void App::drawTiledBackground(int resourceId) {
    for (int frame = 0; frame < 64; ++frame) {
        canvas_.sprite(graphics_.sprite(resourceId, frame),
                       frame % 8 * 64, frame / 8 * 48, false);
    }
}

void App::drawMario(bool talking) {
    canvas_.sprite(graphics_.sprite(1010), 219, 174, false);
    if (screen_ == Screen::Menu && menuBootVisible_)
        canvas_.sprite(graphics_.sprite(1011), 313, 288, false);
    if (talking) {
        const std::uint32_t time = std::min<std::uint32_t>(
            talkingHeadMovie_.duration() - 1,
            introPhaseMilliseconds_ * talkingHeadMovie_.timeScale() / 1000U);
        // The movie's registration point places its opaque head pixels exactly over
        // the static Pak 1014 head at (283,79).
        talkingHeadMovie_.render(canvas_, graphics_, time, 250, 73);
    } else {
        canvas_.sprite(graphics_.sprite(1014), 283, 79, false);
    }
    if (screen_ == Screen::Menu && menuBlinkVisible_)
        canvas_.sprite(graphics_.sprite(1014, 1), 283, 79, false);
    if (screen_ != Screen::Menu || menuBowTieVisible_)
        canvas_.sprite(graphics_.sprite(1100), 270, 174, false);
    const int pointerFrame = screen_ == Screen::Menu
        ? std::clamp(menuSourceSelection_ - 1, 0, 4) : 0;
    canvas_.sprite(graphics_.sprite(1020, pointerFrame), 150, 142, false);

    // CODE 12 $23C8 loads movie 1111, stores duration-1 in its time field,
    // and enables that terminal cel as the title's open right hand. It does
    // not play the checker-stack frames or their effects during the spoken
    // title sequence. Once the menu is live, the selection controller owns
    // movies 1111..1115 instead; drawing both would double the hand/object.
    if (screen_ != Screen::Menu) {
        introHandMovie_.render(canvas_, graphics_, introHandMovie_.duration() - 1,
                               397, 127);
    }
}

void App::renderIntro() {
    canvas_.clear(rgb(0, 0, 0));
    switch (introPhase_) {
    case IntroPhase::Brainstorm:
        canvas_.bitmap(brainstormBitmap_, {0, 0, kLogicalWidth, kLogicalHeight});
        return;
    case IntroPhase::BrainstormFade:
        canvas_.bitmap(brainstormFadeBitmap_, {0, 0, kLogicalWidth, kLogicalHeight});
        return;
    case IntroPhase::SteppingStone:
        canvas_.bitmap(steppingStoneBitmap_, {0, 0, kLogicalWidth, kLogicalHeight});
        return;
    case IntroPhase::SteppingStoneFade:
        canvas_.bitmap(steppingStoneFadeBitmap_, {0, 0, kLogicalWidth, kLogicalHeight});
        return;
    case IntroPhase::StartupBlack:
    case IntroPhase::PublisherGap:
    case IntroPhase::TitleGap:
        return;
    default:
        break;
    }
    if (introPhase_ == IntroPhase::Silhouette) {
        drawTiledBackground(708);
        canvas_.sprite(graphics_.sprite(709), 176, 80, false);
        // CODE 12 creates both title actors before its fifteen-count opening
        // pause, but the easel actor is still under the source black cast.
        // The retained vanilla run shows the complete board/easel outline in
        // black; its colored artwork is revealed only when the greeting state
        // begins. Drawing Pak 710 in full color here exposed the board early.
        canvas_.spriteSilhouette(graphics_.sprite(710), 12, 107,
                                 rgb(0, 0, 0), false);
        return;
    }

    if (introPhase_ == IntroPhase::MenuReveal) {
        drawTiledBackground(1001);
        drawMario(false);
        const std::uint32_t time = std::min<std::uint32_t>(
            menuRevealMovie_.duration() - 1,
            introPhaseMilliseconds_ * menuRevealMovie_.timeScale() / 1000U);
        menuRevealMovie_.render(canvas_, graphics_, time, 12, 107);
        return;
    }

    drawTiledBackground(708);
    canvas_.sprite(graphics_.sprite(710), 12, 107, false);
    drawMario(introPhase_ == IntroPhase::TalkingHead);
}

void App::renderName() {
    drawTiledBackground(1001);
    drawMario(false);
    canvas_.sprite(graphics_.sprite(100), 111, 37, false);
    std::wstring field = playerName_;
    if ((introPhaseMilliseconds_ / 500U) % 2U == 0U) field += L"|";
    canvas_.pakText(graphics_, field, 227, {145, 95, 341, 125},
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                    kMaximumPlayerNameLength + 1);
}

void App::renderCharacter() {
    drawTiledBackground(1001);
    drawMario(false);
    constexpr int panelX = 124;
    constexpr int panelY = 37;
    canvas_.sprite(graphics_.sprite(101, 0), panelX, panelY, false);
    canvas_.sprite(graphics_.sprite(101, 1), panelX + 59, panelY + 87, false);
    canvas_.sprite(graphics_.sprite(101, 2), panelX + 134, panelY + 87, false);
}

void App::renderGameCharacter() {
    if (!game_) return;
    game_->render(canvas_);
    (void)gameCharacterHost_.render(canvas_);
    // The source lets Mario finish movie 11093 over the unobstructed game
    // screen, then reveals the modal Yoshi/Koopa panel.
    if (gameCharacterHost_.active()) return;
    constexpr int panelX = 124;
    constexpr int panelY = 120;
    canvas_.sprite(graphics_.sprite(101, 0), panelX, panelY, false);
    canvas_.sprite(graphics_.sprite(101, 1), panelX + 59, panelY + 87, false);
    canvas_.sprite(graphics_.sprite(101, 2), panelX + 134, panelY + 87, false);
}

void App::renderGameName() {
    if (game_) game_->render(canvas_);
    else renderMenu();
    constexpr int panelX = 124;
    constexpr int panelY = 113;
    canvas_.sprite(graphics_.sprite(100, 0), panelX, panelY, false);
    canvas_.sprite(graphics_.sprite(100, 1), panelX + 41, panelY + 110, false);
    canvas_.sprite(graphics_.sprite(100, 2), panelX + 164, panelY + 110, false);
    std::wstring field = playerName_;
    if ((introPhaseMilliseconds_ / 500U) % 2U == 0U) field += L"|";
    canvas_.pakText(graphics_, field, 227,
                    {panelX + 31, panelY + 57, panelX + 236, panelY + 86},
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                    kMaximumPlayerNameLength + 1);
}

void App::renderMenu() {
    canvas_.clear(rgb(0, 0, 0));
    drawTiledBackground(1001);
    const int selectedGame = menu_catalog::gameIndex(menuSourceSelection_);
    if (selectedGame >= 0) {
        const Rect button = gameButtons_[static_cast<std::size_t>(selectedGame)];
        // The source cast swaps the authored yellow/red palette pair for the
        // active label.  Recoloring only the button interior retains the exact
        // pixel lettering and gold frame from Pak 1001.
        canvas_.swapColors({button.left + 2, button.top + 1,
                            button.right - 2, button.bottom - 1},
                           rgb(247, 223, 0), rgb(251, 11, 11));
    }
    drawMario(false);
    (void)menuBowTieHost_.render(canvas_);
    // CODE 12 resources 1111..1115 animate the easel object belonging to the
    // selected C/G/D/B/Y menu entry.  They are small transparent overlays;
    // keeping them inside the same logical back buffer avoids the historical
    // whole-window shift/flicker failure.
    (void)menuSelectionHost_.render(canvas_);
}

void App::renderDialog() {
    const int resource = dialog_ == Dialog::ConfirmReset ? 105 : 106;
    const int panelX = resource == 105 ? 115 : 139;
    const int panelY = resource == 105 ? 100 : 102;
    const int yesX = resource == 105 ? 44 : 36;
    const int noX = resource == 105 ? 171 : 131;
    const int buttonY = resource == 105 ? 125 : 122;
    canvas_.sprite(graphics_.sprite(resource, 0), panelX, panelY, false);
    canvas_.sprite(graphics_.sprite(resource, 1), panelX + yesX, panelY + buttonY, false);
    canvas_.sprite(graphics_.sprite(resource, 2), panelX + noX, panelY + buttonY, false);
}

void App::renderGameIntro() {
    static constexpr std::array<int, 5> backgrounds{4000, 3000, 2998, 5000, 6000};
    canvas_.clear(rgb(0, 0, 0));
    if (pendingGameIndex_ < 0 || pendingGameIndex_ >= static_cast<int>(backgrounds.size())) return;
    drawTiledBackground(backgrounds[static_cast<std::size_t>(pendingGameIndex_)]);
    for (const GameIntroMovie& instance : gameIntroMovies_) {
        const std::uint32_t time = std::min<std::uint32_t>(
            instance.movie.duration() - 1,
            gameIntroMilliseconds_ * instance.movie.timeScale() / 1000U);
        instance.movie.render(canvas_, graphics_, time, instance.x, instance.y);
    }
}

void App::advanceIntro(IntroPhase phase) {
    introPhase_ = phase;
    introPhaseMilliseconds_ = 0;
}

void App::skipTitleIntro() {
    // The source mouse-down terminates the live title controller immediately.
    // The title hand is already pinned to movie 1111's terminal cel. Stop the
    // title audio and install the live menu controller's selected pose.
    audio_.stop();
    introPhase_ = IntroPhase::MenuReveal;
    introPhaseMilliseconds_ = 0;
    screen_ = Screen::Menu;
    audio_.playMusic(audio_catalog::kMenuMusic);
    showSelectedMenuPose();
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void App::tickIntro(unsigned milliseconds) {
    introPhaseMilliseconds_ += milliseconds;
    switch (introPhase_) {
    case IntroPhase::StartupBlack:
        if (introPhaseMilliseconds_ >= 1800U) {
            audio_.playEffect(audio_catalog::kBrainstormSound);
            advanceIntro(IntroPhase::Brainstorm);
        }
        break;
    case IntroPhase::Brainstorm:
        if (introPhaseMilliseconds_ >= 1400U) advanceIntro(IntroPhase::BrainstormFade);
        break;
    case IntroPhase::BrainstormFade:
        if (introPhaseMilliseconds_ >= 170U) advanceIntro(IntroPhase::PublisherGap);
        break;
    case IntroPhase::PublisherGap:
        if (introPhaseMilliseconds_ >= 850U) {
            audio_.playEffect(audio_catalog::kSteppingStoneSound);
            advanceIntro(IntroPhase::SteppingStone);
        }
        break;
    case IntroPhase::SteppingStone:
        // CODE 12 $1B40 loads 0x1c source control counts for the second
        // publisher card.  The title controller's 15/2/5-count pauses and the
        // original capture establish the controller cadence as 100 ms.
        if (introPhaseMilliseconds_ >= 2800U) advanceIntro(IntroPhase::SteppingStoneFade);
        break;
    case IntroPhase::SteppingStoneFade:
        if (introPhaseMilliseconds_ >= 170U) advanceIntro(IntroPhase::TitleGap);
        break;
    case IntroPhase::TitleGap:
        if (introPhaseMilliseconds_ >= 1400U) {
            audio_.playMusic(audio_catalog::kMenuMusic);
            advanceIntro(IntroPhase::Silhouette);
        }
        break;
    case IntroPhase::Silhouette:
        if (introPhaseMilliseconds_ >= 1500U) {
            audio_.playSound(5012);
            advanceIntro(IntroPhase::Greeting);
        }
        break;
    case IntroPhase::Greeting:
        if (!audio_.soundPlaying()) advanceIntro(IntroPhase::GreetingPause);
        break;
    case IntroPhase::GreetingPause:
        if (introPhaseMilliseconds_ >= 200U) {
            audio_.playSound(5000);
            advanceIntro(IntroPhase::TitleCue);
        }
        break;
    case IntroPhase::TitleCue:
        if (!audio_.soundPlaying()) {
            audio_.playSound(5001);
            advanceIntro(IntroPhase::TitleResponse);
        }
        break;
    case IntroPhase::TitleResponse:
        // CODE 12 state 3 starts its five-count pause only after sound 5001
        // finishes; time spent speaking is not part of that pause.
        if (audio_.soundPlaying()) {
            introPhaseMilliseconds_ = 0;
        } else if (introPhaseMilliseconds_ >= 500U) {
            audio_.playSound(8056);
            advanceIntro(IntroPhase::TalkingHead);
        }
        break;
    case IntroPhase::TalkingHead: {
        const std::uint32_t movieMilliseconds =
            talkingHeadMovie_.duration() * 1000U / talkingHeadMovie_.timeScale();
        // Source state 4 gates on movie 12091 itself, then advances to 5011.
        if (introPhaseMilliseconds_ >= movieMilliseconds) {
            audio_.playSound(5011);
            advanceIntro(IntroPhase::MenuReveal);
        }
        break;
    }
    case IntroPhase::MenuReveal: {
        // Movie 1125 wipes the title picture into the five menu labels while
        // the final title line plays.  Advancing these together prevents the
        // first picture frame from sitting over the live menu for the entire
        // voice clip.
        const std::uint32_t movieMilliseconds =
            menuRevealMovie_.duration() * 1000U / menuRevealMovie_.timeScale();
        if (introPhaseMilliseconds_ >= movieMilliseconds && !audio_.soundPlaying()) {
            screen_ = Screen::Menu;
            showSelectedMenuPose();
        }
        break;
    }
    }
}

void App::enterNameScreen() {
    screen_ = Screen::Name;
    introPhaseMilliseconds_ = 0;
}

void App::beginGameIntro(int index) {
    if (index < 0 || index >= 5) return;
    menuLaunchGameIndex_ = -1;
    menuSelectionHost_.stop();
    menuBowTieHost_.stop();
    menuBootVisible_ = false;
    menuBowTieVisible_ = true;
    menuBlinkVisible_ = false;
    audio_.stop();
    audio_.stopMusic();
    // CODE 12 $1400 starts tracked snd 5010 after the menu actors settle.
    // The shorter snd 5003 belongs to changing the highlighted label.
    audio_.playSound(audio_catalog::kMenuLaunchSound);
    audio_.playMusic(audio_catalog::kPrimaryGameMusic[static_cast<std::size_t>(index)]);
    game_.reset();
    activeGameIndex_ = -1;
    pendingGameIndex_ = index;
    updateOptionsMenu();
    gameIntroMilliseconds_ = 0;
    gameIntroDurationMilliseconds_ = 0;
    gameIntroMovies_.clear();

    const auto addMovie = [this](int id, int x, int y) {
        gameIntroMovies_.push_back({Movie(assets_, id), x, y});
        const Movie& movie = gameIntroMovies_.back().movie;
        const std::uint32_t milliseconds =
            (movie.duration() * 1000U + movie.timeScale() - 1U) / movie.timeScale();
        gameIntroDurationMilliseconds_ = std::max(gameIntroDurationMilliseconds_, milliseconds);
    };

    switch (index) {
    case 0: addMovie(4999, -230, 239); break;
    case 1:
        // CODE 14's title initializer writes the three actor anchors as one
        // shared stage registration (the last actor is four pixels right).
        // MuV 3002/3003 deliberately carry complementary horizontal origins:
        // their terminal cels join 3004 into the five-domino tableau, followed
        // by Yoshi at the right edge.  Pre-subtracting those origins scattered
        // the layers across both axes and left only one partial domino visible.
        addMovie(3002, 18, 220);
        addMovie(3003, 18, 220);
        addMovie(3004, 22, 220);
        break;
    case 2:
        addMovie(2801, -154, 262);
        addMovie(2800, -161, 269);
        break;
    case 3:
        addMovie(5101, 4, 166);
        addMovie(5102, 59, 160);
        break;
    case 4:
        // CODE 18 $120/$17A supplies the two source horizontal anchors.  Their
        // MuV/Img registrations place the upper boat at y=108 and the lower
        // hull/wake at y=240, where the two cels meet without a seam.
        addMovie(6100, -149, 0);
        addMovie(6150, -208, 0);
        break;
    }
    screen_ = Screen::GameIntro;
    updateFileMenu();
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void App::tickGameIntro(unsigned milliseconds) {
    const std::uint32_t previousMilliseconds = gameIntroMilliseconds_;
    gameIntroMilliseconds_ += milliseconds;
    for (const GameIntroMovie& instance : gameIntroMovies_) {
        const Movie& movie = instance.movie;
        const std::uint32_t previousTime = std::min<std::uint32_t>(
            movie.duration(), previousMilliseconds * movie.timeScale() / 1000U);
        const std::uint32_t currentTime = std::min<std::uint32_t>(
            movie.duration(), gameIntroMilliseconds_ * movie.timeScale() / 1000U);
        for (int sound : movie.soundsBetween(previousTime, currentTime)) audio_.playEffect(sound);
    }
    // The original title handlers leave the completed tableau visible for two seconds.
    if (gameIntroMilliseconds_ >= gameIntroDurationMilliseconds_ + 2000U) {
        finishGameIntro();
    }
}

void App::finishGameIntro(bool skippedByInput) {
    if (screen_ != Screen::GameIntro || pendingGameIndex_ < 0) return;
    if (skippedByInput) audio_.stop();
    const int index = pendingGameIndex_;
    pendingGameIndex_ = -1;
    gameIntroMovies_.clear();
    if (index <= 2 && !characterConfirmed_) {
        beginGameCharacter(index);
    } else if (!nameConfirmed_) {
        beginGameName(index);
    } else {
        startGame(index);
    }
}

void App::beginGameCharacter(int index) {
    const std::uint32_t sourceSeed = random_.seed();
    startGame(index);
    if (!game_) return;
    // The board is constructed early only so the source-style chooser can sit
    // over the game screen.  Recreating it after the prompts must not consume
    // the game's shared QuickDraw sequence a second time.
    previewRandomSeed_ = sourceSeed;
    audio_.stop();
    characterGameIndex_ = index;
    game_->setCharacterChooser(true);
    // Movie 11093 contains the valid snd 8046 "Yoshi or Koopa?" cue. Movies
    // 11087 and 11089..11092 point at five absent, unused source sound IDs.
    gameCharacterHost_.play(11093, -12, -1);
    screen_ = Screen::GameCharacter;
}

void App::beginGameName(int index) {
    if (!game_ || activeGameIndex_ != index) {
        const std::uint32_t sourceSeed = random_.seed();
        startGame(index);
        if (!game_) return;
        previewRandomSeed_ = sourceSeed;
        audio_.stop();
    }
    if (index >= 0 && index <= 2) game_->setCharacterChooser(true);
    nameGameIndex_ = index;
    changingName_ = false;
    nameBeforeEdit_ = playerName_;
    introPhaseMilliseconds_ = 0;
    screen_ = Screen::GameName;
}

void App::beginChangeName() {
    if (screen_ != Screen::Menu && screen_ != Screen::Game) return;
    changingName_ = true;
    nameReturnScreen_ = screen_;
    nameGameIndex_ = activeGameIndex_;
    nameBeforeEdit_ = playerName_;
    introPhaseMilliseconds_ = 0;
    screen_ = Screen::GameName;
}

void App::typeCharacter(wchar_t character) {
    if (screen_ != Screen::Name && screen_ != Screen::GameName) return;
    if (character == L'\r') {
        if (!playerName_.empty()) {
            // CODE 6 $C42 uses the modal-confirm sound for Return and buttons.
            audio_.playEffect(9204);
            if (screen_ == Screen::GameName && changingName_) {
                changingName_ = false;
                nameConfirmed_ = true;
                if (game_) game_->setPlayerName(playerName_);
                screen_ = nameReturnScreen_;
                return;
            }
            if (screen_ == Screen::GameName && nameGameIndex_ >= 0) {
                const int index = nameGameIndex_;
                nameGameIndex_ = -1;
                nameConfirmed_ = true;
                startPreviewedGame(index);
            } else {
                screen_ = Screen::Menu;
                showSelectedMenuPose();
            }
        }
        return;
    }
    if (character == L'\b') {
        if (!playerName_.empty()) {
            playerName_.pop_back();
            audio_.playEffect(9203);
        } else {
            audio_.playEffect(9202);
        }
        return;
    }
    if (character >= L' ' && character != 0x7f) {
        if (playerName_.size() < kMaximumPlayerNameLength) {
            playerName_.push_back(character);
            audio_.playEffect(9201);
        } else {
            audio_.playEffect(9202);
        }
    }
}

void App::renderAbout() {
    canvas_.clear(rgb(0, 0, 0));
    renderSourceAbout(canvas_, sourceFonts_, titleBitmap_, {32, 84});
}

void App::renderCredits() { canvas_.bitmap(creditsBitmap_, {0, 0, 512, 384}); }

void App::renderHelp() {
    // These are the nine source PICT help pages rendered by the original
    // QuickDraw code. Keep the live screen underneath, as the Macintosh
    // version displayed this chrome as an in-game modal panel.
    if (helpReturnScreen_ == Screen::Game && game_) game_->render(canvas_);
    else if (helpReturnScreen_ == Screen::Menu) renderMenu();
    else canvas_.clear(rgb(0, 0, 0));
    const int gameIndex = std::clamp(helpGameIndex_, 0, 4);
    const int page = std::clamp(helpPage_, 0, kHelpPageCount[gameIndex] - 1);
    canvas_.bitmap(helpBitmaps_[kHelpBitmapBase[gameIndex] + page], 14, 14);
}

void App::paint(HDC dc) {
    RECT client{}; GetClientRect(window_, &client);
    const int clientWidth = static_cast<int>(client.right);
    const int clientHeight = static_cast<int>(client.bottom);
    if (clientWidth <= 0 || clientHeight <= 0) return;

    // Compose the black letterbox and scaled logical frame completely off
    // screen, then copy that finished client image in one BitBlt.  Clearing
    // and uploading directly to the window exposed black or partially drawn
    // frames when DWM requested several small repaints during activation.
    if (!paintBufferDc_) paintBufferDc_ = CreateCompatibleDC(dc);
    if (!paintBufferDc_) throw std::runtime_error("could not create paint buffer DC");
    if (!paintBufferBitmap_ || paintBufferWidth_ != clientWidth ||
        paintBufferHeight_ != clientHeight) {
        if (paintBufferBitmap_) {
            SelectObject(paintBufferDc_, paintBufferPreviousBitmap_);
            DeleteObject(paintBufferBitmap_);
            paintBufferBitmap_ = nullptr;
        }
        paintBufferBitmap_ = CreateCompatibleBitmap(dc, clientWidth, clientHeight);
        if (!paintBufferBitmap_) throw std::runtime_error("could not create paint buffer bitmap");
        paintBufferPreviousBitmap_ = SelectObject(paintBufferDc_, paintBufferBitmap_);
        paintBufferWidth_ = clientWidth;
        paintBufferHeight_ = clientHeight;
    }
    RECT bufferBounds{0, 0, clientWidth, clientHeight};
    FillRect(paintBufferDc_, &bufferBounds,
             static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    canvas_.present(paintBufferDc_, viewport_);
    BitBlt(dc, 0, 0, clientWidth, clientHeight, paintBufferDc_, 0, 0, SRCCOPY);
}

void App::updateViewport() {
    if (!window_) return;
    RECT client{}; GetClientRect(window_, &client);
    const int width = client.right; const int height = client.bottom;
    const double scale = std::min(width / static_cast<double>(kLogicalWidth), height / static_cast<double>(kLogicalHeight));
    const int drawWidth = std::max(1, static_cast<int>(kLogicalWidth * scale));
    const int drawHeight = std::max(1, static_cast<int>(kLogicalHeight * scale));
    viewport_ = {(width - drawWidth) / 2, (height - drawHeight) / 2,
                 (width + drawWidth) / 2, (height + drawHeight) / 2};
}

Point App::toLogical(Point client) const {
    if (!viewport_.contains(client)) return {-1000, -1000};
    return {(client.x - viewport_.left) * kLogicalWidth / viewport_.width(),
            (client.y - viewport_.top) * kLogicalHeight / viewport_.height()};
}

void App::click(Point logical) {
    if (screen_ == Screen::Intro &&
        macTitleIntroInputSkips(static_cast<int>(introPhase_))) {
        skipTitleIntro();
        return;
    }
    if (screen_ == Screen::GameIntro) {
        switch (macGameIntroMouseAction(pendingGameIndex_)) {
        case MacGameIntroMouseAction::Finish:
            finishGameIntro(true);
            break;
        case MacGameIntroMouseAction::AdvanceOneTick:
            tickGameIntro(33);
            break;
        case MacGameIntroMouseAction::Ignore:
            break;
        }
        return;
    }
    if (dialog_ != Dialog::None) {
        clickDialog(logical);
        return;
    }
    if (screen_ == Screen::About || screen_ == Screen::Credits) {
        audio_.stop();
        screen_ = pictureReturnScreen_;
        updateFileMenu();
        return;
    }
    if (screen_ == Screen::Help) {
        const Rect previous{141, 324, 211, 354};
        const Rect close{225, 324, 294, 354};
        const Rect next{305, 324, 374, 354};
        const int pageCount = kHelpPageCount[std::clamp(helpGameIndex_, 0, 4)];
        if (close.contains(logical)) screen_ = helpReturnScreen_;
        else if (previous.contains(logical) && helpPage_ > 0) {
            --helpPage_;
            audio_.playEffect(5003);
        } else if (next.contains(logical) && helpPage_ + 1 < pageCount) {
            ++helpPage_;
            audio_.playEffect(5003);
        }
        return;
    }
    if (screen_ == Screen::Game && game_) { game_->click(logical); return; }
    if (screen_ == Screen::GameName) {
        constexpr int panelX = 124;
        constexpr int panelY = 113;
        const Rect okay{panelX + 41, panelY + 110, panelX + 118, panelY + 145};
        const Rect cancel{panelX + 164, panelY + 110, panelX + 241, panelY + 145};
        if (okay.contains(logical) && !playerName_.empty() && changingName_) {
            changingName_ = false;
            nameConfirmed_ = true;
            if (game_) game_->setPlayerName(playerName_);
            audio_.playEffect(9204);
            screen_ = nameReturnScreen_;
        } else if (okay.contains(logical) && !playerName_.empty() && nameGameIndex_ >= 0) {
            const int index = nameGameIndex_;
            nameGameIndex_ = -1;
            nameConfirmed_ = true;
            audio_.playEffect(9204);
            startPreviewedGame(index);
        } else if (cancel.contains(logical) && changingName_) {
            changingName_ = false;
            playerName_ = nameBeforeEdit_;
            audio_.playEffect(9204);
            screen_ = nameReturnScreen_;
        } else if (cancel.contains(logical) && nameGameIndex_ >= 0) {
            const int index = nameGameIndex_;
            nameGameIndex_ = -1;
            playerName_ = nameBeforeEdit_;
            nameConfirmed_ = true;
            audio_.playEffect(9204);
            startPreviewedGame(index);
        }
        return;
    }
    if (screen_ == Screen::Character || screen_ == Screen::GameCharacter) {
        if (screen_ == Screen::GameCharacter && gameCharacterHost_.active()) return;
        constexpr int panelX = 124;
        const int panelY = screen_ == Screen::GameCharacter ? 120 : 37;
        bool selected = false;
        if (Rect{panelX + 15, panelY + 72, panelX + 131, panelY + 132}.contains(logical)) {
            playerIsYoshi_ = true;
            selected = true;
        } else if (Rect{panelX + 132, panelY + 72, panelX + 250, panelY + 132}.contains(logical)) {
            playerIsYoshi_ = false;
            selected = true;
        }
        if (selected) {
            characterConfirmed_ = true;
            if (screen_ == Screen::GameCharacter) audio_.stop();
            audio_.playEffect(9204);
            if (screen_ == Screen::GameCharacter && characterGameIndex_ >= 0) {
                const int index = characterGameIndex_;
                characterGameIndex_ = -1;
                if (nameConfirmed_) startPreviewedGame(index);
                else beginGameName(index);
            } else {
                screen_ = Screen::Menu;
                showSelectedMenuPose();
            }
        }
        return;
    }
    if (screen_ != Screen::Menu || menuLaunchGameIndex_ >= 0) return;
    for (std::size_t index = 0; index < gameButtons_.size(); ++index)
        if (gameButtons_[index].contains(logical)) {
            // Source mouse-down posts the clicked destination, lets D36 run
            // that label's complete 1111-1115 movie, and only then lets $1400
            // leave the menu.
            selectMenuSource(menu_catalog::sourceSelection(static_cast<int>(index)));
            requestMenuGame(static_cast<int>(index));
            return;
        }
}

bool App::selectMenuSource(int sourceSelection, bool animate) {
    if (sourceSelection < 1 || sourceSelection > 5 ||
        sourceSelection == menuSourceSelection_) {
        return false;
    }
    menuSourceSelection_ = sourceSelection;
    if (!menuIdleRunning_) menuIdleElapsedTicks_ = 0;
    if (animate && screen_ == Screen::Menu) {
        // CODE 12 $D36 emits snd 5003 when the active cast label changes, then
        // advances the selected 1111-1115 hand/object movie and leaves its last
        // frame visible until another selection replaces it.
        audio_.playEffect(audio_catalog::kMenuSelectionSound);
        menuSelectionHost_.play(menu_catalog::movie(sourceSelection), 397, 127,
                                true, 180U);
    }
    return true;
}

void App::showSelectedMenuPose() {
    menuSelectionHost_.showFrame(menu_catalog::movie(menuSourceSelection_),
                                 397, 127, 180U);
    resetMenuIdleControllers();
}

void App::resetMenuIdleControllers() {
    menuBowTieHost_.stop();
    menuBootVisible_ = false;
    menuBowTieVisible_ = true;
    menuBlinkVisible_ = false;
    menuIdleQaFreeze_ = false;

    // CODE 12 $1032 initializes the host-idle controller in this exact RNG
    // order: delay first, then one of three nonrepeating behaviors.
    menuIdleWaitTicks_ = static_cast<int>(random_.below(100)) + 10;
    menuIdleElapsedTicks_ = 0;
    menuIdleMode_ = static_cast<int>(random_.below(3)) + 1;
    menuIdlePreviousMode_ = menuIdleMode_;
    menuIdlePhase_ = 0;
    menuFootToggleCountdown_ = 0;
    menuFootToggleCount_ = 0;
    menuFootShowNext_ = true;
    menuIdleRunning_ = false;

    // CODE 12 $12F8 owns the independent closed-eye overlay.
    menuBlinkWaitTicks_ = static_cast<int>(random_.below(100)) + 10;
    menuBlinkElapsedTicks_ = 0;
    menuBlinkHoldTicks_ = 0;
    menuBlinkRunning_ = false;
}

void App::resetMenuBlinkDelay() {
    menuBlinkVisible_ = false;
    menuBlinkRunning_ = false;
    menuBlinkElapsedTicks_ = 0;
    if (random_.below(100) < 15)
        menuBlinkWaitTicks_ = static_cast<int>(random_.below(10)) + 10;
    else
        menuBlinkWaitTicks_ = static_cast<int>(random_.below(100)) + 10;
}

void App::finishMenuIdleCycle() {
    menuBowTieHost_.stop();
    menuBootVisible_ = false;
    menuBowTieVisible_ = true;
    menuIdleRunning_ = false;
    menuIdlePhase_ = 0;
    menuIdleElapsedTicks_ = 0;
    menuFootToggleCountdown_ = 0;
    menuFootToggleCount_ = 0;
    menuFootShowNext_ = true;

    // The source rejects the immediately preceding mode before arming the
    // next randomized delay.
    do {
        menuIdleMode_ = static_cast<int>(random_.below(3)) + 1;
    } while (menuIdleMode_ == menuIdlePreviousMode_);
    menuIdlePreviousMode_ = menuIdleMode_;
    menuIdleWaitTicks_ = static_cast<int>(random_.below(100)) + 10;
}

bool App::tickMenuIdleControllers() {
    if (menuIdleQaFreeze_) return false;
    bool repaint = false;

    // Once CODE 12 has a posted game destination, no new idle actor is
    // scheduled. An actor that was already running is allowed to finish.
    if (menuLaunchGameIndex_ >= 0 && !menuIdleRunning_ && !menuBlinkRunning_) return false;

    // Source update order is blink ($12F8) followed by the three-mode host
    // controller ($1032). A blink that starts this tick therefore blocks a
    // simultaneous foot/bow-tie idle from starting until it has completed.
    if (!menuBlinkRunning_) {
        ++menuBlinkElapsedTicks_;
        if (menuBlinkElapsedTicks_ >= menuBlinkWaitTicks_) {
            if (menuIdleRunning_) {
                resetMenuBlinkDelay();
            } else if (!menuSelectionHost_.playing()) {
                menuBlinkRunning_ = true;
                menuBlinkVisible_ = true;
                menuBlinkHoldTicks_ = 2;
                repaint = true;
            }
        }
    } else {
        const int oldHold = menuBlinkHoldTicks_--;
        if (oldHold == 0) {
            resetMenuBlinkDelay();
            repaint = true;
        }
    }

    if (menuLaunchGameIndex_ >= 0 && !menuIdleRunning_) return repaint;

    if (!menuIdleRunning_) {
        ++menuIdleElapsedTicks_;
        if (menuIdleElapsedTicks_ < menuIdleWaitTicks_ || menuBlinkRunning_ ||
            menuSelectionHost_.playing()) {
            return repaint;
        }
        menuIdleRunning_ = true;
        menuIdlePhase_ = 0;
    }

    if (menuIdlePhase_ == 0) {
        if (menuIdleMode_ == 1) {
            const int oldCountdown = menuFootToggleCountdown_--;
            if (oldCountdown != 0) return repaint;
            menuFootToggleCountdown_ = 2;
            if (menuFootShowNext_) {
                menuBootVisible_ = true;
                menuFootShowNext_ = false;
                if (menuFootToggleCount_ > 0 && !audio_.soundPlaying())
                    audio_.playEffect(audio_catalog::kMenuFootSound);
            } else {
                menuBootVisible_ = false;
                menuFootShowNext_ = true;
            }
            ++menuFootToggleCount_;
            repaint = true;
            if (menuFootToggleCount_ > 5 && menuFootShowNext_) ++menuIdlePhase_;
            return repaint;
        }
        if (menuIdleMode_ == 2) {
            menuBowTieVisible_ = false;
            menuBowTieHost_.play(1101, 263, 181, false);
            audio_.playEffect(audio_catalog::kMenuBowTieSound);
            ++menuIdlePhase_;
            return true;
        }

        // Mode 3 intentionally has no visible or audible result.  CODE 12
        // randomly posts either operation 21 or the word 12061 to CODE 16's
        // 23-entry movie controller.  Operation 21's table record has movie 0
        // and sound 0; 12061 is rejected by the controller's range check.
        ++menuIdlePhase_;
        return repaint;
    }

    if (menuIdlePhase_ == 1) {
        if (menuIdleMode_ == 1) {
            audio_.playEffect(audio_catalog::kMenuFootSound);
            ++menuIdlePhase_;
        } else if (menuIdleMode_ == 2) {
            if (menuBowTieHost_.active()) return repaint;
            menuBowTieVisible_ = true;
            ++menuIdlePhase_;
            repaint = true;
        } else {
            ++menuIdlePhase_;
        }
        return repaint;
    }

    finishMenuIdleCycle();
    return true;
}

void App::showMenuIdleQaPose(int mode) {
    // Stable, muted inspection poses for the two visual idle modes and the
    // independent blink actor. Mode 3 is the source controller's deliberate
    // no-op; mode 4 exposes the otherwise independently scheduled blink.
    // QA mode has already disabled Audio before this routine can be reached.
    menuIdleQaFreeze_ = true;
    menuIdleRunning_ = true;
    if (mode == 1) {
        menuBootVisible_ = true;
    } else if (mode == 2) {
        menuBowTieVisible_ = false;
        menuBowTieHost_.showFrame(1101, 263, 181, 480U);
    } else if (mode == 4) {
        menuBlinkVisible_ = true;
    }
}

void App::startSelectedMenuGame() {
    const int gameIndex = menu_catalog::gameIndex(menuSourceSelection_);
    if (gameIndex >= 0) requestMenuGame(gameIndex);
}

void App::requestMenuGame(int index) {
    if (screen_ != Screen::Menu || index < 0 || index >= 5 || menuLaunchGameIndex_ >= 0) return;
    menuLaunchGameIndex_ = index;
    tryStartPendingMenuGame();
}

bool App::tryStartPendingMenuGame() {
    if (screen_ != Screen::Menu || menuLaunchGameIndex_ < 0) return false;
    if (menuSelectionHost_.playing() || menuIdleRunning_ || menuBlinkRunning_) return false;
    const int index = menuLaunchGameIndex_;
    menuLaunchGameIndex_ = -1;
    beginGameIntro(index);
    return true;
}

void App::hoverMenu(Point logical) {
    if (menuLaunchGameIndex_ >= 0) return;
    for (std::size_t index = 0; index < gameButtons_.size(); ++index) {
        if (!gameButtons_[index].contains(logical)) continue;
        if (selectMenuSource(menu_catalog::sourceSelection(static_cast<int>(index))) && window_) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        return;
    }
}

void App::clickDialog(Point logical) {
    const int resource = dialog_ == Dialog::ConfirmReset ? 105 : 106;
    const int panelX = resource == 105 ? 115 : 139;
    const int panelY = resource == 105 ? 100 : 102;
    const int yesX = resource == 105 ? 44 : 36;
    const int noX = resource == 105 ? 171 : 131;
    const int buttonY = resource == 105 ? 125 : 122;
    const Rect yes{panelX + yesX, panelY + buttonY,
                   panelX + yesX + 79, panelY + buttonY + 38};
    const Rect no{panelX + noX, panelY + buttonY,
                  panelX + noX + 79, panelY + buttonY + 38};
    if (!yes.contains(logical) && !no.contains(logical)) return;
    audio_.playEffect(9204);
    const Dialog responseTo = dialog_;
    dialog_ = Dialog::None;
    if (yes.contains(logical)) {
        if (responseTo == Dialog::PlayAgain && game_) {
            if (activeGameIndex_ >= 0 && activeGameIndex_ < 5) {
                audio_.playMusic(audio_catalog::kPrimaryGameMusic[
                    static_cast<std::size_t>(activeGameIndex_)]);
            }
            game_->resetForReplay();
            gameFinishedMilliseconds_ = 0;
        } else {
            startGame(activeGameIndex_);
        }
    } else if (responseTo == Dialog::PlayAgain) {
        returnToMenu();
    }
}

void App::startGame(int index) {
    GameContext context{assets_, graphics_, audio_, random_, [this] { returnToMenu(); },
                        playerName_, playerIsYoshi_};
    switch (index) {
    case 0: game_ = std::make_unique<BackgammonGame>(context); break;
    case 1: game_ = std::make_unique<DominoesGame>(context); break;
    case 2: game_ = std::make_unique<CheckersGame>(context); break;
    case 3: game_ = std::make_unique<GoFishGame>(context); break;
    case 4: game_ = std::make_unique<YachtGame>(context); break;
    default: return;
    }
    activeGameIndex_ = index;
    game_->setAnimatedPieces(animatedPieces_);
    game_->setForcedJumps(forcedJumps_);
    dialog_ = Dialog::None;
    gameFinishedMilliseconds_ = 0;
    pendingGameIndex_ = -1;
    characterGameIndex_ = -1;
    nameGameIndex_ = -1;
    gameIntroMovies_.clear();
    audio_.playMusic(audio_catalog::kPrimaryGameMusic[static_cast<std::size_t>(index)]);
    screen_ = Screen::Game;
    updateFileMenu();
    updateOptionsMenu();
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void App::startPreviewedGame(int index) {
    if (previewRandomSeed_) {
        random_.setSeed(*previewRandomSeed_);
        previewRandomSeed_.reset();
    }
    startGame(index);
}

void App::returnToMenu() {
    if (activeGameIndex_ >= 0 && activeGameIndex_ < 5) {
        menuSourceSelection_ = menu_catalog::sourceSelection(activeGameIndex_);
    }
    menuSelectionHost_.stop();
    game_.reset();
    gameIntroMovies_.clear();
    menuLaunchGameIndex_ = -1;
    pendingGameIndex_ = -1;
    characterGameIndex_ = -1;
    nameGameIndex_ = -1;
    dialog_ = Dialog::None;
    gameFinishedMilliseconds_ = 0;
    previewRandomSeed_.reset();
    audio_.stop();
    activeGameIndex_ = -1;
    audio_.playMusic(audio_catalog::kMenuMusic);
    screen_ = Screen::Menu;
    showSelectedMenuPose();
    updateFileMenu();
    updateOptionsMenu();
}

void App::toggleSound() {
    const bool enabled = !audio_.soundEnabled();
    audio_.setSoundEnabled(enabled);
    if (window_) CheckMenuItem(GetMenu(window_), kCommandSound,
                               MF_BYCOMMAND | (enabled ? MF_CHECKED : MF_UNCHECKED));
}

void App::toggleMusic() {
    const bool enabled = !audio_.musicEnabled();
    audio_.setMusicEnabled(enabled);
    if (window_) CheckMenuItem(GetMenu(window_), kCommandMusic,
                               MF_BYCOMMAND | (enabled ? MF_CHECKED : MF_UNCHECKED));
}

void App::toggleBackground() {
    if (!window_ || fullscreen_.active()) return;
    if (!backgroundHidden_) {
        backgroundWindowRectValid_ = GetWindowRect(window_, &backgroundWindowRect_) != FALSE;
        RECT compact{0, 0, kLogicalWidth, kLogicalHeight};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
        const DWORD extended = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE));
        AdjustWindowRectEx(&compact, style, GetMenu(window_) != nullptr, extended);
        SetWindowPos(window_, nullptr, 0, 0, compact.right - compact.left,
                     compact.bottom - compact.top, SWP_NOMOVE | SWP_NOZORDER);
        backgroundHidden_ = true;
    } else {
        if (backgroundWindowRectValid_) {
            SetWindowPos(window_, nullptr, backgroundWindowRect_.left, backgroundWindowRect_.top,
                         backgroundWindowRect_.right - backgroundWindowRect_.left,
                         backgroundWindowRect_.bottom - backgroundWindowRect_.top, SWP_NOZORDER);
        }
        backgroundHidden_ = false;
    }
    updateOptionsMenu();
}

void App::toggleAnimatedPieces() {
    const int gameIndex = menu_catalog::gameContextIndex(activeGameIndex_, pendingGameIndex_);
    if (gameIndex != 0 && gameIndex != 2) return;
    animatedPieces_ = !animatedPieces_;
    if (game_) game_->setAnimatedPieces(animatedPieces_);
    updateOptionsMenu();
}

void App::toggleForcedJumps() {
    if (menu_catalog::gameContextIndex(activeGameIndex_, pendingGameIndex_) != 2) return;
    forcedJumps_ = !forcedJumps_;
    if (game_) game_->setForcedJumps(forcedJumps_);
    updateOptionsMenu();
}

void App::updateOptionsMenu() {
    if (!window_ || !optionsMenu_) return;
    while (GetMenuItemCount(optionsMenu_) > 0) DeleteMenu(optionsMenu_, 0, MF_BYPOSITION);
    AppendMenuW(optionsMenu_, MF_STRING, kCommandChangeName, L"Change &Name\x2026");
    AppendMenuW(optionsMenu_, MF_STRING | (audio_.soundEnabled() ? MF_CHECKED : 0),
                kCommandSound, L"&Sound\tS");
    AppendMenuW(optionsMenu_, MF_STRING | (audio_.musicEnabled() ? MF_CHECKED : 0),
                kCommandMusic, L"&Music\tM");
    AppendMenuW(optionsMenu_, MF_STRING | (backgroundHidden_ ? MF_CHECKED : 0),
                kCommandHideBackground, L"&Hide Background\tH");
    const int gameIndex = menu_catalog::gameContextIndex(activeGameIndex_, pendingGameIndex_);
    if (gameIndex == 0 || gameIndex == 2) {
        AppendMenuW(optionsMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(optionsMenu_, MF_STRING | (animatedPieces_ ? MF_CHECKED : 0),
                    kCommandAnimatedPieces, L"&Animated Pieces");
        if (gameIndex == 2) {
            AppendMenuW(optionsMenu_, MF_STRING | (forcedJumps_ ? MF_CHECKED : 0),
                        kCommandForcedJumps, L"&Forced Jumps");
        }
    }
    DrawMenuBar(window_);
}

void App::updateFileMenu() {
    if (!window_ || !fileMenu_) return;
    while (GetMenuItemCount(fileMenu_) > 0) DeleteMenu(fileMenu_, 0, MF_BYPOSITION);
    if (menu_catalog::fileQuitAction(activeGameIndex_, pendingGameIndex_) ==
        menu_catalog::FileQuitAction::ExitGame) {
        AppendMenuW(fileMenu_, MF_STRING, kCommandNewGame, L"&New Game\tCtrl+N");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, kCommandMainMenu, L"E&xit Game\tCtrl+Q");
    } else {
        AppendMenuW(fileMenu_, MF_STRING, kCommandExit, L"&Quit\tCtrl+Q");
    }
    DrawMenuBar(window_);
}

void App::updateEditMenu() {
    if (!editMenu_) return;
    const bool editing = screen_ == Screen::Name || screen_ == Screen::GameName;
    const bool hasText = editing && !playerName_.empty();
    EnableMenuItem(editMenu_, kCommandUndo, MF_BYCOMMAND | (editing ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(editMenu_, kCommandCut, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(editMenu_, kCommandCopy, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(editMenu_, kCommandPaste, MF_BYCOMMAND |
                   (editing && IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(editMenu_, kCommandClear, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
}

void App::copyNameToClipboard(bool clearAfterCopy) {
    if ((screen_ != Screen::Name && screen_ != Screen::GameName) || playerName_.empty() ||
        !OpenClipboard(window_)) return;
    const std::size_t bytes = (playerName_.size() + 1U) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        if (void* destination = GlobalLock(memory)) {
            std::memcpy(destination, playerName_.c_str(), bytes);
            GlobalUnlock(memory);
            EmptyClipboard();
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
            else memory = nullptr;
        }
        if (memory) GlobalFree(memory);
    }
    CloseClipboard();
    if (clearAfterCopy) playerName_.clear();
}

void App::pasteNameFromClipboard() {
    if ((screen_ != Screen::Name && screen_ != Screen::GameName) || !OpenClipboard(window_)) return;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
            playerName_.clear();
            for (std::size_t index = 0;
                 text[index] && playerName_.size() < kMaximumPlayerNameLength; ++index) {
                if (text[index] >= L' ' && text[index] != 0x7f) playerName_.push_back(text[index]);
            }
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
}

void App::loadPreferences() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPreferencesKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) return;

    const auto readFlag = [key](const wchar_t* name, bool fallback) {
        DWORD type{};
        DWORD value{};
        DWORD size = sizeof(value);
        return RegQueryValueExW(key, name, nullptr, &type,
                                reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
                       type == REG_DWORD && size == sizeof(value)
                   ? value != 0
                   : fallback;
    };
    audio_.setSoundEnabled(readFlag(L"Sound", audio_.soundEnabled()));
    audio_.setMusicEnabled(readFlag(L"Music", audio_.musicEnabled()));
    restoreBackgroundHidden_ = readFlag(L"HideBackground", false);
    animatedPieces_ = readFlag(L"AnimatedPieces", animatedPieces_);

    std::array<wchar_t, kMaximumPlayerNameLength + 1> savedName{};
    DWORD type{};
    DWORD size = static_cast<DWORD>(savedName.size() * sizeof(wchar_t));
    if (RegQueryValueExW(key, L"PlayerName", nullptr, &type,
                        reinterpret_cast<BYTE*>(savedName.data()), &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        savedName.back() = L'\0';
        std::wstring clean;
        for (wchar_t character : savedName) {
            if (!character) break;
            if (character >= L' ' && character != 0x7f) clean.push_back(character);
        }
        if (!clean.empty()) playerName_ = std::move(clean);
    }
    nameBeforeEdit_ = playerName_;
    RegCloseKey(key);
}

void App::savePreferences() const {
    HKEY key{};
    DWORD disposition{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kPreferencesKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                        &disposition) != ERROR_SUCCESS) return;

    const auto writeFlag = [key](const wchar_t* name, bool enabled) {
        const DWORD value = enabled ? 1U : 0U;
        RegSetValueExW(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value), sizeof(value));
    };
    writeFlag(L"Sound", audio_.soundEnabled());
    writeFlag(L"Music", audio_.musicEnabled());
    writeFlag(L"HideBackground", backgroundHidden_);
    writeFlag(L"AnimatedPieces", animatedPieces_);

    const std::wstring_view savedName = playerName_.empty()
        ? std::wstring_view(L"My friend")
        : std::wstring_view(playerName_).substr(0, kMaximumPlayerNameLength);
    RegSetValueExW(key, L"PlayerName", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(savedName.data()),
                   static_cast<DWORD>((savedName.size() + 1U) * sizeof(wchar_t)));
    RegCloseKey(key);
}

void App::toggleHelp(int gameIndex) {
    if (screen_ == Screen::Help && gameIndex < 0) {
        screen_ = helpReturnScreen_;
    } else if (screen_ == Screen::Menu || screen_ == Screen::Game || screen_ == Screen::Help) {
        if (screen_ != Screen::Help) helpReturnScreen_ = screen_;
        helpGameIndex_ = gameIndex >= 0 ? std::clamp(gameIndex, 0, 4)
                                        : std::max(0, activeGameIndex_);
        helpPage_ = 0;
        screen_ = Screen::Help;
    }
}

void App::toggleFullscreen() {
    fullscreen_.toggle(window_);
}

}  // namespace mf
