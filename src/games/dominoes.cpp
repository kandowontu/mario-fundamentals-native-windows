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

constexpr int dominoDealMovie(bool laterRound, int sourceDraw) {
    // CODE 14 $DCA uses source indices 2/3 on the first round and 4/54 on
    // later rounds. The host table resolves those to these four MuV ids.
    if (laterRound) return sourceDraw < 51 ? 10004 : 10065;
    return sourceDraw < 51 ? 10002 : 10003;
}

constexpr int dominoOpeningMovie(bool marioStarts, int sourceDraw) {
    // CODE 14 $2E9A: Mario's higher doublet uses source index 68 (75%) or
    // index 5; the player's higher-or-equal doublet uses index 6/8 evenly.
    if (marioStarts) return sourceDraw < 75 ? 10084 : 10005;
    return sourceDraw < 50 ? 10006 : 10008;
}

}  // namespace

DominoesGame::DominoesGame(GameContext context)
    : Game(context), host_(context.assets, context.graphics, context.audio, true,
          [](int resourceId, Point scaled) {
              if (resourceId >= 10000) return Point{7, 3};
              return scaled;
          }),
      playerResultAnimation_(context.assets, context.graphics, context.audio, false) { reset(); }

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

    // CODE 14 $DCA chooses from distinct first/later-round deal pools.
    const int dealMovie = dominoDealMovie(nextRoundNumber > 1, context_.random.below(100));
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
        openingMovie_ = dominoOpeningMovie(true, openingChoice);
    } else {
        requiredHumanOpening_ = humanBest.second;
        openingMovie_ = dominoOpeningMovie(false, openingChoice);
    }
    selected_ = draggedIndex_ = -1;
    dragPoint_ = {};
    passes_ = winner_ = computerDelayMilliseconds_ = 0;
    computerDrawsThisTurn_ = 0;
    lastComputerMoveSpeechSourceIndex_ = -99;
    lastHumanMoveSpeechSourceIndex_ = -99;
    // $E1C preloads the first pair's pass counter to two. $EFE clears it
    // after each deal, producing two controller passes between later pairs.
    // Every pair is additionally paced by CODE 1 $B22.
    dealDelayMilliseconds_ = 0;
    dealSoundCount_ = 0;
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
    playerResultAnimation_.stop();
    status_ = L"Let's play, eh?";
    host_.play(dealMovie, 14, 5);
}

void DominoesGame::resetForReplay() {
    reset(true);
}

bool DominoesGame::tick() {
    bool changed = host_.tick();
    changed |= playerResultAnimation_.tick();
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
    if (!dealComplete_) {
        changed = true;
        if (dealDelayMilliseconds_ > 0) {
            dealDelayMilliseconds_ = std::max(0, dealDelayMilliseconds_ - 33);
        } else if (dealSoundCount_ < 7) {
            // $E42 waits on CODE 1 $B22 before exposing each player pair;
            // $EE4 then fires snd 5044. The first pass consequently waits for
            // the opening host line instead of dealing underneath it.
            if (!context_.audio.directSoundBusy()) {
                context_.audio.playEffect(5044);
                ++dealSoundCount_;
                if (dealSoundCount_ < 7) dealDelayMilliseconds_ = 66;
            }
        } else if (!context_.audio.directSoundBusy()) {
            // $F2A applies the same gate after the seventh pair.
            dealComplete_ = true;
            if (pendingComputerOpening_ >= 0) {
                status_ = L"I place the first doublet.";
            } else {
                status_ = L"You go first.";
            }
            host_.play(openingMovie_, 14, 5);
        }
    }
    if (dealComplete_ && pendingComputerOpening_ >= 0 && !host_.active()) {
        const std::size_t index = static_cast<std::size_t>(pendingComputerOpening_);
        chain_.push_back(computer_[index]);
        computer_.erase(computer_.begin() + static_cast<std::ptrdiff_t>(index));
        if (!context_.audio.directSoundBusy()) context_.audio.playEffect(5043);
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

int DominoesGame::sourceHumanHandCapacity() const noexcept {
    // Macintosh CODE 14 creates fourteen hand controls at $5160. DOS
    // overlay 12 creates sixteen records in its corresponding initializer.
    return dosEdition() ? 16 : 14;
}

Rect DominoesGame::humanTileRect(std::size_t index) const noexcept {
    const int sourceIndex = static_cast<int>(index);
    if (dosEdition()) {
        // DOS overlay 12: left=3+17*i, right=left+16, top=154, bottom=187.
        const int left = 3 + 17 * sourceIndex;
        return {left, 154, left + 16, 187};
    }
    // Macintosh CODE 14 $5160: left=6+32*i, right=left+30,
    // top=289, bottom=351.
    const int left = 6 + 32 * sourceIndex;
    return {left, 289, left + 30, 351};
}

void DominoesGame::forcePlayableNextHumanDraw() {
    if (boneyard_.empty() || chain_.empty()) return;
    const int leftEnd = chain_.front().left;
    const int rightEnd = chain_.back().right;
    const auto match = std::find_if(boneyard_.begin(), boneyard_.end(),
        [leftEnd, rightEnd](const Tile& tile) {
            return tile.left == leftEnd || tile.right == leftEnd ||
                   tile.left == rightEnd || tile.right == rightEnd;
        });
    if (match != boneyard_.end()) std::iter_swap(match, boneyard_.end() - 1);
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

bool DominoesGame::sourceOpeningRegressionTest() const {
    constexpr std::array expectedMovies{10002, 10003, 10004, 10065,
                                        10084, 10005, 10006, 10008};
    const bool routesMatch =
        dominoDealMovie(false, 50) == 10002 &&
        dominoDealMovie(false, 51) == 10003 &&
        dominoDealMovie(true, 50) == 10004 &&
        dominoDealMovie(true, 51) == 10065 &&
        dominoOpeningMovie(true, 74) == 10084 &&
        dominoOpeningMovie(true, 75) == 10005 &&
        dominoOpeningMovie(false, 49) == 10006 &&
        dominoOpeningMovie(false, 50) == 10008;
    return routesMatch && std::ranges::all_of(expectedMovies, [&](int movieId) {
        return context_.assets.contains("MuV ", movieId) &&
               context_.assets.contains("Ply ", movieId) &&
               Movie(context_.assets, movieId).resolved();
    });
}

bool DominoesGame::sourceDealPresentationRegressionTest() {
    host_.stop();
    dealComplete_ = false;
    dealDelayMilliseconds_ = 0;
    dealSoundCount_ = 0;

    std::array<int, 7> visibleCounts{};
    std::array<int, 7> transitionTicks{};
    int transitionCount = 0;
    for (int tickIndex = 0; tickIndex < 64 && !dealComplete_; ++tickIndex) {
        const int before = dealSoundCount_;
        if (!tick()) return false;
        if (dealSoundCount_ == before) continue;
        if (dealSoundCount_ != before + 1 || transitionCount >= 7) return false;
        const std::size_t transition = static_cast<std::size_t>(transitionCount++);
        visibleCounts[transition] = dealSoundCount_;
        transitionTicks[transition] = tickIndex;
    }
    return dealComplete_ && transitionCount == 7 &&
           visibleCounts == std::array<int, 7>{1, 2, 3, 4, 5, 6, 7} &&
           transitionTicks == std::array<int, 7>{0, 3, 6, 9, 12, 15, 18};
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
        if (context_.audio.directSoundBusy()) return false;
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
        outcomePhase_ = OutcomePhase::ChainResetWait;
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
            outcomePhase_ = OutcomePhase::ChainResetWait;
        } else {
            outcomePhase_ = OutcomePhase::ReplayPrompt;
        }
        return true;
    case OutcomePhase::ChainResetWait:
        // CODE 14 $1528 drains snd 5023. $1538 then switches to SONG 135
        // and starts movie 3900 for every source result 2, including a
        // blocked-hand player win; it is not limited to the last-tile path.
        if (context_.audio.directSoundBusy()) return false;
        context_.audio.playMusic(audio_catalog::playerWinMusic(dosEdition(), 1));
        // Macintosh CODE 14 derives (231,-13) from the window origin. DOS
        // overlay 12 stores the Point as v/h and derives (-2,-17).
        playerResultAnimation_.play(3900, dosEdition() ? -2 : 231,
                                    dosEdition() ? -17 : -13);
        outcomePhase_ = OutcomePhase::PlayerResultAnimation;
        return true;
    case OutcomePhase::PlayerResultAnimation:
        if (playerResultAnimation_.active()) return false;
        // $15A4 installs the source three-count post-movie hold.
        outcomeDelayTicks_ = 3;
        outcomePhase_ = OutcomePhase::PlayerResultDelay;
        return true;
    case OutcomePhase::PlayerResultDelay:
        if (outcomeDelayTicks_-- > 0) return true;
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

bool DominoesGame::sourceFullMatchRegressionTest() {
    const std::uint32_t savedSeed = context_.random.seed();
    const auto fail = [&]() {
        context_.random.setSeed(savedSeed);
        return false;
    };
    bool sawHumanPlay = false;
    bool sawHumanDraw = false;
    bool sawMarioPlay = false;
    bool sawMarioDraw = false;
    bool sawLastDominoResult = false;

    const auto stateIsValid = [&]() {
        if (human_.size() + computer_.size() + boneyard_.size() + chain_.size() != 28 ||
            passes_ < 0 || passes_ > 2 || winner_ < -1 || winner_ > 2) return false;
        std::array<std::array<bool, 7>, 7> present{};
        const auto record = [&](std::span<const Tile> tiles) {
            for (const Tile& tile : tiles) {
                if (tile.left < 0 || tile.left > 6 || tile.right < 0 || tile.right > 6)
                    return false;
                const int low = std::min(tile.left, tile.right);
                const int high = std::max(tile.left, tile.right);
                if (present[static_cast<std::size_t>(low)][static_cast<std::size_t>(high)])
                    return false;
                present[static_cast<std::size_t>(low)][static_cast<std::size_t>(high)] = true;
            }
            return true;
        };
        if (!record(human_) || !record(computer_) || !record(boneyard_) || !record(chain_))
            return false;
        for (std::size_t index = 1; index < chain_.size(); ++index) {
            if (chain_[index - 1].right != chain_[index].left) return false;
        }
        return selected_ >= -1 && selected_ < static_cast<int>(human_.size()) &&
               draggedIndex_ >= -1 && draggedIndex_ < static_cast<int>(human_.size());
    };

    for (std::uint32_t seed = 1; seed <= 128; ++seed) {
        context_.random.setSeed(seed);
        reset();
        bool matchHadHumanPlay = false;
        bool matchHadMarioPlay = false;

        for (int controllerPass = 0; controllerPass < 20000 && !finished();
             ++controllerPass) {
            host_.stop();
            const std::size_t computerBefore = computer_.size();
            const std::size_t boneyardBefore = boneyard_.size();
            const std::size_t chainBefore = chain_.size();

            const bool playerCanAct = !winner_ && dealComplete_ &&
                pendingComputerOpening_ < 0 && !computerTurnPending_ &&
                !humanTurnCommentaryPending_ && !host_.active();
            if (playerCanAct) {
                std::size_t index = human_.size();
                if (requiredHumanOpening_ >= 0) {
                    index = static_cast<std::size_t>(requiredHumanOpening_);
                } else {
                    index = static_cast<std::size_t>(std::distance(
                        human_.begin(), std::find_if(human_.begin(), human_.end(),
                            [&](const Tile& tile) { return playable(tile); })));
                }

                if (index < human_.size()) {
                    Point drop{dosEdition() ? 160 : 256, dosEdition() ? 90 : 180};
                    if (!chain_.empty()) {
                        const Tile tile = human_[index];
                        const int leftEnd = chain_.front().left;
                        const int rightEnd = chain_.back().right;
                        const bool fitsLeft = tile.left == leftEnd || tile.right == leftEnd;
                        const bool fitsRight = tile.left == rightEnd || tile.right == rightEnd;
                        if (!fitsLeft && !fitsRight) return fail();
                        const int visible = std::min(30, static_cast<int>(chain_.size()));
                        const Rect endpoint = chainTileRect(fitsLeft ? 0 : visible - 1, visible);
                        drop = {(endpoint.left + endpoint.right) / 2,
                                (endpoint.top + endpoint.bottom) / 2};
                    }
                    const Rect handRect = humanTileRect(index);
                    const Point source{(handRect.left + handRect.right) / 2,
                                       (handRect.top + handRect.bottom) / 2};
                    const std::size_t humanBefore = human_.size();
                    mouseDown(source);
                    mouseMove(drop);
                    mouseUp(drop);
                    if (human_.size() + 1 != humanBefore || chain_.size() != chainBefore + 1)
                        return fail();
                    sawHumanPlay = true;
                    matchHadHumanPlay = true;
                } else {
                    const Point draw = dosEdition() ? Point{309, 168} : Point{494, 322};
                    const std::size_t humanBefore = human_.size();
                    const int passesBefore = passes_;
                    click(draw);
                    if (boneyardBefore != 0) {
                        if (human_.size() != humanBefore + 1 ||
                            boneyard_.size() + 1 != boneyardBefore) return fail();
                        sawHumanDraw = true;
                    } else if (passes_ != passesBefore + 1 && !winner_) {
                        return fail();
                    }
                }
            }

            (void)tick();
            if (computer_.size() + 1 == computerBefore && chain_.size() == chainBefore + 1) {
                sawMarioPlay = true;
                matchHadMarioPlay = true;
            }
            if (computer_.size() == computerBefore + 1 &&
                boneyard_.size() + 1 == boneyardBefore) sawMarioDraw = true;
            if (!stateIsValid()) return fail();
        }

        sawLastDominoResult |= outcomeKind_ == OutcomeKind::LastDomino;
        if (!finished() || winner_ == 0 || !matchHadHumanPlay || !matchHadMarioPlay ||
            !stateIsValid()) return fail();
    }

    context_.random.setSeed(savedSeed);
    return sawHumanPlay && sawHumanDraw && sawMarioPlay && sawMarioDraw &&
           sawLastDominoResult;
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
    playerResultAnimation_.stop();
    const int initialMusic = context_.audio.requestedMusicResourceId();
    beginOutcome(blocked ? OutcomeKind::Blocked : OutcomeKind::LastDomino);
    bool sawPlayerResultAnimation = false;
    bool musicChangedBeforePlayerResult = false;
    for (int tickIndex = 0; tickIndex < 2400 && !finished(); ++tickIndex) {
        (void)tick();
        const bool playerResultActive = playerResultAnimation_.active();
        if (!sawPlayerResultAnimation && !playerResultActive &&
            context_.audio.requestedMusicResourceId() != initialMusic) {
            musicChangedBeforePlayerResult = true;
        }
        sawPlayerResultAnimation |= playerResultActive;
    }
    if (!finished() || outcomeMoviesPlayed_.empty() ||
        (outcomeMoviesPlayed_.back() != 10073 && outcomeMoviesPlayed_.back() != 10074)) {
        return false;
    }
    const bool expectPlayerResult = expectedWinner == 1;
    if (musicChangedBeforePlayerResult || sawPlayerResultAnimation != expectPlayerResult ||
        context_.audio.requestedMusicResourceId() !=
            (expectPlayerResult ? audio_catalog::playerWinMusic(dosEdition(), 1)
                                : initialMusic)) {
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
    playerResultAnimation_.stop();
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

void DominoesGame::setQaPlayerResultPresentation(std::uint32_t sourceTime) {
    setQaOutcomePresentation(1, false);
    host_.stop();
    outcomePhase_ = OutcomePhase::PlayerResultAnimation;
    playerResultAnimation_.showFrame(3900, dosEdition() ? -2 : 231,
                                     dosEdition() ? -17 : -13, sourceTime);
    status_ = L"Congratulations! You win!";
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
    // $19C6 lets the move proceed while suppressing 5043 if the direct
    // channel is occupied; it does not overlap another guarded cue.
    if (!context_.audio.directSoundBusy()) context_.audio.playEffect(5043);
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
    const Rect drawButton = dosEdition()
        ? Rect{299, 149, 320, 188}
        : drawButton_;
    if (drawButton.contains(point)) {
        const std::size_t handCapacity =
            static_cast<std::size_t>(sourceHumanHandCapacity());
        // Macintosh CODE 14 $232E/$5744 and DOS overlay 12's equivalent
        // move a matching boneyard record into the next-draw slot when the
        // hand is one short of its edition-specific capacity. This prevents
        // an unplayable full hand from softlocking with bones still present.
        if (!boneyard_.empty() && human_.size() + 1 == handCapacity)
            forcePlayableNextHumanDraw();
        // $2350 reserves 9202 for Macintosh's fourteen-bone limit; the DOS
        // overlay performs the same check at sixteen records.
        if (!boneyard_.empty() && human_.size() >= handCapacity) {
            context_.audio.playSound(9202);
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
            status_ = playable(human_.back()) ? L"Here's your bone - you can play it." : L"Draw again or choose a playable bone.";
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
    const std::size_t visibleHand = std::min(
        human_.size(), static_cast<std::size_t>(sourceHumanHandCapacity()));
    for (std::size_t index = 0; index < visibleHand; ++index) {
        if (humanTileRect(index).contains(point)) {
            // $276C starts 5043 on mouse-down, then the source tracks the bone
            // until mouse-up and resolves the nearest chain endpoint.
            context_.audio.playEffect(5043);
            selected_ = draggedIndex_ = static_cast<int>(index);
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
        int acceptanceDistance = dosEdition() ? 94 : 150;
        if (chain_.size() > 6 && endpointDistance < acceptanceDistance) {
            acceptanceDistance = std::max(dosEdition() ? 38 : 60, endpointDistance / 2);
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
        if (chain_.size() > 6 && endpointDistance < (dosEdition() ? 38 : 60)) {
            const Tile& tile = human_[static_cast<std::size_t>(index)];
            const int leftEnd = chain_.front().left;
            const int rightEnd = chain_.back().right;
            const bool fitsLeft = tile.left == leftEnd || tile.right == leftEnd;
            const bool fitsRight = tile.left == rightEnd || tile.right == rightEnd;
            if (fitsLeft) preferLeft = true;
            else if (fitsRight) preferLeft = false;
        }
    } else if (point.y < (dosEdition() ? 34 : 65) ||
               point.y > (dosEdition() ? 148 : 285)) {
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

    const Rect firstHandRect = humanTileRect(0);
    const Point handPoint{(firstHandRect.left + firstHandRect.right) / 2,
                          (firstHandRect.top + firstHandRect.bottom) / 2};
    const auto endpoints = [this]() {
        const int visible = std::min(30, static_cast<int>(chain_.size()));
        const Rect left = chainTileRect(0, visible);
        const Rect right = chainTileRect(visible - 1, visible);
        return std::pair{
            Point{(left.left + left.right) / 2, (left.top + left.bottom) / 2},
            Point{(right.left + right.right) / 2, (right.top + right.bottom) / 2}};
    };
    const auto distance = [](Point first, Point second) {
        return std::max(std::abs(first.x - second.x),
                        std::abs(first.y - second.y));
    };
    const auto acceptanceDistance = [this, &endpoints, &distance]() {
        const auto [left, right] = endpoints();
        int result = dosEdition() ? 94 : 150;
        if (chain_.size() > 6 && distance(left, right) < result) {
            result = std::max(dosEdition() ? 38 : 60, distance(left, right) / 2);
        }
        return result;
    };

    prepare({{2, 3}, {3, 4}}, {{2, 5}, {0, 1}});
    auto [leftEndpoint, rightEndpoint] = endpoints();
    mouseDown(handPoint);
    mouseUp(rightEndpoint);
    const bool wrongEndRejected = human_.size() == 2 && chain_.size() == 2;
    mouseDown(handPoint);
    mouseMove(leftEndpoint);
    mouseUp(leftEndpoint);
    const bool correctEndAccepted = human_.size() == 1 && chain_.size() == 3 &&
        chain_.front().left == 5 && chain_.front().right == 2;

    // Short chains use the source's full 150-pixel Chebyshev threshold,
    // or its 320x200 coordinate equivalent in the DOS edition.
    prepare({{2, 3}, {3, 4}}, {{4, 5}});
    rightEndpoint = endpoints().second;
    const int shortRadius = acceptanceDistance();
    mouseDown(handPoint);
    mouseUp({rightEndpoint.x + shortRadius - 1, rightEndpoint.y});
    const bool shortChainRadiusAccepted = human_.empty() && chain_.back().right == 5;
    prepare({{2, 3}, {3, 4}}, {{4, 5}});
    rightEndpoint = endpoints().second;
    mouseDown(handPoint);
    mouseUp({rightEndpoint.x + shortRadius, rightEndpoint.y});
    const bool shortChainBoundaryRejected = human_.size() == 1 && chain_.size() == 2;

    // An exact positional tie takes the right endpoint at $29B8-$29E0.
    prepare({{2, 3}, {3, 4}}, {{2, 4}});
    std::tie(leftEndpoint, rightEndpoint) = endpoints();
    const Point tiePoint{(leftEndpoint.x + rightEndpoint.x) / 2,
                         leftEndpoint.y + distance(leftEndpoint, rightEndpoint)};
    mouseDown(handPoint);
    mouseUp(tiePoint);
    const bool tieChoosesRight = human_.empty() && chain_.size() == 3 &&
        chain_.back().left == 4 && chain_.back().right == 2;

    // For a wrapped chain, half the end-to-end separation is used and then
    // clamped to sixty source pixels (38 in the DOS coordinate space).
    std::vector<Tile> wrapped(19, Tile{2, 2});
    prepare(wrapped, {{2, 5}});
    leftEndpoint = endpoints().first;
    const int wrappedRadius = acceptanceDistance();
    mouseDown(handPoint);
    mouseUp({leftEndpoint.x + wrappedRadius - 1, leftEndpoint.y});
    const bool wrappedRadiusAccepted = human_.empty() && chain_.front().left == 5;
    prepare(std::move(wrapped), {{2, 5}});
    leftEndpoint = endpoints().first;
    mouseDown(handPoint);
    mouseUp({leftEndpoint.x + wrappedRadius, leftEndpoint.y});
    const bool wrappedBoundaryRejected = human_.size() == 1 && chain_.size() == 19;

    // At twenty-one bones the rendered ends overlap. The source ignores the
    // geometrically nearer end and uses the only endpoint matching the pips.
    std::vector<Tile> overlapping(21, Tile{2, 2});
    overlapping.back() = {2, 4};
    prepare(std::move(overlapping), {{4, 5}});
    leftEndpoint = endpoints().first;
    mouseDown(handPoint);
    mouseUp(leftEndpoint);
    const bool overlapUsesPips = human_.empty() && chain_.back().right == 5;

    prepare({{2, 3}, {3, 4}}, {{2, 5}});
    mouseDown(handPoint);
    mouseCancel();
    const bool captureLossRestoresBone = draggedIndex_ < 0 && selected_ < 0 &&
        human_.size() == 1 && chain_.size() == 2;

    return wrongEndRejected && correctEndAccepted && shortChainRadiusAccepted &&
           shortChainBoundaryRejected && tieChoosesRight && wrappedRadiusAccepted &&
           wrappedBoundaryRejected && overlapUsesPips && captureLossRestoresBone;
}

bool DominoesGame::sourceBoneyardHitboxRegressionTest() {
    host_.stop();
    dealComplete_ = true;
    pendingComputerOpening_ = -1;
    computerTurnPending_ = false;
    winner_ = 0;
    requiredHumanOpening_ = -1;
    chain_ = {{1, 2}};
    human_.assign(1, Tile{0, 0});
    boneyard_.assign(1, Tile{6, 6});
    const std::size_t humanBefore = human_.size();
    const std::size_t boneyardBefore = boneyard_.size();
    const Point button = dosEdition() ? Point{309, 168} : Point{494, 321};
    click(button);
    const bool ordinaryDraw = human_.size() == humanBefore + 1 &&
        boneyard_.size() + 1 == boneyardBefore;

    // At capacity minus one, the source scans the boneyard and swaps the
    // first endpoint match into its final active record (the next draw).
    host_.stop();
    const std::size_t capacity = static_cast<std::size_t>(sourceHumanHandCapacity());
    human_.assign(capacity - 1, Tile{3, 3});
    chain_ = {{1, 2}};
    boneyard_ = {{0, 0}, {1, 5}, {4, 4}};
    click(button);
    const bool forcedPlayableDraw = human_.size() == capacity &&
        boneyard_.size() == 2 && playable(human_.back()) &&
        human_.back().left == 1 && human_.back().right == 5;

    Canvas withFinalRecord(dosEdition() ? kDosLogicalWidth : kLogicalWidth,
                           dosEdition() ? kDosLogicalHeight : kLogicalHeight);
    render(withFinalRecord);
    const Rect finalRect = humanTileRect(capacity - 1);
    const std::uint64_t visibleFinalHash = withFinalRecord.pixelHash(finalRect);
    const Tile finalTile = human_.back();
    human_.pop_back();
    Canvas withoutFinalRecord(dosEdition() ? kDosLogicalWidth : kLogicalWidth,
                              dosEdition() ? kDosLogicalHeight : kLogicalHeight);
    render(withoutFinalRecord);
    const bool finalRecordRendered =
        visibleFinalHash != withoutFinalRecord.pixelHash(finalRect);
    human_.push_back(finalTile);

    // Every source record, including a newly drawn final record, must be
    // reachable through its edition-specific hit rectangle.
    host_.stop();
    mouseDown({(finalRect.left + finalRect.right) / 2,
               (finalRect.top + finalRect.bottom) / 2});
    const bool finalRecordHittable =
        draggedIndex_ == static_cast<int>(capacity - 1);
    mouseCancel();

    // A further draw is rejected without consuming a boneyard record.
    host_.stop();
    const std::size_t fullBoneyard = boneyard_.size();
    click(button);
    const bool fullHandRejected = human_.size() == capacity &&
        boneyard_.size() == fullBoneyard &&
        context_.audio.lastSampleRequestRoute() == SampleRequestRoute::Tracked &&
        context_.audio.requestedSoundResourceId() == 9202;
    return ordinaryDraw && forcedPlayableDraw && finalRecordRendered &&
           finalRecordHittable && fullHandRejected;
}

void DominoesGame::setQaDragPresentation() {
    host_.stop();
    dealComplete_ = true;
    dealDelayMilliseconds_ = 0;
    dealSoundCount_ = 7;
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
    status_ = L"Drag the 2-5 domino to the left end of the chain.";
}

void DominoesGame::setQaDrawnHandPresentation() {
    host_.stop();
    dealComplete_ = true;
    dealDelayMilliseconds_ = 0;
    dealSoundCount_ = 7;
    pendingComputerOpening_ = -1;
    computerTurnPending_ = false;
    humanTurnCommentaryPending_ = false;
    winner_ = 0;
    requiredHumanOpening_ = -1;
    selected_ = draggedIndex_ = -1;
    dragPoint_ = {};
    human_.clear();
    computer_.clear();
    boneyard_.clear();
    chain_.clear();

    std::vector<Tile> deck;
    for (int left = 0; left <= 6; ++left) {
        for (int right = left; right <= 6; ++right) deck.push_back({left, right});
    }
    const std::size_t capacity = static_cast<std::size_t>(sourceHumanHandCapacity());
    human_.assign(deck.begin(), deck.begin() + static_cast<std::ptrdiff_t>(capacity));
    chain_.push_back(deck[capacity]);
    const auto computerBegin = deck.begin() + static_cast<std::ptrdiff_t>(capacity + 1);
    const auto computerEnd = computerBegin + 7;
    computer_.assign(computerBegin, computerEnd);
    boneyard_.assign(computerEnd, deck.end());
    status_ = dosEdition()
        ? L"The DOS hand exposes all sixteen source records."
        : L"The Macintosh hand exposes all fourteen source records.";
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
    const int half = dosEdition() ? 17 : 28;
    if (vertical) {
        canvas.sprite(first, rect.left, rect.top, false);
        canvas.sprite(second, rect.left, rect.top + half, false);
    } else {
        canvas.sprite(first, rect.left, rect.top, false);
        canvas.sprite(second, rect.left + half, rect.top, false);
    }
    if (selected) {
        canvas.outlineRect({rect.left, rect.top,
                            rect.left + (vertical ? half : half * 2),
                            rect.top + (vertical ? half * 2 : half)},
                           rgb(255, 230, 0), dosEdition() ? 1 : 2);
    }
}

Rect DominoesGame::chainTileRect(int index, int visible) const {
    Rect result{};
    if (index < 8) {
        const int rowCount = std::min(visible, 8);
        const int startX = 256 - rowCount * 28;
        result = {startX + index * 56, 128, startX + (index + 1) * 56, 156};
    } else if (index < 11) {
        result = {452, 156 + (index - 8) * 56, 480, 212 + (index - 8) * 56};
    } else if (index < 19) {
        result = {424 - (index - 11) * 56, 246,
                  480 - (index - 11) * 56, 274};
    } else if (index < 22) {
        result = {32, 190 - (index - 19) * 56,
                  60, 246 - (index - 19) * 56};
    } else {
        result = {60 + (index - 22) * 56, 72,
                  116 + (index - 22) * 56, 100};
    }
    if (!dosEdition()) return result;
    return {dosX(result.left), dosY(result.top), dosX(result.right), dosY(result.bottom)};
}

void DominoesGame::render(Canvas& canvas) {
    canvas.clear(rgb(0, 0, 0));
    drawBackground(canvas, 3001);
    // The independently captured vanilla Macintosh scoreboard begins at
    // (11,11). The DOS panel begins at (7,15), below its nine-pixel menu bar.
    // Scaling an unverified shared anchor had clipped the DOS portrait under
    // that bar and left both editions' scoreboard up/left of the source pose.
    canvas.sprite(context_.graphics.sprite(3700),
                  dosEdition() ? 7 : 11, dosEdition() ? 15 : 11, false);
    if (characterChooser_ || !host_.render(canvas)) {
        canvas.sprite(context_.graphics.sprite(10000),
                      dosEdition() ? 17 : 27, dosEdition() ? 19 : 19, false);
    }
    (void)playerResultAnimation_.render(canvas);
    const int visibleDealCount = std::clamp(dealSoundCount_, 0, 7);
    const int visibleComputerCount = dealComplete_
        ? static_cast<int>(computer_.size())
        : std::min(visibleDealCount, static_cast<int>(computer_.size()));
    if (!characterChooser_ && computer_.size() <= 28) {
        canvas.sprite(context_.graphics.sprite(3701, visibleComputerCount),
                      dosEdition() ? 31 : 50, dosEdition() ? 40 : 77, false);
    }
    const int visible = characterChooser_ || !dealComplete_
                            ? 0 : std::min(30, static_cast<int>(chain_.size()));
    const int start = std::max(0, static_cast<int>(chain_.size()) - visible);
    for (int index = 0; index < visible; ++index) {
        drawTile(canvas, chain_[start + index], chainTileRect(index, visible));
    }
    const std::size_t visibleHumanCount = dealComplete_
        ? std::min(human_.size(), static_cast<std::size_t>(sourceHumanHandCapacity()))
        : std::min(human_.size(), static_cast<std::size_t>(visibleDealCount));
    if (!characterChooser_) for (std::size_t index = 0;
         index < visibleHumanCount; ++index) {
        if (static_cast<int>(index) == draggedIndex_) continue;
        drawTile(canvas, human_[index], humanTileRect(index),
                 selected_ == static_cast<int>(index));
    }
    if (!characterChooser_ && draggedIndex_ >= 0 &&
        draggedIndex_ < static_cast<int>(human_.size())) {
        const int x = std::clamp(dragPoint_.x, dosEdition() ? 9 : 14,
                                 dosEdition() ? 311 : 498);
        const int y = std::clamp(dragPoint_.y, dosEdition() ? 15 : 28,
                                 dosEdition() ? 172 : 330);
        drawTile(canvas, human_[static_cast<std::size_t>(draggedIndex_)],
                 {x - (dosEdition() ? 9 : 14), y - (dosEdition() ? 17 : 28),
                  x + (dosEdition() ? 8 : 14), y + (dosEdition() ? 17 : 28)}, true);
    }
    if (!characterChooser_)
        canvas.sprite(context_.graphics.sprite(3100, 9),
                      dosEdition() ? 299 : 478, dosEdition() ? 149 : 286, false);
    if (!characterChooser_ && boneyard_.size() <= 28) {
        const Sprite& count = context_.graphics.sprite(
            3701, dealComplete_ ? static_cast<int>(boneyard_.size())
                                : 28 - visibleDealCount * 2);
        canvas.sprite(count, (dosEdition() ? 309 : 494) - count.width / 2,
                      dosEdition() ? 173 : 332, false);
    }
    canvas.pakText(context_.graphics,
                   characterChooser_ ? L"Do you want to play as a Yoshi, or as a Koopa?" : status_,
                   224, dosEdition() ? Rect{3, 188, 317, 200} : Rect{5, 359, 507, 383});
}

}  // namespace mf
