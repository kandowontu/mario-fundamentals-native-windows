#include "games/go_fish.hpp"

#include "audio_catalog.hpp"

namespace mf {

namespace {

constexpr std::array<Point, 7> victoryLetterPositions{{
    {161, 200}, {220, 200}, {279, 200},
    {135, 281}, {194, 281}, {253, 281}, {313, 281},
}};

// CODE 17 $371A-$3852 builds thirteen persistent hand records. Pak 5005 is
// 54x76; the source adds five pixels to its width and uses that 59-pixel pitch.
// Records 0..6 form the lower row, but the static source map rotates their
// physical positions: records 0..5 occupy x=104..399 and record 6 wraps to
// x=45. The overflow row uses the same rotation.
constexpr std::array<Point, 13> humanSlotPositions{{
    {104, 281}, {163, 281}, {222, 281}, {281, 281},
    {340, 281}, {399, 281}, {45, 281},
    {133, 200}, {192, 200}, {251, 200},
    {310, 200}, {369, 200}, {74, 200},
}};
constexpr int humanCardWidth = 54;
constexpr int humanCardHeight = 76;

std::vector<int> makeSourceDeck(SourceRandom& random) {
    std::vector<int> deck(52);
    for (int card = 0; card < 52; ++card) deck[static_cast<std::size_t>(card)] = card;
    // CODE 17 $467A performs 300 pairs of 5200/100 index draws and swaps;
    // it is deliberately not the shared CODE 13 Fisher-Yates helper.
    for (int swap = 0; swap < 300; ++swap) {
        const std::size_t first = random.below(5200) / 100;
        const std::size_t second = random.below(5200) / 100;
        std::swap(deck[first], deck[second]);
    }
    return deck;
}

}  // namespace

GoFishGame::GoFishGame(GameContext context)
    : Game(context), host_(context.assets, context.graphics, context.audio, true,
          [](int resourceId, Point scaled) {
              if (resourceId >= 10000) return Point{121, 1};
              if (resourceId == 5090) return Point{114, 16};
              if (resourceId == 5091) return Point{28, 6};
              if (resourceId == 5094) return Point{25, 4};
              return scaled;
          }) {
    for (int index = 0; index < 7; ++index) {
        victoryCardFlips_.push_back(std::make_unique<HostAnimation>(
            context.assets, context.graphics, context.audio));
    }
    reset();
}

std::wstring GoFishGame::rankName(int value) {
    // Pak 5005 is the original thirteen-card character deck, not a conventional
    // ace-through-king deck.
    static const std::array names{
        L"Goombas", L"Piranha Plants", L"Rexes", L"Bowsers", L"Princesses",
        L"Luigis", L"Koopas", L"Marios", L"Little Toadies", L"Rip Van Fish",
        L"Yoshis", L"Big Boos", L"Bob-ombs"};
    return names[static_cast<std::size_t>(value)];
}

int GoFishGame::questionMovie(int value, int variant) {
    // CODE 17 $151A has two grammatically different suffixes for every rank.
    // Variant one follows the four common request lead-ins; variant two follows
    // the less-common "Do you have any..." branch.
    static constexpr std::array variantOne{
        11506, 11503, 11507, 11508, 11505, 11511, 11501,
        11504, 11510, 11512, 11500, 11502, 11509};
    static constexpr std::array variantTwo{
        11519, 11516, 11520, 11521, 11518, 11524, 11514,
        11517, 11523, 11525, 11513, 11515, 11522};
    return (variant == 1 ? variantOne : variantTwo)[static_cast<std::size_t>(value)];
}

int GoFishGame::drawPool(std::vector<int>& movies, std::size_t& cursor) {
    if (cursor >= movies.size()) {
        sourceShuffle(movies, context_.random);
        cursor = 0;
    }
    return movies[cursor++];
}

void GoFishGame::beginConversation(std::span<const int> movies) {
    playConversation(host_, -11, 39, movies);
}

void GoFishGame::appendConversation(std::span<const int> movies) {
    if (directSpeechMilliseconds_ != 0) {
        moviesAfterDirectSpeech_.insert(
            moviesAfterDirectSpeech_.end(), movies.begin(), movies.end());
        return;
    }
    if (!host_.active()) {
        beginConversation(movies);
        return;
    }
    for (int movie : movies) host_.queue(movie);
}

void GoFishGame::beginAfterDirectSpeech(int sound, unsigned milliseconds,
                                        std::span<const int> movies) {
    context_.audio.playEffect(sound);
    directSpeechMilliseconds_ = milliseconds;
    moviesAfterDirectSpeech_.assign(movies.begin(), movies.end());
}

void GoFishGame::reset() {
    const bool initializeModuleState = !moduleStateInitialized_;
    resetSourceIdle();
    human_.clear();
    computer_.clear();
    deck_ = makeSourceDeck(context_.random);
    // The source deals contiguous positions 0..6 and 7..13, then advances
    // through positions 14..51. Reverse only the remaining native vector so
    // its existing pop_back draw path consumes that same forward order.
    human_.insert(human_.end(), deck_.begin(), deck_.begin() + 7);
    computer_.insert(computer_.end(), deck_.begin() + 7, deck_.begin() + 14);
    initializeHumanHandSlots(human_);
    deck_.erase(deck_.begin(), deck_.begin() + 14);
    std::reverse(deck_.begin(), deck_.end());
    humanBooks_ = computerBooks_ = winner_ = 0;
    outcomeDelayTicks_ = victoryLetterCount_ = 0;
    victoryMusicStarted_ = false;
    outcomePhase_ = OutcomePhase::None;
    removeBooks(human_, humanBooks_, true); removeBooks(computer_, computerBooks_);
    humanTurn_ = false;
    pendingComputerRank_ = -1;
    computerTurnWaiting_ = false;
    openingFirstSpeechPlaying_ = false;
    directSpeechMilliseconds_ = 0;
    moviesAfterDirectSpeech_.clear();
    humanQuestionMemory_.fill(99);
    computerQuestionHistory_.fill(99);
    lastComputerRank_ = 99;
    marioHasFished_ = false;
    if (initializeModuleState) {
        openingPoolCursor_ = openingPool_.size();
        prefixPoolCursor_ = prefixPool_.size();
        thinkingPoolCursor_ = thinkingPool_.size();
        humanSuccessPoolCursor_ = humanSuccessPool_.size();
        marioSuccessPoolCursor_ = marioSuccessPool_.size();
        marioFishingPoolCursor_ = marioFishingPool_.size();
        humanTurnPoolCursor_ = humanTurnPool_.size();
        praisePoolCursor_ = praisePool_.size();
        frustrationPoolCursor_ = frustrationPool_.size();
        idlePoolCursor_ = idlePool_.size();
        idleVisualPoolCursor_ = idleVisualPool_.size();
        marioWinPoolCursor_ = marioWinPool_.size();
        humanWinPoolCursor_ = humanWinPool_.size();
        idleAlternationCounter_ = 0;
    }

    // CODE 17 chooses one greeting from this shuffled pool, performs the deal,
    // then always says "I'm-a go first!" before Mario's first question.
    openingDelayMilliseconds_ = 4000;
    openingDealSoundDelayMilliseconds_ = openingDealSoundCount_ = 0;
    const int greeting = drawPool(openingPool_, openingPoolCursor_);
    status_ = greeting == 11600 ? L"Nice to see you again!" :
              greeting == 11603 ? L"Let's play!" :
              greeting == 11003 ? L"I'm so happy to see you again!" : L"Good luck!";
    const std::array movies{greeting};
    beginConversation(movies);
    moduleStateInitialized_ = true;
}

void GoFishGame::resetSourceIdle() {
    idleElapsedSourceTicks_ = 0;
    idlePhase_ = IdlePhase::Waiting;
}

void GoFishGame::cancelSourceIdle() {
    if (idlePhase_ != IdlePhase::Waiting) host_.stop();
    resetSourceIdle();
}

bool GoFishGame::tickSourceIdle(bool eligible) {
    // CODE 17 decrements its 120-count idle timer at the classic 60 Hz
    // TickCount cadence; the native controller is called every 33 ms.
    constexpr unsigned sourceTicksPerNativeTick = 2;
    constexpr unsigned sourceIdleDelay = 120;

    switch (idlePhase_) {
    case IdlePhase::Waiting: {
        if (!eligible) {
            idleElapsedSourceTicks_ = 0;
            return false;
        }
        idleElapsedSourceTicks_ += sourceTicksPerNativeTick;
        if (idleElapsedSourceTicks_ < sourceIdleDelay || host_.active()) return false;
        idleElapsedSourceTicks_ = 0;

        // $1B92 alternates state 5 (the two-line $CD4 selector) with state 0
        // (the five-entry $C66 visual selector), beginning with a line.
        const bool chooseLine = (idleAlternationCounter_++ & 1U) == 0;
        if (chooseLine) {
            const int movie = drawPool(idlePool_, idlePoolCursor_);
            status_ = movie == 11561 ? L"Hey, what's taking so long?"
                                     : L"You still want to play?";
            host_.play(movie, -11, 39);
            idlePhase_ = IdlePhase::Line;
            return true;
        }

        const int movie = drawPool(idleVisualPool_, idleVisualPoolCursor_);
        // Two source entries are the deliberately absent movie 999.  The
        // original controller simply restores its cursor and schedules the
        // next 120-count wait when either one is selected.
        if (movie == 999) {
            idlePhase_ = IdlePhase::Waiting;
            return true;
        }
        const int yOffset = movie == 5094 ? 8 : movie == 5091 ? 3 : -8;
        host_.play(movie, 0, yOffset);
        idlePhase_ = IdlePhase::Visual;
        return true;
    }
    case IdlePhase::Visual:
    case IdlePhase::Line:
        if (host_.active()) return false;
        resetSourceIdle();
        return true;
    }
    return false;
}

std::vector<int> GoFishGame::visibleHumanRanks() const {
    std::vector<int> ranks;
    for (int card : human_) {
        const int value = rank(card);
        if (std::find(ranks.begin(), ranks.end(), value) == ranks.end()) ranks.push_back(value);
    }
    return ranks;
}

void GoFishGame::addHumanRankToSlots(HumanHandSlots& slots, int value, int count) {
    if (count <= 0) return;
    const auto existing = std::find_if(slots.begin(), slots.end(), [&](const HumanHandSlot& slot) {
        return slot.count > 0 && slot.rank == value;
    });
    if (existing != slots.end()) {
        existing->count += count;
        return;
    }
    const auto empty = std::find_if(slots.begin(), slots.end(), [](const HumanHandSlot& slot) {
        return slot.count == 0;
    });
    if (empty != slots.end()) *empty = HumanHandSlot{value, count};
}

void GoFishGame::clearHumanRankFromSlots(HumanHandSlots& slots, int value) {
    for (HumanHandSlot& slot : slots) {
        if (slot.count > 0 && slot.rank == value) slot = {};
    }
}

void GoFishGame::consolidateOpeningSlots(HumanHandSlots& slots) {
    // $30F6 searches the seven opening records from left to right. A later
    // duplicate is merged into the first record; its own slot becomes a hole.
    for (std::size_t first = 0; first < 7; ++first) {
        if (slots[first].count == 0) continue;
        for (std::size_t later = first + 1; later < 7; ++later) {
            if (slots[later].count == 0 || slots[later].rank != slots[first].rank) continue;
            slots[first].count += slots[later].count;
            slots[later] = {};
        }
    }
    // The following $3862 state runs until stable during the opening. It moves
    // surviving first occurrences into the earliest records, while record 6's
    // rotated x=45 position remains the wraparound end of the row.
    std::size_t destination = 0;
    for (std::size_t source = 0; source < 7; ++source) {
        if (slots[source].count == 0) continue;
        if (source != destination) {
            slots[destination] = slots[source];
            slots[source] = {};
        }
        ++destination;
    }
}

void GoFishGame::initializeHumanHandSlots(std::span<const int> cards) {
    humanHandSlots_.fill({});
    openingHumanRanks_.fill(-1);
    const std::size_t openingCount = std::min<std::size_t>(7, cards.size());
    for (std::size_t index = 0; index < openingCount; ++index) {
        const int value = rank(cards[index]);
        openingHumanRanks_[index] = value;
        humanHandSlots_[index] = HumanHandSlot{value, 1};
    }
    consolidateOpeningSlots(humanHandSlots_);
}

void GoFishGame::addHumanCardsToDisplay(std::span<const int> cards) {
    for (int card : cards) addHumanRankToSlots(humanHandSlots_, rank(card));
}

void GoFishGame::clearHumanRankDisplay(int value) {
    clearHumanRankFromSlots(humanHandSlots_, value);
}

int GoFishGame::humanRankAtPoint(const HumanHandSlots& slots, Point point) const {
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (slots[index].count == 0) continue;
        Point position = humanSlotPositions[index];
        int width = humanCardWidth;
        int height = humanCardHeight;
        if (dosEdition()) {
            position = {dosX(position.x), dosY(position.y)};
            const Sprite& card = context_.graphics.sprite(5005, 0);
            width = card.width;
            height = card.height;
        }
        if (Rect{position.x, position.y, position.x + width,
                 position.y + height}.contains(point)) {
            return slots[index].rank;
        }
    }
    return -1;
}

int GoFishGame::removeBooks(std::vector<int>& hand, int& books, bool updateHumanDisplay) {
    int made = 0;
    for (int value = 0; value < 13; ++value) {
        if (std::count_if(hand.begin(), hand.end(), [&](int card) { return rank(card) == value; }) == 4) {
            std::erase_if(hand, [&](int card) { return rank(card) == value; });
            if (updateHumanDisplay) clearHumanRankDisplay(value);
            ++books; ++made;
        }
    }
    return made;
}

void GoFishGame::rememberHumanQuestion(int requestedRank) {
    if (std::find(humanQuestionMemory_.begin(), humanQuestionMemory_.end(), requestedRank) !=
        humanQuestionMemory_.end()) return;
    const auto empty = std::find(humanQuestionMemory_.begin(), humanQuestionMemory_.end(), 99);
    if (empty != humanQuestionMemory_.end()) *empty = requestedRank;
}

int GoFishGame::sourceSelectComputerRank(
    std::span<const int> handRanks,
    std::array<int, 200>& humanQuestionMemory,
    std::array<int, 30>& computerQuestionHistory,
    int& lastComputerRank,
    SourceRandom& random) {
    if (handRanks.empty()) return -1;

    const auto held = [&](int value) {
        return std::find(handRanks.begin(), handRanks.end(), value) != handRanks.end();
    };
    const auto askedBefore = [&](int value) {
        return std::find(computerQuestionHistory.begin(), computerQuestionHistory.end(), value) !=
               computerQuestionHistory.end();
    };
    const auto randomCardIndex = [&]() {
        return static_cast<std::size_t>(random.below(
            static_cast<std::uint16_t>(handRanks.size() * 100U)) / 100U);
    };

    // The shipped difficulty value is two.  That branch calls Random with
    // the adjacent memory-count word, which is explicitly cleared and never
    // incremented: Random(0) returns zero but still advances randSeed.  The
    // resulting scan therefore begins at memory slot zero every time.
    (void)random.below(0);
    int selected = -1;
    for (int remembered : humanQuestionMemory) {
        if (remembered != 99 && held(remembered) && !askedBefore(remembered)) {
            selected = remembered;
            break;
        }
    }
    // $496E gives a one-card hand priority after the memory scan, rather than
    // bypassing the source's otherwise-discarded Random(0) call.
    if (handRanks.size() == 1) selected = handRanks.front();
    if (selected < 0) {
        const std::size_t first = randomCardIndex();
        for (std::size_t offset = 0; offset < handRanks.size(); ++offset) {
            const int candidate = handRanks[(first + offset) % handRanks.size()];
            if (!askedBefore(candidate)) {
                selected = candidate;
                break;
            }
        }
    }
    if (selected < 0) {
        selected = handRanks[randomCardIndex()];
    }

    // CODE 17 records the preliminary choice before its same-as-last retry.
    const auto historySlot = std::find(
        computerQuestionHistory.begin(), computerQuestionHistory.end(), 99);
    if (historySlot != computerQuestionHistory.end()) *historySlot = selected;
    if (selected == lastComputerRank) {
        for (int retry = 0; retry < 5 && selected == lastComputerRank; ++retry)
            selected = handRanks[randomCardIndex()];
    }
    lastComputerRank = selected;
    const auto remembered = std::find(
        humanQuestionMemory.begin(), humanQuestionMemory.end(), selected);
    if (remembered != humanQuestionMemory.end()) *remembered = 99;
    return selected;
}

int GoFishGame::chooseComputerRank() {
    std::vector<int> ranks;
    ranks.reserve(computer_.size());
    for (int card : computer_) ranks.push_back(rank(card));
    std::sort(ranks.begin(), ranks.end());
    return sourceSelectComputerRank(ranks, humanQuestionMemory_, computerQuestionHistory_,
                                    lastComputerRank_, context_.random);
}

bool GoFishGame::sourceStrategyRegressionTest() const {
    SourceRandom deckRandom(1);
    const auto sourceDeck = makeSourceDeck(deckRandom);
    constexpr std::array<int, 14> expectedDeal{
        12, 10, 14, 2, 17, 43, 25, 5, 26, 50, 8, 13, 7, 3};
    if (!std::equal(expectedDeal.begin(), expectedDeal.end(), sourceDeck.begin()) ||
        rank(0) != 0 || rank(3) != 0 || rank(4) != 1 || rank(51) != 12) {
        return false;
    }

    std::array<int, 200> memory;
    std::array<int, 30> history;
    memory.fill(99);
    history.fill(99);
    memory[0] = 5;
    int last = 99;
    SourceRandom random(0x17);
    const std::array ranks{2, 2, 5};
    if (sourceSelectComputerRank(ranks, memory, history, last, random) != 5 ||
        history[0] != 5 || memory[0] != 99 || random.seed() != 386561U) return false;

    memory.fill(99);
    history.fill(99);
    memory[0] = 5;
    history[0] = 5;
    last = 99;
    if (sourceSelectComputerRank(ranks, memory, history, last, random) != 2 ||
        history[1] != 2 || random.seed() != 813729680U) return false;

    memory.fill(99);
    history.fill(99);
    last = 99;
    SourceRandom oneCardRandom(1);
    const std::array oneRank{7};
    return sourceSelectComputerRank(oneRank, memory, history, last, oneCardRandom) == 7 &&
           history[0] == 7 && oneCardRandom.seed() == 16807U;
}

bool GoFishGame::sourceDialogueRegressionTest() const {
    const auto poolMatches = [](std::vector<int> actual, std::vector<int> expected) {
        std::sort(actual.begin(), actual.end());
        std::sort(expected.begin(), expected.end());
        return actual == expected;
    };
    // The live selectors are intentionally mutable and persist across replay,
    // so compare their recovered membership rather than their current order.
    if (!poolMatches(openingPool_, {11600, 11603, 11003, 11526}) ||
        !poolMatches(prefixPool_, {11575, 11576, 11545, 11557}) ||
        !poolMatches(thinkingPool_, {11543, 11544}) ||
        !poolMatches(humanSuccessPool_, {11539, 11540}) ||
        !poolMatches(marioSuccessPool_, {11558, 11550, 11577}) ||
        !poolMatches(marioFishingPool_, {11551, 11555, 11554}) ||
        !poolMatches(humanTurnPool_, {11530, 11422}) ||
        !poolMatches(praisePool_, {11610, 11607, 11608}) ||
        !poolMatches(frustrationPool_, {11629, 11635}) ||
        !poolMatches(idlePool_, {11561, 11562}) ||
        !poolMatches(idleVisualPool_, {5094, 5091, 5090, 999, 999}) ||
        !poolMatches(marioWinPool_, {11569, 11568, 11565, 11566}) ||
        !poolMatches(humanWinPool_, {11570, 11567})) {
        return false;
    }

    SourceRandom random(1);
    std::vector<int> lines{11561, 11562};
    std::vector<int> visuals{5094, 5091, 5090, 999, 999};
    std::size_t lineCursor = lines.size();
    std::size_t visualCursor = visuals.size();
    const auto draw = [&](std::vector<int>& pool, std::size_t& cursor) {
        if (cursor >= pool.size()) {
            sourceShuffle(pool, random);
            cursor = 0;
        }
        return pool[cursor++];
    };
    // $1B92 starts at the voiced half of the alternating controller.  The
    // fixed-seed vector also locks the two lazy-shuffle call counts.
    SourceRandom impossibleThinkingGuard(17);
    return impossibleThinkingGuard.below(1) == 0 &&
           impossibleThinkingGuard.seed() == 285719U &&
           draw(lines, lineCursor) == 11561 &&
           draw(visuals, visualCursor) == 5090 &&
           draw(lines, lineCursor) == 11562 &&
           draw(visuals, visualCursor) == 999 &&
           random.seed() == 1144108930U;
}

bool GoFishGame::sourceHandSlotRegressionTest() const {
    HumanHandSlots slots{};
    constexpr std::array openingRanks{7, 12, 8, 8, 5, 3, 12};
    for (std::size_t index = 0; index < openingRanks.size(); ++index)
        slots[index] = HumanHandSlot{openingRanks[index], 1};
    consolidateOpeningSlots(slots);
    if (slots[0].rank != 7 || slots[0].count != 1 ||
        slots[1].rank != 12 || slots[1].count != 2 ||
        slots[2].rank != 8 || slots[2].count != 2 ||
        slots[3].rank != 5 || slots[3].count != 1 ||
        slots[4].rank != 3 || slots[4].count != 1 ||
        slots[5].count != 0 || slots[6].count != 0) return false;

    // An existing rank stays in its original record. A new rank takes the
    // lowest inactive record, exactly as $308A scans the array.
    addHumanRankToSlots(slots, 12, 2);
    if (slots[1].count != 4) return false;
    clearHumanRankFromSlots(slots, 12);
    addHumanRankToSlots(slots, 6);
    if (slots[1].rank != 6 || slots[1].count != 1) return false;

    // Fill every remaining rank to force use of the source overflow row.
    slots.fill({});
    for (int value = 0; value < 13; ++value) addHumanRankToSlots(slots, value);
    if (slots[7].rank != 7 || slots[12].rank != 12 ||
        humanSlotPositions[0].x != 104 || humanSlotPositions[0].y != 281 ||
        humanSlotPositions[5].x != 399 || humanSlotPositions[5].y != 281 ||
        humanSlotPositions[6].x != 45 || humanSlotPositions[6].y != 281 ||
        humanSlotPositions[7].x != 133 || humanSlotPositions[7].y != 200 ||
        humanSlotPositions[12].x != 74 || humanSlotPositions[12].y != 200) return false;
    const auto cardRect = [this](std::size_t index) {
        Point position = humanSlotPositions[index];
        int width = humanCardWidth;
        int height = humanCardHeight;
        if (dosEdition()) {
            position = {dosX(position.x), dosY(position.y)};
            const Sprite& card = context_.graphics.sprite(5005, 0);
            width = card.width;
            height = card.height;
        }
        return Rect{position.x, position.y, position.x + width,
                    position.y + height};
    };
    const Rect overflowCard = cardRect(7);
    if (humanRankAtPoint(slots, {overflowCard.left, overflowCard.top}) != 7 ||
        humanRankAtPoint(slots, {overflowCard.right - 1, overflowCard.bottom - 1}) != 7 ||
        humanRankAtPoint(slots, {overflowCard.right, overflowCard.bottom - 1}) != -1) return false;
    clearHumanRankFromSlots(slots, 2);
    const Rect clearedCard = cardRect(2);
    return humanRankAtPoint(slots, {clearedCard.left, clearedCard.top}) == -1;
}

bool GoFishGame::sourceOpeningDealRegressionTest() {
    host_.stop();
    directSpeechMilliseconds_ = 0;
    moviesAfterDirectSpeech_.clear();
    openingFirstSpeechPlaying_ = false;
    openingDelayMilliseconds_ = 4000;
    openingDealSoundDelayMilliseconds_ = 0;
    openingDealSoundCount_ = 0;

    std::array<int, 7> visibleCounts{};
    int transitionCount = 0;
    for (int tickIndex = 0; tickIndex < 200 && openingDealSoundCount_ < 7; ++tickIndex) {
        const int before = openingDealSoundCount_;
        if (!tick()) return false;
        if (openingDealSoundCount_ == before) continue;
        if (openingDealSoundCount_ != before + 1 || transitionCount >= 7) return false;
        visibleCounts[static_cast<std::size_t>(transitionCount++)] = openingDealSoundCount_;
    }
    return transitionCount == 7 &&
           visibleCounts == std::array<int, 7>{1, 2, 3, 4, 5, 6, 7};
}

void GoFishGame::ask(bool human, int requestedRank) {
    context_.audio.playEffect(5010);
    auto& asking = human ? human_ : computer_;
    auto& giving = human ? computer_ : human_;
    int& books = human ? humanBooks_ : computerBooks_;
    const int booksBefore = books;
    std::vector<int> transferred;
    for (int card : giving) if (rank(card) == requestedRank) transferred.push_back(card);
    if (!transferred.empty()) {
        std::erase_if(giving, [&](int card) { return rank(card) == requestedRank; });
        asking.insert(asking.end(), transferred.begin(), transferred.end());
        if (human) addHumanCardsToDisplay(transferred);
        else clearHumanRankDisplay(requestedRank);
        const int made = removeBooks(asking, books, human);
        std::vector<int> movies;
        if (human) {
            const std::size_t count = transferred.size();
            if (count == 3) movies.push_back(drawPool(frustrationPool_, frustrationPoolCursor_));
            movies.push_back(count == 1 ? 11533 : count == 2 ? 11534 : 11535);
            if (made) movies.push_back(booksBefore == 0 ? 11531 : 11532);
            movies.push_back(drawPool(humanSuccessPool_, humanSuccessPoolCursor_));
        } else {
            if (made) movies.push_back(booksBefore == 0 ? 11547 : 11548);
            movies.push_back(drawPool(marioSuccessPool_, marioSuccessPoolCursor_));
        }
        beginConversation(movies);
        humanTurn_ = human;
        status_ = human ? L"You got what you wanted—you get another turn!" :
                          L"Mario got what he asked for and gets another turn!";
        if (made) status_ += human ? L" That's a book for you." : L" That's a book for Mario.";
        checkEnd();
        return;
    }

    if (deck_.empty()) {
        checkEnd();
        return;
    }
    const int drawn = deck_.back();
    deck_.pop_back();
    asking.push_back(drawn);
    if (human) addHumanCardsToDisplay(std::span{&drawn, std::size_t{1}});
    context_.audio.playEffect(5013);
    const bool gotRequestedRank = rank(drawn) == requestedRank;
    const int made = removeBooks(asking, books, human);
    std::vector<int> movies;

    if (human) {
        movies.push_back(11528);  // "Go Fish!"
        if (made) movies.push_back(booksBefore == 0 ? 11531 : 11532);
        if (gotRequestedRank) {
            movies.push_back(drawPool(humanSuccessPool_, humanSuccessPoolCursor_));
            humanTurn_ = true;
            status_ = L"You fished what you asked for—you get another turn!";
        } else {
            humanTurn_ = false;
            status_ = L"Go fish! Now it's Mario's turn.";
        }
        beginConversation(movies);
    } else {
        const int fishingMovie = marioHasFished_ ?
            drawPool(marioFishingPool_, marioFishingPoolCursor_) : 11551;
        marioHasFished_ = true;
        movies.push_back(fishingMovie);
        if (made) movies.push_back(booksBefore == 0 ? 11547 : 11548);
        if (gotRequestedRank) {
            movies.push_back(drawPool(marioSuccessPool_, marioSuccessPoolCursor_));
            humanTurn_ = false;
            status_ = L"Mario fished what he asked for and gets another turn!";
        } else {
            humanTurn_ = true;
            status_ = L"Mario went fishing. It's your turn.";
            if (humanBooks_ >= computerBooks_ + 2)
                movies.push_back(drawPool(praisePool_, praisePoolCursor_));
            movies.push_back(drawPool(humanTurnPool_, humanTurnPoolCursor_));
        }
        // This line is a standalone sound in the original resource stream.
        beginAfterDirectSpeech(26015, 1736, movies);
    }
    checkEnd();
}

void GoFishGame::appendHumanTurnAnnouncement() {
    std::vector<int> movies;
    if (humanBooks_ >= computerBooks_ + 2)
        movies.push_back(drawPool(praisePool_, praisePoolCursor_));
    movies.push_back(drawPool(humanTurnPool_, humanTurnPoolCursor_));
    appendConversation(movies);
}

void GoFishGame::computerTurn() {
    if (winner_ || humanTurn_ || pendingComputerRank_ >= 0 || computerTurnWaiting_ ||
        host_.active() || directSpeechMilliseconds_ != 0 || openingDelayMilliseconds_ != 0 ||
        openingFirstSpeechPlaying_) return;

    if (computer_.empty()) {
        if (deck_.empty()) { checkEnd(); return; }
        computer_.push_back(deck_.back());
        deck_.pop_back();
        context_.audio.playEffect(5013);
        const int booksBefore = computerBooks_;
        const int made = removeBooks(computer_, computerBooks_);
        std::vector<int> movies{11553};
        if (made) movies.push_back(booksBefore == 0 ? 11547 : 11548);
        beginConversation(movies);
        status_ = L"Mario needs another card.";
        computerTurnWaiting_ = true;
        checkEnd();
        return;
    }
    if (human_.empty()) {
        if (deck_.empty()) { checkEnd(); return; }
        human_.push_back(deck_.back());
        addHumanCardsToDisplay(std::span{&human_.back(), std::size_t{1}});
        deck_.pop_back();
        context_.audio.playEffect(5013);
        const int booksBefore = humanBooks_;
        const int made = removeBooks(human_, humanBooks_, true);
        std::vector<int> movies{11538};
        if (made) movies.push_back(booksBefore == 0 ? 11531 : 11532);
        beginConversation(movies);
        status_ = L"You need another card.";
        computerTurnWaiting_ = true;
        checkEnd();
        return;
    }

    // CODE 17 $11F8 calls the source range helper with a limit of one before
    // entering $484A.  Its result is necessarily zero, but QuickDraw Random
    // still advances the shared seed and therefore affects every later pool.
    (void)context_.random.below(1);
    pendingComputerRank_ = chooseComputerRank();
    if (pendingComputerRank_ < 0) { checkEnd(); return; }
    // The following `$1208` thinking-pool branch is guarded by that result
    // being nonzero.  Random(1) cannot produce a nonzero value, so indexes
    // 43/44 are authored but unreachable and must not be spoken or shuffled.
    std::vector<int> movies;
    const bool uncommonPrefix = context_.random.below(4) == 0;
    if (uncommonPrefix) {
        status_ = L"Do you have any " + rankName(pendingComputerRank_) + L"?";
        movies.push_back(11546);
        movies.push_back(questionMovie(pendingComputerRank_, 2));
    } else {
        const int prefixMovie = drawPool(prefixPool_, prefixPoolCursor_);
        const std::wstring phrase = prefixMovie == 11575 ? L"I'm looking for " :
                                    prefixMovie == 11576 ? L"I'd like to have your " :
                                    prefixMovie == 11545 ? L"Please give me your " :
                                                           L"I'm fishing for ";
        status_ = phrase + rankName(pendingComputerRank_) + L".";
        movies.push_back(prefixMovie);
        movies.push_back(questionMovie(pendingComputerRank_, 1));
    }
    beginConversation(movies);
}

bool GoFishGame::tick() {
    bool changed = host_.tick();
    for (const auto& flip : victoryCardFlips_) changed |= flip->tick();

    if (directSpeechMilliseconds_ != 0) {
        directSpeechMilliseconds_ = directSpeechMilliseconds_ > 33 ?
            directSpeechMilliseconds_ - 33 : 0;
        if (directSpeechMilliseconds_ == 0 && !moviesAfterDirectSpeech_.empty()) {
            const std::vector<int> movies = std::move(moviesAfterDirectSpeech_);
            moviesAfterDirectSpeech_.clear();
            beginConversation(movies);
        }
        changed = true;
    }

    if (openingDelayMilliseconds_ > 0 && !host_.active() && directSpeechMilliseconds_ == 0) {
        // Every source deal advances the visible hand.  Without marking these
        // controller ticks dirty, Windows presented the first card and then
        // skipped directly to the consolidated hand even though all seven
        // deal counters advanced internally.
        changed = true;
        if (openingDealSoundCount_ < 7) {
            if (openingDealSoundDelayMilliseconds_ <= 0) {
                // $7DA plays snd 5032 after each of the seven opening deals.
                context_.audio.playEffect(5032);
                ++openingDealSoundCount_;
                openingDealSoundDelayMilliseconds_ = 250;
            } else {
                openingDealSoundDelayMilliseconds_ =
                    std::max(0, openingDealSoundDelayMilliseconds_ - 33);
            }
        }
        openingDelayMilliseconds_ = std::max(0, openingDelayMilliseconds_ - 33);
        if (openingDelayMilliseconds_ == 0) {
            status_ = L"I'm-a go first!";
            const std::array movies{11529};
            beginConversation(movies);
            openingFirstSpeechPlaying_ = true;
            changed = true;
        }
    } else if (openingFirstSpeechPlaying_ && !host_.active()) {
        openingFirstSpeechPlaying_ = false;
        computerTurn();
        changed = true;
    } else if (pendingComputerRank_ >= 0 && !host_.active() &&
               directSpeechMilliseconds_ == 0) {
        const int requested = pendingComputerRank_;
        pendingComputerRank_ = -1;
        ask(false, requested);
        if (!winner_ && !humanTurn_) computerTurnWaiting_ = true;
        changed = true;
    } else if (computerTurnWaiting_ && !host_.active() && directSpeechMilliseconds_ == 0) {
        computerTurnWaiting_ = false;
        computerTurn();
        changed = true;
    }
    changed |= tickOutcome();
    const bool waitingForPlayer = !winner_ && humanTurn_ && pendingComputerRank_ < 0 &&
        !computerTurnWaiting_ && openingDelayMilliseconds_ == 0 &&
        !openingFirstSpeechPlaying_ && directSpeechMilliseconds_ == 0 && !host_.active();
    changed |= tickSourceIdle(waitingForPlayer);
    return changed;
}

bool GoFishGame::finished() const {
    return outcomePhase_ == OutcomePhase::Complete;
}

bool GoFishGame::tickOutcome() {
    if (outcomePhase_ == OutcomePhase::None || outcomePhase_ == OutcomePhase::Complete) {
        return false;
    }

    switch (outcomePhase_) {
    case OutcomePhase::Announcement:
        if (host_.active() || directSpeechMilliseconds_ != 0) return false;
        if (winner_ == 1) {
            // CODE 17 $209E-$2218 deals Pak 5211's seven Y-O-U-W-I-N-!
            // faces one at a time after the player's outcome line.
            context_.audio.playEffect(5013);
            outcomeDelayTicks_ = 5;
            victoryLetterCount_ = 0;
            victoryMusicStarted_ = false;
            outcomePhase_ = OutcomePhase::DealLetters;
        } else {
            // Mario-win and tie branches jump directly to state 30 after
            // their current speech has drained; they do not ask for replay.
            outcomeDelayTicks_ = 2;
            outcomePhase_ = OutcomePhase::NonPlayerDelay;
        }
        return true;
    case OutcomePhase::DealLetters:
        if (outcomeDelayTicks_-- > 0) return true;
        if (!victoryMusicStarted_) {
            // $2150 switches to SONG 137 before the fourteen-tick first-card transit.
            context_.audio.playMusic(dosEdition() ? audio_catalog::kDosPlayerWinMusic[3]
                                                  : audio_catalog::kPlayerWinMusic[3]);
            victoryMusicStarted_ = true;
            outcomeDelayTicks_ = 14;
            return true;
        }
        ++victoryLetterCount_;
        if (victoryLetterCount_ < 7) {
            outcomeDelayTicks_ = 16;  // 14 transit ticks and the two-tick placement pause
        } else {
            // $2424 starts seven copies of movie 5210 at these exact card
            // positions. They flip the completed message away together.
            victoryLetterCount_ = 0;
            for (std::size_t index = 0; index < victoryCardFlips_.size(); ++index) {
                victoryCardFlips_[index]->play(
                    5210, victoryLetterPositions[index].x,
                    victoryLetterPositions[index].y, false);
            }
            outcomeDelayTicks_ = 40;
            outcomePhase_ = OutcomePhase::FlipLetters;
        }
        return true;
    case OutcomePhase::FlipLetters:
        if (outcomeDelayTicks_-- > 0) return true;
        outcomeDelayTicks_ = 2;
        outcomePhase_ = OutcomePhase::PreReplayDelay;
        return true;
    case OutcomePhase::PreReplayDelay:
        if (outcomeDelayTicks_-- > 0) return true;
        host_.play(11571, -11, 39);
        outcomePhase_ = OutcomePhase::ReplayPrompt;
        return true;
    case OutcomePhase::ReplayPrompt:
        if (host_.active()) return false;
        outcomeDelayTicks_ = 2;
        outcomePhase_ = OutcomePhase::FinalDelay;
        return true;
    case OutcomePhase::NonPlayerDelay:
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

bool GoFishGame::sourceOutcomeRegressionTest(int expectedWinner) {
    if (expectedWinner != -1 && expectedWinner != 1 && expectedWinner != 2) return false;
    openingDelayMilliseconds_ = 0;
    openingFirstSpeechPlaying_ = false;
    deck_.clear();
    humanBooks_ = expectedWinner == 1 ? 8 : expectedWinner == -1 ? 4 : 6;
    computerBooks_ = expectedWinner == 1 ? 4 : expectedWinner == -1 ? 8 : 6;
    checkEnd();

    int largestLetterCount = 0;
    bool sawFlips = false;
    bool sawReplayPrompt = false;
    for (int tickIndex = 0; tickIndex < 1800 && !finished(); ++tickIndex) {
        (void)tick();
        largestLetterCount = std::max(largestLetterCount, victoryLetterCount_);
        sawFlips |= std::any_of(victoryCardFlips_.begin(), victoryCardFlips_.end(),
            [](const auto& flip) { return flip->active(); });
        sawReplayPrompt |= outcomePhase_ == OutcomePhase::ReplayPrompt;
    }
    const bool expectedCelebration = expectedWinner == 1;
    return winner_ == expectedWinner && finished() &&
           (largestLetterCount > 0) == expectedCelebration &&
           sawFlips == expectedCelebration && sawReplayPrompt == expectedCelebration;
}

void GoFishGame::setQaVictoryPresentation(int letterCount) {
    host_.stop();
    openingDelayMilliseconds_ = 0;
    openingFirstSpeechPlaying_ = false;
    directSpeechMilliseconds_ = 0;
    moviesAfterDirectSpeech_.clear();
    winner_ = 1;
    victoryLetterCount_ = std::clamp(letterCount, 0, 7);
    outcomeDelayTicks_ = 1000000;
    outcomePhase_ = OutcomePhase::DealLetters;
    status_ = L"Congratulations! You have more books than Mario.";
}

void GoFishGame::setQaHandSlotsPresentation(bool afterTransfer) {
    host_.stop();
    openingDelayMilliseconds_ = 0;
    openingDealSoundDelayMilliseconds_ = 0;
    openingDealSoundCount_ = 7;
    openingFirstSpeechPlaying_ = false;
    directSpeechMilliseconds_ = 0;
    moviesAfterDirectSpeech_.clear();
    pendingComputerRank_ = -1;
    computerTurnWaiting_ = false;
    winner_ = 0;
    outcomePhase_ = OutcomePhase::None;
    humanTurn_ = true;
    human_.clear();
    humanHandSlots_.fill({});
    constexpr std::array ranks{7, 12, 8, 5, 3};
    constexpr std::array counts{1, 2, 2, 1, 1};
    constexpr std::array slots{0, 1, 2, 3, 4};
    for (std::size_t index = 0; index < ranks.size(); ++index) {
        if (afterTransfer && ranks[index] == 5) continue;
        humanHandSlots_[slots[index]] = HumanHandSlot{ranks[index], counts[index]};
        for (int suit = 0; suit < counts[index]; ++suit)
            human_.push_back(ranks[index] * 4 + suit);
    }
    if (afterTransfer) {
        computer_.push_back(5 * 4);
        status_ = L"Do you have any Yoshis?";
    } else {
        status_ = L"Nice to see you again!";
    }
}

void GoFishGame::checkEnd() {
    if (winner_ || !deck_.empty()) return;
    winner_ = humanBooks_ == computerBooks_ ? 2 : (humanBooks_ > computerBooks_ ? 1 : -1);
    status_ = winner_ == 2 ? L"It's a tie!" : winner_ > 0 ?
              L"Congratulations! You have more books than Mario." :
              L"Mario has more books this time.";
    std::vector<int> movies;
    if (winner_ < 0) {
        movies.push_back(drawPool(marioWinPool_, marioWinPoolCursor_));
    } else if (winner_ > 0 && winner_ != 2) {
        movies.push_back(drawPool(humanWinPool_, humanWinPoolCursor_));
    }
    appendConversation(movies);
    outcomePhase_ = OutcomePhase::Announcement;
}

void GoFishGame::click(Point point) {
    if (winner_ || !humanTurn_ || human_.empty() || host_.active() ||
        directSpeechMilliseconds_ != 0 || openingDelayMilliseconds_ != 0 ||
        openingFirstSpeechPlaying_) return;
    cancelSourceIdle();
    const int requested = humanRankAtPoint(humanHandSlots_, point);
    if (requested < 0) return;
    rememberHumanQuestion(requested);
    status_ = L"Do you have any " + rankName(requested) + L"?";
    ask(true, requested);
    if (!winner_ && !humanTurn_) {
        if (host_.active()) computerTurnWaiting_ = true;
        else computerTurn();
    }
}

void GoFishGame::render(Canvas& canvas) {
    canvas.clear(rgb(0, 0, 0));
    drawBackground(canvas, 5001);
    // Pak 5300 is the fixed torso/hand layer.  The source composites the live
    // 11000/5090 head actor over it; drawing the torso afterward covered the
    // lower face with its red collar and looked like a duplicated head.
    canvas.sprite(context_.graphics.sprite(5300),
                  dosEdition() ? 119 : 191, dosEdition() ? 52 : 100, false);
    if (!host_.render(canvas))
        canvas.sprite(context_.graphics.sprite(5090),
                      dosEdition() ? 126 : 202, dosEdition() ? 9 : 18, false);
    canvas.sprite(context_.graphics.sprite(5100),
                  dosEdition() ? 217 : 347, dosEdition() ? 89 : 170, false);
    canvas.pakText(context_.graphics,
                   context_.playerName.empty() ? L"MY FRIEND" : context_.playerName,
                   223, dosEdition() ? Rect{225, 10, 313, 27} : Rect{360, 22, 499, 47},
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE, 11);
    canvas.pakText(context_.graphics, std::to_wstring(computerBooks_), 223,
                   dosEdition() ? Rect{65, 28, 94, 41} : Rect{104, 54, 151, 77},
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    canvas.pakText(context_.graphics, std::to_wstring(computer_.size()), 223,
                   dosEdition() ? Rect{65, 41, 94, 54} : Rect{104, 78, 151, 101},
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    canvas.pakText(context_.graphics, std::to_wstring(humanBooks_), 223,
                   dosEdition() ? Rect{282, 28, 312, 41} : Rect{451, 54, 498, 77},
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    canvas.pakText(context_.graphics, std::to_wstring(human_.size()), 223,
                   dosEdition() ? Rect{282, 41, 312, 54} : Rect{451, 78, 498, 101},
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const bool victoryPresentation = winner_ == 1 &&
        outcomePhase_ != OutcomePhase::Announcement &&
        outcomePhase_ != OutcomePhase::None;
    const bool showingOpeningDeal = openingDelayMilliseconds_ > 0 &&
        (openingDealSoundCount_ < 7 || openingDealSoundDelayMilliseconds_ > 0);
    if (!victoryPresentation && showingOpeningDeal) {
        const int visibleDeals = std::clamp(openingDealSoundCount_, 0, 7);
        for (int index = 0; index < visibleDeals; ++index) {
            const int value = openingHumanRanks_[static_cast<std::size_t>(index)];
            if (value < 0) continue;
            Point position = humanSlotPositions[static_cast<std::size_t>(index)];
            if (dosEdition()) position = {dosX(position.x), dosY(position.y)};
            canvas.sprite(context_.graphics.sprite(5005, value), position.x, position.y, false);
            // CODE 17 $3416/$349A/$3588/$36EC selects Pak 5006 frames
            // zero through three for counts one through four. These are the
            // authored red corner numerals, not system-font text.
            canvas.sprite(context_.graphics.sprite(5006, 0),
                          position.x, position.y, false);
        }
    } else if (!victoryPresentation) {
        for (std::size_t index = 0; index < humanHandSlots_.size(); ++index) {
            const HumanHandSlot& slot = humanHandSlots_[index];
            if (slot.count == 0) continue;
            Point position = humanSlotPositions[index];
            if (dosEdition()) position = {dosX(position.x), dosY(position.y)};
            canvas.sprite(context_.graphics.sprite(5005, slot.rank), position.x, position.y, false);
            canvas.sprite(context_.graphics.sprite(5006, std::clamp(slot.count, 1, 4) - 1),
                          position.x, position.y, false);
        }
    }
    if (!victoryPresentation && pendingComputerRank_ >= 0) {
        // Pak 5007 is the original small question card Mario holds beside his head.
        canvas.sprite(context_.graphics.sprite(5007, pendingComputerRank_),
                      dosEdition() ? 113 : 181, dosEdition() ? 18 : 34, false);
    }
    for (int index = 0; index < victoryLetterCount_; ++index) {
        Point position = victoryLetterPositions[static_cast<std::size_t>(index)];
        if (dosEdition()) position = {dosX(position.x), dosY(position.y)};
        canvas.sprite(context_.graphics.sprite(5211, index), position.x, position.y, false);
    }
    for (const auto& flip : victoryCardFlips_) (void)flip->render(canvas);
    canvas.pakText(context_.graphics, status_, 224,
                   dosEdition() ? Rect{3, 188, 317, 200} : Rect{5, 359, 507, 383});
}

}  // namespace mf
