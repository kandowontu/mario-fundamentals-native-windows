#pragma once

#include "game.hpp"

namespace mf {

class DominoesGame final : public Game {
public:
    explicit DominoesGame(GameContext context);
    void render(Canvas& canvas) override;
    void click(Point point) override;
    void mouseDown(Point point) override;
    void mouseMove(Point point) override;
    void mouseUp(Point point) override;
    void mouseCancel() override;
    bool tick() override;
    void resetForReplay() override;
    void setCharacterChooser(bool enabled) override { characterChooser_ = enabled; }
    [[nodiscard]] std::wstring title() const override { return L"Dominoes"; }
    [[nodiscard]] bool finished() const override;
    [[nodiscard]] unsigned postFinishDelayMilliseconds() const noexcept override { return 0; }
    [[nodiscard]] bool sourceStrategyRegressionTest() const;
    [[nodiscard]] bool sourceOpeningRegressionTest() const;
    [[nodiscard]] bool sourceDealPresentationRegressionTest();
    [[nodiscard]] static bool sourceIdleRegressionTest();
    [[nodiscard]] static bool sourceDialogueRegressionTest();
    [[nodiscard]] bool sourceFullMatchRegressionTest();
    [[nodiscard]] bool sourceOutcomeRegressionTest(int expectedWinner, bool blocked);
    [[nodiscard]] bool sourceReplayRegressionTest();
    [[nodiscard]] bool sourceDragRegressionTest();
    [[nodiscard]] bool sourceBoneyardHitboxRegressionTest();
    void setQaDragPresentation();
    void setQaDrawnHandPresentation();
    void setQaOutcomePresentation(int expectedWinner, bool blocked);

private:
    struct Tile { int left; int right; };

    enum class OutcomeKind {
        None,
        Blocked,
        LastDomino,
    };

    enum class OutcomePhase {
        None,
        BlockedOpening,
        BlockedOpeningDelay,
        BlockedScoreDelay,
        BlockedResultDelay,
        HumanInitialDelay,
        HumanFirstDelay,
        HumanSecondDelay,
        MarioAnnouncement,
        MarioAnnouncementDelay,
        ChainResetWait,
        ReplayPrompt,
        ReplayDelay,
        FinalDelay,
        Complete,
    };

    enum class IdlePhase {
        Waiting,
        SingleLine,
        Joke,
    };

    void reset(bool preserveSession = false);
    static void sourceShuffleDeck(std::vector<Tile>& deck, SourceRandom& random);
    [[nodiscard]] bool playable(const Tile& tile) const;
    [[nodiscard]] static std::size_t sourcePreferredTile(const std::vector<Tile>& hand,
                                                         int leftEnd, int rightEnd);
    bool play(std::vector<Tile>& hand, std::size_t index, bool preferLeft,
              bool allowOtherEnd = true);
    void scheduleComputerTurn();
    void scheduleHumanTurnCommentary();
    void beginHumanTurnCommentary();
    void computerStep();
    void resolveBlocked();
    void beginOutcome(OutcomeKind kind);
    bool tickOutcome();
    void playOutcomeMovie(int resourceId);
    [[nodiscard]] bool tickSourceIdle(bool eligible);
    void scheduleNextSourceIdle();
    [[nodiscard]] int chooseWeighted(std::span<const int> resources,
                                     std::span<const int> cumulativePercentages);
    [[nodiscard]] static int pips(const std::vector<Tile>& hand);
    [[nodiscard]] int sourceHumanHandCapacity() const noexcept;
    [[nodiscard]] Rect humanTileRect(std::size_t index) const noexcept;
    void forcePlayableNextHumanDraw();
    void drawTile(Canvas& canvas, const Tile& tile, Rect rect, bool selected = false) const;
    [[nodiscard]] Rect chainTileRect(int index, int visible) const;

    std::vector<Tile> human_;
    std::vector<Tile> computer_;
    std::vector<Tile> boneyard_;
    std::vector<Tile> chain_;
    int selected_{-1};
    int draggedIndex_{-1};
    Point dragPoint_{};
    int passes_{};
    int winner_{};
    int pendingComputerOpening_{-1};
    int requiredHumanOpening_{-1};
    int computerDelayMilliseconds_{};
    int computerDrawsThisTurn_{};
    int lastComputerMoveSpeechSourceIndex_{-99};
    int lastHumanMoveSpeechSourceIndex_{-99};
    int dealDelayMilliseconds_{};
    int dealSoundCount_{};
    int tileCommitSoundDelayMilliseconds_{};
    bool chainReflowSoundPending_{};
    int openingMovie_{};
    int outcomeDelayTicks_{};
    int roundNumber_{1};
    int idleJokeIndex_{};
    int idleJokePart_{};
    unsigned idleGapSourceTicks_{};
    unsigned idleElapsedSourceTicks_{};
    unsigned idleTargetSourceTicks_{120};
    bool computerTurnPending_{};
    bool computerBlockedAnnounced_{};
    bool computerPassCommentaryPlayed_{};
    bool computerStartCommentaryPending_{};
    bool computerMoveCommentaryResolved_{};
    bool computerThinkingUsed_{};
    bool humanHasDrawn_{};
    bool humanTurnCommentaryPending_{};
    unsigned computerThinkingDelaySourceTicks_{};
    bool characterChooser_{};
    bool dealComplete_{};
    OutcomeKind outcomeKind_{OutcomeKind::None};
    OutcomePhase outcomePhase_{OutcomePhase::None};
    IdlePhase idlePhase_{IdlePhase::Waiting};
    std::vector<int> outcomeMoviesPlayed_;
    std::wstring status_;
    HostAnimation host_;
    const Rect drawButton_{476, 286, 512, 357};
    const Rect newButton_{392, 288, 498, 320};
    const Rect menuButton_{392, 328, 498, 360};
};

}  // namespace mf
