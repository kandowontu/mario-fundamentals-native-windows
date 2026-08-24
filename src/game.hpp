#pragma once

#include "audio.hpp"
#include "canvas.hpp"
#include "movie.hpp"
#include "source_random.hpp"

namespace mf {

struct GameContext {
    const AssetStore& assets;
    GraphicsAssets& graphics;
    Audio& audio;
    SourceRandom& random;
    std::function<void()> returnToMenu;
    std::wstring playerName;
    bool playerIsYoshi{true};
};

class HostAnimation {
public:
    using DosPlacement = std::function<Point(int, Point)>;

    HostAnimation(const AssetStore& assets, GraphicsAssets& graphics, Audio& audio,
                  bool scaleDosCoordinates = true, DosPlacement dosPlacement = {})
        : assets_(assets), graphics_(graphics), audio_(audio),
          scaleDosCoordinates_(scaleDosCoordinates),
          dosPlacement_(std::move(dosPlacement)) {}

    void play(int resourceId, int x, int y, bool playAudio = true,
              std::optional<std::uint32_t> holdSourceTime = std::nullopt) {
        queuedMovies_.clear();
        if (scaleDosCoordinates_ && assets_.dialect() == AssetDialect::Dos) {
            x = x * kDosLogicalWidth / kLogicalWidth;
            y = y * kDosLogicalHeight / kLogicalHeight;
        }
        requestedX_ = x;
        requestedY_ = y;
        audioEnabled_ = playAudio;
        holdSourceTime_ = holdSourceTime;
        start(resourceId);
    }

    void showFrame(int resourceId, int x, int y, std::uint32_t sourceTime) {
        play(resourceId, x, y, false, sourceTime);
        elapsedMilliseconds_ = sourceTime * 1000U / movie_->timeScale();
        holdingFrame_ = true;
    }

    void queue(int resourceId) { queuedMovies_.push_back(resourceId); }

    void start(int resourceId) {
        movie_ = std::make_unique<Movie>(assets_, resourceId);
        Point placement{requestedX_, requestedY_};
        if (assets_.dialect() == AssetDialect::Dos && dosPlacement_) {
            placement = dosPlacement_(resourceId, placement);
        }
        x_ = placement.x;
        y_ = placement.y;
        elapsedMilliseconds_ = 0;
        holdingFrame_ = false;
        if (audioEnabled_) {
            bool first = true;
            for (int sound : movie_->soundsAtStart()) {
                if (first) audio_.playSound(sound);
                else audio_.playEffect(sound);
                first = false;
            }
        }
    }

    bool tick(unsigned milliseconds = 33) {
        if (!movie_) return false;
        if (holdingFrame_) return false;
        const std::uint32_t previous = std::min<std::uint32_t>(
            movie_->duration(), elapsedMilliseconds_ * movie_->timeScale() / 1000U);
        elapsedMilliseconds_ += milliseconds;
        const std::uint32_t current = std::min<std::uint32_t>(
            movie_->duration(), elapsedMilliseconds_ * movie_->timeScale() / 1000U);
        if (audioEnabled_) {
            // Opcode-7 events after time zero are authored effects within the
            // movie timeline (dice, footsteps, celebration hits, and so on).
            // They share the source's concurrent effect path; routing them as
            // tracked speech would stop the preceding cue and could keep the
            // replay controller waiting on an unrelated effect.
            for (int sound : movie_->soundsBetween(previous, current))
                audio_.playEffect(sound);
        }
        const std::uint32_t durationMilliseconds =
            (movie_->duration() * 1000U + movie_->timeScale() - 1U) / movie_->timeScale();
        if (elapsedMilliseconds_ >= durationMilliseconds) {
            if (!queuedMovies_.empty()) {
                const int next = queuedMovies_.front();
                queuedMovies_.erase(queuedMovies_.begin());
                start(next);
            } else if (holdSourceTime_) {
                elapsedMilliseconds_ = *holdSourceTime_ * 1000U / movie_->timeScale();
                holdingFrame_ = true;
            } else {
                movie_.reset();
            }
        }
        return true;
    }

    [[nodiscard]] bool render(Canvas& canvas) const {
        if (!movie_) return false;
        const std::uint32_t time = std::min<std::uint32_t>(
            movie_->duration() - 1,
            elapsedMilliseconds_ * movie_->timeScale() / 1000U);
        movie_->render(canvas, graphics_, time, x_, y_);
        return true;
    }

    [[nodiscard]] bool active() const noexcept { return movie_ != nullptr; }
    [[nodiscard]] bool playing() const noexcept {
        return movie_ != nullptr && !holdingFrame_;
    }

    void stop() noexcept {
        movie_.reset();
        queuedMovies_.clear();
        elapsedMilliseconds_ = 0;
        holdingFrame_ = false;
        holdSourceTime_.reset();
    }

private:
    const AssetStore& assets_;
    GraphicsAssets& graphics_;
    Audio& audio_;
    std::unique_ptr<Movie> movie_;
    std::vector<int> queuedMovies_;
    std::uint32_t elapsedMilliseconds_{};
    int x_{};
    int y_{};
    int requestedX_{};
    int requestedY_{};
    bool audioEnabled_{true};
    std::optional<std::uint32_t> holdSourceTime_;
    bool holdingFrame_{};
    bool scaleDosCoordinates_{true};
    DosPlacement dosPlacement_;
};

class Game {
public:
    explicit Game(GameContext context) : context_(std::move(context)) {}
    virtual ~Game() = default;

    virtual void render(Canvas& canvas) = 0;
    virtual void click(Point point) = 0;
    virtual void mouseDown(Point point) { click(point); }
    virtual void mouseMove(Point point) { (void)point; }
    virtual void mouseUp(Point point) { (void)point; }
    virtual void mouseCancel() {}
    virtual void key(unsigned virtualKey) { (void)virtualKey; }
    virtual bool tick() { return false; }
    virtual void resetForReplay() = 0;
    virtual void setCharacterChooser(bool enabled) { (void)enabled; }
    virtual void setAnimatedPieces(bool enabled) { animatedPieces_ = enabled; }
    virtual void setForcedJumps(bool enabled) { forcedJumps_ = enabled; }
    void setPlayerName(std::wstring_view name) { context_.playerName = name; }
    [[nodiscard]] virtual std::wstring title() const = 0;
    [[nodiscard]] virtual bool finished() const = 0;
    [[nodiscard]] virtual unsigned postFinishDelayMilliseconds() const noexcept { return 2000; }

protected:
    [[nodiscard]] bool dosEdition() const noexcept {
        return context_.assets.dialect() == AssetDialect::Dos;
    }
    [[nodiscard]] static int dosX(int value) noexcept {
        return value * kDosLogicalWidth / kLogicalWidth;
    }
    [[nodiscard]] static int dosY(int value) noexcept {
        return value * kDosLogicalHeight / kLogicalHeight;
    }
    void resetIdleConversation() noexcept {
        idleMilliseconds_ = 0;
        idleConversationStep_ = 0;
    }

    [[nodiscard]] bool idleConversationDue(bool waitingForPlayer,
                                           unsigned delayMilliseconds = 30000) noexcept {
        if (!waitingForPlayer) {
            idleMilliseconds_ = 0;
            return false;
        }
        idleMilliseconds_ += 33;
        if (idleMilliseconds_ < delayMilliseconds) return false;
        idleMilliseconds_ = 0;
        return true;
    }

    [[nodiscard]] std::size_t idleConversationStep() const noexcept {
        return idleConversationStep_;
    }

    void advanceIdleConversation() noexcept { ++idleConversationStep_; }

    static void playConversation(HostAnimation& host, int x, int y,
                                 std::span<const int> movies) {
        if (movies.empty()) return;
        host.play(movies.front(), x, y);
        for (std::size_t index = 1; index < movies.size(); ++index) host.queue(movies[index]);
    }

    void drawBackground(Canvas& canvas, int resourceId) const {
        const Sprite& first = context_.graphics.sprite(resourceId, 0);
        for (int frame = 0; frame < 64; ++frame) {
            canvas.sprite(context_.graphics.sprite(resourceId, frame),
                          frame % 8 * first.width, frame / 8 * first.height, false);
        }
    }

    GameContext context_;
    bool animatedPieces_{true};
    bool forcedJumps_{true};
    unsigned idleMilliseconds_{};
    std::size_t idleConversationStep_{};
};

}  // namespace mf
