#pragma once

#include "game.hpp"
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

private:
    enum class Screen { Intro, Menu, GameIntro, Character, Name, Game, Credits };
    enum class Dialog { None, ConfirmReset, PlayAgain };
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
    void renderDialog();
    void clickDialog(Point point);
    void click(Point point);
    void key(unsigned virtualKey);
    void characterInput(wchar_t character);
    void tick();
    void advanceIntro(IntroPhase phase);
    void selectMenu(int sourceSelection, bool animate);
    void beginGameIntro(int gameIndex);
    void finishGameIntro();
    void startGame(int gameIndex);
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
    std::wstring playerName_{L"My Friend"};
    Rect viewport_{};
};

}  // namespace mf
