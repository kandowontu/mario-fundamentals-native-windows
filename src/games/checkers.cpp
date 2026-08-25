#include "games/checkers.hpp"

#include "audio_catalog.hpp"

namespace mf {

namespace {

constexpr std::array<Point, 4> kSourceDirections{{
    {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
}};

constexpr std::array<std::array<int, 5>, 4> kSourceIdleJokes{{
    {{11704, 11705, 11706, 11707, 11708}},
    {{11702, 11703, 11709, 11710, 11711}},
    {{11702, 11703, 11724, 11725, 11726}},
    {{11702, 11703, 11727, 11728, 11729}},
}};

enum class SourceIdleChoiceKind {
    Visual,
    SingleLine,
    Joke,
};

struct SourceIdleChoice {
    SourceIdleChoiceKind kind{};
    int value{};
};

SourceIdleChoice chooseSourceIdle(SourceRandom& random,
                                  std::vector<int>& visualPool,
                                  std::size_t& visualCursor,
                                  int& jokeIndex) {
    // CODE 16 $2880 uses an inclusive > 50 test.  Since $352C returns
    // 0..99, the visual branch has 49 outcomes and the dialogue branch 51.
    if (random.below(100) > 50) {
        if (visualCursor >= visualPool.size()) {
            sourceShuffle(visualPool, random);
            visualCursor = 0;
        }
        return {SourceIdleChoiceKind::Visual, visualPool[visualCursor++]};
    }

    const int dialogue = random.below(100);
    if (dialogue < 30) return {SourceIdleChoiceKind::SingleLine, 11700};
    if (dialogue < 50) return {SourceIdleChoiceKind::SingleLine, 11701};

    ++jokeIndex;
    if (jokeIndex < 0 || jokeIndex >= static_cast<int>(kSourceIdleJokes.size())) {
        jokeIndex = 0;
    }
    return {SourceIdleChoiceKind::Joke, jokeIndex};
}

int pieceOwner(int piece) {
    return (piece > 0) - (piece < 0);
}

bool sourceDirectionAllowed(int piece, int direction) {
    if (std::abs(piece) == 2) return true;
    return piece > 0 ? direction < 2 : direction >= 2;
}

bool sourceChaseBonus(int from, int to, int chaseSquare) {
    if (chaseSquare < 0) return false;
    const int chaseX = chaseSquare % 8;
    const int chaseY = chaseSquare / 8;
    return std::abs(from % 8 - chaseX) > std::abs(to % 8 - chaseX) &&
           std::abs(from / 8 - chaseY) > std::abs(to / 8 - chaseY);
}

}  // namespace

CheckersGame::CheckersGame(GameContext context)
    : Game(context), host_(context.assets, context.graphics, context.audio, true,
          [](int resourceId, Point scaled) {
              if (resourceId >= 10000) return Point{116, 1};
              if (resourceId >= 9000 && resourceId <= 9002) return Point{31, 7};
              return scaled;
          }) { reset(); }

void CheckersGame::reset(bool preserveSession) {
    const int priorMarioWinCount = preserveSession ? marioWinCount_ : 0;
    resetSourceIdle(!preserveSession);
    board_.fill(0);
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 8; ++x)
        if ((x + y) & 1) board_[y * 8 + x] = -1;
    for (int y = 5; y < 8; ++y) for (int x = 0; x < 8; ++x)
        if ((x + y) & 1) board_[y * 8 + x] = 1;
    turn_ = 1;
    selected_ = continuation_ = -1;
    winner_ = quietPlies_ = jumpsThisTurn_ = computerDelayMilliseconds_ = 0;
    lastHumanDestination_ = -1;
    marioWinCount_ = priorMarioWinCount;
    outcomeDelayTicks_ = outcomeWipeSquare_ = 0;
    computerPlan_.clear();
    outcomeMoviesPlayed_.clear();
    outcomeKind_ = OutcomeKind::None;
    outcomePhase_ = OutcomePhase::None;
    pieceAnimation_ = {};
    status_ = L"It's your turn.";
    host_.play(11012, -12, -1);
}

void CheckersGame::resetForReplay() {
    reset(true);
}

std::vector<CheckersGame::Move> CheckersGame::pieceMoves(int square, bool capturesOnly) const {
    std::vector<Move> result;
    const int piece = board_[square];
    if (!piece) return result;
    const int x = square % 8;
    const int y = square / 8;
    std::array<int, 2> vertical{};
    int directionCount = 1;
    if (std::abs(piece) == 2) { vertical = {-1, 1}; directionCount = 2; }
    else vertical[0] = piece > 0 ? -1 : 1;
    for (int directionIndex = 0; directionIndex < directionCount; ++directionIndex) {
        const int dy = vertical[directionIndex];
        for (const int dx : {-1, 1}) {
            const int nearX = x + dx;
            const int nearY = y + dy;
            if (nearX < 0 || nearX >= 8 || nearY < 0 || nearY >= 8) continue;
            const int adjacentSquare = nearY * 8 + nearX;
            if (!capturesOnly && board_[adjacentSquare] == 0) result.push_back({square, adjacentSquare, -1});
            const int farX = x + dx * 2;
            const int farY = y + dy * 2;
            if (farX < 0 || farX >= 8 || farY < 0 || farY >= 8) continue;
            const int landingSquare = farY * 8 + farX;
            if (owner(board_[adjacentSquare]) == -owner(piece) && board_[landingSquare] == 0) {
                result.push_back({square, landingSquare, adjacentSquare});
            }
        }
    }
    if (capturesOnly) {
        std::erase_if(result, [](const Move& move) { return move.captured < 0; });
    }
    return result;
}

std::vector<CheckersGame::Move> CheckersGame::legalMoves(int player, int onlyPiece) const {
    std::vector<Move> captures;
    std::vector<Move> ordinary;
    for (int square = 0; square < 64; ++square) {
        if (owner(board_[square]) != player || (onlyPiece >= 0 && square != onlyPiece)) continue;
        for (const Move& move : pieceMoves(square, false)) {
            (move.captured >= 0 ? captures : ordinary).push_back(move);
        }
    }
    if (captures.empty()) return ordinary;
    if (forcedJumps_ || onlyPiece >= 0) return captures;
    captures.insert(captures.end(), ordinary.begin(), ordinary.end());
    return captures;
}

void CheckersGame::apply(const Move& move) {
    int piece = board_[move.from];
    if (animatedPieces_) {
        pieceAnimation_ = {true, move.from, move.to, piece, 0};
    }
    board_[move.from] = 0;
    board_[move.to] = piece;
    if (move.captured >= 0) {
        board_[move.captured] = 0;
        quietPlies_ = 0;
    } else {
        ++quietPlies_;
    }
    const int row = move.to / 8;
    if (piece == 1 && row == 0) board_[move.to] = 2;
    if (piece == -1 && row == 7) board_[move.to] = -2;
}

void CheckersGame::updateWinner() {
    if (legalMoves(turn_).empty()) {
        winner_ = -turn_;
        status_ = winner_ > 0 ? L"Congratulations! You won!" : L"Looks like Mario wins this time.";
        if (winner_ > 0) {
            const bool marioHasPieces = std::any_of(board_.begin(), board_.end(),
                [](int piece) { return piece < 0; });
            beginOutcome(marioHasPieces ? OutcomeKind::HumanStuckMario
                                        : OutcomeKind::HumanEliminatedMario);
        } else {
            beginOutcome(OutcomeKind::MarioWin);
        }
    }
}

int CheckersGame::drawSelector(std::vector<int>& resources, std::size_t& cursor) {
    if (resources.empty()) throw std::runtime_error("invalid Checkers source outcome pool");
    if (cursor >= resources.size()) {
        sourceShuffle(resources, context_.random);
        cursor = 0;
    }
    return resources[cursor++];
}

void CheckersGame::resetSourceIdle(bool resetSequence) {
    idlePhase_ = IdlePhase::Waiting;
    idleElapsedSourceTicks_ = 0;
    idleTargetSourceTicks_ = 120;
    idleGapSourceTicks_ = 0;
    idleJokePart_ = 0;
    if (resetSequence) {
        idleVisualPool_ = {9000, 9001, 9002};
        idleVisualPoolCursor_ = idleVisualPool_.size();
        idleJokeIndex_ = 4;
    }
}

void CheckersGame::cancelSourceIdle() {
    // A mouse event passes the source controller's cancel flag.  Only stop
    // the host when the controller itself owns it; ordinary move dialogue is
    // still allowed to finish before another board click is accepted.
    if (idlePhase_ != IdlePhase::Waiting) host_.stop();
    idlePhase_ = IdlePhase::Waiting;
    idleElapsedSourceTicks_ = 0;
    idleGapSourceTicks_ = 0;
    idleJokePart_ = 0;
}

void CheckersGame::scheduleNextSourceIdle() {
    // CODE 16 $2B62 installs a random 60..309 source-tick delay after speech.
    idleTargetSourceTicks_ = context_.random.below(250) + 60U;
    idleElapsedSourceTicks_ = 0;
    idlePhase_ = IdlePhase::Waiting;
}

bool CheckersGame::tickSourceIdle(bool eligible) {
    // The classic controller advances at the 60 Hz TickCount cadence while
    // the native window timer runs at 33 ms.  Two source ticks per callback
    // preserves the source's 120-tick opening and 500-tick visual delays.
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
        const SourceIdleChoice choice = chooseSourceIdle(
            context_.random, idleVisualPool_, idleVisualPoolCursor_, idleJokeIndex_);
        if (choice.kind == SourceIdleChoiceKind::Visual) {
            // The movie headers place the full-body Mario at (165,18); CODE
            // 16 $2924 then applies the source's explicit (+3,-2) adjustment.
            host_.play(choice.value, -42, 43);
            idlePhase_ = IdlePhase::Visual;
        } else if (choice.kind == SourceIdleChoiceKind::SingleLine) {
            status_ = choice.value == 11700 ? L"Hey, what's taking so long?"
                                            : L"Do you still want to play?";
            host_.play(choice.value, -12, -1);
            idlePhase_ = IdlePhase::SingleLine;
        } else {
            status_ = L"Knock knock...";
            idleJokePart_ = 0;
            idleGapSourceTicks_ = 5;
            host_.play(kSourceIdleJokes[static_cast<std::size_t>(choice.value)][0],
                       -12, -1);
            idlePhase_ = IdlePhase::Joke;
        }
        return true;
    }
    case IdlePhase::Visual:
        if (host_.active()) return false;
        // A completed full-body animation waits 500 source ticks before the
        // controller is eligible again ($2912-$291A).
        idleTargetSourceTicks_ = 500;
        idleElapsedSourceTicks_ = 0;
        idlePhase_ = IdlePhase::Waiting;
        return true;
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
        host_.play(kSourceIdleJokes[static_cast<std::size_t>(idleJokeIndex_)]
                                   [static_cast<std::size_t>(idleJokePart_)],
                   -12, -1);
        return true;
    }
    return false;
}

void CheckersGame::playOutcomeMovie(int resourceId) {
    outcomeMoviesPlayed_.push_back(resourceId);
    host_.play(resourceId, -12, -1);
}

void CheckersGame::beginOutcome(OutcomeKind kind) {
    if (!winner_ || outcomePhase_ != OutcomePhase::None) return;
    outcomeKind_ = kind;
    outcomeMoviesPlayed_.clear();
    outcomeWipeSquare_ = 0;
    outcomeDelayTicks_ = 5;
    if (kind == OutcomeKind::MarioWin) ++marioWinCount_;
    // CODE 16 $0F86 stops any in-flight commentary before its five-tick
    // result delay, so a crowning or move prompt cannot overlap the ending.
    host_.stop();
    outcomePhase_ = OutcomePhase::InitialDelay;
}

bool CheckersGame::finished() const {
    return outcomePhase_ == OutcomePhase::Complete;
}

bool CheckersGame::tickOutcome() {
    if (outcomePhase_ == OutcomePhase::None || outcomePhase_ == OutcomePhase::Complete) {
        return false;
    }

    switch (outcomePhase_) {
    case OutcomePhase::InitialDelay:
        if (host_.active()) return false;
        if (outcomeDelayTicks_-- > 0) return true;
        outcomePhase_ = OutcomePhase::Announcement;
        return true;
    case OutcomePhase::Announcement:
        if (host_.active()) return false;
        if (outcomeKind_ == OutcomeKind::HumanEliminatedMario) {
            // $0FB0 / -$1C8E: source indices 50..57, 43, and 45.
            playOutcomeMovie(drawSelector(humanWinPool_, humanWinPoolCursor_));
            outcomePhase_ = OutcomePhase::PlayerBoardWipe;
        } else if (outcomeKind_ == OutcomeKind::HumanStuckMario) {
            // $1164: source index 49, "I can't move anywhere."
            playOutcomeMovie(11636);
            outcomeDelayTicks_ = 6;
            outcomePhase_ = OutcomePhase::PreReplayDelay;
        } else {
            if (marioWinCount_ == 1) {
                // $113C: the first Mario win uses source index 48, "I won!"
                playOutcomeMovie(11058);
            } else {
                // -$1C76: later Mario wins cycle through 48/57/47/52/46.
                playOutcomeMovie(drawSelector(laterMarioWinPool_, laterMarioWinPoolCursor_));
            }
            outcomeDelayTicks_ = 6;
            outcomePhase_ = OutcomePhase::PreReplayDelay;
        }
        return true;
    case OutcomePhase::PlayerBoardWipe:
        if (host_.active()) return false;
        // $0FCE..$1060 clears one occupied source square per controller pass
        // and fires the original click effect for each removed piece.
        while (outcomeWipeSquare_ < static_cast<int>(board_.size())) {
            const int square = outcomeWipeSquare_++;
            if (!board_[square]) continue;
            board_[square] = 0;
            context_.audio.playEffect(5003);
            return true;
        }
        selected_ = continuation_ = -1;
        context_.audio.playMusic(dosEdition() ? audio_catalog::kDosPlayerWinMusic[2]
                                              : audio_catalog::kPlayerWinMusic[2]);
        outcomeDelayTicks_ = 6;
        outcomePhase_ = OutcomePhase::PreReplayDelay;
        return true;
    case OutcomePhase::PreReplayDelay:
        if (host_.active()) return false;
        if (outcomeDelayTicks_-- > 0) return true;
        outcomePhase_ = OutcomePhase::ReplayPrompt;
        return true;
    case OutcomePhase::ReplayPrompt: {
        if (host_.active()) return false;
        // $1196 / -$1B9E uses the source two-entry shuffled replay selector.
        playOutcomeMovie(drawSelector(replayPool_, replayPoolCursor_));
        outcomePhase_ = OutcomePhase::ReplayWait;
        return true;
    }
    case OutcomePhase::ReplayWait:
        if (host_.active()) return false;
        outcomePhase_ = OutcomePhase::Complete;
        return true;
    case OutcomePhase::None:
    case OutcomePhase::Complete:
        return false;
    }
    return false;
}

void CheckersGame::finishTurn() {
    selected_ = continuation_ = -1;
    jumpsThisTurn_ = 0;
    computerPlan_.clear();
    turn_ = -turn_;
    updateWinner();
    if (!winner_) {
        status_ = turn_ > 0 ? L"It's your turn." : L"It's my turn now.";
        if (turn_ < 0) computerDelayMilliseconds_ = 700;
    }
}

std::vector<CheckersGame::TurnMove> CheckersGame::sourceTurnMoves(
        const std::array<int, 64>& board, int player, bool forcedCaptures) {
    std::vector<TurnMove> captures;
    std::vector<TurnMove> ordinary;

    const auto appendCaptures = [&](auto&& self, const std::array<int, 64>& position,
                                    int square, int movingPiece, TurnMove path) -> void {
        bool extended = false;
        const int x = square % 8;
        const int y = square / 8;
        for (int direction = 0; direction < 4; ++direction) {
            if (!sourceDirectionAllowed(movingPiece, direction)) continue;
            const int dx = kSourceDirections[direction].x;
            const int dy = kSourceDirections[direction].y;
            const int nearX = x + dx;
            const int nearY = y + dy;
            const int farX = x + dx * 2;
            const int farY = y + dy * 2;
            if (farX < 0 || farX >= 8 || farY < 0 || farY >= 8 ||
                nearX < 0 || nearX >= 8 || nearY < 0 || nearY >= 8) continue;
            const int captured = nearY * 8 + nearX;
            const int landing = farY * 8 + farX;
            if (pieceOwner(position[captured]) != -player || position[landing] != 0) continue;

            extended = true;
            auto next = position;
            next[square] = 0;
            next[captured] = 0;
            next[landing] = movingPiece;
            TurnMove continuation = path;
            continuation.steps.push_back({square, landing, captured});
            if (!forcedCaptures) captures.push_back(continuation);
            self(self, next, landing, movingPiece, std::move(continuation));
        }
        // With forced jumps enabled CODE 16 keeps only completed jump paths.
        // With the option disabled each prefix was already emitted above, so
        // adding the terminal path again would duplicate the full sequence.
        if (!extended && !path.steps.empty() && forcedCaptures) {
            captures.push_back(std::move(path));
        }
    };

    // CODE 16 walks its 32-square table in row-major playable-square order.
    // For each piece it tries UL, UR, DL, DR (jump directions 4..7 use the
    // same order), then makes a second board pass for ordinary moves.
    for (int square = 0; square < 64; ++square) {
        const int piece = board[square];
        if (pieceOwner(piece) != player || ((square % 8 + square / 8) & 1) == 0) continue;
        appendCaptures(appendCaptures, board, square, piece, {});
    }
    if (forcedCaptures && !captures.empty()) return captures;

    for (int square = 0; square < 64; ++square) {
        const int piece = board[square];
        if (pieceOwner(piece) != player || ((square % 8 + square / 8) & 1) == 0) continue;
        const int x = square % 8;
        const int y = square / 8;
        for (int direction = 0; direction < 4; ++direction) {
            if (!sourceDirectionAllowed(piece, direction)) continue;
            const int toX = x + kSourceDirections[direction].x;
            const int toY = y + kSourceDirections[direction].y;
            if (toX < 0 || toX >= 8 || toY < 0 || toY >= 8) continue;
            const int to = toY * 8 + toX;
            if (board[to] == 0) ordinary.push_back({{{square, to, -1}}, 0});
        }
    }
    captures.insert(captures.end(), ordinary.begin(), ordinary.end());
    return captures;
}

std::array<int, 64> CheckersGame::sourceApplyTurn(
        const std::array<int, 64>& board, const TurnMove& turn) {
    auto result = board;
    if (turn.steps.empty()) return result;
    const int piece = result[turn.steps.front().from];
    for (const Move& move : turn.steps) {
        result[move.from] = 0;
        if (move.captured >= 0) result[move.captured] = 0;
        result[move.to] = piece;
    }
    const int destination = turn.steps.back().to;
    if (piece == 1 && destination / 8 == 0) result[destination] = 2;
    if (piece == -1 && destination / 8 == 7) result[destination] = -2;
    return result;
}

int CheckersGame::sourceImmediateScore(const std::array<int, 64>& board,
                                       const TurnMove& turn, int player,
                                       int chaseSquare) {
    if (turn.steps.empty()) return 0;
    int score = 0;
    auto position = board;
    const int piece = position[turn.steps.front().from];
    for (const Move& move : turn.steps) {
        if (move.captured >= 0) {
            score += 2;
            if (std::abs(position[move.captured]) == 2) score += 2;
            position[move.captured] = 0;
        }
        position[move.from] = 0;
        position[move.to] = piece;
    }
    const int destination = turn.steps.back().to;
    const bool crowns = std::abs(piece) == 1 &&
                        ((player > 0 && destination / 8 == 0) ||
                         (player < 0 && destination / 8 == 7));
    if (crowns) score += 4;
    if (sourceChaseBonus(turn.steps.front().from, destination, chaseSquare)) ++score;
    return score;
}

std::optional<CheckersGame::TurnMove> CheckersGame::sourceBestTurn(
        const std::array<int, 64>& board, int player, int depth, int rootDepth,
        int chaseSquare, SourceRandom& random) {
    const auto candidates = sourceTurnMoves(board, player, true);
    std::optional<TurnMove> best;
    for (TurnMove candidate : candidates) {
        candidate.score = sourceImmediateScore(
            board, candidate, player, depth == rootDepth ? chaseSquare : -1);
        const auto result = sourceApplyTurn(board, candidate);
        const bool opponentHasPiece = std::any_of(result.begin(), result.end(),
            [player](int piece) { return pieceOwner(piece) == -player; });
        if (!opponentHasPiece) {
            candidate.score = 300;
        } else if (depth > 1) {
            const auto reply = sourceBestTurn(result, -player, depth - 1,
                                              rootDepth, -1, random);
            if (reply) {
                if (reply->score == 300) candidate.score = -300;
                else if (reply->score == -300) candidate.score = 300;
                else candidate.score -= reply->score;
            }
        }
        if (!best || candidate.score > best->score ||
            (candidate.score == best->score && random.below(256) > 127)) {
            best = std::move(candidate);
        }
    }
    return best;
}

int CheckersGame::sourceSearchDepth() const {
    const int pieces = static_cast<int>(std::count_if(board_.begin(), board_.end(),
        [](int piece) { return piece != 0; }));
    const bool hasKing = std::any_of(board_.begin(), board_.end(),
        [](int piece) { return std::abs(piece) == 2; });
    // The source normally searches the medium four-ply setting and raises the
    // late king endgame to its six-ply cap.
    return hasKing && pieces < 4 ? 6 : 4;
}

void CheckersGame::computerTurn() {
    if (winner_ || turn_ >= 0) return;
    if (computerPlan_.empty()) {
        const int depth = sourceSearchDepth();
        const auto selected = sourceBestTurn(board_, -1, depth, depth,
                                             lastHumanDestination_, context_.random);
        if (!selected || selected->steps.empty()) { finishTurn(); return; }
        computerPlan_ = selected->steps;
    }
    const Move move = computerPlan_.front();
    computerPlan_.erase(computerPlan_.begin());
    const bool crowns = board_[move.from] == -1 && move.to / 8 == 7;
    apply(move);
    context_.audio.playEffect(5003);
    if (move.captured >= 0) ++jumpsThisTurn_;
    if (!computerPlan_.empty()) {
        continuation_ = move.to;
        computerDelayMilliseconds_ = 500;
        status_ = L"Mario has another jump.";
        return;
    }
    if (crowns) host_.play(11190, -12, -1);
    const int completedJumps = jumpsThisTurn_;
    finishTurn();
    if (winner_) return;
    if (crowns) host_.queue(11012);
    else if (completedJumps >= 3) { host_.play(11185, -12, -1); host_.queue(11012); }
    else if (completedJumps == 2) { host_.play(11184, -12, -1); host_.queue(11012); }
    else host_.play(11012, -12, -1);
}

bool CheckersGame::tick() {
    bool changed = host_.tick();
    if (pieceAnimation_.active) {
        pieceAnimation_.elapsedMilliseconds += 33;
        if (pieceAnimation_.elapsedMilliseconds >= 330U) pieceAnimation_.active = false;
        changed = true;
    }
    if (outcomePhase_ != OutcomePhase::None && outcomePhase_ != OutcomePhase::Complete) {
        return tickOutcome() || changed;
    }
    if (!winner_ && turn_ < 0 && !host_.active() && !pieceAnimation_.active) {
        if (computerDelayMilliseconds_ > 0) {
            const int before = computerDelayMilliseconds_;
            computerDelayMilliseconds_ = std::max(0, computerDelayMilliseconds_ - 33);
            if (before != computerDelayMilliseconds_) changed = true;
        }
        if (computerDelayMilliseconds_ == 0) {
            computerTurn();
            changed = true;
        }
    }
    const bool idleEligible = !winner_ && !characterChooser_ && turn_ > 0 &&
                              !pieceAnimation_.active;
    changed |= tickSourceIdle(idleEligible);
    return changed;
}

Point CheckersGame::squareCenter(int square) const {
    const int row = square / 8;
    const int column = square % 8;
    if (dosEdition()) {
        const double yTop = 96.0 + row * 11.5;
        const double yMiddle = yTop + 5.75;
        const double progress = (yMiddle - 96.0) / 92.0;
        const double left = 34.0 - 33.0 * progress;
        const double right = 286.0 + 33.0 * progress;
        const double width = (right - left) / 8.0;
        return {static_cast<int>(left + (column + 0.5) * width),
                static_cast<int>(yMiddle)};
    }
    const double yTop = 186.0 + row * 21.5;
    const double yMiddle = yTop + 10.75;
    const double progress = (yMiddle - 186.0) / 172.0;
    const double left = 69.0 - 63.0 * progress;
    const double right = 443.0 + 63.0 * progress;
    const double width = (right - left) / 8.0;
    return {static_cast<int>(left + (column + 0.5) * width),
            static_cast<int>(yMiddle)};
}

int CheckersGame::hitSquare(Point point) const {
    if (dosEdition()) {
        if (point.y < 96 || point.y >= 188) return -1;
        const int row = std::clamp((point.y - 96) * 8 / 92, 0, 7);
        const double progress = (point.y - 96.0) / 92.0;
        const double left = 34.0 - 33.0 * progress;
        const double right = 286.0 + 33.0 * progress;
        if (point.x < left || point.x >= right) return -1;
        const int column = std::clamp(
            static_cast<int>((point.x - left) * 8.0 / (right - left)), 0, 7);
        return row * 8 + column;
    }
    if (point.y < 186 || point.y >= 358) return -1;
    const int row = std::clamp((point.y - 186) * 8 / 172, 0, 7);
    const double progress = (point.y - 186.0) / 172.0;
    const double left = 69.0 - 63.0 * progress;
    const double right = 443.0 + 63.0 * progress;
    if (point.x < left || point.x >= right) return -1;
    const int column = std::clamp(static_cast<int>((point.x - left) * 8.0 / (right - left)), 0, 7);
    return row * 8 + column;
}

void CheckersGame::click(Point point) {
    cancelSourceIdle();
    if (winner_ || turn_ < 0 || pieceAnimation_.active) return;
    const int square = hitSquare(point);
    if (square < 0) return;
    const auto moves = legalMoves(1, continuation_);
    if (owner(board_[square]) == 1 && (continuation_ < 0 || square == continuation_)) {
        const bool pieceCanMove = std::any_of(moves.begin(), moves.end(), [&](const Move& move) {
            return move.from == square;
        });
        if (!pieceCanMove) {
            const bool captureRequired = forcedJumps_ && std::any_of(moves.begin(), moves.end(),
                [](const Move& move) { return move.captured >= 0; });
            status_ = captureRequired ? L"You must make a jump now." : L"You can't go there.";
            host_.play(captureRequired ? 11082 : 11180, -12, -1);
            return;
        }
        selected_ = square;
        status_ = L"Click where you want to go.";
        host_.play(11080, -12, -1);
        return;
    }
    if (selected_ < 0) {
        if (owner(board_[square]) < 0) {
            status_ = L"Oops! You clicked on one of Mario's pieces!";
            host_.play(11084, -12, -1);
        } else {
            status_ = L"Click on the piece you want to move.";
            host_.play(11083, -12, -1);
        }
        return;
    }
    const auto found = std::find_if(moves.begin(), moves.end(), [&](const Move& move) {
        return move.from == selected_ && move.to == square;
    });
    if (found == moves.end()) {
        const bool captureRequired = forcedJumps_ && std::any_of(moves.begin(), moves.end(),
            [](const Move& move) { return move.captured >= 0; });
        status_ = captureRequired ? L"You must make a jump now." : L"Hey, that's not a legal move.";
        host_.play(captureRequired ? 11082 : 11085, -12, -1);
        return;
    }
    const bool captured = found->captured >= 0;
    const bool crowns = board_[found->from] == 1 && found->to / 8 == 0;
    apply(*found);
    lastHumanDestination_ = found->to;
    if (captured) ++jumpsThisTurn_;
    context_.audio.playEffect(5003);
    selected_ = square;
    if (captured && forcedJumps_ && !crowns && !pieceMoves(square, true).empty()) {
        continuation_ = square;
        status_ = L"You've got another jump you can make.";
        if (jumpsThisTurn_ >= 3) { host_.play(11185, -12, -1); host_.queue(11081); }
        else if (jumpsThisTurn_ == 2) { host_.play(11184, -12, -1); host_.queue(11081); }
        else host_.play(11081, -12, -1);
        return;
    }
    if (crowns) host_.play(11183, -12, -1);
    else if (jumpsThisTurn_ >= 3) host_.play(11185, -12, -1);
    else if (jumpsThisTurn_ == 2) host_.play(11184, -12, -1);
    finishTurn();
}

bool CheckersGame::sourceStrategyRegressionTest() const {
    SourceRandom random(0x434f4445U);

    std::array<int, 64> captureBoard{};
    captureBoard[2 * 8 + 1] = -1;
    captureBoard[3 * 8 + 2] = 1;
    captureBoard[5 * 8 + 4] = 1;
    const auto forced = sourceTurnMoves(captureBoard, -1, true);
    if (forced.size() != 1 || forced[0].steps.size() != 2 ||
        forced[0].steps[0].to != 4 * 8 + 3 || forced[0].steps[1].to != 6 * 8 + 5) {
        return false;
    }
    const auto optional = sourceTurnMoves(captureBoard, -1, false);
    const auto pathCount = [&](std::size_t steps) {
        return std::count_if(optional.begin(), optional.end(), [steps](const TurnMove& move) {
            return move.steps.size() == steps && move.steps.front().captured >= 0;
        });
    };
    const int ordinaryCount = static_cast<int>(std::count_if(
        optional.begin(), optional.end(), [](const TurnMove& move) {
            return move.steps.size() == 1 && move.steps.front().captured < 0;
        }));
    if (pathCount(1) != 1 || pathCount(2) != 1 || ordinaryCount != 1) return false;

    std::array<int, 64> kingBoard{};
    kingBoard[2 * 8 + 3] = -1;
    kingBoard[3 * 8 + 2] = 2;
    kingBoard[3 * 8 + 4] = 1;
    constexpr int depth = 1;
    const auto kingCapture = sourceBestTurn(kingBoard, -1, depth, depth, -1, random);
    if (!kingCapture || kingCapture->steps.empty() ||
        kingCapture->steps[0].captured != 3 * 8 + 2 || kingCapture->score != 4) {
        return false;
    }

    std::array<int, 64> crownBoard{};
    crownBoard[6 * 8 + 1] = -1;
    crownBoard[0 * 8 + 1] = 1;
    const auto crown = sourceBestTurn(crownBoard, -1, depth, depth, -1, random);
    return crown && !crown->steps.empty() && crown->steps[0].to / 8 == 7 &&
           crown->score == 4;
}

bool CheckersGame::sourceIdleRegressionTest() {
    const auto choose = [](std::uint32_t seed) {
        SourceRandom random(seed);
        std::vector<int> visuals{9000, 9001, 9002};
        std::size_t visualCursor = visuals.size();
        int jokeIndex = 4;
        const SourceIdleChoice choice =
            chooseSourceIdle(random, visuals, visualCursor, jokeIndex);
        return std::tuple{choice.kind, choice.value, jokeIndex, random.seed()};
    };

    // These four seeds cover every branch and prove the otherwise invisible
    // selector shuffle calls that keep the shared source seed synchronized.
    return choose(1) == std::tuple{SourceIdleChoiceKind::Visual, 9001, 4,
                                  std::uint32_t{1622650073U}} &&
           choose(3) == std::tuple{SourceIdleChoiceKind::SingleLine, 11700, 4,
                                  std::uint32_t{847425747U}} &&
           choose(26) == std::tuple{SourceIdleChoiceKind::SingleLine, 11701, 4,
                                   std::uint32_t{901905533U}} &&
           choose(2) == std::tuple{SourceIdleChoiceKind::Joke, 0, 0,
                                  std::uint32_t{564950498U}};
}

bool CheckersGame::sourceFullMatchRegressionTest() {
    const std::uint32_t savedSeed = context_.random.seed();
    const bool savedAnimatedPieces = animatedPieces_;
    const bool savedForcedJumps = forcedJumps_;
    const auto fail = [&]() {
        context_.random.setSeed(savedSeed);
        animatedPieces_ = savedAnimatedPieces;
        forcedJumps_ = savedForcedJumps;
        return false;
    };
    constexpr std::array<std::uint32_t, 4> seeds{
        1U, 0x434f4445U, 0x13579bdfU, 0x7ffffffeU};
    bool sawCapture = false;
    bool sawCrown = false;
    bool sawHumanMove = false;
    bool sawMarioMove = false;

    animatedPieces_ = true;
    forcedJumps_ = true;
    for (const std::uint32_t seed : seeds) {
        context_.random.setSeed(seed);
        reset();
        int previousHumanPieces = 12;
        int previousMarioPieces = 12;
        bool matchHadHumanMove = false;
        bool matchHadMarioMove = false;

        // Fast-forward only host speech. Board input still enters through the
        // real two-click selector, piece movement still drains its 330 ms
        // actor, and Mario still advances through his delayed live plan.
        for (int controllerPass = 0; controllerPass < 60000 && !finished();
             ++controllerPass) {
            host_.stop();
            const auto before = board_;

            if (!winner_ && turn_ > 0 && !pieceAnimation_.active) {
                const auto moves = legalMoves(1, continuation_);
                if (moves.empty()) return fail();
                const Move move = moves.front();
                click(squareCenter(move.from));
                host_.stop();
                click(squareCenter(move.to));
            }

            (void)tick();
            int humanPieces = 0;
            int marioPieces = 0;
            for (int square = 0; square < static_cast<int>(board_.size()); ++square) {
                const int piece = board_[static_cast<std::size_t>(square)];
                if (piece < -2 || piece > 2) return fail();
                if (piece != 0 && ((square % 8 + square / 8) & 1) == 0) return fail();
                humanPieces += piece > 0;
                marioPieces += piece < 0;
                sawCrown |= std::abs(piece) == 2;
            }
            if (humanPieces > previousHumanPieces || marioPieces > previousMarioPieces ||
                humanPieces > 12 || marioPieces > 12) return fail();
            sawCapture |= humanPieces < previousHumanPieces || marioPieces < previousMarioPieces;
            previousHumanPieces = humanPieces;
            previousMarioPieces = marioPieces;

            if (board_ != before) {
                if (turn_ < 0 || (pieceAnimation_.active && pieceAnimation_.piece > 0)) {
                    sawHumanMove = true;
                    matchHadHumanMove = true;
                }
                if (turn_ > 0 || (pieceAnimation_.active && pieceAnimation_.piece < 0)) {
                    sawMarioMove = true;
                    matchHadMarioMove = true;
                }
            }
            if (selected_ < -1 || selected_ >= 64 || continuation_ < -1 ||
                continuation_ >= 64 || (winner_ < -1 || winner_ > 1)) return fail();
        }

        if (!finished() || winner_ == 0 || !matchHadHumanMove || !matchHadMarioMove)
            return fail();
    }

    context_.random.setSeed(savedSeed);
    animatedPieces_ = savedAnimatedPieces;
    forcedJumps_ = savedForcedJumps;
    return sawCapture && sawCrown && sawHumanMove && sawMarioMove;
}

bool CheckersGame::sourceOutcomeRegressionTest(int outcomeVariant) {
    if (outcomeVariant != 1 && outcomeVariant != 2 &&
        outcomeVariant != -1 && outcomeVariant != -2) return false;

    host_.stop();
    pieceAnimation_ = {};
    computerPlan_.clear();
    winner_ = outcomeVariant > 0 ? 1 : -1;
    outcomePhase_ = OutcomePhase::None;
    if (outcomeVariant == -2) marioWinCount_ = 1;
    const OutcomeKind kind = outcomeVariant == 2
        ? OutcomeKind::HumanStuckMario
        : outcomeVariant > 0 ? OutcomeKind::HumanEliminatedMario
                             : OutcomeKind::MarioWin;
    beginOutcome(kind);

    bool sawBoardWipe = false;
    for (int tickIndex = 0; tickIndex < 2400 && !finished(); ++tickIndex) {
        sawBoardWipe |= outcomePhase_ == OutcomePhase::PlayerBoardWipe;
        (void)tick();
    }
    if (!finished() || outcomeMoviesPlayed_.size() != 2 ||
        (outcomeMoviesPlayed_.back() != 11073 && outcomeMoviesPlayed_.back() != 11074)) {
        return false;
    }
    const auto in = [](int value, std::span<const int> pool) {
        return std::find(pool.begin(), pool.end(), value) != pool.end();
    };
    if (outcomeVariant == 1) {
        static constexpr std::array playerPool{
            11063, 11064, 11065, 11067, 11068,
            11069, 11071, 11072, 11053, 11055};
        return sawBoardWipe && in(outcomeMoviesPlayed_.front(), playerPool) &&
               std::none_of(board_.begin(), board_.end(), [](int piece) { return piece != 0; });
    }
    if (outcomeVariant == 2) {
        return !sawBoardWipe && outcomeMoviesPlayed_.front() == 11636;
    }
    if (outcomeVariant == -1) {
        return !sawBoardWipe && outcomeMoviesPlayed_.front() == 11058;
    }
    static constexpr std::array laterMarioPool{11058, 11072, 11057, 11065, 11056};
    return !sawBoardWipe && in(outcomeMoviesPlayed_.front(), laterMarioPool);
}

bool CheckersGame::sourceReplayRegressionTest() {
    marioWinCount_ = 2;
    resetForReplay();
    return marioWinCount_ == 2 && winner_ == 0 && turn_ == 1 &&
           outcomePhase_ == OutcomePhase::None &&
           std::count_if(board_.begin(), board_.end(), [](int piece) { return piece > 0; }) == 12 &&
           std::count_if(board_.begin(), board_.end(), [](int piece) { return piece < 0; }) == 12;
}

void CheckersGame::setQaOutcomePresentation(int outcomeVariant) {
    host_.stop();
    pieceAnimation_ = {};
    computerPlan_.clear();
    winner_ = outcomeVariant > 0 ? 1 : -1;
    status_ = winner_ > 0 ? L"Congratulations! You won!" : L"Looks like Mario wins this time.";
    outcomePhase_ = OutcomePhase::None;
    if (outcomeVariant == -2) marioWinCount_ = 1;
    beginOutcome(outcomeVariant == 2 ? OutcomeKind::HumanStuckMario
                                    : outcomeVariant > 0 ? OutcomeKind::HumanEliminatedMario
                                                         : OutcomeKind::MarioWin);
}

void CheckersGame::render(Canvas& canvas) {
    canvas.clear(rgb(0, 0, 0));
    drawBackground(canvas, 2999);
    canvas.sprite(context_.graphics.sprite(9000),
                  dosEdition() ? 103 : 165, dosEdition() ? 9 : 18, false);
    if (!characterChooser_) (void)host_.render(canvas);
    canvas.pakText(context_.graphics,
                   context_.playerName.empty() ? L"PLAYER" : context_.playerName,
                   226, dosEdition() ? Rect{219, 10, 313, 27} : Rect{351, 23, 495, 48},
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE, 11);
    if (!characterChooser_) {
        for (int index = 0; index < 64; ++index) {
            const int piece = board_[index];
            if (!piece || (pieceAnimation_.active && index == pieceAnimation_.to)) continue;
            const Point center = squareCenter(index);
            const int frame = pieceFrame(piece);
            const Sprite& sprite = context_.graphics.sprite(2501, frame);
            canvas.sprite(sprite, center.x - sprite.width / 2,
                          center.y - sprite.height + (dosEdition() ? 7 : 13), false);
            if (index == selected_) {
                const int halfWidth = dosEdition() ? 10 : 20;
                const int top = dosEdition() ? 12 : 24;
                const int bottom = dosEdition() ? 8 : 15;
                canvas.outlineRect({center.x - halfWidth, center.y - top,
                                    center.x + halfWidth, center.y + bottom},
                                   rgb(255, 255, 0), dosEdition() ? 1 : 2);
            }
        }
        if (pieceAnimation_.active) {
            const Point from = squareCenter(pieceAnimation_.from);
            const Point to = squareCenter(pieceAnimation_.to);
            const unsigned elapsed = std::min(pieceAnimation_.elapsedMilliseconds, 330U);
            const int x = from.x + (to.x - from.x) * static_cast<int>(elapsed) / 330;
            const int y = from.y + (to.y - from.y) * static_cast<int>(elapsed) / 330;
            const Sprite& sprite = context_.graphics.sprite(2501, pieceFrame(pieceAnimation_.piece));
            canvas.sprite(sprite, x - sprite.width / 2,
                          y - sprite.height + (dosEdition() ? 7 : 13), false);
        }
        const int marioPortrait = context_.playerIsYoshi ? 13 : 12;
        const int playerPortrait = context_.playerIsYoshi ? 12 : 13;
        canvas.sprite(context_.graphics.sprite(2501, marioPortrait),
                      dosEdition() ? 8 : 16, dosEdition() ? 25 : 51, false);
        canvas.sprite(context_.graphics.sprite(2501, playerPortrait),
                      dosEdition() ? 281 : 449, dosEdition() ? 25 : 51, false);
    }
    canvas.pakText(context_.graphics,
                   characterChooser_ ? L"Do you want to play as a Yoshi, or as a Koopa?" : status_,
                   224, dosEdition() ? Rect{3, 188, 317, 200} : Rect{5, 359, 507, 383});
}

int CheckersGame::pieceFrame(int piece) const {
    const bool human = piece > 0;
    const bool drawYoshi = human ? context_.playerIsYoshi : !context_.playerIsYoshi;
    if (human) {
        return drawYoshi ? (std::abs(piece) == 2 ? 2 : 0)
                         : (std::abs(piece) == 2 ? 6 : 4);
    }
    return drawYoshi ? (std::abs(piece) == 2 ? 3 : 1)
                     : (std::abs(piece) == 2 ? 7 : 5);
}

}  // namespace mf
