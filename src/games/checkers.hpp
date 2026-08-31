#pragma once

#include "game.hpp"

namespace mf {

class CheckersGame final : public Game {
public:
    explicit CheckersGame(GameContext context);
    void render(Canvas& canvas) override;
    void click(Point point) override;
    bool tick() override;
    void resetForReplay() override;
    void setCharacterChooser(bool enabled) override { characterChooser_ = enabled; }
    [[nodiscard]] std::wstring title() const override { return L"Checkers"; }
    [[nodiscard]] bool finished() const override;
    [[nodiscard]] unsigned postFinishDelayMilliseconds() const noexcept override { return 0; }
    [[nodiscard]] bool sourceStrategyRegressionTest() const;
    [[nodiscard]] static bool sourceIdleRegressionTest();
    [[nodiscard]] static bool sourcePieceGeometryRegressionTest();
    [[nodiscard]] bool sourceFullMatchRegressionTest();
    [[nodiscard]] bool sourceOutcomeRegressionTest(int outcomeVariant);
    [[nodiscard]] bool sourceReplayRegressionTest();
    void setQaOutcomePresentation(int outcomeVariant);
    void setQaIdleVisualPresentation(std::uint32_t sourceTime);

private:
    struct Move { int from; int to; int captured; };
    struct TurnMove {
        std::vector<Move> steps;
        int score{};
    };

    enum class OutcomeKind {
        None,
        HumanEliminatedMario,
        HumanStuckMario,
        MarioWin,
    };

    enum class OutcomePhase {
        None,
        InitialDelay,
        Announcement,
        PlayerBoardWipe,
        PreReplayDelay,
        ReplayPrompt,
        ReplayWait,
        Complete,
    };

    enum class IdlePhase {
        Waiting,
        Visual,
        SingleLine,
        Joke,
    };

    void reset(bool preserveSession = false);
    [[nodiscard]] std::vector<Move> legalMoves(int player, int onlyPiece = -1) const;
    [[nodiscard]] std::vector<Move> pieceMoves(int square, bool capturesOnly) const;
    void apply(const Move& move);
    void finishTurn();
    void computerTurn();
    void updateWinner();
    void beginOutcome(OutcomeKind kind);
    bool tickOutcome();
    void playOutcomeMovie(int resourceId);
    [[nodiscard]] int drawSelector(std::vector<int>& resources, std::size_t& cursor);
    void resetSourceIdle(bool resetSequence);
    void cancelSourceIdle();
    [[nodiscard]] bool tickSourceIdle(bool eligible);
    void scheduleNextSourceIdle();
    [[nodiscard]] static std::vector<TurnMove> sourceTurnMoves(
        const std::array<int, 64>& board, int player, bool forcedCaptures);
    [[nodiscard]] static std::optional<TurnMove> sourceBestTurn(
        const std::array<int, 64>& board, int player, int depth, int rootDepth,
        int chaseSquare, SourceRandom& random);
    [[nodiscard]] static std::array<int, 64> sourceApplyTurn(
        const std::array<int, 64>& board, const TurnMove& turn);
    [[nodiscard]] static int sourceImmediateScore(
        const std::array<int, 64>& board, const TurnMove& turn, int player,
        int chaseSquare);
    [[nodiscard]] int sourceSearchDepth() const;
    [[nodiscard]] int pieceFrame(int piece) const;
    [[nodiscard]] Point squareCenter(int square) const;
    [[nodiscard]] Point piecePosition(int square, int frame) const;
    [[nodiscard]] int hitSquare(Point point) const;
    [[nodiscard]] static int owner(int piece) { return (piece > 0) - (piece < 0); }

    std::array<int, 64> board_{};
    int turn_{1};
    int selected_{-1};
    int continuation_{-1};
    int winner_{};
    int quietPlies_{};
    int jumpsThisTurn_{};
    int computerDelayMilliseconds_{};
    int lastHumanDestination_{-1};
    int marioWinCount_{};
    int outcomeDelayTicks_{};
    int outcomeWipeSquare_{};
    std::vector<Move> computerPlan_;
    std::vector<int> outcomeMoviesPlayed_;
    std::vector<int> humanWinPool_{
        11063, 11064, 11065, 11067, 11068, 11069, 11071, 11072, 11053, 11055};
    std::size_t humanWinPoolCursor_{10};
    std::vector<int> laterMarioWinPool_{11058, 11072, 11057, 11065, 11056};
    std::size_t laterMarioWinPoolCursor_{5};
    std::vector<int> replayPool_{11074, 11073};
    std::size_t replayPoolCursor_{2};
    std::vector<int> idleVisualPool_{9000, 9001, 9002};
    std::size_t idleVisualPoolCursor_{3};
    int idleJokeIndex_{4};
    int idleJokePart_{};
    unsigned idleGapSourceTicks_{};
    unsigned idleElapsedSourceTicks_{};
    unsigned idleTargetSourceTicks_{120};
    IdlePhase idlePhase_{IdlePhase::Waiting};
    struct PieceAnimation {
        bool active{};
        int from{};
        int to{};
        int piece{};
        unsigned elapsedMilliseconds{};
    } pieceAnimation_;
    OutcomeKind outcomeKind_{OutcomeKind::None};
    OutcomePhase outcomePhase_{OutcomePhase::None};
    bool characterChooser_{};
    std::wstring status_;
    HostAnimation host_;
    const Rect newButton_{382, 276, 496, 310};
    const Rect menuButton_{382, 320, 496, 354};
};

}  // namespace mf
