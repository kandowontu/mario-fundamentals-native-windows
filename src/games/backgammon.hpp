#pragma once

#include "game.hpp"

namespace mf {

class BackgammonGame final : public Game {
public:
    explicit BackgammonGame(GameContext context);
    void render(Canvas& canvas) override;
    void click(Point point) override;
    bool tick() override;
    void resetForReplay() override { reset(true); }
    void setCharacterChooser(bool enabled) override { characterChooser_ = enabled; }
    [[nodiscard]] std::wstring title() const override { return L"Backgammon"; }
    [[nodiscard]] bool finished() const override;
    [[nodiscard]] unsigned postFinishDelayMilliseconds() const noexcept override { return 0; }
    [[nodiscard]] bool sourceStrategyRegressionTest() const;
    [[nodiscard]] static bool sourceIdleRegressionTest();
    [[nodiscard]] bool sourceDialogueRegressionTest() const;
    [[nodiscard]] bool sourceStartupRegressionTest();
    [[nodiscard]] bool sourceSetupRevealRegressionTest();
    [[nodiscard]] static bool sourceCheckerGeometryRegressionTest();
    [[nodiscard]] bool sourceOutcomeRegressionTest(bool humanWins);
    [[nodiscard]] bool sourceFullMatchRegressionTest();
    [[nodiscard]] bool sourceReplayRegressionTest();
    void setQaSetupRevealPresentation(int revealedCheckers);

private:
    struct State {
        std::array<int, 24> points{};
        int humanBar{};
        int computerBar{};
        int humanOff{};
        int computerOff{};
    };
    struct Move { int from; int to; int die; };
    enum class OutcomePhase {
        None,
        Announcement,
        VictoryMovies,
        PreReplayDelay,
        ReplayPrompt,
        FinalDelay,
        Complete,
    };

    enum class IdlePhase {
        Waiting,
        Visual,
        Line,
    };

    enum class StartupPhase {
        PreGreeting,
        Greeting,
        RollPrompt,
        PostPromptDelay,
        SetupInitialize,
        Setup,
        Complete,
    };

    void reset(bool preserveSession = false);
    void openingRoll();
    void rollDice(bool human);
    void resolveRoll();
    [[nodiscard]] std::vector<Move> legalSingle(const State& state, int player, int die) const;
    [[nodiscard]] std::vector<std::vector<Move>> sequences(const State& state, int player,
                                                           std::vector<int> dice) const;
    [[nodiscard]] std::vector<Move> legalFirstMoves() const;
    [[nodiscard]] Move sourcePreferredMove(const State& state, const std::vector<int>& dice,
                                           const std::vector<Move>& legalFirst) const;
    [[nodiscard]] std::vector<Move> sourcePreferredTurn(
        const State& state, const std::vector<int>& dice) const;
    static void apply(State& state, int player, const Move& move);
    [[nodiscard]] static bool allHome(const State& state, int player);
    void finishHumanTurn();
    void computerTurn(bool roll = true);
    void computerStep();
    void updateWinner();
    bool tickOutcome();
    bool tickStartup();
    bool tickSetupReveal();
    [[nodiscard]] bool tickSourceIdle(bool eligible);
    void beginPieceAnimation(int player, const Move& move);
    void queuePostMoveMovie(int resourceId);
    [[nodiscard]] bool postMoveAudioPending() const noexcept;
    [[nodiscard]] Point checkerPosition(int point, int player, int stackIndex,
                                        const Sprite& sprite) const;
    [[nodiscard]] Rect pointRect(int point) const;
    [[nodiscard]] int hitPoint(Point point) const;

    State state_{};
    std::vector<int> dice_;
    std::vector<Move> computerMoves_;
    std::array<int, 3> openingGreetingPool_{11603, 11604, 11600};
    std::size_t openingGreetingPoolCursor_{};
    std::array<int, 3> computerNoMovePool_{11613, 11614, 11617};
    std::size_t computerNoMovePoolCursor_{};
    std::array<int, 3> computerThinkingPool_{11632, 11631, 11630};
    std::size_t computerThinkingPoolCursor_{};
    int lastIdleLine_{-1};
    int lastIdleVisual_{-1};
    unsigned idleElapsedSourceTicks_{};
    IdlePhase idlePhase_{IdlePhase::Waiting};
    std::size_t computerMoveIndex_{};
    int selected_{-99};
    int winner_{};
    int pendingRoll_{};
    int pendingFirst_{};
    int pendingSecond_{};
    int diceOwner_{};
    int computerDelayMilliseconds_{};
    std::array<int, 24> setupVisiblePoints_{};
    std::size_t setupStackSoundIndex_{};
    int setupStackRemaining_{};
    int startupDelayTicks_{};
    int outcomeDelayTicks_{};
    bool rolled_{};
    bool opening_{true};
    bool computerRollPending_{};
    bool computerMovesPending_{};
    bool characterChooser_{};
    bool setupStackRevealing_{};
    bool qaSetupRevealPresentation_{};
    StartupPhase startupPhase_{StartupPhase::PreGreeting};
    OutcomePhase outcomePhase_{OutcomePhase::None};
    struct PieceAnimation {
        bool active{};
        int player{};
        int destination{};
        int frame{};
        Point from{};
        Point to{};
        unsigned elapsedMilliseconds{};
    } pieceAnimation_;
    bool postMoveGate_{};
    bool postMoveHitSound_{};
    std::vector<int> postMoveMovies_;
    std::wstring status_;
    HostAnimation host_;
    HostAnimation diceRoll_;
    HostAnimation secondDiceRoll_;
    HostAnimation victoryLeft_;
    HostAnimation victoryRight_;
    const Rect rollButton_{343, 70, 505, 160};
    const Rect newButton_{407, 286, 501, 320};
    const Rect menuButton_{407, 330, 501, 364};
    const Rect barRect_{244, 182, 268, 358};
    const Rect offRect_{0, 182, 32, 358};
};

}  // namespace mf
