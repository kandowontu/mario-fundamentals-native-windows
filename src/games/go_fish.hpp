#pragma once

#include "game.hpp"

namespace mf {

class GoFishGame final : public Game {
public:
    explicit GoFishGame(GameContext context);
    void render(Canvas& canvas) override;
    void click(Point point) override;
    bool tick() override;
    void resetForReplay() override { reset(); }
    [[nodiscard]] std::wstring title() const override { return L"Go Fish"; }
    [[nodiscard]] bool finished() const override;
    [[nodiscard]] unsigned postFinishDelayMilliseconds() const noexcept override { return 0; }
    [[nodiscard]] bool sourceStrategyRegressionTest() const;
    [[nodiscard]] bool sourceDialogueRegressionTest() const;
    [[nodiscard]] bool sourceHandSlotRegressionTest() const;
    [[nodiscard]] bool sourceOpeningDealRegressionTest();
    [[nodiscard]] bool sourceEmptyHandRefillRegressionTest();
    [[nodiscard]] bool sourceFullMatchRegressionTest();
    [[nodiscard]] bool sourceOutcomeRegressionTest(int expectedWinner);
    [[nodiscard]] bool sourceReplayRegressionTest();
    void setQaVictoryPresentation(int letterCount);
    void setQaHandSlotsPresentation(bool afterTransfer);
    void setQaQuestionPresentation();

private:
    enum class IdlePhase {
        Waiting,
        Visual,
        Line,
    };

    enum class OutcomePhase {
        None,
        Announcement,
        DealLetters,
        FlipLetters,
        PreReplayDelay,
        ReplayPrompt,
        NonPlayerDelay,
        FinalDelay,
        Complete,
    };

    enum class OpeningPhase {
        Greeting,
        DealGap,
        Deal,
        PostDealGap,
        WaitForMotion,
        Consolidate,
        Stabilize,
        FirstTurnSpeech,
        FirstTurnGap,
        Complete,
    };

    struct HumanHandSlot {
        int rank{-1};
        int count{};
    };
    using HumanHandSlots = std::array<HumanHandSlot, 13>;

    struct OpeningCardMotion {
        bool active{};
        bool destinationWasOccupied{};
        int sourceSlot{-1};
        int destinationSlot{-1};
        int rank{-1};
        int count{};
        int elapsedPasses{};
        int totalPasses{};
    };

    void reset();
    int removeBooks(std::vector<int>& hand, int& books, bool updateHumanDisplay = false);
    void initializeHumanHandSlots(std::span<const int> cards);
    void addHumanCardsToDisplay(std::span<const int> cards);
    void clearHumanRankDisplay(int rank);
    static void addHumanRankToSlots(HumanHandSlots& slots, int rank, int count = 1);
    static void clearHumanRankFromSlots(HumanHandSlots& slots, int rank);
    static bool mergeOneOpeningDuplicate(
        HumanHandSlots& slots, int& sourceSlot, int& destinationSlot,
        HumanHandSlot& movingCard);
    static bool settleOneOpeningHole(
        HumanHandSlots& slots, int& sourceSlot, int& destinationSlot,
        HumanHandSlot& movingCard);
    static void consolidateOpeningSlots(HumanHandSlots& slots);
    void beginOpeningCardMotion(int sourceSlot, int destinationSlot,
                                HumanHandSlot movingCard, bool destinationWasOccupied);
    [[nodiscard]] bool tickOpeningCardMotion();
    [[nodiscard]] bool tickOpeningController();
    [[nodiscard]] bool openingActive() const noexcept {
        return openingPhase_ != OpeningPhase::Complete;
    }
    [[nodiscard]] int humanRankAtPoint(const HumanHandSlots& slots, Point point) const;
    void ask(bool human, int rank);
    [[nodiscard]] bool refillHumanHand(bool marioContinues);
    void computerTurn();
    [[nodiscard]] int chooseComputerRank();
    void rememberHumanQuestion(int rank);
    void beginConversation(std::span<const int> movies);
    void appendConversation(std::span<const int> movies);
    void beginAfterTrackedSpeech(int sound, std::span<const int> movies);
    void beginAfterTrackedThenDirectSpeech(int trackedSound, int directSound,
                                           std::span<const int> movies);
    void beginAfterDirectSpeech(int sound, std::span<const int> movies);
    void resetSourceIdle();
    void cancelSourceIdle();
    [[nodiscard]] bool tickSourceIdle(bool eligible);
    [[nodiscard]] int drawPool(std::vector<int>& movies, std::size_t& cursor);
    void appendHumanTurnAnnouncement();
    [[nodiscard]] static int sourceSelectComputerRank(
        std::span<const int> handRanks,
        std::array<int, 200>& humanQuestionMemory,
        std::array<int, 30>& computerQuestionHistory,
        int& lastComputerRank,
        SourceRandom& random);
    [[nodiscard]] std::vector<int> visibleHumanRanks() const;
    void checkEnd();
    bool tickOutcome();
    [[nodiscard]] static int rank(int card) { return card / 4; }
    [[nodiscard]] static std::wstring rankName(int rank);
    [[nodiscard]] static int questionMovie(int rank, int variant);

    std::vector<int> human_;
    std::vector<int> computer_;
    std::vector<int> deck_;
    HumanHandSlots humanHandSlots_{};
    HumanHandSlots openingPresentationSlots_{};
    int humanBooks_{};
    int computerBooks_{};
    int winner_{};
    int outcomeDelayTicks_{};
    int victoryLetterCount_{};
    bool victoryMusicStarted_{};
    bool humanTurn_{true};
    int pendingComputerRank_{-1};
    bool computerTurnWaiting_{};
    OpeningPhase openingPhase_{OpeningPhase::Complete};
    OpeningCardMotion openingCardMotion_{};
    int openingDelayControllerPasses_{};
    int openingDealCount_{};
    bool directSpeechPending_{};
    int directSpeechAfterTracked_{-1};
    std::vector<int> moviesAfterDirectSpeech_;
    std::array<int, 200> humanQuestionMemory_{};
    std::array<int, 30> computerQuestionHistory_{};
    int lastComputerRank_{99};
    bool marioHasFished_{};
    bool moduleStateInitialized_{};
    unsigned idleAlternationCounter_{};
    unsigned idleElapsedSourceTicks_{};
    IdlePhase idlePhase_{IdlePhase::Waiting};
    OutcomePhase outcomePhase_{OutcomePhase::None};

    std::vector<int> openingPool_{11600, 11603, 11003, 11526};
    std::size_t openingPoolCursor_{4};
    std::vector<int> prefixPool_{11575, 11576, 11545, 11557};
    std::size_t prefixPoolCursor_{4};
    std::vector<int> thinkingPool_{11543, 11544};
    std::size_t thinkingPoolCursor_{2};
    std::vector<int> humanSuccessPool_{11539, 11540};
    std::size_t humanSuccessPoolCursor_{2};
    std::vector<int> marioSuccessPool_{11558, 11550, 11577};
    std::size_t marioSuccessPoolCursor_{3};
    std::vector<int> marioFishingPool_{11551, 11555, 11554};
    std::size_t marioFishingPoolCursor_{3};
    std::vector<int> humanTurnPool_{11530, 11422};
    std::size_t humanTurnPoolCursor_{2};
    std::vector<int> praisePool_{11610, 11607, 11608};
    std::size_t praisePoolCursor_{3};
    std::vector<int> frustrationPool_{11629, 11635};
    std::size_t frustrationPoolCursor_{2};
    std::vector<int> idlePool_{11561, 11562};
    std::size_t idlePoolCursor_{2};
    std::vector<int> idleVisualPool_{5094, 5091, 5090, 999, 999};
    std::size_t idleVisualPoolCursor_{5};
    std::vector<int> marioWinPool_{11569, 11568, 11565, 11566};
    std::size_t marioWinPoolCursor_{4};
    std::vector<int> humanWinPool_{11570, 11567};
    std::size_t humanWinPoolCursor_{2};
    std::wstring status_;
    HostAnimation host_;
    std::vector<std::unique_ptr<HostAnimation>> victoryCardFlips_;
    const Rect newButton_{390, 294, 502, 326};
    const Rect menuButton_{390, 338, 502, 370};
};

}  // namespace mf
