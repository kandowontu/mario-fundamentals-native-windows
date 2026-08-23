#include "dos_app.hpp"

#include "audio_catalog.hpp"
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
constexpr std::array<Rect, 5> kMenuButtons{{
    {58, 78, 137, 94}, {58, 95, 137, 111}, {58, 112, 137, 128},
    {58, 129, 137, 145}, {58, 146, 137, 162},
}};

}  // namespace

DosApp::DosApp(HINSTANCE instance)
    : instance_(instance),
      assets_(instance, IDR_DOS_ASSET_PACK, AssetDialect::Dos),
      graphics_(assets_), audio_(assets_),
      talkingTitle_(assets_, graphics_, audio_, false),
      menuReveal_(assets_, graphics_, audio_, false),
      menuSelection_(assets_, graphics_, audio_, false) {
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
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
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

void DosApp::renderQaFrames(std::wstring_view outputDirectory) {
    audio_.setEnabled(false);
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

    screen_ = Screen::Menu;
    menuSourceSelection_ = 1;
    save(L"04-menu.bmp");

    pendingGameIndex_ = 0;
    screen_ = Screen::Character;
    save(L"05-character.bmp");
    screen_ = Screen::Name;
    save(L"06-name.bmp");

    for (int gameIndex = 0; gameIndex < 5; ++gameIndex) {
        startGame(gameIndex);
        game_->render(canvas_);
        const std::wstring name = L"1" + std::to_wstring(gameIndex) + L"-game.bmp";
        canvas_.saveBmp((root / name).wstring());
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
    case WM_LBUTTONDOWN:
        click(toLogical({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
        return 0;
    case WM_MOUSEMOVE:
        if (screen_ == Screen::Game && game_ && dialog_ == Dialog::None)
            game_->mouseMove(toLogical({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
        return 0;
    case WM_LBUTTONUP:
        if (screen_ == Screen::Game && game_ && dialog_ == Dialog::None)
            game_->mouseUp(toLogical({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
        return 0;
    case WM_CHAR:
        characterInput(static_cast<wchar_t>(wParam));
        return 0;
    case WM_KEYDOWN:
        key(static_cast<unsigned>(wParam));
        return 0;
    case WM_CLOSE:
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
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    render();
    canvas_.present(dc, viewport_);
}

void DosApp::drawTiledPage(int resourceId) {
    const Sprite& first = graphics_.sprite(resourceId, 0);
    for (int frame = 0; frame < 64; ++frame) {
        canvas_.sprite(graphics_.sprite(resourceId, frame),
                       frame % 8 * first.width, frame / 8 * first.height, false);
    }
}

void DosApp::drawLiveTitle(bool talking) {
    drawTiledPage(1001);
    canvas_.sprite(graphics_.sprite(1010), 154, 104, false);
    canvas_.sprite(graphics_.sprite(1020), 115, 80, false);
    canvas_.sprite(graphics_.sprite(1100), 189, 107, false);
    if (!talking || !talkingTitle_.render(canvas_)) {
        canvas_.sprite(graphics_.sprite(1014), 180, 49, false);
    }
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
        drawLiveTitle(false);
        break;
    case IntroPhase::TalkingTitle:
        drawLiveTitle(true);
        break;
    case IntroPhase::MenuReveal:
        drawLiveTitle(false);
        (void)menuReveal_.render(canvas_);
        break;
    }
}

void DosApp::renderMenu() {
    drawLiveTitle(false);
    canvas_.sprite(graphics_.sprite(1040, menuSourceSelection_ - 1),
                   59, 78 + (menuSourceSelection_ - 1) * 17, false);
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
    if (pendingGameIndex_ >= 0) drawTiledPage(kGameBackgrounds[static_cast<std::size_t>(pendingGameIndex_)]);
    canvas_.sprite(graphics_.sprite(101), 83, 60, false);
    canvas_.sprite(graphics_.sprite(101, playerIsYoshi_ ? 3 : 4),
                   playerIsYoshi_ ? 115 : 160, 113, false);
}

void DosApp::renderName() {
    if (pendingGameIndex_ >= 0) drawTiledPage(kGameBackgrounds[static_cast<std::size_t>(pendingGameIndex_)]);
    canvas_.sprite(graphics_.sprite(100), 59, 58, false);
    canvas_.pakText(graphics_, playerName_, 225, {75, 93, 245, 107},
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE, 15);
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
    }
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
    InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::beginGameIntro(int gameIndex) {
    if (gameIndex < 0 || gameIndex >= 5) return;
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
    const auto add = [this](int id) {
        gameIntroMovies_.push_back({Movie(assets_, id), 0, 0});
        const Movie& movie = gameIntroMovies_.back().movie;
        gameIntroDurationMilliseconds_ = std::max(
            gameIntroDurationMilliseconds_,
            (movie.duration() * 1000U + movie.timeScale() - 1U) / movie.timeScale());
    };
    switch (gameIndex) {
    case 0: add(4999); break;
    case 1: add(3002); break;
    case 2: add(2801); add(2800); break;
    case 3: add(5101); add(5102); break;
    case 4: add(6100); add(6150); break;
    }
    screen_ = Screen::GameIntro;
    InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::finishGameIntro() {
    gameIntroMovies_.clear();
    if (pendingGameIndex_ <= 2 && !characterConfirmed_) {
        screen_ = Screen::Character;
    } else if (!nameConfirmed_) {
        screen_ = Screen::Name;
    } else {
        startGame(pendingGameIndex_);
    }
    InvalidateRect(window_, nullptr, FALSE);
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
    dialog_ = Dialog::None;
    gameFinishedMilliseconds_ = 0;
    pendingGameIndex_ = -1;
    screen_ = Screen::Game;
    InvalidateRect(window_, nullptr, FALSE);
}

void DosApp::returnToMenu() {
    audio_.stop();
    game_.reset();
    activeGameIndex_ = -1;
    pendingGameIndex_ = -1;
    dialog_ = Dialog::None;
    gameFinishedMilliseconds_ = 0;
    audio_.playMusic(audio_catalog::kDosMenuMusic);
    screen_ = Screen::Menu;
    selectMenu(menuSourceSelection_, false);
    InvalidateRect(window_, nullptr, FALSE);
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
        audio_.stop();
        advanceIntro(IntroPhase::DimTitle);
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
            audio_.playEffect(9204);
            if (nameConfirmed_) startGame(pendingGameIndex_);
            else InvalidateRect(window_, nullptr, FALSE);
        }
    } else if (screen_ == Screen::Name) {
        if (Rect{79, 113, 128, 127}.contains(point)) {
            nameConfirmed_ = true;
            audio_.playEffect(9204);
            startGame(pendingGameIndex_);
        } else if (Rect{191, 113, 240, 127}.contains(point)) {
            returnToMenu();
        }
    } else if (screen_ == Screen::Game && game_) {
        if (dialog_ != Dialog::None) clickDialog(point);
        else game_->mouseDown(point);
    } else if (screen_ == Screen::Credits) {
        returnToMenu();
    }
}

void DosApp::key(unsigned virtualKey) {
    if (virtualKey == VK_ESCAPE) {
        if (screen_ == Screen::Menu) DestroyWindow(window_);
        else if (screen_ == Screen::Intro) {
            audio_.stop();
            advanceIntro(IntroPhase::DimTitle);
        }
        else returnToMenu();
        return;
    }
    if (screen_ == Screen::Game && virtualKey == 'N' && dialog_ == Dialog::None) {
        dialog_ = Dialog::ConfirmReset;
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (screen_ != Screen::Name && virtualKey == 'S') {
        audio_.setSoundEnabled(!audio_.soundEnabled());
        return;
    }
    if (screen_ != Screen::Name && virtualKey == 'M') {
        audio_.setMusicEnabled(!audio_.musicEnabled());
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
        if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT) {
            playerIsYoshi_ = !playerIsYoshi_;
            InvalidateRect(window_, nullptr, FALSE);
        } else if (virtualKey == VK_RETURN) {
            characterConfirmed_ = true;
            screen_ = nameConfirmed_ ? Screen::Game : Screen::Name;
            if (nameConfirmed_) startGame(pendingGameIndex_);
            else InvalidateRect(window_, nullptr, FALSE);
        }
    } else if (screen_ == Screen::Name && virtualKey == VK_RETURN) {
        nameConfirmed_ = true;
        startGame(pendingGameIndex_);
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
    InvalidateRect(window_, nullptr, FALSE);
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
        if (gameIntroMilliseconds_ >= gameIntroDurationMilliseconds_ + 2000U)
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
