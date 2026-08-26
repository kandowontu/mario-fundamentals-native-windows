#pragma once

#include "game.hpp"
#include "fullscreen.hpp"
#include "menu_catalog.hpp"
#include "resource_ids.h"

#include <windows.h>

namespace mf {

class DosApp {
public:
    explicit DosApp(HINSTANCE instance);
    ~DosApp();
    int run(int showCommand);
    void renderQaFrames(std::wstring_view outputDirectory);
    [[nodiscard]] static bool sourceIntroSkipRegressionTest();
    [[nodiscard]] static bool sourceGameIntroCompletionRegressionTest();

private:
    enum class Screen { Intro, Menu, GameIntro, Character, Name, Game, Credits, Help };
    enum class Dialog { None, ConfirmReset, PlayAgain };
    enum class MenuPopup { None, File, Options, Help };
    enum class IntroPhase {
        Interplay,
        Presage,
        Credits,
        DimTitle,
        LightBuzz,
        Snare,
        Crash,
        TalkingTitle,
        MenuReveal,
    };

    struct IntroMovie {
        Movie movie;
        int x{};
        int y{};
    };

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message,
                                             WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void createWindow(int showCommand);
    void updateViewport();
    [[nodiscard]] Point toLogical(Point client) const;
    void paint(HDC dc);
    void render();
    void drawTiledPage(int resourceId);
    void drawLiveTitle(bool talking, bool concealMenu);
    void renderIntro();
    void renderMenu();
    void renderGameIntro();
    void renderCharacter();
    void renderName();
    void renderHelp();
    void renderDialog();
    void renderMenuBar();
    void renderMenuPopup();
    [[nodiscard]] bool menuBarVisible() const noexcept;
    [[nodiscard]] bool clickMenuBar(Point point);
    void openHelp(int gameIndex);
    void beginChangeName();
    void clickDialog(Point point);
    void click(Point point);
    void key(unsigned virtualKey);
    void characterInput(wchar_t character);
    void tick();
    void skipIntro();
    void advanceIntro(IntroPhase phase);
    void selectMenu(int sourceSelection, bool animate);
    void beginGameIntro(int gameIndex);
    void finishGameIntro(bool skippedByInput = false);
    void beginFirstUsePreview(int gameIndex, bool askCharacter);
    void startGame(int gameIndex);
    void startPreviewedGame(int gameIndex);
    void returnToMenu();

    HINSTANCE instance_{};
    HWND window_{};
    AssetStore assets_;
    GraphicsAssets graphics_;
    Audio audio_;
    SourceRandom random_{1};
    Canvas canvas_{kDosLogicalWidth, kDosLogicalHeight};
    Movie menuRevealMovie_;
    HostAnimation talkingTitle_;
    HostAnimation menuReveal_;
    HostAnimation menuSelection_;
    HostAnimation characterQuestion_;
    std::unique_ptr<Game> game_;
    std::vector<IntroMovie> gameIntroMovies_;
    Screen screen_{Screen::Intro};
    IntroPhase introPhase_{IntroPhase::Interplay};
    std::uint32_t phaseMilliseconds_{};
    std::uint32_t gameIntroMilliseconds_{};
    std::uint32_t gameIntroDurationMilliseconds_{};
    int menuSourceSelection_{1};
    int pendingGameIndex_{-1};
    int activeGameIndex_{-1};
    Dialog dialog_{Dialog::None};
    std::uint32_t gameFinishedMilliseconds_{};
    bool playerIsYoshi_{true};
    bool characterConfirmed_{};
    bool nameConfirmed_{};
    std::optional<std::uint32_t> previewRandomSeed_;
    std::wstring playerName_{L"My Friend"};
    std::wstring nameBeforeEdit_;
    Screen nameReturnScreen_{Screen::Menu};
    Screen pictureReturnScreen_{Screen::Menu};
    Screen helpReturnScreen_{Screen::Menu};
    MenuPopup menuPopup_{MenuPopup::None};
    int helpGameIndex_{};
    int helpPage_{};
    int qaGameIntroCompletionCount_{};
    bool changingName_{};
    bool qaGameIntroInputProbe_{};
    bool suppressNextCharacter_{};
    bool animatedPieces_{true};
    bool forcedJumps_{true};
    Rect viewport_{};
    FullscreenController fullscreen_;
};

}  // namespace mf
