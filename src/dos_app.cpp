#include "dos_app.hpp"

#include "audio_catalog.hpp"
#include "dos_help_overlay.hpp"
#include "games/backgammon.hpp"
#include "games/checkers.hpp"
#include "games/dominoes.hpp"
#include "games/go_fish.hpp"
#include "games/yacht.hpp"

#include <cwchar>
#include <filesystem>
#include <windowsx.h>

namespace mf {
namespace {

constexpr std::array<int, 5> kGameBackgrounds{4001, 3001, 2999, 5001, 6001};
constexpr std::array<int, 5> kGameIntroBackgrounds{4000, 3000, 2998, 5000, 6000};
constexpr std::array<int, 5> kSelectionY{79, 79, 77, 82, 78};
constexpr std::array<int, 5> kButtonY{77, 93, 108, 123, 138};
constexpr std::array<int, 5> kNeutralHandY{101, 101, 99, 104, 100};
constexpr std::array<Rect, 5> kMenuButtons{{
    {58, 77, 136, 93}, {58, 93, 136, 108}, {58, 108, 136, 123},
    {58, 123, 136, 138}, {58, 138, 136, 153},
}};

constexpr int kDosMenuBarHeight = 8;
constexpr std::uint32_t kDosMenuYellow = rgb(252, 244, 64);
constexpr std::uint32_t kDosMenuHotKey = rgb(176, 0, 0);
constexpr std::uint64_t kDosSourceMenuBarHash = 0x1CC61A67AEFDBAA5ULL;

constexpr std::array<std::string_view, 8> kDosFileLabel{{
    "rrrrrrr...##.....###..........", ".rr...r...........##..........",
    ".rr.r....###......##.....####.", ".rrrr.....##......##....##..##",
    ".rr.r.....##......##....######", ".rr.......##......##....##....",
    "rrrr.....####....####....####.", "..............................",
}};
constexpr std::array<std::string_view, 8> kDosOptionsLabel{{
    "..rrr..............#......##..........................",
    ".rr.rr............##..................................",
    "rr...rr.##.###...#####...###.....####...#####....#####",
    "rr...rr..##..##...##......##....##..##..##..##..##....",
    "rr...rr..##..##...##......##....##..##..##..##...####.",
    ".rr.rr...#####....##.#....##....##..##..##..##......##",
    "..rrr....##........##....####....####...##..##..#####.",
    "........####..........................................",
}};
constexpr std::array<std::string_view, 8> kDosHelpLabel{{
    "rr..rr...........###...........", "rr..rr............##...........",
    "rr..rr...####.....##....##.###.", "rrrrrr..##..##....##.....##..##",
    "rr..rr..######....##.....##..##", "rr..rr..##........##.....#####.",
    "rr..rr...####....####....##....", "........................####...",
}};

void drawDosMenuLabel(Canvas& canvas, int left,
                      const std::array<std::string_view, 8>& mask) {
    for (int y = 0; y < static_cast<int>(mask.size()); ++y) {
        for (int x = 0; x < static_cast<int>(mask[static_cast<std::size_t>(y)].size()); ++x) {
            const char pixel = mask[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
            if (pixel == '#') canvas.fillRect({left + x, y, left + x + 1, y + 1}, rgb(0, 0, 0));
            else if (pixel == 'r')
                canvas.fillRect({left + x, y, left + x + 1, y + 1}, kDosMenuHotKey);
        }
    }
}

// The native game index follows the title-board order (Backgammon,
// Dominoes, Checkers, Go Fish, Yacht), while the authored instruction
// controller stores Checkers before Dominoes.
constexpr std::array<int, 5> kDosHelpPageBase{0, 4, 2, 6, 8};

enum class IntroSkipTarget { DimTitle, MenuReveal, Menu };

constexpr IntroSkipTarget introSkipTarget(int phase) {
    // Interplay, Presage, and Credits retain the original jump to the title.
    // Once the title tableau is visible, skipping must move monotonically into
    // the board reveal; skipping that reveal completes the menu.
    if (phase <= 2) return IntroSkipTarget::DimTitle;
    if (phase < 8) return IntroSkipTarget::MenuReveal;
    return IntroSkipTarget::Menu;
}

constexpr bool dosGameIntroComplete(std::uint32_t elapsedMilliseconds,
                                    std::uint32_t durationMilliseconds) {
    return elapsedMilliseconds >= durationMilliseconds;
}

constexpr Point dosCharacterQuestionPoint(int gameIndex) {
    // Backgammon and Checkers share the centred host actor registration.
    // Dominoes animates the small Mario portrait in its upper-left scorecard.
    return gameIndex == 1 ? Point{7, 3} : Point{116, 1};
}

constexpr std::array<std::uint64_t, 3> kDosCharacterQuestionQaHashes{
    0xC52CEFC40E134C64ULL, 0xD4A8DF111A71BA1FULL, 0x753AF8B08643B4DDULL};
constexpr std::array<std::uint64_t, 3> kDosCharacterChoiceQaHashes{
    0x5ECB5FF01CC23392ULL, 0xBB7CCF36B37D9131ULL, 0xFC049B8CCAD7E8E8ULL};
constexpr std::array<std::uint64_t, 5> kDosNamePromptQaHashes{
    0x92F8BACD0F4D7E58ULL, 0x0665714D87179076ULL, 0x44524397F669CCE0ULL,
    0x8301F9BED88DE3E3ULL, 0xDB68C1B280DB61A1ULL};
constexpr std::array<std::uint64_t, 2> kDosGoFishScoreboardQaHashes{
    0x30BDE2EDD196DDFBULL, 0xC5478D472EF086DEULL};
constexpr std::uint64_t kDosYachtIdleActorQaHash = 0xD58D8A2935D94949ULL;

}  // namespace

DosApp::DosApp(HINSTANCE instance)
    : instance_(instance),
      assets_(instance, IDR_DOS_ASSET_PACK, AssetDialect::Dos),
      graphics_(assets_), audio_(assets_),
      menuRevealMovie_(assets_, 1125),
      talkingTitle_(assets_, graphics_, audio_, false),
      menuReveal_(assets_, graphics_, audio_, false),
      menuSelection_(assets_, graphics_, audio_, false),
      characterQuestion_(assets_, graphics_, audio_, false) {
    const auto sourceTicks = static_cast<std::uint32_t>(GetTickCount64() * 60ULL / 1000ULL);
    random_.discard(sourceTicks & 0x3ffU);
}

DosApp::~DosApp() {
    audio_.stop();
    audio_.stopMusic();
}

void DosApp::createWindow(int showCommand) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = L"MarioGameGalleryNativeWindow";
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("DOS game window class registration failed");
    }
    RECT bounds{0, 0, 960, 600};
    AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0);
    window_ = CreateWindowExW(
        0, windowClass.lpszClassName, L"Mario's Game Gallery - DOS 1.0",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance_, this);
    if (!window_) throw std::runtime_error("DOS game window creation failed");
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    SetTimer(window_, 1, 33, nullptr);
}

int DosApp::run(int showCommand) {
    createWindow(showCommand);
    const wchar_t* commandLine = GetCommandLineW();
    const bool qa = std::wcsstr(commandLine, L"--qa-dos") != nullptr;
    if (qa) {
        audio_.setEnabled(false);
        if (std::wcsstr(commandLine, L"--qa-dos-menu")) {
            screen_ = Screen::Menu;
            selectMenu(1, false);
        } else if (const wchar_t* game = std::wcsstr(commandLine, L"--qa-dos-game=")) {
            nameConfirmed_ = characterConfirmed_ = true;
            startGame(static_cast<int>(std::wcstol(game + 14, nullptr, 10)));
        } else {
            screen_ = Screen::Intro;
            introPhase_ = IntroPhase::DimTitle;
        }
    } else {
        audio_.prewarmMusicOutput();
        advanceIntro(IntroPhase::Interplay);
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool DosApp::sourceIntroSkipRegressionTest() {
    return introSkipTarget(static_cast<int>(IntroPhase::Interplay)) ==
               IntroSkipTarget::DimTitle &&
           introSkipTarget(static_cast<int>(IntroPhase::Presage)) ==
               IntroSkipTarget::DimTitle &&
           introSkipTarget(static_cast<int>(IntroPhase::Credits)) ==
               IntroSkipTarget::DimTitle &&
           introSkipTarget(static_cast<int>(IntroPhase::DimTitle)) ==
               IntroSkipTarget::MenuReveal &&
           introSkipTarget(static_cast<int>(IntroPhase::TalkingTitle)) ==
               IntroSkipTarget::MenuReveal &&
           introSkipTarget(static_cast<int>(IntroPhase::MenuReveal)) ==
               IntroSkipTarget::Menu;
}

bool DosApp::sourceGameIntroCompletionRegressionTest() {
    constexpr std::uint32_t duration = 7000;
    return !dosGameIntroComplete(0, duration) &&
           !dosGameIntroComplete(duration - 1, duration) &&
           dosGameIntroComplete(duration, duration) &&
           dosGameIntroComplete(duration + 1, duration);
}

void DosApp::renderQaFrames(std::wstring_view outputDirectory) {
    audio_.setEnabled(false);
    // QA must not inherit the constructor's boot-time source seed. Stable
    // first-use game previews and later interaction frames need identical
    // decks/dice/selectors on every release build.
    random_.setSeed(0x4d474731U);
    const std::filesystem::path root(outputDirectory);
    std::filesystem::create_directories(root);
    const auto save = [this, &root](std::wstring_view name) {
        render();
        canvas_.saveBmp((root / name).wstring());
    };

    screen_ = Screen::Intro;
    introPhase_ = IntroPhase::Interplay;
    save(L"00-interplay.bmp");
    introPhase_ = IntroPhase::Presage;
    save(L"01-presage.bmp");
    introPhase_ = IntroPhase::Credits;
    save(L"02-credits.bmp");
    introPhase_ = IntroPhase::DimTitle;
    save(L"03-title-dim.bmp");
    introPhase_ = IntroPhase::LightBuzz;
    save(L"03a-title-live-concealed.bmp");
    introPhase_ = IntroPhase::TalkingTitle;
    talkingTitle_.showFrame(12091, 179, 48, 600);
    save(L"03b-title-talking.bmp");
    talkingTitle_.stop();

    introPhase_ = IntroPhase::MenuReveal;
    constexpr std::array<std::pair<std::uint32_t, std::wstring_view>, 4> revealFrames{{
        {0, L"03c-menu-reveal-00.bmp"},
        {180, L"03d-menu-reveal-03.bmp"},
        {420, L"03e-menu-reveal-07.bmp"},
        {840, L"03f-menu-reveal-14.bmp"},
    }};
    for (const auto& [sourceTime, name] : revealFrames) {
        menuReveal_.showFrame(1125, 45, 55, sourceTime);
        save(name);
    }
    menuReveal_.stop();

    // Exercise the live input path as well as direct timeline sampling. A
    // title-stage click must begin the reveal, and another click must leave the
    // completed board visible rather than returning to DimTitle.
    screen_ = Screen::Intro;
    introPhase_ = IntroPhase::DimTitle;
    skipIntro();
    save(L"03g-skip-reveal-start.bmp");
    skipIntro();
    save(L"03h-skip-menu-complete.bmp");

    screen_ = Screen::Menu;
    menuSourceSelection_ = 1;
    save(L"04-menu.bmp");
    if (canvas_.pixelHash({0, 0, kDosLogicalWidth, kDosMenuBarHeight}) !=
        kDosSourceMenuBarHash) {
        throw std::runtime_error(
            "DOS source menu bar raster regression: " +
            std::to_string(canvas_.pixelHash({0, 0, kDosLogicalWidth,
                                              kDosMenuBarHeight})));
    }
    menuPopup_ = MenuPopup::File;
    save(L"04a-file-menu.bmp");
    menuPopup_ = MenuPopup::Options;
    save(L"04b-options-menu.bmp");
    menuPopup_ = MenuPopup::Help;
    save(L"04c-help-menu.bmp");
    menuPopup_ = MenuPopup::None;
    for (int sourceSelection = 1; sourceSelection <= 5; ++sourceSelection) {
        menuSourceSelection_ = sourceSelection;
        const int movieId = menu_catalog::movie(sourceSelection);
        const Movie selection(assets_, movieId);
        menuSelection_.showFrame(
            movieId, 242, kSelectionY[static_cast<std::size_t>(sourceSelection - 1)],
            selection.duration() / 2);
        save(L"04-selection-" + std::to_wstring(sourceSelection) + L".bmp");
    }
    menuSelection_.stop();

    // Exercise the real first-use input controller, including its locked
    // talking phase and preview-RNG rollback. A click on the still-hidden
    // panel must do nothing; the same click after movie 11093 ends must enter
    // the name prompt, whose confirmation creates exactly one source-random
    // game instance rather than retaining the preview's consumed state.
    const std::uint32_t firstUseSourceSeed = random_.seed();
    characterConfirmed_ = false;
    nameConfirmed_ = false;
    pendingGameIndex_ = 0;
    beginFirstUsePreview(0, true);
    click({130, 118});
    if (screen_ != Screen::Character || characterConfirmed_ || !previewRandomSeed_)
        throw std::runtime_error("DOS first-use question accepted input before completion");
    for (int tick = 0; tick < 100 && characterQuestion_.active(); ++tick)
        (void)characterQuestion_.tick();
    if (characterQuestion_.active())
        throw std::runtime_error("DOS first-use question did not reach its authored end");
    click({130, 118});
    if (screen_ != Screen::Name || !characterConfirmed_ || !previewRandomSeed_ || !game_)
        throw std::runtime_error("DOS first-use character choice did not retain its preview");
    click({100, 120});
    if (screen_ != Screen::Game || !nameConfirmed_ || previewRandomSeed_ || !game_ ||
        pendingGameIndex_ != -1) {
        throw std::runtime_error("DOS first-use name confirmation did not start the game");
    }
    const std::uint32_t firstUseActualSeed = random_.seed();
    random_.setSeed(firstUseSourceSeed);
    startGame(0);
    if (random_.seed() != firstUseActualSeed)
        throw std::runtime_error("DOS first-use preview consumed the playable RNG sequence twice");
    game_.reset();
    random_.setSeed(firstUseSourceSeed);

    // First-use prompts are live game states, not panels over bare tiled
    // pages. Capture the talking question and terminal choice panel for all
    // three character-bearing games, then the name panel over all five games.
    characterConfirmed_ = false;
    nameConfirmed_ = false;
    for (int gameIndex = 0; gameIndex <= 2; ++gameIndex) {
        pendingGameIndex_ = gameIndex;
        beginFirstUsePreview(gameIndex, true);
        const Point point = dosCharacterQuestionPoint(gameIndex);
        characterQuestion_.showFrame(11093, point.x, point.y, 600);
        save(L"05-character-question-" + std::to_wstring(gameIndex) + L".bmp");
        const std::uint64_t questionHash =
            canvas_.pixelHash({0, 0, kDosLogicalWidth, kDosLogicalHeight});
        if (questionHash !=
            kDosCharacterQuestionQaHashes[static_cast<std::size_t>(gameIndex)]) {
            throw std::runtime_error("DOS first-use question composition changed: " +
                                     std::to_string(questionHash));
        }
        characterQuestion_.stop();
        save(gameIndex == 0 ? L"05-character.bmp" :
             L"05-character-" + std::to_wstring(gameIndex) + L".bmp");
        const std::uint64_t choiceHash =
            canvas_.pixelHash({0, 0, kDosLogicalWidth, kDosLogicalHeight});
        if (choiceHash != kDosCharacterChoiceQaHashes[static_cast<std::size_t>(gameIndex)]) {
            throw std::runtime_error("DOS first-use character panel composition changed: " +
                                     std::to_string(choiceHash));
        }
        game_.reset();
        if (previewRandomSeed_) random_.setSeed(*previewRandomSeed_);
        previewRandomSeed_.reset();
    }
    characterConfirmed_ = true;
    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        pendingGameIndex_ = gameIndex;
        beginFirstUsePreview(gameIndex, false);
        save(gameIndex == 0 ? L"06-name.bmp" :
             L"06-name-" + std::to_wstring(gameIndex) + L".bmp");
        const std::uint64_t nameHash =
            canvas_.pixelHash({0, 0, kDosLogicalWidth, kDosLogicalHeight});
        if (nameHash != kDosNamePromptQaHashes[static_cast<std::size_t>(gameIndex)]) {
            throw std::runtime_error("DOS first-use name panel composition changed: " +
                                     std::to_string(gameIndex) + ":" +
                                     std::to_string(nameHash));
        }
        game_.reset();
        if (previewRandomSeed_) random_.setSeed(*previewRandomSeed_);
        previewRandomSeed_.reset();
    }
    nameConfirmed_ = true;
    pendingGameIndex_ = -1;
    activeGameIndex_ = -1;

    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        helpGameIndex_ = gameIndex;
        for (int page = 0; page < 2; ++page) {
            helpPage_ = page;
            screen_ = Screen::Help;
            save(L"06-help-" + std::to_wstring(gameIndex) + L"-" +
                 std::to_wstring(page) + L".bmp");
        }
    }

    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        beginGameIntro(gameIndex);
        for (int sample = 0; sample <= 20; ++sample) {
            gameIntroMilliseconds_ = gameIntroDurationMilliseconds_ *
                                     static_cast<std::uint32_t>(sample) / 20U;
            // The independent Checkers reference was captured on source tick
            // 4200, where Pak 2801 frame 7 and Pak 2800 frame 6 meet. Preserve
            // that exact authored instant in the standard QA sequence instead
            // of landing 75 ticks later through integer twentieths.
            if (gameIndex == 2 && sample == 19) gameIntroMilliseconds_ = 7000U;
            save(L"07-intro-" + std::to_wstring(gameIndex) + L"-" +
                 (sample < 10 ? L"0" : L"") + std::to_wstring(sample) + L".bmp");
        }
    }
    gameIntroMovies_.clear();

    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        startGame(gameIndex);
        constexpr std::array<int, 8> openingTicks{0, 1, 8, 16, 32, 64, 96, 128};
        int elapsedTicks = 0;
        for (const int targetTicks : openingTicks) {
            while (elapsedTicks < targetTicks) {
                (void)game_->tick();
                ++elapsedTicks;
            }
            const std::wstring name = L"1" + std::to_wstring(gameIndex) +
                                      L"-opening-" + std::to_wstring(targetTicks) + L".bmp";
            save(name);
        }
        if (gameIndex == 2) {
            menuPopup_ = MenuPopup::File;
            save(L"12-checkers-file-menu.bmp");
            menuPopup_ = MenuPopup::Options;
            save(L"12-checkers-options-menu.bmp");
            menuPopup_ = MenuPopup::None;
        }
    }

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
        if (canvas_.pixelHash({4, 10, 100, 55}) != kDosGoFishScoreboardQaHashes[0] ||
            canvas_.pixelHash({218, 10, 315, 55}) != kDosGoFishScoreboardQaHashes[1]) {
            throw std::runtime_error("DOS Go Fish scoreboard captions or values changed");
        }
        if (canvas_.pixelHash({145, 20, 177, 42}) != 0x5E37997BB4C35C26ULL)
            throw std::runtime_error("DOS Go Fish idle head is not the solid source actor");
        goFish->setQaQuestionPresentation();
        save(L"35-gofish-question.bmp");
        goFish->setQaHandSlotsPresentation(true);
        save(L"35-gofish-hand-transfer.bmp");
        if (canvas_.pixelHash({145, 20, 177, 42}) != 0x5E37997BB4C35C26ULL)
            throw std::runtime_error("DOS Go Fish question card damaged the idle head");
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
        if (canvas_.pixelHash({119, 9, 184, 97}) != kDosYachtIdleActorQaHash)
            throw std::runtime_error("DOS Yacht idle actor lost its composed hand layer");
        yacht->setQaDiceSelectionPresentation();
        save(L"36-yacht-dice-selection.bmp");
        yacht->setQaVictoryPresentation();
        save(L"36-yacht-victory.bmp");
        for (std::uint32_t sourceTime = 0; sourceTime < 600; sourceTime += 60) {
            yacht->setQaRerollGesturePresentation(sourceTime);
            save(sourceTime == 240 ? L"36-yacht-reroll-gesture.bmp" :
                 L"36-yacht-reroll-gesture-" + std::to_wstring(sourceTime) + L".bmp");
        }

        yacht->setQaRollPresentation();
        constexpr std::array<int, 7> rollTicks{0, 4, 8, 16, 32, 48, 56};
        int elapsedTicks = 0;
        for (const int targetTicks : rollTicks) {
            while (elapsedTicks < targetTicks) {
                (void)yacht->tick();
                ++elapsedTicks;
            }
            save(L"30-yacht-roll-" + std::to_wstring(targetTicks) + L".bmp");
        }
    }

    dialog_ = Dialog::PlayAgain;
    save(L"15-play-again.bmp");
}

LRESULT CALLBACK DosApp::windowProcedure(HWND window, UINT message,
                                         WPARAM wParam, LPARAM lParam) {
    DosApp* app = reinterpret_cast<DosApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        app = static_cast<DosApp*>(create->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app ? app->handleMessage(window, message, wParam, lParam)
               : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT DosApp::handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (isFullscreenShortcut(message, wParam, lParam)) {
        fullscreen_.toggle(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        updateViewport();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paintInfo{};
        HDC dc = BeginPaint(window, &paintInfo);
        paint(dc);
        EndPaint(window, &paintInfo);
        return 0;
    }
    case WM_TIMER:
        tick();
        return 0;
    case WM_LBUTTONDOWN: {
        const Point logical = toLogical({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (clickMenuBar(logical)) {
            // The source menu consumes mouse-down before the active board.
        } else if (screen_ == Screen::Game && game_ && dialog_ == Dialog::None) {
            SetCapture(window);
            game_->mouseDown(logical);
        } else {
            click(logical);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (screen_ == Screen::Game && game_ && dialog_ == Dialog::None &&
            menuPopup_ == MenuPopup::None)
            game_->mouseMove(toLogical({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
        return 0;
    case WM_LBUTTONUP:
        if (screen_ == Screen::Game && game_ && dialog_ == Dialog::None &&
            menuPopup_ == MenuPopup::None)
            game_->mouseUp(toLogical({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
        if (GetCapture() == window) ReleaseCapture();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CAPTURECHANGED:
        if (screen_ == Screen::Game && game_) game_->mouseCancel();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CHAR:
        characterInput(static_cast<wchar_t>(wParam));
        return 0;
    case WM_KEYDOWN:
        key(static_cast<unsigned>(wParam));
        return 0;
    case WM_SYSCHAR:
        if (wParam == VK_RETURN) return 0;
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_CLOSE:
        fullscreen_.restore(window);
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        audio_.stop();
        audio_.stopMusic();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void DosApp::updateViewport() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    const int scale = std::max(1, std::min(clientWidth / kDosLogicalWidth,
                                           clientHeight / kDosLogicalHeight));
    const int width = kDosLogicalWidth * scale;
    const int height = kDosLogicalHeight * scale;
    viewport_ = {(clientWidth - width) / 2, (clientHeight - height) / 2,
                 (clientWidth - width) / 2 + width, (clientHeight - height) / 2 + height};
}

Point DosApp::toLogical(Point client) const {
    if (viewport_.width() <= 0 || viewport_.height() <= 0) return {};
    return {
        std::clamp((client.x - viewport_.left) * kDosLogicalWidth / viewport_.width(),
                   0, kDosLogicalWidth - 1),
        std::clamp((client.y - viewport_.top) * kDosLogicalHeight / viewport_.height(),
                   0, kDosLogicalHeight - 1),
    };
}

void DosApp::paint(HDC dc) {
    RECT client{};
    GetClientRect(window_, &client);

    // Compose the complete logical frame before touching the window DC.  The
    // previous full-client black fill was visible while the CPU built the next
    // frame and produced a black flash on every timer repaint.
    render();
    if (viewport_.width() <= 0 || viewport_.height() <= 0) {
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return;
    }

    // Clear only the letterbox.  Never paint black through the game viewport
    // immediately before presenting its already-composed frame.
    const int savedDc = SaveDC(dc);
    ExcludeClipRect(dc, viewport_.left, viewport_.top, viewport_.right, viewport_.bottom);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    RestoreDC(dc, savedDc);
    canvas_.present(dc, viewport_);
}

void DosApp::drawTiledPage(int resourceId) {
    const Sprite& first = graphics_.sprite(resourceId, 0);
    for (int frame = 0; frame < 64; ++frame) {
        canvas_.sprite(graphics_.sprite(resourceId, frame),
                       frame % 8 * first.width, frame / 8 * first.height, false);
    }
}

void DosApp::drawLiveTitle(bool talking, bool concealMenu) {
    drawTiledPage(1001);
    canvas_.sprite(graphics_.sprite(1010), 154, 104, false);
    canvas_.sprite(graphics_.sprite(1020), 115, 80, false);
    canvas_.sprite(graphics_.sprite(1100), 189, 107, false);
    if (!talking || !talkingTitle_.render(canvas_)) {
        canvas_.sprite(graphics_.sprite(1014), 180, 49, false);
    }
    // Every Snap* selection movie ends on frame zero, the open right hand.
    // The original actor retains that terminal cel.  Our movie wrapper stops
    // at completion, so explicitly restore the same cel while it is idle.
    if (!menuSelection_.active()) {
        const int selection = std::clamp(menuSourceSelection_ - 1, 0, 4);
        canvas_.sprite(graphics_.sprite(menu_catalog::movie(selection + 1), 0),
                       244, kNeutralHandY[static_cast<std::size_t>(selection)], false);
    }
    // Page 1001 is the final menu page and therefore already contains all five
    // labels.  Before movie 1125 starts, its authored blank base cel must cover
    // that area so the completed menu is not exposed ahead of the board flip.
    if (concealMenu) menuRevealMovie_.render(canvas_, graphics_, 0, 45, 55);
}

void DosApp::renderIntro() {
    switch (introPhase_) {
    case IntroPhase::Interplay:
        drawTiledPage(500);
        break;
    case IntroPhase::Presage:
        drawTiledPage(501);
        break;
    case IntroPhase::Credits:
        drawTiledPage(600);
        break;
    case IntroPhase::DimTitle:
        drawTiledPage(708);
        canvas_.sprite(graphics_.sprite(709), 133, 53, false);
        break;
    case IntroPhase::LightBuzz:
    case IntroPhase::Snare:
    case IntroPhase::Crash:
        drawLiveTitle(false, true);
        break;
    case IntroPhase::TalkingTitle:
        drawLiveTitle(true, true);
        break;
    case IntroPhase::MenuReveal:
        drawLiveTitle(false, false);
        (void)menuReveal_.render(canvas_);
        break;
    }
}

void DosApp::renderMenu() {
    drawLiveTitle(false, false);
    canvas_.sprite(graphics_.sprite(1040, menuSourceSelection_ - 1),
                   58, kButtonY[static_cast<std::size_t>(menuSourceSelection_ - 1)], false);
    (void)menuSelection_.render(canvas_);
}

void DosApp::renderGameIntro() {
    if (pendingGameIndex_ < 0 || pendingGameIndex_ >= 5) return;
    drawTiledPage(kGameIntroBackgrounds[static_cast<std::size_t>(pendingGameIndex_)]);
    for (const IntroMovie& instance : gameIntroMovies_) {
        const std::uint32_t time = std::min<std::uint32_t>(
            instance.movie.duration() - 1,
            gameIntroMilliseconds_ * instance.movie.timeScale() / 1000U);
        instance.movie.render(canvas_, graphics_, time, instance.x, instance.y);
    }
}

void DosApp::renderCharacter() {
    if (game_) game_->render(canvas_);
    else if (pendingGameIndex_ >= 0)
        drawTiledPage(kGameBackgrounds[static_cast<std::size_t>(pendingGameIndex_)]);
    if (characterQuestion_.render(canvas_)) return;
    canvas_.sprite(graphics_.sprite(101), 83, 60, false);
    canvas_.sprite(graphics_.sprite(101, playerIsYoshi_ ? 3 : 4),
                   playerIsYoshi_ ? 115 : 160, 113, false);
}

void DosApp::renderName() {
    if (changingName_ && nameReturnScreen_ == Screen::Game && game_)
        game_->render(canvas_);
    else if (!changingName_ && pendingGameIndex_ >= 0 && game_)
        game_->render(canvas_);
    else if (pendingGameIndex_ >= 0)
        drawTiledPage(kGameBackgrounds[static_cast<std::size_t>(pendingGameIndex_)]);
    else
        drawLiveTitle(false, false);
    canvas_.sprite(graphics_.sprite(100), 59, 58, false);
    canvas_.pakText(graphics_, playerName_, 225, {75, 93, 245, 107},
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE, 15);
}

void DosApp::renderHelp() {
    canvas_.sprite(graphics_.sprite(8000), 0, 0, false);
    const int game = std::clamp(helpGameIndex_, 0, 4);
    const int page = std::clamp(helpPage_, 0, 1);
    const int sourcePage = kDosHelpPageBase[static_cast<std::size_t>(game)] + page;
    canvas_.sprite(dosHelpOverlay(sourcePage), 0, 0, false);
    canvas_.sprite(graphics_.sprite(8001, page == 0 ? 6 : 0), 80, 170, false);
    canvas_.sprite(graphics_.sprite(8001, 1), 130, 170, false);
    canvas_.sprite(graphics_.sprite(8001, page == 1 ? 8 : 2), 180, 170, false);
}

bool DosApp::menuBarVisible() const noexcept {
    return screen_ == Screen::Menu || screen_ == Screen::Game;
}

void DosApp::renderMenuBar() {
    if (!menuBarVisible()) return;
    canvas_.fillRect({0, 0, kDosLogicalWidth, kDosMenuBarHeight}, kDosMenuYellow);
    drawDosMenuLabel(canvas_, 24, kDosFileLabel);
    drawDosMenuLabel(canvas_, 72, kDosOptionsLabel);
    drawDosMenuLabel(canvas_, 144, kDosHelpLabel);
    renderMenuPopup();
}

void DosApp::renderMenuPopup() {
    if (menuPopup_ == MenuPopup::None) return;
    Rect panel{};
    if (menuPopup_ == MenuPopup::File)
        panel = screen_ == Screen::Game ? Rect{20, 8, 139, 45} : Rect{20, 8, 94, 34};
    else if (menuPopup_ == MenuPopup::Options)
        panel = screen_ == Screen::Game ? Rect{72, 8, 190, activeGameIndex_ == 2 ? 70 : 60}
                                        : Rect{72, 8, 170, 51};
    else
        panel = {137, 8, 259, 62};
    canvas_.fillRect(panel, kDosMenuYellow);
    canvas_.outlineRect(panel, rgb(0, 0, 0));

    const auto item = [this](int x, int y, std::wstring_view value, bool checked = false) {
        if (checked)
            canvas_.pakText(graphics_, L"*", 224, {x, y, x + 8, y + 7},
                            DT_LEFT | DT_TOP | DT_SINGLELINE);
        canvas_.pakText(graphics_, value, 224,
                        {x + 9, y, kDosLogicalWidth - 2, y + 7},
                        DT_LEFT | DT_TOP | DT_SINGLELINE);
    };
    if (menuPopup_ == MenuPopup::File) {
        if (screen_ == Screen::Game) {
            item(23, 10, L"New Game");
            item(23, 20, L"Exit to Main Menu");
            canvas_.line({21, 31}, {137, 31}, rgb(0, 0, 0));
            item(23, 34, L"Exit");
        } else {
            item(23, 10, L"Credits");
            canvas_.line({21, 21}, {92, 21}, rgb(0, 0, 0));
            item(23, 24, L"Exit");
        }
    } else if (menuPopup_ == MenuPopup::Options) {
        item(75, 10, L"Change Name");
        canvas_.line({73, 21}, {panel.right - 2, 21}, rgb(0, 0, 0));
        item(75, 24, L"Music", audio_.musicEnabled());
        item(75, 34, L"Sound", audio_.soundEnabled());
        if (screen_ == Screen::Game) {
            canvas_.line({73, 45}, {panel.right - 2, 45}, rgb(0, 0, 0));
            item(75, 48, L"Animated Pieces", animatedPieces_);
            if (activeGameIndex_ == 2)
                item(75, 58, L"Forced Jumps", forcedJumps_);
        }
    } else {
        item(140, 10, L"Backgammon Help");
        item(140, 20, L"Checkers Help");
        item(140, 30, L"Dominoes Help");
        item(140, 40, L"Go Fish Help");
        item(140, 50, L"Yacht Help");
    }
}

void DosApp::renderDialog() {
    if (dialog_ == Dialog::None) return;
    const int resourceId = dialog_ == Dialog::ConfirmReset ? 105 : 106;
    const Sprite& panel = graphics_.sprite(resourceId, 0);
    const int panelX = (kDosLogicalWidth - panel.width) / 2;
    const int panelY = (kDosLogicalHeight - panel.height) / 2;
    canvas_.sprite(panel, panelX, panelY, false);
    canvas_.sprite(graphics_.sprite(resourceId, 1), panelX + 9, panelY + 62, false);
    canvas_.sprite(graphics_.sprite(resourceId, 2), panelX + panel.width - 58,
                   panelY + 62, false);
}

void DosApp::render() {
    canvas_.clear(rgb(0, 0, 0));
    switch (screen_) {
    case Screen::Intro: renderIntro(); break;
    case Screen::Menu: renderMenu(); break;
    case Screen::GameIntro: renderGameIntro(); break;
    case Screen::Character: renderCharacter(); break;
    case Screen::Name: renderName(); break;
    case Screen::Game:
        if (game_) {
            game_->render(canvas_);
            renderDialog();
        }
        break;
    case Screen::Credits:
        drawTiledPage(600);
        break;
    case Screen::Help:
        renderHelp();
        break;
    }
    renderMenuBar();
}

void DosApp::advanceIntro(IntroPhase phase) {
    introPhase_ = phase;
    phaseMilliseconds_ = 0;
    switch (phase) {
    case IntroPhase::Interplay: audio_.playSound(8039); break;
    case IntroPhase::Presage: audio_.playSound(8042); break;
    case IntroPhase::LightBuzz: audio_.playSound(5012); break;
    case IntroPhase::Snare: audio_.playSound(5000); break;
    case IntroPhase::Crash: audio_.playSound(5001); break;
    case IntroPhase::TalkingTitle: talkingTitle_.play(12091, 179, 48); break;
    case IntroPhase::MenuReveal:
        audio_.playSound(5011);
        menuReveal_.play(1125, 45, 55, false);
        audio_.playMusic(audio_catalog::kDosMenuMusic);
        break;
    default: break;
    }
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::selectMenu(int sourceSelection, bool animate) {
    sourceSelection = std::clamp(sourceSelection, 1, 5);
    if (sourceSelection == menuSourceSelection_ && !animate) return;
    menuSourceSelection_ = sourceSelection;
    if (animate) {
        audio_.playEffect(5003);
        menuSelection_.play(menu_catalog::movie(sourceSelection), 242,
                            kSelectionY[static_cast<std::size_t>(sourceSelection - 1)]);
    }
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::beginGameIntro(int gameIndex) {
    if (gameIndex < 0 || gameIndex >= 5) return;
    characterQuestion_.stop();
    previewRandomSeed_.reset();
    audio_.stop();
    audio_.stopMusic();
    audio_.playSound(5010);
    audio_.playMusic(audio_catalog::kDosPrimaryGameMusic[static_cast<std::size_t>(gameIndex)]);
    pendingGameIndex_ = gameIndex;
    activeGameIndex_ = -1;
    game_.reset();
    gameIntroMovies_.clear();
    gameIntroMilliseconds_ = 0;
    gameIntroDurationMilliseconds_ = 0;
    menuPopup_ = MenuPopup::None;
    const auto add = [this](int id, int x, int y) {
        gameIntroMovies_.push_back({Movie(assets_, id), x, y});
        const Movie& movie = gameIntroMovies_.back().movie;
        gameIntroDurationMilliseconds_ = std::max(
            gameIntroDurationMilliseconds_,
            (movie.duration() * 1000U + movie.timeScale() - 1U) / movie.timeScale());
    };
    switch (gameIndex) {
    // These direct-render offsets reconcile the positions written by DOS
    // overlays 1, 13, 7, 17, and 30 with each MuV's intrinsic registration.
    case 0:
        // Overlay 1 stores the Backgammon actor registration at (-100,126),
        // while MuV 4999 contributes its own (101,18) bounding origin. The
        // original controller removes that origin before handing the actor to
        // the movie player. Passing the raw registration directly made the
        // foreground land 101 pixels right and 18 pixels low; later cels only
        // disguised that as broken timing when they re-entered the screen.
        // This resolved origin puts Pak 4999 frame 2 at the independently
        // captured vanilla position (108,126).
        add(4999, -201, 108);
        break;
    // Overlay 13 anchors the surviving composite actor at stage y=120, while
    // movie 3002 registers its image plane at y=128.  Subtract the movie
    // registration instead of adding the actor anchor a second time.
    case 1: add(3002, 0, -8); break;
    case 2:
        // Checkers supplies registered actor points too; remove MuV 2801's
        // (3,10) and MuV 2800's (14,14) bounds origins before rendering.
        add(2801, -96, 134);
        add(2800, -107, 130);
        break;
    case 3:
        // The two Go Fish swimmers have independent vertical registrations.
        // Applying those origins twice put both actors below the vanilla lane.
        add(5101, 0, 79);
        add(5102, 0, 85);
        break;
    case 4: add(6100, -90, 0); add(6150, -123, 0); break;
    }
    screen_ = Screen::GameIntro;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::finishGameIntro() {
    gameIntroMovies_.clear();
    const int gameIndex = pendingGameIndex_;
    if (gameIndex < 0 || gameIndex >= 5) return;
    if (gameIndex <= 2 && !characterConfirmed_) {
        beginFirstUsePreview(gameIndex, true);
    } else if (!nameConfirmed_) {
        beginFirstUsePreview(gameIndex, false);
    } else {
        startGame(gameIndex);
    }
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::beginFirstUsePreview(int gameIndex, bool askCharacter) {
    if (gameIndex < 0 || gameIndex >= 5) return;
    const std::uint32_t sourceSeed = random_.seed();
    startGame(gameIndex);
    if (!game_) return;
    // Construct the authentic game presentation underneath the modal, then
    // roll the shared source RNG back before the actual playable instance is
    // created. Otherwise the first deck/board/dialogue selectors are consumed
    // twice merely because this is the first game of the session.
    previewRandomSeed_ = sourceSeed;
    pendingGameIndex_ = gameIndex;
    game_->setCharacterChooser(gameIndex <= 2);
    audio_.stop();
    if (askCharacter) {
        const Point point = dosCharacterQuestionPoint(gameIndex);
        characterQuestion_.play(11093, point.x, point.y);
        screen_ = Screen::Character;
    } else {
        characterQuestion_.stop();
        screen_ = Screen::Name;
    }
}

void DosApp::startGame(int gameIndex) {
    if (gameIndex < 0 || gameIndex >= 5) return;
    GameContext context{assets_, graphics_, audio_, random_, [this] { returnToMenu(); },
                        playerName_, playerIsYoshi_};
    switch (gameIndex) {
    case 0: game_ = std::make_unique<BackgammonGame>(context); break;
    case 1: game_ = std::make_unique<DominoesGame>(context); break;
    case 2: game_ = std::make_unique<CheckersGame>(context); break;
    case 3: game_ = std::make_unique<GoFishGame>(context); break;
    case 4: game_ = std::make_unique<YachtGame>(context); break;
    }
    activeGameIndex_ = gameIndex;
    game_->setAnimatedPieces(animatedPieces_);
    game_->setForcedJumps(forcedJumps_);
    dialog_ = Dialog::None;
    gameFinishedMilliseconds_ = 0;
    pendingGameIndex_ = -1;
    screen_ = Screen::Game;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::startPreviewedGame(int gameIndex) {
    if (previewRandomSeed_) {
        random_.setSeed(*previewRandomSeed_);
        previewRandomSeed_.reset();
    }
    startGame(gameIndex);
}

void DosApp::returnToMenu() {
    characterQuestion_.stop();
    previewRandomSeed_.reset();
    audio_.stop();
    game_.reset();
    activeGameIndex_ = -1;
    pendingGameIndex_ = -1;
    dialog_ = Dialog::None;
    gameFinishedMilliseconds_ = 0;
    menuPopup_ = MenuPopup::None;
    audio_.playMusic(audio_catalog::kDosMenuMusic);
    screen_ = Screen::Menu;
    selectMenu(menuSourceSelection_, false);
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::openHelp(int gameIndex) {
    if (gameIndex < 0 || gameIndex >= 5) return;
    helpReturnScreen_ = screen_;
    helpGameIndex_ = gameIndex;
    helpPage_ = 0;
    menuPopup_ = MenuPopup::None;
    screen_ = Screen::Help;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::beginChangeName() {
    nameBeforeEdit_ = playerName_;
    nameReturnScreen_ = screen_;
    pendingGameIndex_ = -1;
    changingName_ = true;
    menuPopup_ = MenuPopup::None;
    screen_ = Screen::Name;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

bool DosApp::clickMenuBar(Point point) {
    if (!menuBarVisible() && menuPopup_ == MenuPopup::None) return false;
    if (point.y < kDosMenuBarHeight && menuBarVisible()) {
        MenuPopup requested = MenuPopup::None;
        if (point.x >= 20 && point.x < 66) requested = MenuPopup::File;
        else if (point.x >= 70 && point.x < 132) requested = MenuPopup::Options;
        else if (point.x >= 136 && point.x < 181) requested = MenuPopup::Help;
        menuPopup_ = requested == menuPopup_ ? MenuPopup::None : requested;
        return true;
    }
    if (menuPopup_ == MenuPopup::None) return false;

    const MenuPopup popup = menuPopup_;
    menuPopup_ = MenuPopup::None;
    if (popup == MenuPopup::File) {
        if (screen_ == Screen::Game) {
            if (point.x >= 20 && point.x < 139 && point.y >= 8 && point.y < 19) {
                dialog_ = Dialog::ConfirmReset;
            } else if (point.x >= 20 && point.x < 139 && point.y >= 19 && point.y < 31) {
                returnToMenu();
            } else if (point.x >= 20 && point.x < 139 && point.y >= 32 && point.y < 45) {
                if (window_) DestroyWindow(window_);
            }
        } else if (point.x >= 20 && point.x < 94 && point.y >= 8 && point.y < 21) {
            pictureReturnScreen_ = screen_;
            screen_ = Screen::Credits;
            audio_.playSound(audio_catalog::kCreditsSound);
        } else if (point.x >= 20 && point.x < 94 && point.y >= 22 && point.y < 34) {
            if (window_) DestroyWindow(window_);
        }
    } else if (popup == MenuPopup::Options && point.x >= 72 && point.x < 190) {
        if (point.y >= 8 && point.y < 21) {
            beginChangeName();
        } else if (point.y >= 22 && point.y < 33) {
            audio_.setMusicEnabled(!audio_.musicEnabled());
        } else if (point.y >= 33 && point.y < 44) {
            audio_.setSoundEnabled(!audio_.soundEnabled());
        } else if (screen_ == Screen::Game && point.y >= 46 && point.y < 57) {
            animatedPieces_ = !animatedPieces_;
            if (game_) game_->setAnimatedPieces(animatedPieces_);
        } else if (screen_ == Screen::Game && activeGameIndex_ == 2 &&
                   point.y >= 57 && point.y < 69) {
            forcedJumps_ = !forcedJumps_;
            if (game_) game_->setForcedJumps(forcedJumps_);
        }
    } else if (popup == MenuPopup::Help && point.x >= 137 && point.x < 259 &&
               point.y >= 8 && point.y < 62) {
        const int sourceRow = std::clamp((point.y - 9) / 10, 0, 4);
        // Source menu order is Backgammon, Checkers, Dominoes, Go Fish, Yacht;
        // native controller order is Backgammon, Dominoes, Checkers, Go Fish, Yacht.
        constexpr std::array<int, 5> helpGameOrder{0, 2, 1, 3, 4};
        openHelp(helpGameOrder[static_cast<std::size_t>(sourceRow)]);
    }
    if (window_) InvalidateRect(window_, nullptr, FALSE);
    return true;
}

void DosApp::clickDialog(Point point) {
    if (dialog_ == Dialog::None) return;
    const int resourceId = dialog_ == Dialog::ConfirmReset ? 105 : 106;
    const Sprite& panel = graphics_.sprite(resourceId, 0);
    const int panelX = (kDosLogicalWidth - panel.width) / 2;
    const int panelY = (kDosLogicalHeight - panel.height) / 2;
    const Rect yes{panelX + 9, panelY + 62, panelX + 58, panelY + 82};
    const Rect no{panelX + panel.width - 58, panelY + 62,
                  panelX + panel.width - 9, panelY + 82};
    if (!yes.contains(point) && !no.contains(point)) return;
    audio_.playEffect(9204);
    const Dialog responseTo = dialog_;
    dialog_ = Dialog::None;
    if (yes.contains(point)) {
        if (responseTo == Dialog::PlayAgain && game_) {
            game_->resetForReplay();
            gameFinishedMilliseconds_ = 0;
            if (activeGameIndex_ >= 0 && activeGameIndex_ < 5)
                audio_.playMusic(
                    audio_catalog::kDosPrimaryGameMusic[static_cast<std::size_t>(activeGameIndex_)]);
        } else {
            startGame(activeGameIndex_);
        }
    } else if (responseTo == Dialog::PlayAgain) {
        returnToMenu();
    }
}

void DosApp::click(Point point) {
    if (screen_ == Screen::Intro) {
        skipIntro();
        return;
    }
    if (screen_ == Screen::Menu) {
        for (int index = 0; index < 5; ++index) {
            if (!kMenuButtons[static_cast<std::size_t>(index)].contains(point)) continue;
            if (menuSourceSelection_ != index + 1) selectMenu(index + 1, true);
            else beginGameIntro(menu_catalog::gameIndex(index + 1));
            return;
        }
    } else if (screen_ == Screen::Character) {
        if (characterQuestion_.active()) return;
        if (Rect{115, 113, 157, 125}.contains(point)) {
            playerIsYoshi_ = true;
            characterConfirmed_ = true;
            screen_ = nameConfirmed_ ? Screen::Game : Screen::Name;
        } else if (Rect{160, 113, 204, 125}.contains(point)) {
            playerIsYoshi_ = false;
            characterConfirmed_ = true;
            screen_ = nameConfirmed_ ? Screen::Game : Screen::Name;
        }
        if (characterConfirmed_) {
            characterQuestion_.stop();
            audio_.stop();
            audio_.playEffect(9204);
            if (nameConfirmed_) startPreviewedGame(pendingGameIndex_);
            else if (window_) InvalidateRect(window_, nullptr, FALSE);
        }
    } else if (screen_ == Screen::Name) {
        if (Rect{79, 113, 128, 127}.contains(point)) {
            if (playerName_.empty()) return;
            nameConfirmed_ = true;
            audio_.playEffect(9204);
            if (changingName_) {
                changingName_ = false;
                if (game_) game_->setPlayerName(playerName_);
                screen_ = nameReturnScreen_;
            } else {
                startPreviewedGame(pendingGameIndex_);
            }
        } else if (Rect{191, 113, 240, 127}.contains(point)) {
            if (changingName_) {
                changingName_ = false;
                playerName_ = nameBeforeEdit_;
                screen_ = nameReturnScreen_;
            } else {
                returnToMenu();
            }
        }
    } else if (screen_ == Screen::Game && game_) {
        if (dialog_ != Dialog::None) clickDialog(point);
        else game_->mouseDown(point);
    } else if (screen_ == Screen::Credits) {
        audio_.stop();
        screen_ = pictureReturnScreen_;
    } else if (screen_ == Screen::Help) {
        if (Rect{80, 170, 127, 188}.contains(point) && helpPage_ > 0) {
            --helpPage_;
            audio_.playEffect(5003);
        } else if (Rect{130, 170, 177, 188}.contains(point)) {
            screen_ = helpReturnScreen_;
        } else if (Rect{180, 170, 227, 188}.contains(point) && helpPage_ < 1) {
            ++helpPage_;
            audio_.playEffect(5003);
        }
    }
}

void DosApp::skipIntro() {
    audio_.stop();
    talkingTitle_.stop();
    const IntroSkipTarget target = introSkipTarget(static_cast<int>(introPhase_));
    if (target == IntroSkipTarget::DimTitle) {
        menuReveal_.stop();
        advanceIntro(IntroPhase::DimTitle);
    } else if (target == IntroSkipTarget::MenuReveal) {
        menuReveal_.stop();
        advanceIntro(IntroPhase::MenuReveal);
    } else {
        menuReveal_.stop();
        screen_ = Screen::Menu;
        selectMenu(1, false);
        if (window_) InvalidateRect(window_, nullptr, FALSE);
    }
}

void DosApp::key(unsigned virtualKey) {
    if (menuPopup_ != MenuPopup::None && virtualKey == VK_ESCAPE) {
        menuPopup_ = MenuPopup::None;
        if (window_) InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (virtualKey == VK_ESCAPE) {
        if (screen_ == Screen::Menu) DestroyWindow(window_);
        else if (screen_ == Screen::Intro) {
            skipIntro();
        }
        else if (screen_ == Screen::Help) screen_ = helpReturnScreen_;
        else if (screen_ == Screen::Credits) {
            audio_.stop();
            screen_ = pictureReturnScreen_;
        }
        else if (screen_ == Screen::Name && changingName_) {
            changingName_ = false;
            playerName_ = nameBeforeEdit_;
            screen_ = nameReturnScreen_;
        }
        else returnToMenu();
        if (window_ && IsWindow(window_)) InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (screen_ == Screen::Game && virtualKey == 'N' && dialog_ == Dialog::None) {
        dialog_ = Dialog::ConfirmReset;
        if (window_) InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (screen_ != Screen::Name && virtualKey == 'S') {
        audio_.setSoundEnabled(!audio_.soundEnabled());
        if (window_) InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (screen_ != Screen::Name && virtualKey == 'M') {
        audio_.setMusicEnabled(!audio_.musicEnabled());
        if (window_) InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (virtualKey == VK_F1 && menuBarVisible()) {
        const int gameIndex = screen_ == Screen::Game
            ? activeGameIndex_ : menu_catalog::gameIndex(menuSourceSelection_);
        openHelp(gameIndex);
        return;
    }
    if (screen_ == Screen::Menu) {
        int selection = 0;
        if (virtualKey == VK_UP || virtualKey == VK_LEFT)
            selection = menuSourceSelection_ == 1 ? 5 : menuSourceSelection_ - 1;
        else if (virtualKey == VK_DOWN || virtualKey == VK_RIGHT)
            selection = menuSourceSelection_ == 5 ? 1 : menuSourceSelection_ + 1;
        else if (virtualKey == 'C') selection = 1;
        else if (virtualKey == 'G') selection = 2;
        else if (virtualKey == 'D') selection = 3;
        else if (virtualKey == 'B') selection = 4;
        else if (virtualKey == 'Y') selection = 5;
        if (selection) selectMenu(selection, true);
        else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE)
            beginGameIntro(menu_catalog::gameIndex(menuSourceSelection_));
    } else if (screen_ == Screen::Character) {
        if (characterQuestion_.active()) return;
        if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT) {
            playerIsYoshi_ = !playerIsYoshi_;
            if (window_) InvalidateRect(window_, nullptr, FALSE);
        } else if (virtualKey == VK_RETURN) {
            characterConfirmed_ = true;
            characterQuestion_.stop();
            audio_.stop();
            audio_.playEffect(9204);
            screen_ = nameConfirmed_ ? Screen::Game : Screen::Name;
            if (nameConfirmed_) startPreviewedGame(pendingGameIndex_);
            else if (window_) InvalidateRect(window_, nullptr, FALSE);
        }
    } else if (screen_ == Screen::Name && virtualKey == VK_RETURN && !playerName_.empty()) {
        nameConfirmed_ = true;
        if (changingName_) {
            changingName_ = false;
            if (game_) game_->setPlayerName(playerName_);
            screen_ = nameReturnScreen_;
        } else {
            audio_.playEffect(9204);
            startPreviewedGame(pendingGameIndex_);
        }
    } else if (screen_ == Screen::Game && game_ && dialog_ == Dialog::None) {
        game_->key(virtualKey);
    }
}

void DosApp::characterInput(wchar_t character) {
    if (screen_ != Screen::Name) return;
    if (character == L'\b') {
        if (!playerName_.empty()) playerName_.pop_back();
    } else if (character >= L' ' && character < 0x7f && playerName_.size() < 15) {
        playerName_.push_back(character);
    } else {
        return;
    }
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::tick() {
    audio_.tick(33);
    bool repaint = false;
    if (screen_ == Screen::Intro) {
        phaseMilliseconds_ += 33;
        switch (introPhase_) {
        case IntroPhase::Interplay:
            if (!audio_.soundPlaying() && phaseMilliseconds_ >= 1200) advanceIntro(IntroPhase::Presage);
            break;
        case IntroPhase::Presage:
            if (!audio_.soundPlaying() && phaseMilliseconds_ >= 1200) advanceIntro(IntroPhase::Credits);
            break;
        case IntroPhase::Credits:
            if (phaseMilliseconds_ >= 3000) advanceIntro(IntroPhase::DimTitle);
            break;
        case IntroPhase::DimTitle:
            if (phaseMilliseconds_ >= 1500) advanceIntro(IntroPhase::LightBuzz);
            break;
        case IntroPhase::LightBuzz:
            if (audio_.soundPlaying()) phaseMilliseconds_ = 0;
            else if (phaseMilliseconds_ >= 200) advanceIntro(IntroPhase::Snare);
            break;
        case IntroPhase::Snare:
            if (!audio_.soundPlaying()) advanceIntro(IntroPhase::Crash);
            break;
        case IntroPhase::Crash:
            if (audio_.soundPlaying()) phaseMilliseconds_ = 0;
            else if (phaseMilliseconds_ >= 500) advanceIntro(IntroPhase::TalkingTitle);
            break;
        case IntroPhase::TalkingTitle:
            repaint |= talkingTitle_.tick();
            if (!talkingTitle_.active()) advanceIntro(IntroPhase::MenuReveal);
            break;
        case IntroPhase::MenuReveal:
            repaint |= menuReveal_.tick();
            if (!menuReveal_.active() && !audio_.soundPlaying()) {
                screen_ = Screen::Menu;
                selectMenu(1, false);
            }
            break;
        }
        repaint = true;
    } else if (screen_ == Screen::Menu) {
        repaint |= menuSelection_.tick();
    } else if (screen_ == Screen::Character) {
        repaint |= characterQuestion_.tick();
    } else if (screen_ == Screen::GameIntro) {
        const std::uint32_t previousMilliseconds = gameIntroMilliseconds_;
        gameIntroMilliseconds_ += 33;
        for (const IntroMovie& instance : gameIntroMovies_) {
            const Movie& movie = instance.movie;
            const std::uint32_t previousTime = std::min<std::uint32_t>(
                movie.duration(), previousMilliseconds * movie.timeScale() / 1000U);
            const std::uint32_t currentTime = std::min<std::uint32_t>(
                movie.duration(), gameIntroMilliseconds_ * movie.timeScale() / 1000U);
            for (int sound : movie.soundsBetween(previousTime, currentTime))
                audio_.playEffect(sound);
        }
        repaint = true;
        // The DOS overlay controllers leave as soon as their final live actor
        // completes.  The Macintosh shell's two-second tableau hold does not
        // exist here; retaining it exposed blank/trailing cels in every DOS
        // introduction, most visibly the separated Yacht hull.
        if (dosGameIntroComplete(gameIntroMilliseconds_, gameIntroDurationMilliseconds_))
            finishGameIntro();
    } else if (screen_ == Screen::Game && game_) {
        if (dialog_ == Dialog::None) repaint |= game_->tick();
        if (game_->finished() && dialog_ == Dialog::None) {
            gameFinishedMilliseconds_ += 33;
            if (gameFinishedMilliseconds_ >= game_->postFinishDelayMilliseconds() &&
                !audio_.soundPlaying()) {
                dialog_ = Dialog::PlayAgain;
                repaint = true;
            }
        }
    }
    if (repaint && window_) InvalidateRect(window_, nullptr, FALSE);
}

}  // namespace mf
