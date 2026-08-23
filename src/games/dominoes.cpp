#include "games/dominoes.hpp"

#include "audio_catalog.hpp"

namespace mf {

namespace {

constexpr std::array<std::array<int, 5>, 4> kDominoIdleJokes{{
    {{10094, 10095, 10096, 10097, 10098}},
    {{10092, 10093, 10099, 10100, 10101}},
    {{10092, 10093, 10114, 10115, 10116}},
    {{10092, 10093, 10117, 10118, 10119}},
}};

enum class DominoIdleChoiceKind {
    SingleLine,
    Joke,
};

struct DominoIdleChoice {
    DominoIdleChoiceKind kind{};
    int value{};
};

enum class ComputerMoveSpeechKind {
    Complete,
    Retry,
    Thinking,
};

struct ComputerMoveSpeechChoice {
    ComputerMoveSpeechKind kind{};
    int sourceIndex{-1};
    unsigned delaySourceTicks{};
};

constexpr int dominoMovieForSourceIndex(int sourceIndex) {
    switch (sourceIndex) {
    case 8: return 10008;
    case 9: return 10009;
    case 10: return 10011;
    case 11: return 10012;
    case 12: return 10013;
    case 13: return 10014;
    case 14: return 10015;
    case 15: return 10016;
    case 17: return 10018;
    case 20: return 10021;
    case 21: return 10022;
    case 22: return 10023;
    case 23: return 10024;
    case 24: return 10025;
    case 25: return 10026;
    case 26: return 10037;
    case 28: return 10039;
    case 31: return 10042;
    case 32: return 10043;
    case 33: return 10044;
    case 34: return 10045;
    case 35: return 10046;
    case 52: return 10063;
    case 55: return 10067;
    case 58: return 10071;
    case 66: return 10082;
    case 67: return 10083;
    case 69: return 10085;
    default: return 0;
    }
}

std::wstring_view dominoStatusForSourceIndex(int sourceIndex) {
    switch (sourceIndex) {
    case 8: return L"Okey-dokey, your move!";
    case 9: return L"You're next!";
    case 10: return L"It's my turn now.";
    case 11: return L"It's your turn.";
    case 12: return L"That's a good move!";
    case 13: return L"That's a great move!";
    case 14: return L"Oh, brother!";
    case 15: return L"Mamma-mia...";
    case 17: return L"Uh-oh!";
    case 20: return L"Eureka!";
    case 21: return L"How about this move!";
    case 22: return L"Hmmmm...";
    case 23: return L"Let's see...";
    case 24: return L"I got it!";
    case 25: return L"You got Mario confused!";
    case 26: return L"You got Mario thinking now!";
    case 28: return L"Let me think...";
    case 31: return L"Maybe I should draw from the boneyard.";
    case 32: return L"I'm stuck!";
    case 33: return L"I pass.";
    case 34: return L"Mario's got to pass.";
    case 35: return L"I can't use any of my dominoes!";
    case 52: return L"You play so well!";
    case 55: return L"Oh, you're so good!";
    case 58: return L"You're such a good player!";
    case 66: return L"Here's your bone.";
    case 67: return L"Oh boy.";
    case 69: return L"Mario has to draw a bone.";
    default: return L"";
    }
}

int chooseComputerPassSpeech(SourceRandom& random) {
    // CODE 14 $1810: 50/20/10/20 across source indices 34/33/32/35.
    const int value = random.below(100);
    if (value < 50) return 34;
    if (value < 70) return 33;
    if (value < 80) return 32;
    return 35;
}

int chooseComputerDrawSpeech(SourceRandom& random) {
    // CODE 14 $1CFC: a below-11 split used before Mario's first draw.
    const int value = random.below(11);
    if (value < 4) return 69;
    if (value < 7) return 31;
    return 14;
}

int chooseHumanTurnSpeech(SourceRandom& random, bool hasDrawn) {
    // CODE 14 $2112 consumes below(500) even when the result is discarded by
    // the post-draw dynamic-text branch.  Only 9% of a first turn is voiced.
    const int value = random.below(500);
    if (hasDrawn) return -1;
    if (value < 15) return 8;
    if (value < 30) return 9;
    if (value < 45) return 11;
    return -1;
}

int chooseHumanDrawSpeech(SourceRandom& random, bool hasDrawn) {
    // CODE 14 $23DA: later draws use dynamic source slot 1 without RNG.
    if (hasDrawn) return -1;
    return random.below(100) > 30 ? 66 : -1;
}

int chooseHumanMoveSpeech(SourceRandom& random, int remainingTiles,
                          int& lastSourceIndex) {
    // CODE 14 $2D80 always draws below 11 for a non-winning move, then
    // suppresses an immediate repeat of the selected source index.
    const int value = random.below(11);
    int sourceIndex = -1;
    if (remainingTiles < 3 && value < 5) {
        if (value < 2) sourceIndex = 17;
        else if (value < 4) sourceIndex = 12;
        else sourceIndex = 13;
    } else {
        if (value < 2) sourceIndex = 58;
        else if (value < 3) sourceIndex = 55;
        else if (value < 4) sourceIndex = 52;
        else if (value < 5) sourceIndex = 12;
    }
    if (sourceIndex < 0 || sourceIndex == lastSourceIndex) return -1;
    lastSourceIndex = sourceIndex;
    return sourceIndex;
}

ComputerMoveSpeechChoice chooseComputerMoveSpeech(
    SourceRandom& random, int computerTiles, int humanTiles, int drawsThisTurn,
    bool& thinkingUsed, int& lastSourceIndex) {
    // CODE 14 $1CAC.  The one-time Hmmmm branch consumes a second below(5)
    // and postpones the actual selection by 3..7 source controller ticks.
    const int value = random.below(11);
    if (!thinkingUsed && computerTiles > 3 && value < 6) {
        thinkingUsed = true;
        return {ComputerMoveSpeechKind::Thinking, 22,
                static_cast<unsigned>(random.below(5) + 3)};
    }

    int sourceIndex = -1;
    if (computerTiles > 6 && humanTiles < 3) {
        if (value == 1) sourceIndex = 25;
        else if (value == 2) sourceIndex = 26;
        else if (value == 3) sourceIndex = 15;
        else if (value == 4) sourceIndex = 28;
    } else {
        if (value < 2) sourceIndex = 21;
        else if (value < 4) sourceIndex = 20;
        else if (value < 6) sourceIndex = 23;
        else if (value < 7) sourceIndex = 24;
        else if (value < 8) sourceIndex = 67;
    }

    if ((drawsThisTurn > 0 && sourceIndex < 0) ||
        (sourceIndex >= 0 && sourceIndex == lastSourceIndex)) {
        return {ComputerMoveSpeechKind::Retry};
    }
    if (sourceIndex >= 0) lastSourceIndex = sourceIndex;
    return {ComputerMoveSpeechKind::Complete, sourceIndex};
}

DominoIdleChoice chooseDominoIdle(SourceRandom& random, int& jokeIndex) {
    const int choice = random.below(100);
    if (choice < 30) return {DominoIdleChoiceKind::SingleLine, 10090};
    if (choice < 50) return {DominoIdleChoiceKind::SingleLine, 10091};
    ++jokeIndex;
    if (jokeIndex < 0 || jokeIndex >= static_cast<int>(kDominoIdleJokes.size())) {
        jokeIndex = 0;
    }
    return {DominoIdleChoiceKind::Joke, jokeIndex};
}

}  // namespace

DominoesGame::DominoesGame(GameContext context)
    : Game(context), host_(context.assets, context.graphics, context.audio) { reset(); }

void DominoesGame::sourceShuffleDeck(std::vector<Tile>& deck, SourceRandom& random) {
    if (deck.size() != 28) {
        throw std::runtime_error("Dominoes source deck must contain 28 tiles");
    }

    // CODE 14 $852 makes three complete passes over the array.  Each
    // position is swapped with an independently selected position in 0..27;
    // this is deliberately not the shared CODE 13 Fisher-Yates routine.
    for (int pass = 0; pass < 3; ++pass) {
        for (std::size_t index = 0; index < deck.size(); ++index) {
            const std::size_t selected = random.below(28);
            std::swap(deck[index], deck[selected]);
        }
    }

    // The deal consumes positions 27 down through 14.  The original checks
    // that half for a doublet and, only when none exists, moves the first
    // doublet in positions 0..13 to a random position 14..26.
    const auto isDouble = [](const Tile& tile) { return tile.left == tile.right; };
    if (std::none_of(deck.begin() + 14, deck.end(), isDouble)) {
        const auto firstDouble = std::find_if(deck.begin(), deck.begin() + 14, isDouble);
        if (firstDouble == deck.begin() + 14) {
            throw std::runtime_error("Dominoes source deck lost all doublets");
        }
        const std::size_t selected = static_cast<std::size_t>(random.below(13)) + 14;
        std::iter_swap(firstDouble, deck.begin() + static_cast<std::ptrdiff_t>(selected));
    }
}

void DominoesGame::reset(bool preserveSession) {
    const int nextRoundNumber = preserveSession ? roundNumber_ + 1 : 1;
    idlePhase_ = IdlePhase::Waiting;
    idleElapsedSourceTicks_ = 0;
    idleTargetSourceTicks_ = 120;
    idleGapSourceTicks_ = 0;
    idleJokeIndex_ = 0;
    idleJokePart_ = 0;
    human_.clear(); computer_.clear(); boneyard_.clear(); chain_.clear();

    // CODE 14 $1E10 performs twelve random swaps on a four-word table while
    // initializing its idle-conversation state.  No game-side read of that
    // table exists, but the calls advance QuickDraw's shared randSeed before
    // the domino shuffle, so reproduce them here.
    std::array<int, 4> idleSelector{0, 1, 2, 3};
    for (int pass = 0; pass < 3; ++pass) {
        for (std::size_t index = 0; index < idleSelector.size(); ++index) {
            const std::size_t selected = context_.random.below(4);
            std::swap(idleSelector[index], idleSelector[selected]);
        }
    }

    for (int left = 0; left <= 6; ++left) for (int right = left; right <= 6; ++right)
        boneyard_.push_back({left, right});
    sourceShuffleDeck(boneyard_, context_.random);

    // CODE 14 $DCA chooses the line spoken while the two hands are dealt.
    const int dealMovie = context_.random.below(100) < 51 ? 10015 : 10065;
    for (int index = 0; index < 7; ++index) {
        human_.push_back(boneyard_.back()); boneyard_.pop_back();
        computer_.push_back(boneyard_.back()); boneyard_.pop_back();
    }
    auto bestDouble = [](const std::vector<Tile>& hand) {
        int value = -1; int index = -1;
        for (std::size_t i = 0; i < hand.size(); ++i) {
            if (hand[i].left == hand[i].right && hand[i].left > value) {
                value = hand[i].left;
                index = static_cast<int>(i);
            }
        }
        return std::pair{value, index};
    };
    const auto humanBest = bestDouble(human_);
    const auto computerBest = bestDouble(computer_);
    pendingComputerOpening_ = requiredHumanOpening_ = -1;
    const int openingChoice = context_.random.below(100);
    if (computerBest.first > humanBest.first) {
        pendingComputerOpening_ = computerBest.second;
        openingMovie_ = openingChoice < 75 ? 10055 : 10016;
    } else {
        requiredHumanOpening_ = humanBest.second;
        openingMovie_ = openingChoice < 50 ? 10017 : 10019;
    }
    selected_ = draggedIndex_ = -1;
    dragPoint_ = {};
    passes_ = winner_ = computerDelayMilliseconds_ = 0;
    computerDrawsThisTurn_ = 0;
    lastComputerMoveSpeechSourceIndex_ = -99;
    lastHumanMoveSpeechSourceIndex_ = -99;
    dealDelayMilliseconds_ = 3000;
    dealSoundDelayMilliseconds_ = dealSoundCount_ = 0;
    tileCommitSoundDelayMilliseconds_ = 0;
    chainReflowSoundPending_ = false;
    outcomeDelayTicks_ = 0;
    roundNumber_ = nextRoundNumber;
    dealComplete_ = false;
    computerTurnPending_ = computerBlockedAnnounced_ = false;
    computerPassCommentaryPlayed_ = false;
    computerStartCommentaryPending_ = false;
    computerMoveCommentaryResolved_ = false;
    computerThinkingUsed_ = false;
    computerThinkingDelaySourceTicks_ = 0;
    humanHasDrawn_ = false;
    humanTurnCommentaryPending_ = false;
    outcomeKind_ = OutcomeKind::None;
    outcomePhase_ = OutcomePhase::None;
    outcomeMoviesPlayed_.clear();
    status_ = L"Let's play, eh?";
    host_.play(dealMovie, 14, 5);
}

void DominoesGame::resetForReplay() {
    reset(true);
}

bool DominoesGame::tick() {
    bool changed = host_.tick();
    if (tileCommitSoundDelayMilliseconds_ > 0) {
        tileCommitSoundDelayMilliseconds_ =
            std::max(0, tileCommitSoundDelayMilliseconds_ - 33);
        if (tileCommitSoundDelayMilliseconds_ == 0) {
            // $1BDA/$2ACA/$2B78 emit 5017 after the tile transit finishes.
            context_.audio.playEffect(5017);
        }
        changed = true;
    }
    if (chainReflowSoundPending_ && tileCommitSoundDelayMilliseconds_ == 0 && !host_.active()) {
        // $5298/$52F0 waits for the host, recenters the chain, and emits 5023.
        context_.audio.playEffect(5023);
        chainReflowSoundPending_ = false;
        changed = true;
    }
    if (dealDelayMilliseconds_ > 0) {
        if (dealSoundCount_ < 7) {
            if (dealSoundDelayMilliseconds_ <= 0) {
                // $EE4 fires snd 5044 once for each dealt pair of bones.
                context_.audio.playEffect(5044);
                ++dealSoundCount_;
                dealSoundDelayMilliseconds_ = 250;
            } else {
                dealSoundDelayMilliseconds_ = std::max(0, dealSoundDelayMilliseconds_ - 33);
            }
        }
        dealDelayMilliseconds_ = std::max(0, dealDelayMilliseconds_ - 33);
        if (dealDelayMilliseconds_ == 0) {
            dealComplete_ = true;
            if (pendingComputerOpening_ >= 0) {
                status_ = L"I place the first doublet.";
            } else {
                status_ = L"You go first.";
            }
            host_.play(openingMovie_, 14, 5);
        }
        changed = true;
    }
    if (dealComplete_ && pendingComputerOpening_ >= 0 && !host_.active()) {
        const std::size_t index = static_cast<std::size_t>(pendingComputerOpening_);
        chain_.push_back(computer_[index]);
        computer_.erase(computer_.begin() + static_cast<std::ptrdiff_t>(index));
        context_.audio.playEffect(5043);
        context_.audio.playEffect(5042);
        tileCommitSoundDelayMilliseconds_ = 396;
        pendingComputerOpening_ = -1;
        scheduleHumanTurnCommentary();
        changed = true;
    }
    if (humanTurnCommentaryPending_ && !host_.active()) {
        beginHumanTurnCommentary();
        changed = true;
    }
    if (computerTurnPending_ && pendingComputerOpening_ < 0 && !host_.active()) {
        if (computerStartCommentaryPending_) {
            // CODE 14 $169E consumes below(100): 70% uses dynamic source
            // slot zero and 30% voices index 10 (MuV 10011).
            computerStartCommentaryPending_ = false;
            status_ = L"It's my turn.";
            if (context_.random.below(100) >= 70) host_.play(10011, 14, 5);
            changed = true;
        } else if (computerDelayMilliseconds_ > 0) {
            const int before = computerDelayMilliseconds_;
            computerDelayMilliseconds_ = std::max(0, computerDelayMilliseconds_ - 33);
            changed |= before != computerDelayMilliseconds_;
        } else {
            computerStep();
            changed = true;
        }
    }
    changed |= tickOutcome();
    const bool waitingForPlayer = !winner_ && !characterChooser_ && dealComplete_ &&
        pendingComputerOpening_ < 0 && !computerTurnPending_;
    changed |= tickSourceIdle(waitingForPlayer);
    return changed;
}

void DominoesGame::scheduleNextSourceIdle() {
    idleTargetSourceTicks_ = context_.random.below(250) + 60U;
    idleElapsedSourceTicks_ = 0;
    idlePhase_ = IdlePhase::Waiting;
}

bool DominoesGame::tickSourceIdle(bool eligible) {
    constexpr unsigned sourceTicksPerNativeTick = 2;

    switch (idlePhase_) {
    case IdlePhase::Waiting: {
        if (!eligible) {
            idleElapsedSourceTicks_ = 0;
            return false;
        }
        idleElapsedSourceTicks_ += sourceTicksPerNativeTick;
        if (idleElapsedSourceTicks_ < idleTargetSourceTicks_ || host_.active()) return false;
        idleElapsedSourceTicks_ = 0;

        const DominoIdleChoice choice = chooseDominoIdle(context_.random, idleJokeIndex_);
        if (choice.kind == DominoIdleChoiceKind::SingleLine) {
            status_ = choice.value == 10090 ? L"Hey, what's taking so long?"
                                            : L"You still want to play?";
            host_.play(choice.value, 14, 5);
            idlePhase_ = IdlePhase::SingleLine;
        } else {
            status_ = L"Knock knock...";
            idleJokePart_ = 0;
            idleGapSourceTicks_ = 5;
            host_.play(kDominoIdleJokes[static_cast<std::size_t>(choice.value)][0], 14, 5);
            idlePhase_ = IdlePhase::Joke;
        }
        return true;
    }
    case IdlePhase::SingleLine:
        if (host_.active()) return false;
        scheduleNextSourceIdle();
        return true;
    case IdlePhase::Joke:
        if (host_.active()) return false;
        if (idleJokePart_ >= 4) {
            scheduleNextSourceIdle();
            return true;
        }
        if (idleGapSourceTicks_ > sourceTicksPerNativeTick) {
            idleGapSourceTicks_ -= sourceTicksPerNativeTick;
            return true;
        }
        idleGapSourceTicks_ = 5;
        ++idleJokePart_;
        host_.play(kDominoIdleJokes[static_cast<std::size_t>(idleJokeIndex_)]
                                  [static_cast<std::size_t>(idleJokePart_)],
                   14, 5);
        return true;
    }
    return false;
}

bool DominoesGame::playable(const Tile& tile) const {
    return chain_.empty() || tile.left == chain_.front().left || tile.right == chain_.front().left ||
           tile.left == chain_.back().right || tile.right == chain_.back().right;
}

std::size_t DominoesGame::sourcePreferredTile(const std::vector<Tile>& hand,
                                              int leftEnd, int rightEnd) {
    std::size_t selected = hand.size();
    int smallestExposed = 99;
    const auto consider = [&](std::size_t index, int exposed) {
        if (exposed < smallestExposed) {
            smallestExposed = exposed;
            selected = index;
        }
    };

    // CODE 14 $2EFE emits candidates in hand order, checking the left end
    // before the right end.  $1870 then keeps the first candidate with the
    // lowest value in record field +6: the pip left exposed after placement.
    for (std::size_t index = 0; index < hand.size(); ++index) {
        const Tile& tile = hand[index];
        if (tile.left == leftEnd) consider(index, tile.right);
        if (tile.right == leftEnd && tile.left != tile.right) consider(index, tile.left);
        if (tile.left == rightEnd) consider(index, tile.right);
        if (tile.right == rightEnd && tile.left != tile.right) consider(index, tile.left);
    }
    return selected;
}

bool DominoesGame::sourceStrategyRegressionTest() const {
    const std::vector<Tile> smallestExposure{{6, 6}, {5, 6}, {0, 4}};
    const std::vector<Tile> reversedMatch{{2, 5}, {3, 4}};
    const std::vector<Tile> firstTieWins{{2, 4}, {4, 6}};
    const std::vector<Tile> noMove{{0, 1}, {3, 4}};
    SourceRandom deckRandom(1);
    for (int index = 0; index < 12; ++index) (void)deckRandom.below(4);
    std::vector<Tile> deck;
    for (int left = 0; left <= 6; ++left) {
        for (int right = left; right <= 6; ++right) deck.push_back({left, right});
    }
    sourceShuffleDeck(deck, deckRandom);
    static constexpr std::array expectedDeck{
        Tile{4, 5}, Tile{0, 6}, Tile{1, 4}, Tile{1, 1}, Tile{0, 5}, Tile{3, 4},
        Tile{2, 4}, Tile{2, 3}, Tile{4, 6}, Tile{1, 6}, Tile{0, 4}, Tile{0, 0},
        Tile{3, 5}, Tile{4, 4}, Tile{3, 3}, Tile{0, 1}, Tile{6, 6}, Tile{2, 5},
        Tile{1, 2}, Tile{5, 6}, Tile{2, 6}, Tile{1, 5}, Tile{1, 3}, Tile{3, 6},
        Tile{5, 5}, Tile{2, 2}, Tile{0, 2}, Tile{0, 3}};
    const bool exactDeck = deck.size() == expectedDeck.size() &&
        std::equal(deck.begin(), deck.end(), expectedDeck.begin(),
                   [](const Tile& left, const Tile& right) {
                       return left.left == right.left && left.right == right.right;
                   });

    SourceRandom correctionRandom(215);
    for (int index = 0; index < 12; ++index) (void)correctionRandom.below(4);
    std::vector<Tile> correctedDeck;
    for (int left = 0; left <= 6; ++left) {
        for (int right = left; right <= 6; ++right) correctedDeck.push_back({left, right});
    }
    sourceShuffleDeck(correctedDeck, correctionRandom);
    const bool correctionBranch = correctedDeck[26].left == 1 && correctedDeck[26].right == 1;

    return exactDeck && correctionBranch &&
           sourcePreferredTile(smallestExposure, 6, 4) == 2 &&
           sourcePreferredTile(reversedMatch, 3, 5) == 0 &&
           sourcePreferredTile(firstTieWins, 2, 6) == 0 &&
           sourcePreferredTile(noMove, 2, 6) == noMove.size();
}

bool DominoesGame::play(std::vector<Tile>& hand, std::size_t index, bool preferLeft,
                        bool allowOtherEnd) {
    if (index >= hand.size()) return false;
    Tile tile = hand[index];
    if (chain_.empty()) {
        chain_.push_back(tile);
        hand.erase(hand.begin() + static_cast<std::ptrdiff_t>(index));
        context_.audio.playEffect(5042);
        tileCommitSoundDelayMilliseconds_ = 396;
        chainReflowSoundPending_ = false;
        passes_ = 0;
        return true;
    }
    const int leftEnd = chain_.front().left;
    const int rightEnd = chain_.back().right;
    auto attachLeft = [&] {
        if (tile.right == leftEnd) { chain_.insert(chain_.begin(), tile); return true; }
        if (tile.left == leftEnd) { std::swap(tile.left, tile.right); chain_.insert(chain_.begin(), tile); return true; }
        return false;
    };
    auto attachRight = [&] {
        if (tile.left == rightEnd) { chain_.push_back(tile); return true; }
        if (tile.right == rightEnd) { std::swap(tile.left, tile.right); chain_.push_back(tile); return true; }
        return false;
    };
    bool attached = preferLeft ? attachLeft() : attachRight();
    if (!attached && allowOtherEnd) attached = preferLeft ? attachRight() : attachLeft();
    if (!attached) return false;
    hand.erase(hand.begin() + static_cast<std::ptrdiff_t>(index));
    context_.audio.playEffect(5042);
    tileCommitSoundDelayMilliseconds_ = 396;
    // These are the four turns in the native rendering of the source chain;
    // the original recenters at the equivalent endpoint-distance thresholds.
    chainReflowSoundPending_ =
        chain_.size() == 9 || chain_.size() == 12 || chain_.size() == 20 || chain_.size() == 23;
    passes_ = 0;
    return true;
}

int DominoesGame::pips(const std::vector<Tile>& hand) {
    int total = 0;
    for (const Tile& tile : hand) total += tile.left + tile.right;
    return total;
}

void DominoesGame::resolveBlocked() {
    if (passes_ < 2) return;
    const int humanScore = pips(human_);
    const int computerScore = pips(computer_);
    winner_ = humanScore == computerScore ? 2 : (humanScore < computerScore ? 1 : -1);
    status_ = winner_ == 2 ? L"It's a tie!" :
              winner_ > 0 ? L"You got the better score. You win!" :
                            L"Mario got the better score this time.";
    beginOutcome(OutcomeKind::Blocked);
}

int DominoesGame::chooseWeighted(std::span<const int> resources,
                                 std::span<const int> cumulativePercentages) {
    if (resources.empty() || resources.size() != cumulativePercentages.size() ||
        cumulativePercentages.back() != 100) {
        throw std::runtime_error("invalid Dominoes source outcome pool");
    }
    const int value = context_.random.below(100);
    for (std::size_t index = 0; index < resources.size(); ++index) {
        if (value < cumulativePercentages[index]) return resources[index];
    }
    return resources.back();
}

void DominoesGame::playOutcomeMovie(int resourceId) {
    outcomeMoviesPlayed_.push_back(resourceId);
    host_.play(resourceId, 14, 5);
}

void DominoesGame::beginOutcome(OutcomeKind kind) {
    if (!winner_ || outcomePhase_ != OutcomePhase::None) return;
    outcomeKind_ = kind;
    outcomeMoviesPlayed_.clear();
    if (kind == OutcomeKind::Blocked) {
        outcomePhase_ = OutcomePhase::BlockedOpening;
    } else if (winner_ > 0) {
        // CODE 14 $1304 starts the last-domino player branch with ten ticks.
        outcomeDelayTicks_ = 10;
        outcomePhase_ = OutcomePhase::HumanInitialDelay;
    } else {
        outcomePhase_ = OutcomePhase::MarioAnnouncement;
    }
}

bool DominoesGame::finished() const {
    return outcomePhase_ == OutcomePhase::Complete;
}

bool DominoesGame::tickOutcome() {
    if (outcomePhase_ == OutcomePhase::None || outcomePhase_ == OutcomePhase::Complete) {
        return false;
    }

    static constexpr std::array equalWeights{50, 100};
    static constexpr std::array fortyFortyTwenty{40, 80, 100};
    switch (outcomePhase_) {
    case OutcomePhase::BlockedOpening: {
        if (host_.active()) return false;
        // $0FC6 chooses source indices 29/30: the two blocked-hand lines.
        static constexpr std::array movies{10040, 10041};
        playOutcomeMovie(chooseWeighted(movies, equalWeights));
        outcomeDelayTicks_ = 15;
        outcomePhase_ = OutcomePhase::BlockedOpeningDelay;
        return true;
    }
    case OutcomePhase::BlockedOpeningDelay:
        if (host_.active()) return false;
        if (outcomeDelayTicks_-- > 0) return true;
        // $1028 uses source index 48 while the two pip totals are shown.
        playOutcomeMovie(10059);
        outcomeDelayTicks_ = 15;
        outcomePhase_ = OutcomePhase::BlockedScoreDelay;
        return true;
    case OutcomePhase::BlockedScoreDelay:
        if (host_.active()) return false;
        if (outcomeDelayTicks_-- > 0) return true;
        // $1132 restores the neutral host movie, then plays 5034 for Mario's
        // win (source result 1) or 5057 for a player win/tie.
        playOutcomeMovie(10000);
        context_.audio.playSound(winner_ < 0 ? 5034 : 5057);
        outcomeDelayTicks_ = 30;
        outcomePhase_ = OutcomePhase::BlockedResultDelay;
        return true;
    case OutcomePhase::BlockedResultDelay: {
        if (host_.active()) return false;
        if (outcomeDelayTicks_-- > 0) return true;
        // Source state 3 waits for the result cue to drain before speech.
        if (context_.audio.soundPlaying()) return false;
        int movie = 10062;
        if (winner_ < 0) {
            // Result one at $1190: source indices 49/45/46.
            static constexpr std::array movies{10060, 10056, 10057};
            movie = chooseWeighted(movies, fortyFortyTwenty);
        } else if (winner_ > 0 && winner_ != 2) {
            // Result two at $11CA: source indices 50/42/44.
            static constexpr std::array movies{10061, 10053, 10055};
            movie = chooseWeighted(movies, fortyFortyTwenty);
        }
        playOutcomeMovie(movie);
        outcomeDelayTicks_ = 15;
        outcomePhase_ = OutcomePhase::MarioAnnouncementDelay;
        return true;
    }
    case OutcomePhase::HumanInitialDelay: {
        if (host_.active()) return false;
        if (outcomeDelayTicks_-- > 0) return true;
        context_.audio.playMusic(audio_catalog::kPlayerWinMusic[1]);
        // $136A uses six authored player-win variants with 20/20/20/20/10/10 odds.
        static constexpr std::array movies{10051, 10087, 10055, 10053, 10086, 10052};
        static constexpr std::array weights{20, 40, 60, 80, 90, 100};
        playOutcomeMovie(chooseWeighted(movies, weights));
        outcomeDelayTicks_ = 5;
        outcomePhase_ = OutcomePhase::HumanFirstDelay;
        return true;
    }
    case OutcomePhase::HumanFirstDelay: {
        if (host_.active()) return false;
        if (outcomeDelayTicks_-- > 0) return true;
        int movie = 0;
        if (roundNumber_ == 1) {
            // First game: source indices 52/54/55/58.
            static constexpr std::array movies{10063, 10065, 10067, 10071};
            static constexpr std::array weights{30, 50, 80, 100};
            movie = chooseWeighted(movies, weights);
        } else {
            // Later games: source indices 53/56/57/59.
            static constexpr std::array movies{10064, 10068, 10069, 10072};
            static constexpr std::array weights{30, 50, 80, 100};
            movie = chooseWeighted(movies, weights);
        }
        playOutcomeMovie(movie);
        outcomeDelayTicks_ = 5;
        outcomePhase_ = OutcomePhase::HumanSecondDelay;
        return true;
    }
    case OutcomePhase::HumanSecondDelay:
        if (host_.active()) return false;
        if (outcomeDelayTicks_-- > 0) return true;
        // The common result controller runs $14C6 before the replay prompt
        // for source result 2 (the player), resetting the chain with 5023.
        context_.audio.playEffect(5023);
        outcomePhase_ = OutcomePhase::ReplayPrompt;
        return true;
    case OutcomePhase::MarioAnnouncement: {
        if (host_.active()) return false;
        // $128C uses source indices 47/45/46 for a last-domino Mario win.
        static constexpr std::array movies{10058, 10056, 10057};
        playOutcomeMovie(chooseWeighted(movies, fortyFortyTwenty));
        outcomeDelayTicks_ = 10;
        outcomePhase_ = OutcomePhase::MarioAnnouncementDelay;
        return true;
    }
    case OutcomePhase::MarioAnnouncementDelay:
        if (host_.active()) return false;
        if (outcomeDelayTicks_-- > 0) return true;
        if (outcomeKind_ == OutcomeKind::Blocked && winner_ > 0 && winner_ != 2) {
            context_.audio.playEffect(5023);
        }
        outcomePhase_ = OutcomePhase::ReplayPrompt;
        return true;
    case OutcomePhase::ReplayPrompt: {
        if (host_.active()) return false;
        // $0C3E draws below 10 (rather than below 100): values 0..4 select
        // source index 61 and values 5..9 select index 60.
        playOutcomeMovie(context_.random.below(10) < 5 ? 10074 : 10073);
        outcomeDelayTicks_ = 1;
        outcomePhase_ = OutcomePhase::ReplayDelay;
        return true;
    }
    case OutcomePhase::ReplayDelay:
        if (host_.active()) return false;
        if (outcomeDelayTicks_-- > 0) return true;
        outcomeDelayTicks_ = 1;
        outcomePhase_ = OutcomePhase::FinalDelay;
        return true;
    case OutcomePhase::FinalDelay:
        if (outcomeDelayTicks_-- > 0) return true;
        outcomePhase_ = OutcomePhase::Complete;
        return true;
    case OutcomePhase::None:
    case OutcomePhase::Complete:
        return false;
    }
    return false;
}

bool DominoesGame::sourceOutcomeRegressionTest(int expectedWinner, bool blocked) {
    if (expectedWinner != -1 && expectedWinner != 1 && expectedWinner != 2) return false;
    if (!blocked && expectedWinner == 2) return false;
    host_.stop();
    dealDelayMilliseconds_ = 0;
    dealComplete_ = true;
    pendingComputerOpening_ = -1;
    computerTurnPending_ = false;
    winner_ = expectedWinner;
    outcomePhase_ = OutcomePhase::None;
    beginOutcome(blocked ? OutcomeKind::Blocked : OutcomeKind::LastDomino);
    for (int tickIndex = 0; tickIndex < 2400 && !finished(); ++tickIndex) (void)tick();
    if (!finished() || outcomeMoviesPlayed_.empty() ||
        (outcomeMoviesPlayed_.back() != 10073 && outcomeMoviesPlayed_.back() != 10074)) {
        return false;
    }
    const auto in = [](int value, std::span<const int> pool) {
        return std::find(pool.begin(), pool.end(), value) != pool.end();
    };
    if (blocked) {
        static constexpr std::array openings{10040, 10041};
        static constexpr std::array marioResults{10060, 10056, 10057};
        static constexpr std::array humanResults{10061, 10053, 10055};
        const std::span<const int> resultPool = expectedWinner < 0
            ? std::span<const int>(marioResults)
            : expectedWinner > 0 && expectedWinner != 2
                ? std::span<const int>(humanResults)
                : std::span<const int>();
        return outcomeMoviesPlayed_.size() == 5 && in(outcomeMoviesPlayed_[0], openings) &&
               outcomeMoviesPlayed_[1] == 10059 && outcomeMoviesPlayed_[2] == 10000 &&
               (expectedWinner == 2 ? outcomeMoviesPlayed_[3] == 10062
                                    : in(outcomeMoviesPlayed_[3], resultPool));
    }
    if (expectedWinner < 0) {
        static constexpr std::array results{10058, 10056, 10057};
        return outcomeMoviesPlayed_.size() == 2 && in(outcomeMoviesPlayed_[0], results);
    }
    static constexpr std::array firstResults{10051, 10087, 10055, 10053, 10086, 10052};
    static constexpr std::array comments{10063, 10065, 10067, 10071};
    return outcomeMoviesPlayed_.size() == 3 && in(outcomeMoviesPlayed_[0], firstResults) &&
           in(outcomeMoviesPlayed_[1], comments);
}

bool DominoesGame::sourceReplayRegressionTest() {
    roundNumber_ = 1;
    resetForReplay();
    return roundNumber_ == 2 && winner_ == 0 && outcomePhase_ == OutcomePhase::None &&
           human_.size() == 7 && computer_.size() == 7 && chain_.empty();
}

void DominoesGame::setQaOutcomePresentation(int expectedWinner, bool blocked) {
    host_.stop();
    dealDelayMilliseconds_ = 0;
    dealComplete_ = true;
    pendingComputerOpening_ = -1;
    computerTurnPending_ = false;
    winner_ = expectedWinner < 0 ? -1 : expectedWinner == 2 ? 2 : 1;
    status_ = winner_ == 2 ? L"It's a tie!" : winner_ > 0
        ? L"Congratulations! You win!" : L"Mario wins this time.";
    outcomePhase_ = OutcomePhase::None;
    beginOutcome(blocked ? OutcomeKind::Blocked : OutcomeKind::LastDomino);
}

void DominoesGame::scheduleComputerTurn() {
    if (winner_) return;
    computerTurnPending_ = true;
    computerBlockedAnnounced_ = false;
    computerPassCommentaryPlayed_ = false;
    computerStartCommentaryPending_ = true;
    computerMoveCommentaryResolved_ = false;
    computerThinkingUsed_ = false;
    computerThinkingDelaySourceTicks_ = 0;
    computerDrawsThisTurn_ = 0;
    computerDelayMilliseconds_ = 0;
    status_ = L"It's my turn.";
}

void DominoesGame::scheduleHumanTurnCommentary() {
    if (!winner_) humanTurnCommentaryPending_ = true;
}

void DominoesGame::beginHumanTurnCommentary() {
    humanTurnCommentaryPending_ = false;
    status_ = L"It's your turn.";
    const int sourceIndex = chooseHumanTurnSpeech(context_.random, humanHasDrawn_);
    const int movie = dominoMovieForSourceIndex(sourceIndex);
    if (movie != 0) {
        status_ = std::wstring(dominoStatusForSourceIndex(sourceIndex));
        host_.play(movie, 14, 5);
    }
}

void DominoesGame::computerStep() {
    if (winner_ || !computerTurnPending_) return;
    // $1C62 consumes below(11) on every post-draw decision.  When bones
    // remain, $1D0E also starts tracked snd 5003 before Mario either draws
    // again or plays the bone he just found.
    if (computerDrawsThisTurn_ > 0) {
        (void)context_.random.below(11);
        if (!boneyard_.empty()) context_.audio.playSound(5003);
    }
    const bool hasPlayable = std::any_of(computer_.begin(), computer_.end(),
        [&](const Tile& tile) { return playable(tile); });
    if (!hasPlayable) {
        if (boneyard_.empty()) {
            if (!computerPassCommentaryPlayed_) {
                // The shared $1C62 commentary selector has already been
                // entered before state 4 reaches the $1810 pass pool, so an
                // otherwise discarded below(11) precedes below(100).
                (void)context_.random.below(11);
                computerPassCommentaryPlayed_ = true;
                const int sourceIndex = chooseComputerPassSpeech(context_.random);
                status_ = std::wstring(dominoStatusForSourceIndex(sourceIndex));
                host_.play(dominoMovieForSourceIndex(sourceIndex), 14, 5);
                return;
            }
            ++passes_;
            computerTurnPending_ = false;
            status_ = L"It's your turn.";
            resolveBlocked();
            if (!winner_) scheduleHumanTurnCommentary();
            return;
        }
        if (!computerBlockedAnnounced_) {
            computerBlockedAnnounced_ = true;
            const int sourceIndex = chooseComputerDrawSpeech(context_.random);
            status_ = std::wstring(dominoStatusForSourceIndex(sourceIndex));
            host_.play(dominoMovieForSourceIndex(sourceIndex), 14, 5);
            return;
        }
        computer_.push_back(boneyard_.back());
        boneyard_.pop_back();
        ++computerDrawsThisTurn_;
        status_ = L"Mario draws a bone.";
        if (boneyard_.empty()) {
            status_ = L"Oh-oh, no more dominoes in the boneyard.";
            host_.play(10080, 14, 5);
        }
        return;
    }

    if (computerThinkingDelaySourceTicks_ != 0) {
        constexpr unsigned sourceTicksPerNativeTick = 2;
        if (computerThinkingDelaySourceTicks_ > sourceTicksPerNativeTick) {
            computerThinkingDelaySourceTicks_ -= sourceTicksPerNativeTick;
        } else {
            computerThinkingDelaySourceTicks_ = 0;
        }
        return;
    }
    if (!computerMoveCommentaryResolved_) {
        const ComputerMoveSpeechChoice choice = chooseComputerMoveSpeech(
            context_.random, static_cast<int>(computer_.size()),
            static_cast<int>(human_.size()), computerDrawsThisTurn_,
            computerThinkingUsed_, lastComputerMoveSpeechSourceIndex_);
        if (choice.kind == ComputerMoveSpeechKind::Retry) return;
        if (choice.kind == ComputerMoveSpeechKind::Thinking) {
            computerThinkingDelaySourceTicks_ = choice.delaySourceTicks;
            status_ = std::wstring(dominoStatusForSourceIndex(choice.sourceIndex));
            host_.play(dominoMovieForSourceIndex(choice.sourceIndex), 14, 5);
            return;
        }
        computerMoveCommentaryResolved_ = true;
        if (choice.sourceIndex >= 0) {
            status_ = std::wstring(dominoStatusForSourceIndex(choice.sourceIndex));
            host_.play(dominoMovieForSourceIndex(choice.sourceIndex), 14, 5);
        }
    }
    const int leftEnd = chain_.front().left;
    const int rightEnd = chain_.back().right;
    const std::size_t best = sourcePreferredTile(computer_, leftEnd, rightEnd);
    const Tile chosen = computer_[best];
    const bool fitsLeft = chosen.left == leftEnd || chosen.right == leftEnd;
    const bool fitsRight = chosen.left == rightEnd || chosen.right == rightEnd;
    bool preferLeft = fitsLeft;
    if (fitsLeft && fitsRight) {
        preferLeft = context_.random.below(2) == 0;
    }
    // CODE 14 $19D4 selects Mario's tile with snd 5043 before $2A58
    // commits it with the normal 5042 placement sound.
    context_.audio.playEffect(5043);
    play(computer_, best, preferLeft);
    computerTurnPending_ = false;
    if (computer_.empty()) {
        winner_ = -1;
        status_ = L"Mario used his last domino this time.";
        beginOutcome(OutcomeKind::LastDomino);
    } else {
        status_ = L"It's your turn.";
        scheduleHumanTurnCommentary();
    }
}

void DominoesGame::click(Point point) {
    idleElapsedSourceTicks_ = 0;
    if (winner_ || !dealComplete_ || pendingComputerOpening_ >= 0 ||
        computerTurnPending_ || host_.active()) return;
    if (drawButton_.contains(point)) {
        // $2350 reserves 9202 for the source's fourteen-bone hand limit.
        if (!boneyard_.empty() && human_.size() >= 14) {
            context_.audio.playEffect(9202);
            status_ = L"You can't hold any more dominoes."; return;
        }
        if (boneyard_.empty()) {
            // $236E starts the empty-boneyard cue before the pass sequence.
            context_.audio.playEffect(5024);
            ++passes_; status_ = L"No more dominoes in the boneyard. You pass.";
            host_.play(10080, 14, 5);
            resolveBlocked(); if (!winner_) scheduleComputerTurn();
        } else {
            // $23C4 starts 5003 for every player draw; $24D8 supplies the
            // subsequent tile-transit effect.
            context_.audio.playEffect(5003);
            human_.push_back(boneyard_.back()); boneyard_.pop_back();
            context_.audio.playEffect(5042);
            status_ = playable(human_.back()) ? L"Here's your bone—you can play it." : L"Draw again or choose a playable bone.";
            const int sourceIndex = chooseHumanDrawSpeech(context_.random, humanHasDrawn_);
            humanHasDrawn_ = true;
            const int movie = dominoMovieForSourceIndex(sourceIndex);
            if (movie != 0) host_.play(movie, 14, 5);
        }
        return;
    }
}

void DominoesGame::mouseDown(Point point) {
    idleElapsedSourceTicks_ = 0;
    if (winner_ || !dealComplete_ || pendingComputerOpening_ >= 0 ||
        computerTurnPending_ || host_.active()) return;
    if (point.y >= 286 && point.y < 358 && point.x >= 7 && point.x < 476) {
        const int index = (point.x - 7) / 35;
        if (index >= 0 && index < static_cast<int>(human_.size())) {
            // $276C starts 5043 on mouse-down, then the source tracks the bone
            // until mouse-up and resolves the nearest chain endpoint.
            context_.audio.playEffect(5043);
            selected_ = draggedIndex_ = index;
            dragPoint_ = point;
            return;
        }
    }
    click(point);
}

void DominoesGame::mouseMove(Point point) {
    if (draggedIndex_ >= 0) dragPoint_ = point;
}

void DominoesGame::mouseCancel() {
    draggedIndex_ = -1;
    selected_ = -1;
    dragPoint_ = {};
}

void DominoesGame::mouseUp(Point point) {
    if (draggedIndex_ < 0) return;
    const int index = draggedIndex_;
    draggedIndex_ = -1;
    dragPoint_ = point;
    if (index >= static_cast<int>(human_.size()) || winner_ || host_.active()) {
        selected_ = -1;
        return;
    }
    if (requiredHumanOpening_ >= 0 && index != requiredHumanOpening_) {
        selected_ = -1;
        status_ = L"Start with your highest doublet.";
        return;
    }
    if (!playable(human_[static_cast<std::size_t>(index)])) {
        selected_ = -1;
        status_ = L"I don't think you can put that domino there.";
        return;
    }

    bool preferLeft = true;
    if (!chain_.empty()) {
        const int visible = std::min(30, static_cast<int>(chain_.size()));
        const Rect left = chainTileRect(0, visible);
        const Rect right = chainTileRect(visible - 1, visible);
        const Point leftCenter{(left.left + left.right) / 2,
                               (left.top + left.bottom) / 2};
        const Point rightCenter{(right.left + right.right) / 2,
                                (right.top + right.bottom) / 2};
        const auto distance = [](Point first, Point second) {
            return std::max(std::abs(first.x - second.x),
                            std::abs(first.y - second.y));
        };
        const int endpointDistance = distance(leftCenter, rightCenter);
        int acceptanceDistance = 150;
        if (chain_.size() > 6 && endpointDistance < acceptanceDistance) {
            acceptanceDistance = std::max(60, endpointDistance / 2);
        }
        const int leftDistance = distance(point, leftCenter);
        const int rightDistance = distance(point, rightCenter);
        if (std::min(leftDistance, rightDistance) >= acceptanceDistance) {
            selected_ = -1;
            status_ = L"Drag the domino to an end of the chain.";
            return;
        }
        // CODE 14 $29B8 takes the right branch for an exact distance tie.
        preferLeft = leftDistance < rightDistance;
        // Once a long wrapped chain brings both end points within sixty pixels,
        // $29E6-$2A50 resolves the ambiguous end from the dragged bone's pips.
        if (chain_.size() > 6 && endpointDistance < 60) {
            const Tile& tile = human_[static_cast<std::size_t>(index)];
            const int leftEnd = chain_.front().left;
            const int rightEnd = chain_.back().right;
            const bool fitsLeft = tile.left == leftEnd || tile.right == leftEnd;
            const bool fitsRight = tile.left == rightEnd || tile.right == rightEnd;
            if (fitsLeft) preferLeft = true;
            else if (fitsRight) preferLeft = false;
        }
    } else if (point.y < 65 || point.y > 285) {
        selected_ = -1;
        status_ = L"Drag the first domino onto the table.";
        return;
    }

    if (!play(human_, static_cast<std::size_t>(index), preferLeft, false)) {
        selected_ = -1;
        status_ = L"That domino doesn't fit on this end.";
        return;
    }
    if (requiredHumanOpening_ >= 0) requiredHumanOpening_ = -1;
    selected_ = -1;
    if (human_.empty()) {
        winner_ = 1;
        status_ = L"Congratulations! You used your last domino!";
        beginOutcome(OutcomeKind::LastDomino);
        return;
    }

    scheduleComputerTurn();
    const int sourceIndex = chooseHumanMoveSpeech(
        context_.random, static_cast<int>(human_.size()), lastHumanMoveSpeechSourceIndex_);
    const int movie = dominoMovieForSourceIndex(sourceIndex);
    if (movie != 0) {
        status_ = std::wstring(dominoStatusForSourceIndex(sourceIndex));
        host_.play(movie, 14, 5);
    }
}

bool DominoesGame::sourceDragRegressionTest() {
    const auto prepare = [&](std::vector<Tile> chain, std::vector<Tile> hand) {
        host_.stop();
        dealComplete_ = true;
        pendingComputerOpening_ = -1;
        computerTurnPending_ = false;
        winner_ = 0;
        requiredHumanOpening_ = -1;
        chain_ = std::move(chain);
        human_ = std::move(hand);
        selected_ = draggedIndex_ = -1;
    };

    prepare({{2, 3}, {3, 4}}, {{2, 5}, {0, 1}});
    mouseDown({20, 315});
    mouseUp({284, 142});
    const bool wrongEndRejected = human_.size() == 2 && chain_.size() == 2;
    mouseDown({20, 315});
    mouseMove({228, 142});
    mouseUp({228, 142});
    const bool correctEndAccepted = human_.size() == 1 && chain_.size() == 3 &&
        chain_.front().left == 5 && chain_.front().right == 2;

    // Short chains use the source's full 150-pixel Chebyshev threshold.
    prepare({{2, 3}, {3, 4}}, {{4, 5}});
    mouseDown({20, 315});
    mouseUp({433, 142});  // 149 pixels from the right endpoint.
    const bool shortChainRadiusAccepted = human_.empty() && chain_.back().right == 5;
    prepare({{2, 3}, {3, 4}}, {{4, 5}});
    mouseDown({20, 315});
    mouseUp({434, 142});  // The comparison is strict: 150 is rejected.
    const bool shortChainBoundaryRejected = human_.size() == 1 && chain_.size() == 2;

    // An exact positional tie takes the right endpoint at $29B8-$29E0.
    prepare({{2, 3}, {3, 4}}, {{2, 4}});
    mouseDown({20, 315});
    mouseUp({256, 142});
    const bool tieChoosesRight = human_.empty() && chain_.size() == 3 &&
        chain_.back().left == 4 && chain_.back().right == 2;

    // For a wrapped chain, half the end-to-end separation is used and then
    // clamped to sixty pixels. A nineteen-bone chain has ends 118 pixels apart.
    std::vector<Tile> wrapped(19, Tile{2, 2});
    prepare(wrapped, {{2, 5}});
    mouseDown({20, 315});
    mouseUp({119, 142});  // 59 pixels from the left endpoint.
    const bool wrappedRadiusAccepted = human_.empty() && chain_.front().left == 5;
    prepare(std::move(wrapped), {{2, 5}});
    mouseDown({20, 315});
    mouseUp({120, 142});  // The clamped sixty-pixel boundary is rejected.
    const bool wrappedBoundaryRejected = human_.size() == 1 && chain_.size() == 19;

    // At twenty-one bones the rendered ends overlap. The source ignores the
    // geometrically nearer end and uses the only endpoint matching the pips.
    std::vector<Tile> overlapping(21, Tile{2, 2});
    overlapping.back() = {2, 4};
    prepare(std::move(overlapping), {{4, 5}});
    mouseDown({20, 315});
    mouseUp({60, 142});
    const bool overlapUsesPips = human_.empty() && chain_.back().right == 5;

    prepare({{2, 3}, {3, 4}}, {{2, 5}});
    mouseDown({20, 315});
    mouseCancel();
    const bool captureLossRestoresBone = draggedIndex_ < 0 && selected_ < 0 &&
        human_.size() == 1 && chain_.size() == 2;

    return wrongEndRejected && correctEndAccepted && shortChainRadiusAccepted &&
           shortChainBoundaryRejected && tieChoosesRight && wrappedRadiusAccepted &&
           wrappedBoundaryRejected && overlapUsesPips && captureLossRestoresBone;
}

void DominoesGame::setQaDragPresentation() {
    host_.stop();
    dealComplete_ = true;
    dealDelayMilliseconds_ = 0;
    dealSoundDelayMilliseconds_ = 0;
    pendingComputerOpening_ = -1;
    computerTurnPending_ = false;
    winner_ = 0;
    requiredHumanOpening_ = -1;
    chain_ = {{2, 3}, {3, 4}};
    human_ = {{2, 5}, {0, 1}};
    computer_ = {{0, 0}, {1, 1}, {3, 3}};
    boneyard_.clear();
    selected_ = draggedIndex_ = -1;
    dragPoint_ = {};
    status_ = L"Drag the 2–5 domino to the left end of the chain.";
}

bool DominoesGame::sourceIdleRegressionTest() {
    const auto choose = [](std::uint32_t seed) {
        SourceRandom random(seed);
        int jokeIndex = 0;
        const DominoIdleChoice choice = chooseDominoIdle(random, jokeIndex);
        return std::tuple{choice.kind, choice.value, jokeIndex, random.seed()};
    };

    // The source begins its joke cursor at zero and increments before use, so
    // the first joke branch is record one rather than the pizza record.
    return choose(2) == std::tuple{DominoIdleChoiceKind::SingleLine, 10090, 0,
                                  std::uint32_t{33614U}} &&
           choose(11) == std::tuple{DominoIdleChoiceKind::SingleLine, 10091, 0,
                                   std::uint32_t{184877U}} &&
           choose(1) == std::tuple{DominoIdleChoiceKind::Joke, 1, 1,
                                  std::uint32_t{16807U}};
}

bool DominoesGame::sourceDialogueRegressionTest() {
    const auto one = [](std::uint32_t seed, auto chooser) {
        SourceRandom random(seed);
        const int choice = chooser(random);
        return std::pair{choice, random.seed()};
    };

    const bool passPools =
        one(7, [](SourceRandom& random) { return chooseComputerPassSpeech(random); }) ==
            std::pair{34, std::uint32_t{117649U}} &&
        one(39, [](SourceRandom& random) { return chooseComputerPassSpeech(random); }) ==
            std::pair{33, std::uint32_t{655473U}} &&
        one(32, [](SourceRandom& random) { return chooseComputerPassSpeech(random); }) ==
            std::pair{32, std::uint32_t{537824U}} &&
        one(9, [](SourceRandom& random) { return chooseComputerPassSpeech(random); }) ==
            std::pair{35, std::uint32_t{151263U}};

    const bool playerTurnPools =
        one(353, [](SourceRandom& random) { return chooseHumanTurnSpeech(random, false); }) ==
            std::pair{8, std::uint32_t{5932871U}} &&
        one(392, [](SourceRandom& random) { return chooseHumanTurnSpeech(random, false); }) ==
            std::pair{9, std::uint32_t{6588344U}} &&
        one(513, [](SourceRandom& random) { return chooseHumanTurnSpeech(random, false); }) ==
            std::pair{11, std::uint32_t{8621991U}} &&
        one(14, [](SourceRandom& random) { return chooseHumanTurnSpeech(random, false); }) ==
            std::pair{-1, std::uint32_t{235298U}} &&
        one(353, [](SourceRandom& random) { return chooseHumanTurnSpeech(random, true); }) ==
            std::pair{-1, std::uint32_t{5932871U}};

    SourceRandom thinkingRandom(2);
    bool thinkingUsed = false;
    int lastComputer = -99;
    const ComputerMoveSpeechChoice thinking = chooseComputerMoveSpeech(
        thinkingRandom, 7, 7, 0, thinkingUsed, lastComputer);
    const bool thinkingPath = thinking.kind == ComputerMoveSpeechKind::Thinking &&
        thinking.sourceIndex == 22 && thinking.delaySourceTicks == 7 && thinkingUsed &&
        thinkingRandom.seed() == 564950498U;

    SourceRandom moveRandom(3);
    int lastHuman = -99;
    const int firstMove = chooseHumanMoveSpeech(moveRandom, 2, lastHuman);
    SourceRandom repeatRandom(3);
    const int repeatedMove = chooseHumanMoveSpeech(repeatRandom, 2, lastHuman);
    const bool humanMovePath = firstMove == 12 && repeatedMove == -1 && lastHuman == 12 &&
        moveRandom.seed() == 50421U && repeatRandom.seed() == 50421U;

    return passPools && playerTurnPools && thinkingPath && humanMovePath;
}

void DominoesGame::drawTile(Canvas& canvas, const Tile& tile, Rect rect, bool selected) const {
    const bool vertical = rect.height() > rect.width();
    const Sprite& first = context_.graphics.sprite(3100, tile.left);
    const Sprite& second = context_.graphics.sprite(3100, tile.right);
    if (vertical) {
        canvas.sprite(first, rect.left, rect.top, false);
        canvas.sprite(second, rect.left, rect.top + 28, false);
    } else {
        canvas.sprite(first, rect.left, rect.top, false);
        canvas.sprite(second, rect.left + 28, rect.top, false);
    }
    if (selected) {
        canvas.outlineRect({rect.left, rect.top,
                            rect.left + (vertical ? 28 : 56),
                            rect.top + (vertical ? 56 : 28)}, rgb(255, 230, 0), 2);
    }
}

Rect DominoesGame::chainTileRect(int index, int visible) const {
    if (index < 8) {
        const int rowCount = std::min(visible, 8);
        const int startX = 256 - rowCount * 28;
        return {startX + index * 56, 128, startX + (index + 1) * 56, 156};
    }
    if (index < 11) return {452, 156 + (index - 8) * 56, 480, 212 + (index - 8) * 56};
    if (index < 19) return {424 - (index - 11) * 56, 246,
                            480 - (index - 11) * 56, 274};
    if (index < 22) return {32, 190 - (index - 19) * 56,
                            60, 246 - (index - 19) * 56};
    return {60 + (index - 22) * 56, 72, 116 + (index - 22) * 56, 100};
}

void DominoesGame::render(Canvas& canvas) {
    canvas.clear(rgb(0, 0, 0));
    drawBackground(canvas, 3001);
    canvas.sprite(context_.graphics.sprite(3700), 3, 5, false);
    if (characterChooser_ || !host_.render(canvas)) {
        canvas.sprite(context_.graphics.sprite(10000), 19, 13, false);
    }
    if (!characterChooser_ && computer_.size() <= 28) {
        canvas.sprite(context_.graphics.sprite(3701,
                                                dealComplete_ ? static_cast<int>(computer_.size()) : 0),
                      50, 77, false);
    }
    const int visible = characterChooser_ || !dealComplete_
                            ? 0 : std::min(30, static_cast<int>(chain_.size()));
    const int start = std::max(0, static_cast<int>(chain_.size()) - visible);
    for (int index = 0; index < visible; ++index) {
        drawTile(canvas, chain_[start + index], chainTileRect(index, visible));
    }
    if (!characterChooser_ && dealComplete_) for (std::size_t index = 0; index < human_.size(); ++index) {
        if (static_cast<int>(index) == draggedIndex_) continue;
        const int x = 7 + static_cast<int>(index) * 35;
        drawTile(canvas, human_[index], {x, 289, x + 28, 345},
                 selected_ == static_cast<int>(index));
    }
    if (!characterChooser_ && draggedIndex_ >= 0 &&
        draggedIndex_ < static_cast<int>(human_.size())) {
        const int x = std::clamp(dragPoint_.x, 14, 498);
        const int y = std::clamp(dragPoint_.y, 28, 330);
        drawTile(canvas, human_[static_cast<std::size_t>(draggedIndex_)],
                 {x - 14, y - 28, x + 14, y + 28}, true);
    }
    if (!characterChooser_) canvas.sprite(context_.graphics.sprite(3100, 9), 478, 286, false);
    if (!characterChooser_ && boneyard_.size() <= 28) {
        const Sprite& count = context_.graphics.sprite(
            3701, dealComplete_ ? static_cast<int>(boneyard_.size()) : 28);
        canvas.sprite(count, 494 - count.width / 2, 332, false);
    }
    canvas.pakText(context_.graphics,
                   characterChooser_ ? L"Do you want to play as a Yoshi, or as a Koopa?" : status_,
                   224, {5, 359, 507, 383});
}

}  // namespace mf
