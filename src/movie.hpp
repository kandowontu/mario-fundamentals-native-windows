#pragma once

#include "asset_store.hpp"

namespace mf {

class Canvas;
class GraphicsAssets;

class Movie {
public:
    Movie(const AssetStore& assets, int resourceId);

    void render(Canvas& canvas, GraphicsAssets& graphics, std::uint32_t time,
                int x = 0, int y = 0) const;
    [[nodiscard]] std::vector<int> soundsAtStart() const;
    [[nodiscard]] std::vector<int> soundCues() const;
    [[nodiscard]] std::vector<int> soundsBetween(std::uint32_t previous,
                                                  std::uint32_t current) const;
    [[nodiscard]] std::size_t activeImageCount(std::uint32_t time) const noexcept;
    [[nodiscard]] Rect visualBounds(int x = 0, int y = 0) const noexcept;
    [[nodiscard]] Rect imageBounds(std::size_t index, int x = 0, int y = 0) const noexcept;
    [[nodiscard]] std::size_t imageCount() const noexcept { return images_.size(); }
    [[nodiscard]] int id() const noexcept { return id_; }
    [[nodiscard]] int imageSheetId() const noexcept { return imageSheetId_; }
    [[nodiscard]] std::uint32_t duration() const noexcept { return duration_; }
    [[nodiscard]] std::uint16_t timeScale() const noexcept { return timeScale_; }
    [[nodiscard]] std::size_t commandCount() const noexcept { return commands_.size(); }
    [[nodiscard]] bool resolved() const noexcept { return resolved_; }

private:
    struct ImageRecord {
        int xOffset{};
        int yOffset{};
        Rect source{};
    };
    struct Command {
        std::uint8_t opcode{};
        std::uint8_t flags{};
        std::uint16_t parameter{};
        std::uint32_t start{};
        std::uint32_t duration{};
        int offsetX{};
        int offsetY{};
    };

    int id_{};
    int imageSheetId_{};
    int originX_{};
    int originY_{};
    int width_{};
    int height_{};
    std::uint32_t duration_{};
    std::uint16_t timeScale_{};
    std::uint16_t tickDuration_{};
    bool resolved_{true};
    std::vector<ImageRecord> images_;
    std::vector<Command> commands_;
};

}  // namespace mf
