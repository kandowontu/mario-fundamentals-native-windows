#include "games/backgammon.hpp"

#include "audio_catalog.hpp"

namespace mf {

namespace {

enum class BackgammonIdleKind {
    Visual,
    Line,
};

struct BackgammonIdleChoice {
    BackgammonIdleKind kind{};
    int resource{};
};

struct BackgammonSetupStack {
    int point{};
    int player{};
    int count{};
};

// CODE 11's six-byte records at A5-$3ACE are scanned from point zero upward.
// The source points map into the native logical board as (point + 12) % 24.
constexpr std::array<BackgammonSetupStack, 8> kBackgammonSetupStacks{{
    {12,  1, 2},
    {17, -1, 5},
    {19, -1, 3},
    {23,  1, 5},
    { 0, -1, 5},
    { 4,  1, 3},
    { 6,  1, 5},
    {11, -1, 2},
}};

struct BackgammonCheckerBase {
    int x{};
    int y{};
};

// CODE 11 $59A6/$5A82 index these two 24-entry Point tables with the
// original point number.  The native rule engine rotates that numbering by
// twelve, so source point n is native point (n + 12) % 24.  These are actor
// top-left coordinates after the source helper swaps QuickDraw's v/h words.
constexpr std::array<BackgammonCheckerBase, 24> kShellCheckerBases{{
    { 45, 175}, { 79, 175}, {112, 175}, {146, 175},
    {180, 175}, {212, 175}, {270, 175}, {302, 175},
    {337, 175}, {370, 175}, {404, 175}, {438, 175},
    {469, 333}, {430, 333}, {392, 333}, {352, 333},
    {314, 333}, {274, 333}, {208, 333}, {168, 333},
    {131, 333}, { 91, 333}, { 52, 333}, { 14, 333},
}};

constexpr std::array<BackgammonCheckerBase, 24> kEggCheckerBases{{
    { 47, 169}, { 81, 169}, {115, 169}, {148, 169},
    {182, 169}, {215, 169}, {272, 169}, {305, 169},
    {339, 169}, {372, 169}, {406, 169}, {441, 169},
    {470, 322}, {430, 322}, {393, 322}, {354, 322},
    {316, 322}, {275, 322}, {211, 322}, {170, 322},
    {133, 322}, { 93, 322}, { 55, 322}, { 16, 322},
}};

constexpr int sourceCheckerPoint(int nativePoint) noexcept {
    return (nativePoint + 12) % 24;
}

constexpr int checkerHorizontalSlope(int sourcePoint) noexcept {
    if (sourcePoint <= 2 || (sourcePoint >= 12 && sourcePoint <= 14)) return -2;
    if ((sourcePoint >= 3 && sourcePoint <= 4) ||
        (sourcePoint >= 15 && sourcePoint <= 16)) return -1;
    if ((sourcePoint >= 7 && sourcePoint <= 8) ||
        (sourcePoint >= 19 && sourcePoint <= 20)) return 1;
    if ((sourcePoint >= 9 && sourcePoint <= 11) || sourcePoint >= 21) return 2;
    return 0;
}

Point sourceCheckerPosition(int nativePoint, int frame, int stackIndex) noexcept {
    const int sourcePoint = sourceCheckerPoint(nativePoint);
    const int boundedIndex = std::clamp(stackIndex, 0, 14);
    const int withinGroup = boundedIndex % 5;
    const int group = boundedIndex / 5;
    const bool egg = frame == 0;
    const BackgammonCheckerBase base = egg
        ? kEggCheckerBases[static_cast<std::size_t>(sourcePoint)]
        : kShellCheckerBases[static_cast<std::size_t>(sourcePoint)];
    const int verticalStep = egg ? 10 : 11;
    const int verticalDirection = sourcePoint < 12 ? 1 : -1;
    const int groupDirection = sourcePoint <= 5 || sourcePoint >= 18 ? -1 : 1;
    return {
        base.x + checkerHorizontalSlope(sourcePoint) * withinGroup +
            groupDirection * 2 * group,
        base.y + verticalDirection * verticalStep * withinGroup,
    };
}

int sourceNonRepeatingBucket(SourceRandom& random, int& previous) {
    int bucket = 0;
    do bucket = random.below(300) / 100;
    while (bucket == previous);
    previous = bucket;
    return bucket;
}

BackgammonIdleChoice chooseBackgammonIdle(SourceRandom& random,
                                          int& lastVisual,
                                          int& lastLine) {
    // CODE 11 $259C chooses state 5 (a line) for 0..99 and state 0
    // (a full-body visual) for 100..199.
    if (random.below(200) / 100 == 0) {
        static constexpr std::array lines{11638, 11639, 11637};
        const int bucket = sourceNonRepeatingBucket(random, lastLine);
        return {BackgammonIdleKind::Line, lines[static_cast<std::size_t>(bucket)]};
    }
    const int bucket = sourceNonRepeatingBucket(random, lastVisual);
    return {BackgammonIdleKind::Visual, 9000 + bucket};
}

template <std::size_t Size>
int drawBackgammonPool(std::array<int, Size>& pool, std::size_t& cursor,
                       SourceRandom& random) {
    if (cursor >= pool.size()) {
        sourceShuffle(pool, random);
        cursor = 0;
    }
    return pool[cursor++];
}

}  // namespace

BackgammonGame::BackgammonGame(GameContext context)
    : Game(context), host_(context.assets, context.graphics, context.audio, true,
          [](int resourceId, Point scaled) {
              if (resourceId >= 10000) return Point{116, 1};
              if (resourceId >= 9000 && resourceId <= 9002) return Point{31, 7};
              return scaled;
          }),
      diceRoll_(context.assets, context.graphics, context.audio),
      secondDiceRoll_(context.assets, context.graphics, context.audio),
      victoryLeft_(context.assets, context.graphics, context.audio),
      victoryRight_(context.assets, context.graphics, context.audio) { reset(); }

void BackgammonGame::reset(bool preserveSession) {
    idleElapsedSourceTicks_ = 0;
    idlePhase_ = IdlePhase::Waiting;
    state_ = {};
    // Original screen orientation: the player's pieces occupy the upper-left,
    // upper-right, lower-left-centre, and lower-right-centre points.
    state_.points[12] = 2; state_.points[23] = 5; state_.points[6] = 5; state_.points[4] = 3;
    state_.points[17] = -5; state_.points[19] = -3; state_.points[11] = -2; state_.points[0] = -5;
    dice_.clear(); computerMoves_.clear();
    if (!preserveSession) {
        // CODE 13 shuffles these selectors only when their cursor first wraps;
        // eager reset-time shuffles would consume four source RNG calls early.
        openingGreetingPool_ = {11603, 11604, 11600};
        openingGreetingPoolCursor_ = openingGreetingPool_.size();
        computerNoMovePool_ = {11613, 11614, 11617};
        computerNoMovePoolCursor_ = computerNoMovePool_.size();
        computerThinkingPool_ = {11632, 11631, 11630};
        computerThinkingPoolCursor_ = computerThinkingPool_.size();
        lastIdleLine_ = -1;
        lastIdleVisual_ = -1;
    }
    selected_ = -99; winner_ = pendingRoll_ = pendingFirst_ = pendingSecond_ = 0;
    diceOwner_ = computerDelayMilliseconds_ = 0;
    setupVisiblePoints_.fill(0);
    setupStackSoundIndex_ = 0;
    setupStackRemaining_ = 0;
    startupDelayTicks_ = 2;
    outcomeDelayTicks_ = 0;
    computerMoveIndex_ = 0;
    rolled_ = computerRollPending_ = computerMovesPending_ = false;
    setupStackRevealing_ = false;
    qaSetupRevealPresentation_ = false;
    startupPhase_ = StartupPhase::PreGreeting;
    pieceAnimation_ = {};
    outcomePhase_ = OutcomePhase::None;
    opening_ = true;
    status_ = L"Let's roll to see who goes first.";
    host_.stop();
    diceRoll_.stop();
    secondDiceRoll_.stop();
    victoryLeft_.stop();
    victoryRight_.stop();
}

bool BackgammonGame::allHome(const State& state, int player) {
    if (player > 0) {
        if (state.humanBar) return false;
        for (int point = 6; point < 24; ++point) if (state.points[point] > 0) return false;
    } else {
        if (state.computerBar) return false;
        for (int point = 0; point < 18; ++point) if (state.points[point] < 0) return false;
    }
    return true;
}

std::vector<BackgammonGame::Move> BackgammonGame::legalSingle(const State& state, int player, int die) const {
    std::vector<Move> result;
    const int bar = player > 0 ? state.humanBar : state.computerBar;
    const auto open = [&](int target) {
        return target >= 0 && target < 24 && (player > 0 ? state.points[target] >= -1 : state.points[target] <= 1);
    };
    if (bar > 0) {
        const int target = player > 0 ? 24 - die : die - 1;
        if (open(target)) result.push_back({player > 0 ? 24 : -1, target, die});
        return result;
    }
    for (int from = 0; from < 24; ++from) {
        if ((player > 0 && state.points[from] <= 0) || (player < 0 && state.points[from] >= 0)) continue;
        const int target = from + (player > 0 ? -die : die);
        if (target >= 0 && target < 24) {
            if (open(target)) result.push_back({from, target, die});
            continue;
        }
        if (!allHome(state, player)) continue;
        if (player > 0) {
            if (target == -1) result.push_back({from, -2, die});
            else if (target < -1) {
                bool higher = false; for (int point = from + 1; point <= 5; ++point) higher |= state.points[point] > 0;
                if (!higher) result.push_back({from, -2, die});
            }
        } else {
            if (target == 24) result.push_back({from, 24, die});
            else if (target > 24) {
                bool lower = false; for (int point = 18; point < from; ++point) lower |= state.points[point] < 0;
                if (!lower) result.push_back({from, 24, die});
            }
        }
    }
    return result;
}

void BackgammonGame::apply(State& state, int player, const Move& move) {
    if (player > 0) {
        if (move.from == 24) --state.humanBar; else --state.points[move.from];
        if (move.to == -2) ++state.humanOff;
        else {
            if (state.points[move.to] == -1) { state.points[move.to] = 0; ++state.computerBar; }
            ++state.points[move.to];
        }
    } else {
        if (move.from == -1) --state.computerBar; else ++state.points[move.from];
        if (move.to == 24) ++state.computerOff;
        else {
            if (state.points[move.to] == 1) { state.points[move.to] = 0; ++state.humanBar; }
            --state.points[move.to];
        }
    }
}

std::vector<std::vector<BackgammonGame::Move>> BackgammonGame::sequences(
    const State& state, int player, std::vector<int> dice) const {
    std::vector<std::vector<Move>> result;
    bool extended = false;
    for (std::size_t dieIndex = 0; dieIndex < dice.size(); ++dieIndex) {
        if (dieIndex && dice[dieIndex] == dice[dieIndex - 1]) continue;
        for (const Move& move : legalSingle(state, player, dice[dieIndex])) {
            extended = true;
            State next = state; apply(next, player, move);
            auto remaining = dice; remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(dieIndex));
            auto tails = sequences(next, player, remaining);
            if (tails.empty()) result.push_back({move});
            else for (auto& tail : tails) { tail.insert(tail.begin(), move); result.push_back(std::move(tail)); }
        }
    }
    if (!extended) return {};
    std::size_t maximum = 0; for (const auto& sequence : result) maximum = std::max(maximum, sequence.size());
    std::erase_if(result, [&](const auto& sequence) { return sequence.size() != maximum; });
    if (maximum == 1 && dice.size() == 2 && dice[0] != dice[1]) {
        const int high = std::max(dice[0], dice[1]);
        if (std::any_of(result.begin(), result.end(), [&](const auto& sequence) { return sequence[0].die == high; }))
            std::erase_if(result, [&](const auto& sequence) { return sequence[0].die != high; });
    }
    return result;
}

std::vector<BackgammonGame::Move> BackgammonGame::legalFirstMoves() const {
    auto sortedDice = dice_; std::sort(sortedDice.begin(), sortedDice.end());
    const auto all = sequences(state_, 1, sortedDice);
    std::vector<Move> moves;
    for (const auto& sequence : all) if (!sequence.empty()) {
        const Move move = sequence.front();
        if (std::none_of(moves.begin(), moves.end(), [&](const Move& item) {
            return item.from == move.from && item.to == move.to && item.die == move.die;
        })) moves.push_back(move);
    }
    return moves;
}

BackgammonGame::Move BackgammonGame::sourcePreferredMove(
    const State& state, const std::vector<int>& dice,
    const std::vector<Move>& legalFirst) const {
    // CODE 11 $161A dispatches a fixed family of move selectors rather than a
    // numerical board evaluator.  The original point numbers run in the
    // opposite direction to this native board, so source point n is native
    // point 23-n.
    const auto sameMove = [](const Move& left, const Move& right) {
        return left.from == right.from && left.to == right.to && left.die == right.die;
    };
    const auto findMove = [&](int from, int to, int die, Move& result) {
        const Move wanted{from, to, die};
        const auto found = std::find_if(legalFirst.begin(), legalFirst.end(),
                                        [&](const Move& move) { return sameMove(move, wanted); });
        if (found == legalFirst.end()) return false;
        result = *found;
        return true;
    };
    const auto nativePoint = [](int sourcePoint) { return 23 - sourcePoint; };
    const auto computerCount = [&](int sourcePoint) {
        const int count = state.points[nativePoint(sourcePoint)];
        return count < 0 ? -count : 0;
    };
    const auto humanCount = [&](int sourcePoint) {
        return std::max(0, state.points[nativePoint(sourcePoint)]);
    };

    std::vector<int> orderedDice = dice;
    std::sort(orderedDice.rbegin(), orderedDice.rend());
    orderedDice.erase(std::unique(orderedDice.begin(), orderedDice.end()), orderedDice.end());
    Move selected = legalFirst.front();

    // $18D4: a checker on the bar must enter.  The source checks hits first,
    // then an own blot, then any legal entry, with the larger die first.
    if (state.computerBar > 0) {
        for (const int die : orderedDice) {
            const int target = 24 - die;
            if (humanCount(target) == 1 && findMove(-1, nativePoint(target), die, selected))
                return selected;
        }
        for (const int die : orderedDice) {
            const int target = 24 - die;
            if (computerCount(target) == 1 && findMove(-1, nativePoint(target), die, selected))
                return selected;
        }
        for (const int die : orderedDice) {
            const int target = 24 - die;
            if (findMove(-1, nativePoint(target), die, selected)) return selected;
        }
        return selected;
    }

    // $1FB6: once all computer checkers are home, bear off exact rolls from
    // stacks first, then any exact roll, then the highest checker on an
    // oversized roll.
    if (allHome(state, -1)) {
        for (int source = 0; source <= 5; ++source) {
            if (computerCount(source) <= 2) continue;
            for (const int die : orderedDice)
                if (source + 1 == die &&
                    findMove(nativePoint(source), 24, die, selected)) return selected;
        }
        for (int source = 0; source <= 5; ++source) {
            if (computerCount(source) == 0) continue;
            for (const int die : orderedDice)
                if (source + 1 == die &&
                    findMove(nativePoint(source), 24, die, selected)) return selected;
        }
        int highest = -1;
        for (int source = 23; source >= 0; --source) {
            if (computerCount(source) != 0) { highest = source; break; }
        }
        if (highest >= 0) for (const int die : orderedDice)
            if (highest < die && findMove(nativePoint(highest), 24, die, selected))
                return selected;
    }

    int computerHigh = -1;
    int humanLow = -1;
    for (int source = 23; source >= 0; --source)
        if (computerCount(source) != 0) { computerHigh = source; break; }
    for (int source = 0; source < 24; ++source)
        if (humanCount(source) != 0) { humanLow = source; break; }
    const bool contact = computerHigh > humanLow;

    if (contact) {
        // $1A90: close own blots with spare checkers, then merge exposed
        // singletons into existing points.  The scan proceeds from point 0.
        for (int source = 0; source < 24; ++source) {
            const int count = computerCount(source);
            if (count > 2) for (const int die : orderedDice) {
                const int target = source - die;
                if (target >= 0 && computerCount(target) == 1 &&
                    findMove(nativePoint(source), nativePoint(target), die, selected))
                    return selected;
            }
            if (count == 1) for (const int die : orderedDice) {
                const int target = source - die;
                if (target >= 0 && computerCount(target) != 0 &&
                    findMove(nativePoint(source), nativePoint(target), die, selected))
                    return selected;
            }
        }

        // $1E90: doubles may peel a made point; otherwise move spare checkers
        // onto an existing computer point, scanning from the rear.
        const bool doubles = dice.size() >= 2 &&
            std::all_of(dice.begin() + 1, dice.end(), [&](int die) { return die == dice.front(); });
        if (doubles) for (int source = 23; source >= 0; --source) {
            if (computerCount(source) != 2) continue;
            for (const int die : orderedDice) {
                const int target = source - die;
                if (target >= 0 &&
                    findMove(nativePoint(source), nativePoint(target), die, selected))
                    return selected;
            }
        }
        for (int source = 23; source >= 0; --source) {
            if (computerCount(source) <= 2) continue;
            for (const int die : orderedDice) {
                const int target = source - die;
                if (target >= 0 && computerCount(target) != 0 &&
                    findMove(nativePoint(source), nativePoint(target), die, selected))
                    return selected;
            }
        }
    }

    if (!allHome(state, -1)) {
        // $1C58/$1D1E: before looking for hits, move a rear blot or spare
        // checker to a point which no human checker (or checker on the bar)
        // can hit on the next roll.
        for (int source = 23; source >= 6; --source) {
            const int count = computerCount(source);
            if (count == 0 || count == 2) continue;
            for (const int die : orderedDice) {
                const int target = source - die;
                if (target <= 0) continue;
                bool threatened = target < 6 && state.humanBar > 0;
                for (int point = target; !threatened && point >= std::max(0, target - 6); --point)
                    threatened = humanCount(point) != 0;
                if (!threatened &&
                    findMove(nativePoint(source), nativePoint(target), die, selected))
                    return selected;
            }
        }
    }

    // $1BD0: hit the first reachable human blot, scanning the opponent's
    // advanced points from 0 upward and trying the larger die first. In the
    // non-home dispatcher this follows $1C58; in the all-home dispatcher the
    // safety selector is omitted, so the same block follows $1E90 directly.
    for (int target = 0; target < 24; ++target) {
        if (humanCount(target) != 1) continue;
        for (const int die : orderedDice) {
            const int source = target + die;
            if (source < 24 && computerCount(source) != 0 &&
                findMove(nativePoint(source), nativePoint(target), die, selected))
                return selected;
        }
    }

    // $1D76: in contact, preserve made points unless a stack has a spare
    // checker (or doubles can rebuild the point); finally take the first legal
    // rear-to-front move.
    if (contact) {
        const bool doubles = dice.size() >= 2 &&
            std::all_of(dice.begin() + 1, dice.end(), [&](int die) { return die == dice.front(); });
        for (int source = 23; source >= 0; --source) {
            const int count = computerCount(source);
            if (count <= 2 && !(count == 2 && doubles)) continue;
            for (const int die : orderedDice) {
                const int target = source - die;
                if (target >= 0 &&
                    findMove(nativePoint(source), nativePoint(target), die, selected))
                    return selected;
            }
        }
    }
    for (int source = 23; source >= 0; --source) {
        if (computerCount(source) == 0) continue;
        for (const int die : orderedDice) {
            const int target = source - die;
            if (target >= 0 &&
                findMove(nativePoint(source), nativePoint(target), die, selected))
                return selected;
        }
    }
    return selected;
}

std::vector<BackgammonGame::Move> BackgammonGame::sourcePreferredTurn(
    const State& state, const std::vector<int>& dice) const {
    const auto sameMove = [](const Move& left, const Move& right) {
        return left.from == right.from && left.to == right.to && left.die == right.die;
    };
    State next = state;
    std::vector<int> remainingDice = dice;
    std::sort(remainingDice.rbegin(), remainingDice.rend());
    std::vector<Move> result;
    while (!remainingDice.empty()) {
        std::vector<Move> legalFirst;
        for (std::size_t dieIndex = 0; dieIndex < remainingDice.size(); ++dieIndex) {
            if (dieIndex != 0 && remainingDice[dieIndex] == remainingDice[dieIndex - 1]) continue;
            for (const Move& move : legalSingle(next, -1, remainingDice[dieIndex])) {
                if (std::none_of(legalFirst.begin(), legalFirst.end(),
                                 [&](const Move& existing) { return sameMove(existing, move); }))
                    legalFirst.push_back(move);
            }
        }
        if (legalFirst.empty()) break;

        // CODE 11 $161A is called again after every individual move. It does
        // not prefilter first moves through a modern maximum-dice sequence;
        // each selector sees the current board and the dice still available.
        const Move preferred = sourcePreferredMove(next, remainingDice, legalFirst);
        result.push_back(preferred);
        apply(next, -1, preferred);
        const auto usedDie = std::find(
            remainingDice.begin(), remainingDice.end(), preferred.die);
        if (usedDie == remainingDice.end()) break;
        remainingDice.erase(usedDie);
    }
    return result;
}

bool BackgammonGame::sourceStrategyRegressionTest() const {
    const auto selects = [&](const State& state, std::vector<int> dice, const Move& expected) {
        std::sort(dice.rbegin(), dice.rend());
        const auto chosen = sourcePreferredTurn(state, dice);
        return !chosen.empty() && chosen.front().from == expected.from &&
               chosen.front().to == expected.to && chosen.front().die == expected.die;
    };

    State hit{};
    hit.points[15] = -1;  // source point 8
    hit.points[20] = 1;   // source point 3: human blot
    if (!selects(hit, {5, 2}, {15, 20, 5})) return false;

    State makePoint{};
    makePoint.points[15] = -3;  // source point 8: spare checkers
    makePoint.points[20] = -1;  // source point 3: own blot
    makePoint.points[22] = 1;   // keep the position in contact
    if (!selects(makePoint, {5, 2}, {15, 20, 5})) return false;

    State bearOff{};
    bearOff.points[23] = -3;  // source point 0: exact roll 1 from a stack
    bearOff.points[19] = -1;  // source point 4: exact roll 5
    bearOff.computerOff = 11;
    if (!selects(bearOff, {5, 1}, {23, 24, 1})) return false;

    State enter{};
    enter.computerBar = 1;
    enter.points[5] = 1;  // source point 18: hit on entry with a six
    if (!selects(enter, {6, 1}, {-1, 5, 6})) return false;

    State enterOwnBlot{};
    enterOwnBlot.computerBar = 1;
    enterOwnBlot.points[5] = -1;  // source point 18: own blot, preferred to empty low entry
    if (!selects(enterOwnBlot, {6, 1}, {-1, 5, 6})) return false;

    State safeRearBlot{};
    safeRearBlot.points[3] = -1;  // source point 20
    safeRearBlot.points[2] = 2;   // source point 21 keeps the position out of contact
    if (!selects(safeRearBlot, {6, 1}, {3, 9, 6})) return false;

    State safeBeforeHit{};
    safeBeforeHit.points[3] = -1;   // source point 20 has a safe six to source point 14
    safeBeforeHit.points[17] = -1;  // source point 6 can hit the blot on source point 0
    safeBeforeHit.points[23] = 1;
    if (!selects(safeBeforeHit, {6, 1}, {3, 9, 6})) return false;

    State doublesPoint{};
    doublesPoint.points[13] = -2;  // source point 10: peel a made point on doubles
    doublesPoint.points[23] = 2;   // source point 0: position remains in contact
    if (!selects(doublesPoint, {3, 3, 3, 3}, {13, 16, 3})) return false;

    State fallback{};
    fallback.points[3] = -2;  // source point 20: safety skips an exact made point
    fallback.points[2] = 2;   // source point 21: no contact
    if (!selects(fallback, {6, 1}, {3, 9, 6})) return false;

    State finalChecker{};
    finalChecker.points[19] = -1;  // source point 4
    finalChecker.computerOff = 14;
    // $1FB6 immediately bears off the last checker with the high oversized
    // die instead of first moving it with the low die to maximize dice used.
    if (!selects(finalChecker, {6, 2}, {19, 24, 6})) return false;
    const auto finalTurn = sourcePreferredTurn(finalChecker, {6, 2});
    if (finalTurn.size() != 1 || finalTurn.front().die != 6 ||
        finalTurn.front().from != 19 || finalTurn.front().to != 24) return false;
    return true;
}

void BackgammonGame::rollDice(bool human) {
    // CODE 11 $3346 and $33B0 preserve the original percent-bucket draws.
    pendingFirst_ = context_.random.below(600) / 100 + 1;
    pendingSecond_ = context_.random.below(1200) / 200 + 1;
    pendingRoll_ = human ? 1 : -1;
    dice_.clear();
    rolled_ = false;
    selected_ = -99;
    computerMovesPending_ = false;
    status_ = human ? L"Roll the dice!" : L"Mario rolls the dice.";
    diceRoll_.play(4020, 87, 75);
    secondDiceRoll_.play(4020, 170, 75, false);
}

void BackgammonGame::openingRoll() {
    // The opposed opening dice each take the one-die $3346 path.
    pendingFirst_ = context_.random.below(600) / 100 + 1;
    pendingSecond_ = context_.random.below(600) / 100 + 1;
    pendingRoll_ = 2;
    dice_.clear();
    rolled_ = false;
    selected_ = -99;
    status_ = L"Let's roll to see who goes first.";
    diceRoll_.play(4020, 87, 75);
    secondDiceRoll_.play(4020, 170, 75, false);
}

void BackgammonGame::resolveRoll() {
    const int kind = pendingRoll_;
    pendingRoll_ = 0;
    // The source roll controller adds its settle sound after the authored
    // movie-4020 rattles have drained.
    context_.audio.playEffect(5019);
    if (kind == 2) {
        const int humanDie = pendingFirst_;
        const int marioDie = pendingSecond_;
        diceOwner_ = 0;
        dice_ = {humanDie, marioDie};
        if (humanDie == marioDie) {
            rolled_ = false;
            status_ = L"You both rolled " + std::to_wstring(humanDie) + L". Roll again.";
            return;
        }
        opening_ = false;
        rolled_ = true;
        if (humanDie > marioDie) {
            host_.play(11620, -11, -1);
            status_ = L"You win the opening roll " + std::to_wstring(humanDie) + L"-" +
                      std::to_wstring(marioDie) + L". Select a piece.";
            if (legalFirstMoves().empty()) finishHumanTurn();
        } else {
            host_.play(11619, -11, -1);
            status_ = L"Mario wins the opening roll.";
            computerTurn(false);
        }
        return;
    }

    const bool human = kind > 0;
    diceOwner_ = human ? 1 : -1;
    dice_ = pendingFirst_ == pendingSecond_
        ? std::vector<int>(4, pendingFirst_)
        : std::vector<int>{pendingFirst_, pendingSecond_};
    rolled_ = true;
    selected_ = -99;
    status_ = (human ? L"You rolled " : L"Mario rolled ") +
              std::to_wstring(pendingFirst_) + L" and " + std::to_wstring(pendingSecond_) + L".";
    if (pendingFirst_ == pendingSecond_) {
        host_.play(human ? 11623 : 11624, -11, -1);
    } else if (!human) {
        // Once the first player turn has happened, CODE 11 $1320 draws from
        // A5-$3B3A's selector [32,31,30] after each ordinary Mario roll.
        // [5,7,9] belongs to the necessarily unreachable $121C branch.
        host_.play(drawBackgammonPool(computerThinkingPool_, computerThinkingPoolCursor_,
                                      context_.random),
                   -11, -1);
    }
    if (human) {
        if (legalFirstMoves().empty()) {
            status_ += L" You can't move anywhere.";
            finishHumanTurn();
        } else if (state_.humanBar) {
            status_ += L" Move off the bar first.";
            if (host_.active()) host_.queue(11621); else host_.play(11621, -11, -1);
        } else {
            status_ += L" Select a piece.";
        }
    } else {
        computerTurn(false);
    }
}

void BackgammonGame::updateWinner() {
    if (winner_ != 0) return;
    if (state_.humanOff == 15) {
        winner_ = 1;
        status_ = L"Congratulations! Looks like you won!";
        context_.audio.playMusic(audio_catalog::playerWinMusic(dosEdition(), 0));
        // CODE 11 $3BB6 chooses indices 42/46 with equal probability.
        const int movie = context_.random.below(200) / 100 == 0 ? 11642 : 11646;
        if (host_.active()) host_.queue(movie); else host_.play(movie, -11, -1);
        outcomePhase_ = OutcomePhase::Announcement;
    }
    if (state_.computerOff == 15) {
        winner_ = -1;
        status_ = L"Looks like Mario wins this time.";
        // CODE 11 $3B88 similarly chooses indices 43/44 for Mario's win.
        const int movie = context_.random.below(200) / 100 == 0 ? 11643 : 11644;
        if (host_.active()) host_.queue(movie); else host_.play(movie, -11, -1);
        outcomePhase_ = OutcomePhase::Announcement;
    }
}

bool BackgammonGame::finished() const {
    return outcomePhase_ == OutcomePhase::Complete;
}

bool BackgammonGame::sourceOutcomeRegressionTest(bool humanWins) {
    // Outcome coverage must begin at the source result controller, not spend
    // several seconds completing a newly constructed game's greeting/setup
    // controller first.  In live play this state is already Complete.
    startupPhase_ = StartupPhase::Complete;
    setupStackSoundIndex_ = kBackgammonSetupStacks.size();
    setupVisiblePoints_ = state_.points;
    host_.stop();
    winner_ = 0;
    outcomePhase_ = OutcomePhase::None;
    state_.humanOff = humanWins ? 15 : 0;
    state_.computerOff = humanWins ? 0 : 15;
    updateWinner();

    const int expectedWinner = humanWins ? 1 : -1;
    bool sawPairedVictoryMovies = false;
    bool sawMarioDelay = false;
    bool sawReplayPrompt = false;
    for (int tickIndex = 0; tickIndex < 1200 && !finished(); ++tickIndex) {
        (void)tick();
        sawPairedVictoryMovies |= victoryLeft_.active() && victoryRight_.active();
        sawMarioDelay |= outcomePhase_ == OutcomePhase::PreReplayDelay &&
                         outcomeDelayTicks_ == 20;
        sawReplayPrompt |= outcomePhase_ == OutcomePhase::ReplayPrompt;
    }
    return winner_ == expectedWinner && finished() && sawReplayPrompt &&
           (humanWins ? sawPairedVictoryMovies : sawMarioDelay && !sawPairedVictoryMovies);
}

bool BackgammonGame::sourceFullMatchRegressionTest() {
    const std::uint32_t savedSeed = context_.random.seed();
    const auto fail = [&]() {
        context_.random.setSeed(savedSeed);
        return false;
    };
    constexpr std::array<std::uint32_t, 8> seeds{
        1U, 17U, 0x1234U, 0x4d415249U,
        0x00c0ffeeU, 0x13579bdfU, 0x2468ace0U, 0x7ffffffeU};
    bool sawHumanRoll = false;
    bool sawMarioRoll = false;
    bool sawHumanMove = false;
    bool sawMarioMove = false;
    bool sawHit = false;
    bool sawBarEntry = false;
    bool sawBearOff = false;
    bool sawFriendlyPointBuild = false;
    bool sawSelectionCancel = false;

    const auto sameState = [](const State& left, const State& right) {
        return left.points == right.points && left.humanBar == right.humanBar &&
               left.computerBar == right.computerBar && left.humanOff == right.humanOff &&
               left.computerOff == right.computerOff;
    };
    const auto stateIsValid = [&]() {
        int human = state_.humanBar + state_.humanOff;
        int computer = state_.computerBar + state_.computerOff;
        if (state_.humanBar < 0 || state_.humanOff < 0 || state_.computerBar < 0 ||
            state_.computerOff < 0 || state_.humanBar > 15 || state_.humanOff > 15 ||
            state_.computerBar > 15 || state_.computerOff > 15) return false;
        for (const int count : state_.points) {
            if (count < -15 || count > 15) return false;
            if (count > 0) human += count;
            else computer -= count;
        }
        if (human != 15 || computer != 15 || dice_.size() > 4 ||
            std::any_of(dice_.begin(), dice_.end(), [](int die) { return die < 1 || die > 6; }) ||
            pendingRoll_ < -1 || pendingRoll_ > 2 || pendingFirst_ < 0 || pendingFirst_ > 6 ||
            pendingSecond_ < 0 || pendingSecond_ > 6 || diceOwner_ < -1 || diceOwner_ > 1 ||
            winner_ < -1 || winner_ > 1 ||
            (computerMovesPending_ && computerMoveIndex_ >= computerMoves_.size())) return false;
        if (dice_.size() > 2 && !std::all_of(dice_.begin() + 1, dice_.end(),
                [&](int die) { return die == dice_.front(); })) return false;
        return selected_ == -99 || selected_ == 24 ||
               (selected_ >= 0 && selected_ < 24);
    };
    const auto center = [&](int point) {
        Rect rect{};
        if (point == 24) {
            rect = barRect_;
            if (dosEdition())
                rect = {dosX(rect.left), dosY(rect.top), dosX(rect.right), dosY(rect.bottom)};
        } else if (point == -2) {
            rect = offRect_;
            if (dosEdition())
                rect = {dosX(rect.left), dosY(rect.top), dosX(rect.right), dosY(rect.bottom)};
        } else {
            rect = pointRect(point);
        }
        return Point{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    };

    bool firstMatch = true;
    for (const std::uint32_t seed : seeds) {
        context_.random.setSeed(seed);
        if (firstMatch) reset(false);
        else resetForReplay();
        firstMatch = false;
        bool matchSawStartup = false;
        bool matchSawOpening = false;
        bool matchSawHumanMove = false;
        bool matchSawMarioMove = false;

        for (int controllerPass = 0; controllerPass < 30000 && !finished();
             ++controllerPass) {
            host_.stop();
            const bool playerCanAct = startupPhase_ == StartupPhase::Complete && !winner_ &&
                !characterChooser_ && pendingRoll_ == 0 && !computerRollPending_ &&
                !computerMovesPending_ && !pieceAnimation_.active && !diceRoll_.active() &&
                !secondDiceRoll_.active() && !host_.active();
            if (playerCanAct) {
                if (!rolled_) {
                    Rect rect = rollButton_;
                    if (dosEdition())
                        rect = {dosX(rect.left), dosY(rect.top),
                                dosX(rect.right), dosY(rect.bottom)};
                    click({(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2});
                    if (pendingRoll_ == 1) sawHumanRoll = true;
                    if (pendingRoll_ == 0 || !diceRoll_.active() || !secondDiceRoll_.active())
                        return fail();
                } else {
                    const std::vector<Move> legal = legalFirstMoves();
                    if (legal.empty()) return fail();
                    const auto rank = [&](const Move& move) {
                        int value = move.die * 100;
                        if (move.to == -2) value += 100000;
                        if (move.from == 24) value += 10000;
                        if (move.to >= 0 && move.to < 24) {
                            if (state_.points[move.to] == -1) value += 20000;
                            if (state_.points[move.to] == 1) value += 2000;
                            if (state_.points[move.to] == 0) value -= 100;
                        }
                        if (move.from >= 0 && move.from < 24) {
                            value += (23 - move.from) * 10;
                            if (state_.points[move.from] == 2) value -= 500;
                        }
                        return value;
                    };
                    const Move chosen = *std::max_element(
                        legal.begin(), legal.end(), [&](const Move& left, const Move& right) {
                            return rank(left) < rank(right);
                        });
                    const auto actual = std::find_if(legal.begin(), legal.end(), [&](const Move& move) {
                        return move.from == chosen.from && move.to == chosen.to;
                    });
                    if (actual == legal.end()) return fail();
                    const State before = state_;
                    std::vector<int> expectedDice = dice_;
                    State expected = before;
                    apply(expected, 1, *actual);
                    const auto usedDie = std::find(expectedDice.begin(), expectedDice.end(), actual->die);
                    if (usedDie == expectedDice.end()) return fail();
                    expectedDice.erase(usedDie);
                    if (expected.humanOff != 15 && !expectedDice.empty()) {
                        auto sortedDice = expectedDice;
                        std::sort(sortedDice.begin(), sortedDice.end());
                        if (sequences(expected, 1, sortedDice).empty()) expectedDice.clear();
                    }
                    click(center(chosen.from));
                    if (selected_ != chosen.from) return fail();
                    if (!sawSelectionCancel) {
                        click(center(chosen.from));
                        if (selected_ != -99) return fail();
                        sawSelectionCancel = true;
                        click(center(chosen.from));
                        if (selected_ != chosen.from) return fail();
                    }
                    click(center(chosen.to));
                    if (!sameState(state_, expected) || dice_ != expectedDice || selected_ != -99)
                        return fail();
                    sawHumanMove = matchSawHumanMove = true;
                    sawFriendlyPointBuild |= chosen.to >= 0 && chosen.to < 24 &&
                                             before.points[chosen.to] > 0;
                    sawHit |= state_.computerBar > before.computerBar;
                    sawBarEntry |= state_.humanBar < before.humanBar;
                    sawBearOff |= state_.humanOff > before.humanOff;
                }
            }

            const State beforeTick = state_;
            Move expectedComputerMove{};
            bool haveExpectedComputerMove = false;
            if (computerMovesPending_ && computerMoveIndex_ < computerMoves_.size()) {
                expectedComputerMove = computerMoves_[computerMoveIndex_];
                haveExpectedComputerMove = true;
            }
            const int pendingBefore = pendingRoll_;
            (void)tick();
            matchSawStartup |= startupPhase_ == StartupPhase::Complete;
            matchSawOpening |= !opening_;
            sawMarioRoll |= pendingBefore == -1 || pendingRoll_ == -1;

            if (!sameState(beforeTick, state_)) {
                if (!haveExpectedComputerMove) return fail();
                State expected = beforeTick;
                apply(expected, -1, expectedComputerMove);
                if (!sameState(state_, expected)) return fail();
                sawMarioMove = matchSawMarioMove = true;
                sawHit |= state_.humanBar > beforeTick.humanBar;
                sawBarEntry |= state_.computerBar < beforeTick.computerBar;
                sawBearOff |= state_.computerOff > beforeTick.computerOff;
            }
            if (!stateIsValid()) return fail();
        }

        if (!finished() || winner_ == 0 || !matchSawStartup || !matchSawOpening ||
            !matchSawHumanMove || !matchSawMarioMove || !stateIsValid()) return fail();
    }

    context_.random.setSeed(savedSeed);
    return sawHumanRoll && sawMarioRoll && sawHumanMove && sawMarioMove && sawHit &&
           sawBarEntry && sawBearOff && sawFriendlyPointBuild && sawSelectionCancel;
}

bool BackgammonGame::sourceReplayRegressionTest() {
    const std::uint32_t savedSeed = context_.random.seed();
    openingGreetingPool_ = {11604, 11600, 11603};
    computerNoMovePool_ = {11617, 11613, 11614};
    computerThinkingPool_ = {11631, 11630, 11632};
    openingGreetingPoolCursor_ = 1;
    computerNoMovePoolCursor_ = 2;
    computerThinkingPoolCursor_ = 1;
    lastIdleLine_ = 2;
    lastIdleVisual_ = 1;
    const auto openingPool = openingGreetingPool_;
    const auto noMovePool = computerNoMovePool_;
    const auto thinkingPool = computerThinkingPool_;
    state_ = {};
    state_.humanOff = 15;
    state_.computerBar = 15;
    dice_ = {6, 6, 6, 6};
    winner_ = 1;
    pendingRoll_ = 1;
    rolled_ = true;
    opening_ = false;
    outcomePhase_ = OutcomePhase::Complete;
    animatedPieces_ = false;
    context_.playerIsYoshi = false;

    resetForReplay();

    State expected{};
    expected.points[12] = 2;
    expected.points[23] = 5;
    expected.points[6] = 5;
    expected.points[4] = 3;
    expected.points[17] = -5;
    expected.points[19] = -3;
    expected.points[11] = -2;
    expected.points[0] = -5;
    const bool valid = state_.points == expected.points && state_.humanBar == 0 &&
        state_.computerBar == 0 && state_.humanOff == 0 && state_.computerOff == 0 &&
        dice_.empty() && computerMoves_.empty() && winner_ == 0 && pendingRoll_ == 0 &&
        !rolled_ && opening_ && outcomePhase_ == OutcomePhase::None &&
        startupPhase_ == StartupPhase::PreGreeting && startupDelayTicks_ == 2 &&
        std::all_of(setupVisiblePoints_.begin(), setupVisiblePoints_.end(),
                    [](int count) { return count == 0; }) &&
        openingGreetingPool_ == openingPool && computerNoMovePool_ == noMovePool &&
        computerThinkingPool_ == thinkingPool && openingGreetingPoolCursor_ == 1 &&
        computerNoMovePoolCursor_ == 2 && computerThinkingPoolCursor_ == 1 &&
        lastIdleLine_ == 2 && lastIdleVisual_ == 1 && !animatedPieces_ &&
        !context_.playerIsYoshi && context_.random.seed() == savedSeed &&
        !host_.active() && !diceRoll_.active() && !secondDiceRoll_.active() &&
        !pieceAnimation_.active;
    context_.random.setSeed(savedSeed);
    return valid;
}

bool BackgammonGame::tickOutcome() {
    if (outcomePhase_ == OutcomePhase::None || outcomePhase_ == OutcomePhase::Complete) {
        return false;
    }

    switch (outcomePhase_) {
    case OutcomePhase::Announcement:
        if (host_.active()) return false;
        if (winner_ > 0) {
            // CODE 11 $3C2C starts MuV 4022 and 4023 together. The source
            // adds (0,15)/(414,47) to their intrinsic origins and halves
            // each movie object's tick interval, hence the 2x tick below.
            victoryLeft_.play(4022, 3, 12);
            victoryRight_.play(4023, 414, 47);
            outcomePhase_ = OutcomePhase::VictoryMovies;
        } else {
            // $3C1C waits twenty controller ticks after Mario's line.
            outcomeDelayTicks_ = 20;
            outcomePhase_ = OutcomePhase::PreReplayDelay;
        }
        return true;
    case OutcomePhase::VictoryMovies:
        if (victoryLeft_.active() || victoryRight_.active()) return false;
        // $3CD4 installs the shared two-tick pause before $3CF0.
        outcomeDelayTicks_ = 2;
        outcomePhase_ = OutcomePhase::PreReplayDelay;
        return true;
    case OutcomePhase::PreReplayDelay:
        if (outcomeDelayTicks_-- > 0) return true;
        host_.play(11640, -11, -1);
        outcomePhase_ = OutcomePhase::ReplayPrompt;
        return true;
    case OutcomePhase::ReplayPrompt:
        if (host_.active()) return false;
        // $3D16 applies the same two-tick settling pause after the prompt.
        outcomeDelayTicks_ = 2;
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

void BackgammonGame::finishHumanTurn() {
    dice_.clear(); rolled_ = false; selected_ = -99; updateWinner();
    if (!winner_) {
        computerRollPending_ = true;
        computerDelayMilliseconds_ = 500;
        status_ = L"Mario's turn.";
    }
}

void BackgammonGame::computerTurn(bool roll) {
    if (roll) {
        computerRollPending_ = false;
        rollDice(false);
        return;
    }
    auto sorted = dice_; std::sort(sorted.rbegin(), sorted.rend());
    computerMoves_ = sourcePreferredTurn(state_, sorted);
    if (computerMoves_.empty()) {
        dice_.clear(); rolled_ = false;
        computerMovesPending_ = false;
        status_ = L"Mario can't move. It's your turn.";
        // CODE 11 $214A draws from the shuffled [13,14,17] selector rather
        // than reusing the ordinary turn hand-off line.
        const int movie = drawBackgammonPool(computerNoMovePool_, computerNoMovePoolCursor_,
                                             context_.random);
        if (host_.active()) host_.queue(movie); else host_.play(movie, -11, -1);
        return;
    }
    computerMoveIndex_ = 0;
    computerMovesPending_ = true;
    computerDelayMilliseconds_ = 500;
}

void BackgammonGame::computerStep() {
    if (!computerMovesPending_ || computerMoveIndex_ >= computerMoves_.size()) return;
    const Move move = computerMoves_[computerMoveIndex_++];
    const bool hitHuman = move.to >= 0 && move.to < 24 && state_.points[move.to] == 1;
    beginPieceAnimation(-1, move);
    apply(state_, -1, move);
    const auto die = std::find(dice_.begin(), dice_.end(), move.die);
    if (die != dice_.end()) dice_.erase(die);
    if (hitHuman) host_.play(11625, -11, -1);
    if (computerMoveIndex_ < computerMoves_.size()) {
        status_ = L"Mario can still move.";
        computerDelayMilliseconds_ = 450;
        return;
    }
    computerMovesPending_ = false;
    computerMoves_.clear();
    dice_.clear();
    rolled_ = false;
    updateWinner();
    if (!winner_) {
        status_ = L"It's your turn. Roll the dice.";
        // CODE 11 $15B4 draws below 200 and divides by 100, producing the
        // source's equal choice between movie indices 11 and 16.
        const int handoffMovie = context_.random.below(200) / 100 == 1 ? 11611 : 11616;
        if (host_.active()) host_.queue(handoffMovie);
        else host_.play(handoffMovie, -11, -1);
    }
}

bool BackgammonGame::tick() {
    bool changed = host_.tick();
    changed |= tickStartup();
    // Both source victory objects have their normal tick duration divided by
    // two at $3C48/$3C84, so advance their timelines twice per native tick.
    changed |= victoryLeft_.tick(66);
    changed |= victoryRight_.tick(66);
    const bool rollWasActive = diceRoll_.active() || secondDiceRoll_.active();
    changed |= diceRoll_.tick();
    changed |= secondDiceRoll_.tick();
    if (rollWasActive && !diceRoll_.active() && !secondDiceRoll_.active()) {
        resolveRoll();
        changed = true;
    }
    if (pieceAnimation_.active) {
        pieceAnimation_.elapsedMilliseconds += 33;
        if (pieceAnimation_.elapsedMilliseconds >= 360U) pieceAnimation_.active = false;
        changed = true;
    }
    if (!host_.active() && !diceRoll_.active() && !secondDiceRoll_.active() &&
        !pieceAnimation_.active) {
        if (computerRollPending_) {
            if (computerDelayMilliseconds_ > 0) {
                computerDelayMilliseconds_ = std::max(0, computerDelayMilliseconds_ - 33);
                changed = true;
            }
            if (computerDelayMilliseconds_ == 0) {
                computerTurn(true);
                changed = true;
            }
        } else if (computerMovesPending_) {
            if (computerDelayMilliseconds_ > 0) {
                computerDelayMilliseconds_ = std::max(0, computerDelayMilliseconds_ - 33);
                changed = true;
            }
            if (computerDelayMilliseconds_ == 0) {
                computerStep();
                changed = true;
            }
        }
    }
    changed |= tickOutcome();
    const bool waitingForPlayer = startupPhase_ == StartupPhase::Complete && !winner_ &&
        !characterChooser_ && pendingRoll_ == 0 &&
        !computerRollPending_ && !computerMovesPending_ && !pieceAnimation_.active &&
        !diceRoll_.active() && !secondDiceRoll_.active();
    changed |= tickSourceIdle(waitingForPlayer);
    return changed;
}

bool BackgammonGame::tickStartup() {
    switch (startupPhase_) {
    case StartupPhase::PreGreeting:
        // `$DD8` spends state zero installing the controller and state one
        // presenting the game window. The shuffled greeting starts in state
        // two on the third controller pass.
        if (startupDelayTicks_ > 0) {
            --startupDelayTicks_;
            return true;
        }
        host_.play(drawBackgammonPool(openingGreetingPool_, openingGreetingPoolCursor_,
                                      context_.random),
                   -11, -1);
        startupPhase_ = StartupPhase::Greeting;
        return true;
    case StartupPhase::Greeting:
        if (host_.active()) return false;
        // `$EFE` starts fixed host-table index 58, recovered as MuV 11618,
        // only after A5-$3B52's [3,4,0] greeting selector has completed.
        host_.play(11618, -11, -1);
        startupPhase_ = StartupPhase::RollPrompt;
        return true;
    case StartupPhase::RollPrompt:
        if (host_.active()) return false;
        startupDelayTicks_ = 2;
        startupPhase_ = StartupPhase::PostPromptDelay;
        return true;
    case StartupPhase::PostPromptDelay:
        // `$F2C` uses the old value of its two-count word, so values 2, 1,
        // and 0 occupy three controller passes before setup initialization.
        if (startupDelayTicks_-- > 0) return true;
        startupPhase_ = StartupPhase::SetupInitialize;
        return true;
    case StartupPhase::SetupInitialize:
        // `$F50` calls `$F78(0)` on its own pass; the first `$F78(1)` sound
        // and reveal-controller pass follows on the next scheduler tick.
        startupPhase_ = StartupPhase::Setup;
        return true;
    case StartupPhase::Setup: {
        const bool changed = tickSetupReveal();
        if (setupStackSoundIndex_ >= kBackgammonSetupStacks.size()) {
            startupPhase_ = StartupPhase::Complete;
        }
        return changed;
    }
    case StartupPhase::Complete:
        return false;
    }
    return false;
}

bool BackgammonGame::tickSetupReveal() {
    if (qaSetupRevealPresentation_ || setupStackSoundIndex_ >= kBackgammonSetupStacks.size()) {
        return false;
    }

    const BackgammonSetupStack& stack = kBackgammonSetupStacks[setupStackSoundIndex_];
    if (!setupStackRevealing_) {
        // $1078 starts 5042 once for the newly encountered non-empty record.
        // State one begins on the following controller pass, so no checker is
        // exposed on the sound-start pass itself.
        context_.audio.playSound(5042);
        setupStackRemaining_ = stack.count;
        setupStackRevealing_ = true;
        return true;
    }

    // $10B4 tests the old counter value after decrementing the stored word.
    // Positive old values expose exactly one checker; the zero pass merely
    // closes this record, and the next pass starts the following stack.
    if (setupStackRemaining_ > 0) {
        setupVisiblePoints_[stack.point] += stack.player;
        --setupStackRemaining_;
        return true;
    }

    setupStackRevealing_ = false;
    ++setupStackSoundIndex_;
    return true;
}

bool BackgammonGame::tickSourceIdle(bool eligible) {
    if (idlePhase_ != IdlePhase::Waiting) {
        if (host_.active()) return false;
        idlePhase_ = IdlePhase::Waiting;
        idleElapsedSourceTicks_ = 0;
        return true;
    }
    if (!eligible || host_.active()) {
        idleElapsedSourceTicks_ = 0;
        return false;
    }

    // The source decrements its 200-count controller at the classic 60 Hz
    // cadence.  The native host timer is 33 ms, hence two source ticks here.
    idleElapsedSourceTicks_ += 2;
    if (idleElapsedSourceTicks_ < 200) return false;
    idleElapsedSourceTicks_ = 0;

    const BackgammonIdleChoice choice = chooseBackgammonIdle(
        context_.random, lastIdleVisual_, lastIdleLine_);
    if (choice.kind == BackgammonIdleKind::Visual) {
        host_.play(choice.resource, -45, 45);
        idlePhase_ = IdlePhase::Visual;
    } else {
        status_ = choice.resource == 11637 ? L"Mamma mia..." :
                  choice.resource == 11638 ? L"Hey, what's taking so long?" :
                                             L"You still want to play?";
        host_.play(choice.resource, -11, -1);
        idlePhase_ = IdlePhase::Line;
    }
    return true;
}

Rect BackgammonGame::pointRect(int point) const {
    const bool top = point >= 12;
    const int column = top ? point - 12 : 11 - point;
    const int left = column < 6 ? 38 + column * 34 : 268 + (column - 6) * 34;
    const Rect result{left, top ? 182 : 270, left + 38, top ? 270 : 358};
    if (!dosEdition()) return result;
    return {dosX(result.left), dosY(result.top), dosX(result.right), dosY(result.bottom)};
}

int BackgammonGame::hitPoint(Point point) const {
    for (int index = 0; index < 24; ++index) if (pointRect(index).contains(point)) return index;
    const Rect bar = dosEdition()
        ? Rect{dosX(barRect_.left), dosY(barRect_.top), dosX(barRect_.right), dosY(barRect_.bottom)}
        : barRect_;
    const Rect off = dosEdition()
        ? Rect{dosX(offRect_.left), dosY(offRect_.top), dosX(offRect_.right), dosY(offRect_.bottom)}
        : offRect_;
    if (bar.contains(point)) return 24;
    if (off.contains(point)) return -2;
    return -99;
}

void BackgammonGame::click(Point point) {
    idleElapsedSourceTicks_ = 0;
    if (startupPhase_ != StartupPhase::Complete || winner_ || host_.active() ||
        diceRoll_.active() || secondDiceRoll_.active() ||
        computerRollPending_ || computerMovesPending_ || pieceAnimation_.active) return;
    const Rect roll = dosEdition()
        ? Rect{dosX(rollButton_.left), dosY(rollButton_.top),
               dosX(rollButton_.right), dosY(rollButton_.bottom)}
        : rollButton_;
    if (roll.contains(point)) {
        if (!rolled_) {
            // The source's roll-control actor starts its short 5024 press cue
            // at $2FC8 before the dice controller takes over.
            context_.audio.playSound(5024);
            if (opening_) { openingRoll(); return; }
            rollDice(true);
        }
        return;
    }
    if (!rolled_) return;
    const int target = hitPoint(point);
    const auto legal = legalFirstMoves();
    if (selected_ == -99) {
        if (target == 24 && state_.humanBar > 0) {
            selected_ = 24;
            context_.audio.playEffect(5053);
            status_ = L"Choose an entry point.";
        } else if (target >= 0 && target < 24 && state_.points[target] > 0 &&
                   state_.humanBar == 0) {
            selected_ = target;
            context_.audio.playEffect(5053);
            status_ = L"Choose where to move.";
        }
        return;
    }
    // CODE 11 $8CC-$91A handles a second click only after a checker has
    // entered the selected state. Clicking it again cancels selection;
    // otherwise the destination validator runs even when that point already
    // contains a friendly stack. Treating every friendly point as a fresh
    // selection made legal point-building moves impossible through the UI.
    if (target == selected_) {
        selected_ = -99;
        context_.audio.playEffect(5053);
        status_ = L"Select a piece.";
        return;
    }
    const auto found = std::find_if(legal.begin(), legal.end(), [&](const Move& move) {
        return move.from == selected_ && move.to == target;
    });
    if (found == legal.end()) {
        context_.audio.playEffect(5054);
        status_ = L"That move is blocked or does not use the dice correctly.";
        return;
    }
    const bool hitMario = found->to >= 0 && found->to < 24 && state_.points[found->to] == -1;
    beginPieceAnimation(1, *found);
    apply(state_, 1, *found);
    if (hitMario) context_.audio.playEffect(5010);
    if (hitMario) host_.play(11654, -11, -1);
    const auto die = std::find(dice_.begin(), dice_.end(), found->die);
    if (die != dice_.end()) dice_.erase(die);
    selected_ = -99; updateWinner();
    if (winner_) return;
    if (dice_.empty() || legalFirstMoves().empty()) finishHumanTurn();
    else {
        status_ = L"You can still move. Select another piece.";
        if (hitMario) host_.queue(11622); else host_.play(11622, -11, -1);
    }
}

bool BackgammonGame::sourceIdleRegressionTest() {
    const auto choose = [](std::uint32_t seed, int lastVisual, int lastLine) {
        SourceRandom random(seed);
        const BackgammonIdleChoice choice =
            chooseBackgammonIdle(random, lastVisual, lastLine);
        return std::tuple{choice.kind, choice.resource, lastVisual, lastLine, random.seed()};
    };

    // Seed one takes the visual path. Repeating bucket two forces the exact
    // rejection loop and its extra RNG call; seed two covers the speech path.
    return choose(1, -1, -1) ==
               std::tuple{BackgammonIdleKind::Visual, 9002, 2, -1,
                          std::uint32_t{282475249U}} &&
           choose(1, 2, -1) ==
               std::tuple{BackgammonIdleKind::Visual, 9000, 0, -1,
                          std::uint32_t{1622650073U}} &&
           choose(2, -1, -1) ==
               std::tuple{BackgammonIdleKind::Line, 11637, -1, 2,
                          std::uint32_t{564950498U}};
}

bool BackgammonGame::sourceDialogueRegressionTest() const {
    const auto hasMembers = [](auto pool, auto expected) {
        std::sort(pool.begin(), pool.end());
        std::sort(expected.begin(), expected.end());
        return pool == expected;
    };
    return hasMembers(openingGreetingPool_, std::array{11603, 11604, 11600}) &&
           hasMembers(computerThinkingPool_, std::array{11632, 11631, 11630}) &&
           hasMembers(computerNoMovePool_, std::array{11613, 11614, 11617});
}

bool BackgammonGame::sourceStartupRegressionTest() {
    const std::uint32_t savedSeed = context_.random.seed();
    context_.random.setSeed(1);
    reset(false);

    bool valid = startupPhase_ == StartupPhase::PreGreeting &&
                 startupDelayTicks_ == 2 && !host_.active() &&
                 std::all_of(setupVisiblePoints_.begin(), setupVisiblePoints_.end(),
                             [](int count) { return count == 0; });

    (void)tick();
    valid &= startupPhase_ == StartupPhase::PreGreeting && startupDelayTicks_ == 1 &&
             !host_.active();
    (void)tick();
    valid &= startupPhase_ == StartupPhase::PreGreeting && startupDelayTicks_ == 0 &&
             !host_.active();
    (void)tick();
    valid &= startupPhase_ == StartupPhase::Greeting && host_.active() &&
             openingGreetingPool_ == std::array{11603, 11604, 11600} &&
             openingGreetingPoolCursor_ == 1 &&
             context_.random.seed() == 282475249U;

    int guard = 0;
    while (startupPhase_ == StartupPhase::Greeting && guard++ < 100) (void)tick();
    valid &= startupPhase_ == StartupPhase::RollPrompt && host_.active();

    guard = 0;
    while (startupPhase_ != StartupPhase::Setup && guard++ < 200) {
        valid &= std::all_of(setupVisiblePoints_.begin(), setupVisiblePoints_.end(),
                             [](int count) { return count == 0; });
        (void)tick();
    }
    valid &= startupPhase_ == StartupPhase::Setup && !host_.active() &&
             setupStackSoundIndex_ == 0 && !setupStackRevealing_;

    int setupTicks = 0;
    while (startupPhase_ == StartupPhase::Setup && setupTicks < 100) {
        (void)tick();
        ++setupTicks;
    }
    valid &= startupPhase_ == StartupPhase::Complete && setupTicks == 46 &&
             setupVisiblePoints_ == state_.points;

    context_.random.setSeed(savedSeed);
    return valid;
}

bool BackgammonGame::sourceSetupRevealRegressionTest() {
    setupVisiblePoints_.fill(0);
    setupStackSoundIndex_ = 0;
    setupStackRemaining_ = 0;
    setupStackRevealing_ = false;
    qaSetupRevealPresentation_ = false;

    std::vector<int> soundStartTicks;
    std::vector<int> revealedPoints;
    int tickCount = 0;
    while (setupStackSoundIndex_ < kBackgammonSetupStacks.size() && tickCount < 100) {
        const auto before = setupVisiblePoints_;
        const bool wasRevealing = setupStackRevealing_;
        const std::size_t priorStack = setupStackSoundIndex_;
        if (!tickSetupReveal()) return false;
        ++tickCount;

        if (!wasRevealing && setupStackRevealing_ &&
            setupStackSoundIndex_ == priorStack) {
            soundStartTicks.push_back(tickCount);
        }
        for (int point = 0; point < 24; ++point) {
            const int delta = setupVisiblePoints_[point] - before[point];
            if (delta != 0) {
                if (std::abs(delta) != 1) return false;
                revealedPoints.push_back(point);
            }
        }
    }

    std::array<int, 24> expectedPoints{};
    std::vector<int> expectedRevealPoints;
    for (const BackgammonSetupStack& stack : kBackgammonSetupStacks) {
        expectedPoints[stack.point] = stack.player * stack.count;
        for (int checker = 0; checker < stack.count; ++checker) {
            expectedRevealPoints.push_back(stack.point);
        }
    }
    static constexpr std::array expectedSoundStartTicks{1, 5, 12, 17, 24, 31, 36, 43};
    return tickCount == 46 &&
           soundStartTicks == std::vector<int>(expectedSoundStartTicks.begin(),
                                               expectedSoundStartTicks.end()) &&
           revealedPoints == expectedRevealPoints &&
           setupVisiblePoints_ == expectedPoints && state_.points == expectedPoints;
}

bool BackgammonGame::sourceCheckerGeometryRegressionTest() {
    const auto at = [](int nativePoint, int frame, int stackIndex, int x, int y) {
        const Point actual = sourceCheckerPosition(nativePoint, frame, stackIndex);
        return actual.x == x && actual.y == y;
    };

    // The eight opening stacks prove the native/source point rotation and
    // both decoded coordinate tables against the retained original frame.
    if (!at(12, 0, 0, 47, 169) || !at(23, 0, 0, 441, 169) ||
        !at(4, 0, 0, 316, 322) || !at(6, 0, 0, 211, 322) ||
        !at(17, 1, 0, 212, 175) || !at(19, 1, 0, 302, 175) ||
        !at(0, 1, 0, 469, 333) || !at(11, 1, 0, 14, 333)) {
        return false;
    }

    // $558A/$5756 keep fifteen actors per point. Every block of five reuses
    // the vertical lane and moves two pixels sideways; it never clamps at
    // checker five or replaces the remaining actors with a numeric label.
    return at(12, 0, 4, 39, 209) && at(12, 0, 5, 45, 169) &&
           at(12, 0, 9, 37, 209) && at(12, 0, 10, 43, 169) &&
           at(12, 0, 14, 35, 209) &&
           at(23, 0, 4, 449, 209) && at(23, 0, 5, 443, 169) &&
           at(23, 0, 10, 445, 169) && at(23, 0, 14, 453, 209) &&
           at(0, 1, 4, 461, 289) && at(0, 1, 5, 471, 333) &&
           at(0, 1, 10, 473, 333) && at(0, 1, 14, 465, 289) &&
           at(11, 1, 4, 22, 289) && at(11, 1, 5, 12, 333) &&
           at(11, 1, 10, 10, 333) && at(11, 1, 14, 18, 289);
}

void BackgammonGame::setQaSetupRevealPresentation(int revealedCheckers) {
    qaSetupRevealPresentation_ = true;
    startupPhase_ = StartupPhase::Complete;
    host_.stop();
    setupVisiblePoints_.fill(0);
    int remaining = std::clamp(revealedCheckers, 0, 30);
    for (const BackgammonSetupStack& stack : kBackgammonSetupStacks) {
        const int visible = std::min(remaining, stack.count);
        setupVisiblePoints_[stack.point] = stack.player * visible;
        remaining -= visible;
    }
}

void BackgammonGame::render(Canvas& canvas) {
    canvas.clear(rgb(0, 0, 0));
    drawBackground(canvas, 4001);
    canvas.sprite(context_.graphics.sprite(4021),
                  dosEdition() ? 103 : 165, dosEdition() ? 9 : 18, false);
    if (!characterChooser_) (void)host_.render(canvas);
    canvas.pakText(context_.graphics,
                   context_.playerName.empty() ? L"PLAYER" : context_.playerName,
                   226, dosEdition() ? Rect{206, 9, 312, 27} : Rect{330, 21, 497, 48},
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE, 11);
    const int humanFrame = context_.playerIsYoshi ? 0 : 1;
    const int computerFrame = context_.playerIsYoshi ? 1 : 0;
    if (!characterChooser_) {
        // The source UI identifies each side with its piece and uses the cup as
        // the roll control. These are Pak 4011's original panel-sized frames.
        canvas.sprite(context_.graphics.sprite(4011, computerFrame == 0 ? 18 : 19),
                      dosEdition() ? 9 : 15, dosEdition() ? 59 : 113, false);
        canvas.sprite(context_.graphics.sprite(4011, humanFrame == 0 ? 18 : 19),
                      dosEdition() ? 284 : 455, dosEdition() ? 59 : 113, false);
        canvas.sprite(context_.graphics.sprite(4011, 2),
                      dosEdition() ? 284 : 455, dosEdition() ? 30 : 58, false);
    }
    const bool renderingSetup = qaSetupRevealPresentation_ ||
                                setupStackSoundIndex_ < kBackgammonSetupStacks.size();
    const std::array<int, 24>& renderedPoints =
        renderingSetup ? setupVisiblePoints_ : state_.points;
    if (!characterChooser_) for (int point = 0; point < 24; ++point) {
        const Rect area = pointRect(point);
        const int count = std::abs(renderedPoints[point]);
        const bool movingHere = !renderingSetup && pieceAnimation_.active &&
                                pieceAnimation_.destination == point &&
                                ((pieceAnimation_.player > 0 && state_.points[point] > 0) ||
                                 (pieceAnimation_.player < 0 && state_.points[point] < 0));
        const int visibleCount = std::max(0, count - (movingHere ? 1 : 0));
        for (int index = 0; index < std::min(visibleCount, 15); ++index) {
            const int frame = renderedPoints[point] > 0 ? humanFrame : computerFrame;
            const Sprite& sprite = context_.graphics.sprite(4011, frame);
            Point position = sourceCheckerPosition(point, frame, index);
            if (dosEdition()) position = {dosX(position.x), dosY(position.y)};
            canvas.sprite(sprite, position.x, position.y, false);
        }
        if (selected_ == point)
            canvas.outlineRect(area, rgb(255, 255, 0), dosEdition() ? 1 : 2);
    }
    if (!characterChooser_) for (int index = 0; index < state_.humanBar; ++index) {
        const Sprite& sprite = context_.graphics.sprite(4011, humanFrame);
        canvas.sprite(sprite, (dosEdition() ? 160 : 255) - sprite.width / 2,
                      (dosEdition() ? 141 : 270) + index * (dosEdition() ? 7 : 14), false);
    }
    if (!characterChooser_) for (int index = 0; index < state_.computerBar; ++index) {
        const Sprite& sprite = context_.graphics.sprite(4011, computerFrame);
        canvas.sprite(sprite, (dosEdition() ? 160 : 255) - sprite.width / 2,
                      (dosEdition() ? 96 : 184) + index * (dosEdition() ? 7 : 14), false);
    }
    if (!characterChooser_ && pieceAnimation_.active) {
        const unsigned elapsed = std::min(pieceAnimation_.elapsedMilliseconds, 360U);
        const int x = pieceAnimation_.from.x +
                      (pieceAnimation_.to.x - pieceAnimation_.from.x) *
                          static_cast<int>(elapsed) / 360;
        const int y = pieceAnimation_.from.y +
                      (pieceAnimation_.to.y - pieceAnimation_.from.y) *
                          static_cast<int>(elapsed) / 360;
        canvas.sprite(context_.graphics.sprite(4011, pieceAnimation_.frame), x, y, false);
    }
    if (!characterChooser_ && (diceRoll_.active() || secondDiceRoll_.active())) {
        (void)diceRoll_.render(canvas);
        (void)secondDiceRoll_.render(canvas);
    } else if (!characterChooser_) for (std::size_t index = 0; index < dice_.size() && index < 2; ++index) {
        int base = diceOwner_ > 0 ? 10 : 4;
        if (diceOwner_ == 0) base = index == 0 ? 10 : 4;
        canvas.sprite(context_.graphics.sprite(4011, base + std::clamp(dice_[index] - 1, 0, 5)),
                      (dosEdition() ? 53 : 84) + static_cast<int>(index) *
                          (dosEdition() ? 52 : 83),
                      dosEdition() ? 43 : 83, false);
    }
    if (!characterChooser_) {
        (void)victoryLeft_.render(canvas);
        (void)victoryRight_.render(canvas);
    }
    canvas.pakText(context_.graphics,
                   characterChooser_ ? L"Do you want to play as a Yoshi, or as a Koopa?" : status_,
                   224, dosEdition() ? Rect{3, 188, 317, 200} : Rect{5, 359, 507, 383});
}

Point BackgammonGame::checkerPosition(int point, int player, int stackIndex,
                                       const Sprite& sprite) const {
    if (point >= 0 && point < 24) {
        const int frame = sprite.height >= 30 ? 0 : 1;
        Point position = sourceCheckerPosition(point, frame, stackIndex);
        if (dosEdition()) position = {dosX(position.x), dosY(position.y)};
        return position;
    }
    if ((player > 0 && point == 24) || (player < 0 && point == -1)) {
        return {(dosEdition() ? 160 : 255) - sprite.width / 2,
                (player > 0 ? (dosEdition() ? 141 : 270) : (dosEdition() ? 96 : 184)) +
                    std::max(0, stackIndex) * (dosEdition() ? 7 : 14)};
    }
    return {dosEdition() ? 5 : 8,
            player > 0 ? (dosEdition() ? 172 : 330) - sprite.height
                       : (dosEdition() ? 99 : 190)};
}

void BackgammonGame::beginPieceAnimation(int player, const Move& move) {
    if (move.from == 24 || move.from == -1) {
        // $17C8 removes the entering checker from the bar and starts 5072.
        context_.audio.playSound(5072);
    }
    // $4AAA is the shared checker-motion path and starts snd 5034 for either side.
    context_.audio.playEffect(5034);
    if (!animatedPieces_) {
        pieceAnimation_ = {};
        return;
    }
    const int frame = player > 0 ? (context_.playerIsYoshi ? 0 : 1)
                                 : (context_.playerIsYoshi ? 1 : 0);
    const Sprite& sprite = context_.graphics.sprite(4011, frame);
    int sourceIndex = 0;
    if (move.from >= 0 && move.from < 24) sourceIndex = std::abs(state_.points[move.from]) - 1;
    else sourceIndex = player > 0 ? state_.humanBar - 1 : state_.computerBar - 1;
    int destinationIndex = 0;
    if (move.to >= 0 && move.to < 24) destinationIndex = std::abs(state_.points[move.to]);
    else destinationIndex = player > 0 ? state_.humanOff : state_.computerOff;
    pieceAnimation_ = {true, player, move.to, frame,
                       checkerPosition(move.from, player, sourceIndex, sprite),
                       checkerPosition(move.to, player, destinationIndex, sprite), 0};
}

}  // namespace mf
