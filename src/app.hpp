#pragma once

#include "game.hpp"
#include "fullscreen.hpp"
#include "movie.hpp"
#include "resource_ids.h"
#include "source_fonts.hpp"

#include <windows.h>

namespace mf {

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();
    int run(int showCommand);
    void renderQaFrames(std::wstring_view outputDirectory);
    [[nodiscard]] static bool sourceIntroSkipRegressionTest();

private:
    enum class Screen {
        Intro, Name, Character, Menu, GameIntro, GameCharacter, GameName, Game, About, Credits, Help
    };
    enum class Dialog { None, ConfirmReset, PlayAgain };
    enum class MenuSelectionPhase {
        Idle,
        RetractOutgoing,
        StepPointer,
        PreIncomingDelay,
        ShowSelectedLabel,
        StartIncoming,
        RevealIncoming,
    };
    enum class IntroPhase {
        StartupBlack,
        Brainstorm,
        BrainstormFade,
        PublisherGap,
        SteppingStone,
        SteppingStoneFade,
        TitleGap,
        Silhouette,
        Greeting,
        GreetingPause,
        TitleCue,
        TitleResponse,
        TalkingHead,
        MenuReveal
    };

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void createWindow(int showCommand);
    void runShellQa();
    void render();
    void renderIntro();
    void renderName();
    void renderCharacter();
    void renderGameCharacter();
    void renderGameName();
    void renderMenu();
    void renderGameIntro();
    void renderAbout();
    void renderCredits();
    void renderHelp();
    void renderDialog();
    void paint(HDC dc);
    void click(Point logical);
    void typeCharacter(wchar_t character);
    void tickIntro(unsigned milliseconds);
    void advanceIntro(IntroPhase phase);
    void skipTitleIntro();
    void enterNameScreen();
    void beginGameIntro(int index);
    void beginGameCharacter(int index);
    void beginGameName(int index);
    void beginChangeName();
    void tickGameIntro(unsigned milliseconds);
    void finishGameIntro(bool skippedByInput = false);
    void drawTiledBackground(int resourceId);
    void drawMario(bool talking = false);
    bool selectMenuSource(int sourceSelection, bool animate = true,
                          bool pressedVisual = false);
    void showSelectedMenuPose();
    void beginMenuSelectionTransition();
    bool tickMenuSelectionController(unsigned milliseconds = 33);
    [[nodiscard]] bool menuSelectionTransitionActive() const noexcept;
    void resetMenuIdleControllers();
    bool tickMenuIdleControllers();
    void finishMenuIdleCycle();
    void resetMenuBlinkDelay();
    void showMenuIdleQaPose(int mode);
    void startSelectedMenuGame();
    void requestMenuGame(int index);
    bool tryStartPendingMenuGame();
    void hoverMenu(Point logical);
    [[nodiscard]] Point toLogical(Point client) const;
    void updateViewport();
    void startGame(int index);
    void startPreviewedGame(int index);
    void returnToMenu();
    void clickDialog(Point logical);
    void toggleFullscreen();
    void toggleSound();
    void toggleMusic();
    void toggleBackground();
    void toggleAnimatedPieces();
    void toggleForcedJumps();
    void updateFileMenu();
    void updateEditMenu();
    void updateOptionsMenu();
    void copyNameToClipboard(bool clearAfterCopy);
    void pasteNameFromClipboard();
    void toggleHelp(int gameIndex = -1);
    void loadPreferences();
    void savePreferences() const;

    HINSTANCE instance_{};
    HWND window_{};
    HMENU fileMenu_{};
    HMENU editMenu_{};
    HMENU optionsMenu_{};
    AssetStore assets_;
    GraphicsAssets graphics_;
    Audio audio_;
    SourceFonts sourceFonts_;
    Canvas canvas_{kLogicalWidth, kLogicalHeight};
    Movie introHandMovie_;
    Movie talkingHeadMovie_;
    Movie menuRevealMovie_;
    HostAnimation menuSelectionHost_;
    HostAnimation menuBowTieHost_;
    HostAnimation gameCharacterHost_;
    SourceRandom random_;
    std::unique_ptr<Game> game_;
    struct GameIntroMovie {
        Movie movie;
        int x{};
        int y{};
    };
    std::vector<GameIntroMovie> gameIntroMovies_;
    Screen screen_{Screen::Intro};
    Dialog dialog_{Dialog::None};
    Screen helpReturnScreen_{Screen::Menu};
    Screen pictureReturnScreen_{Screen::Menu};
    Screen nameReturnScreen_{Screen::Menu};
    IntroPhase introPhase_{IntroPhase::StartupBlack};
    std::uint32_t introPhaseMilliseconds_{};
    std::uint32_t gameIntroMilliseconds_{};
    std::uint32_t gameIntroDurationMilliseconds_{};
    int menuSourceSelection_{1};
    int menuPointerSourceSelection_{1};
    int menuSelectedLabelSourceSelection_{1};
    int menuPressedSourceSelection_{};
    int menuTransitionWorkingSelection_{1};
    int menuTransitionTargetSelection_{1};
    int menuTransitionDirection_{};
    int menuTransitionDelayTicks_{};
    MenuSelectionPhase menuSelectionPhase_{MenuSelectionPhase::Idle};
    int menuIdleWaitTicks_{};
    int menuIdleElapsedTicks_{};
    int menuIdleMode_{1};
    int menuIdlePreviousMode_{1};
    int menuIdlePhase_{};
    int menuFootToggleCountdown_{};
    int menuFootToggleCount_{};
    int menuBlinkWaitTicks_{};
    int menuBlinkElapsedTicks_{};
    int menuBlinkHoldTicks_{};
    bool menuIdleRunning_{};
    bool menuFootShowNext_{true};
    bool menuBootVisible_{};
    bool menuBowTieVisible_{true};
    bool menuBlinkRunning_{};
    bool menuBlinkVisible_{};
    bool menuIdleQaFreeze_{};
    int menuLaunchGameIndex_{-1};
    int menuLaunchDelayMilliseconds_{};
    bool menuLaunchSoundStarted_{};
    int pendingGameIndex_{-1};
    int characterGameIndex_{-1};
    int nameGameIndex_{-1};
    std::wstring playerName_{L"My friend"};
    std::wstring nameBeforeEdit_{L"My friend"};
    bool playerIsYoshi_{true};
    bool nameConfirmed_{};
    bool characterConfirmed_{};
    bool changingName_{};
    bool backgroundHidden_{};
    bool backgroundWindowRectValid_{};
    bool animatedPieces_{true};
    bool forcedJumps_{true};
    bool preferencesEnabled_{true};
    bool suppressNextCharacter_{};
    bool restoreBackgroundHidden_{};
    Rect viewport_{};
    HBITMAP titleBitmap_{};
    HBITMAP creditsBitmap_{};
    HBITMAP brainstormBitmap_{};
    HBITMAP brainstormFadeBitmap_{};
    HBITMAP steppingStoneBitmap_{};
    HBITMAP steppingStoneFadeBitmap_{};
    HDC paintBufferDc_{};
    HBITMAP paintBufferBitmap_{};
    HGDIOBJ paintBufferPreviousBitmap_{};
    int paintBufferWidth_{};
    int paintBufferHeight_{};
    std::array<HBITMAP, 9> helpBitmaps_{};
    int helpGameIndex_{};
    int helpPage_{};
    FullscreenController fullscreen_;
    int activeGameIndex_{-1};
    std::optional<std::uint32_t> previewRandomSeed_;
    std::uint32_t gameFinishedMilliseconds_{};
    RECT backgroundWindowRect_{};
    std::array<Rect, 5> gameButtons_{{
        {38, 219, 186, 246}, {38, 191, 186, 218}, {38, 135, 186, 162},
        {38, 163, 186, 190}, {38, 247, 186, 274}}};
    const Rect creditsButton_{420, 346, 502, 376};
    const Rect soundButton_{10, 346, 92, 376};
};

}  // namespace mf
