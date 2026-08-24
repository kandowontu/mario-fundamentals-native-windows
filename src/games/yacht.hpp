#pragma once

#include "game.hpp"

namespace mf {

[[nodiscard]] int yachtCategoryScore(int category, const std::array<int, 5>& dice);
[[nodiscard]] std::array<bool, 5> yachtComputerRerolls(
    const std::array<int, 5>& dice, const std::array<int, 12>& scores);
[[nodiscard]] std::array<bool, 5> yachtComputerHeldDice(
    const std::array<int, 5>& dice, const std::array<int, 12>& scores);
[[nodiscard]] int yachtComputerCategory(
    const std::array<int, 5>& dice, const std::array<int, 12>& scores);
[[nodiscard]] int yachtScoreCategoryAt(Point point);
[[nodiscard]] Rect yachtDieRect(int index, bool held);
[[nodiscard]] int yachtRemainingRollMarkers(int completedRolls, bool rollPending);
[[nodiscard]] bool yachtComputerRerollSpeechEligible(
    int rollsRemaining, const std::array<bool, 5>& held);
[[nodiscard]] bool yachtHasWhiteDieToReroll(const std::array<bool, 5>& held);

class YachtGame final : public Game {
public:
    explicit YachtGame(GameContext context);
    void render(Canvas& canvas) override;
    void click(Point point) override;
    bool tick() override;
    void resetForReplay() override { reset(); }
    [[nodiscard]] std::wstring title() const override { return L"Yacht"; }
    [[nodiscard]] bool finished() const override;
    [[nodiscard]] unsigned postFinishDelayMilliseconds() const noexcept override { return 0; }
    [[nodiscard]] bool sourceOutcomeRegressionTest(int expectedWinner);
    [[nodiscard]] bool sourceComputerSelectionRegressionTest();
    [[nodiscard]] bool sourceTurnOrderRegressionTest();
    [[nodiscard]] bool sourceCupPresentationRegressionTest();
    [[nodiscard]] static bool sourceDialogueRegressionTest();
    void setQaVictoryPresentation();
    void setQaScorecardPresentation();
    void setQaDiceSelectionPresentation();

private:
    enum class IntroPhase {
        GoodLuck,
        GoodLuckGap,
        IGoFirst,
        IGoFirstGap,
        OptionalTurnGap,
        OptionalTurnSpeech,
        Complete,
    };

    enum class IdlePhase {
        Waiting,
        SingleLine,
        Joke,
    };

    enum class OutcomePhase {
        None,
        Announcement,
        VictoryDice,
        Celebration,
        PreReplayDelay,
        ReplayPrompt,
        PostReplayDelay,
        ScorecardClear,
        FinishDelay,
        Complete,
    };

    void reset();
    void roll();
    void resolveHumanRoll();
    void resolveComputerRoll();
    void finishComputerTurn(int preferredCategory = -1);
    void beginComputerRoll();
    void scoreHuman(int category);
    void advanceRound();
    void beginOutcome();
    bool tickOutcome();
    bool tickIntro();
    bool tickSourceIdle(bool eligible);
    void cancelSourceIdle();
    void scheduleNextSourceIdle();
    void beginComputerSelection();
    void continueComputerReroll();
    [[nodiscard]] int drawDialoguePool(std::vector<int>& movies, std::size_t& cursor);
    [[nodiscard]] int total(const std::array<int, 12>& scores) const;
    [[nodiscard]] bool shouldDrawStationaryCup() const noexcept;
    [[nodiscard]] static std::wstring_view categoryName(int category);
    [[nodiscard]] static int categoryMovie(int category);

    std::array<int, 5> dice_{};
    std::array<bool, 5> held_{};
    std::array<bool, 5> computerTargetHeld_{};
    std::array<int, 12> humanScores_{};
    std::array<int, 12> computerScores_{};
    int rolls_{};
    int round_{};
    int winner_{};
    int outcomeDelayTicks_{};
    int outcomeClearTick_{};
    int outcomeVisibleDiceCount_{};
    int openingDelayMilliseconds_{};
    int introGapSourceTicks_{};
    bool showComputerDice_{};
    bool pendingComputerAfterSpeech_{};
    bool pendingComputerRerollSpeech_{};
    std::wstring status_;
    HostAnimation host_;
    HostAnimation rollAnimation_;
    HostAnimation gestureAnimation_;
    // 0 = none, 1 = resolve a player roll, -1 = resolve Mario's roll.
    int pendingRollPlayer_{};
    int settlingDieIndex_{};
    int computerAttempt_{};
    int computerRerollStage_{};
    int computerSelectionCursor_{};
    unsigned computerSelectionDelaySourceTicks_{};
    int computerReadyCategory_{-1};
    int pendingComputerScoreCategory_{-1};
    unsigned computerScoreDelaySourceTicks_{};
    int computerAnnouncementCount_{};
    IntroPhase introPhase_{IntroPhase::GoodLuck};
    IdlePhase idlePhase_{IdlePhase::Waiting};
    unsigned idleElapsedSourceTicks_{};
    unsigned idleTargetSourceTicks_{80};
    unsigned idleGapSourceTicks_{};
    int idleJokeIndex_{};
    int idleJokePart_{};
    std::vector<int> thinkingPool_{11430, 11429, 11429, 11431};
    std::size_t thinkingPoolCursor_{4};
    std::vector<int> positiveReactionPool_{11448, 11446, 11447};
    std::size_t positiveReactionPoolCursor_{3};
    std::vector<int> worriedReactionPool_{11421, 11424, 11425};
    std::size_t worriedReactionPoolCursor_{3};
    std::vector<int> friendlyReactionPool_{11451, 11452};
    std::size_t friendlyReactionPoolCursor_{2};
    OutcomePhase outcomePhase_{OutcomePhase::None};
    HostAnimation outcomeAnimation_;
    const Rect rollButton_{217, 166, 295, 270};
    const Rect newButton_{38, 300, 174, 334};
    const Rect menuButton_{38, 342, 174, 376};
};

}  // namespace mf
