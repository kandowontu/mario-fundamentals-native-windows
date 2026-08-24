#pragma once

#include "asset_store.hpp"

namespace mf {

struct Sprite {
    int width{};
    int height{};
    int originX{};
    int originY{};
    std::vector<std::uint32_t> colors;
    std::vector<std::uint8_t> alpha;
};

class PakSheet {
public:
    PakSheet(std::span<const std::uint8_t> compressed,
             AssetDialect dialect = AssetDialect::Macintosh);

    [[nodiscard]] int frameCount() const noexcept { return static_cast<int>(offsets_.size()); }
    [[nodiscard]] Sprite decodeFrame(int index, const std::array<std::uint32_t, 256>& palette) const;

private:
    std::vector<std::uint8_t> unpacked_;
    AssetDialect dialect_{AssetDialect::Macintosh};
    std::uint16_t flags_{};
    std::vector<std::uint32_t> offsets_;
};

class GraphicsAssets {
public:
    explicit GraphicsAssets(const AssetStore& assets);

    [[nodiscard]] const Sprite& sprite(int resourceId, int frame = 0);
    [[nodiscard]] const std::array<std::uint32_t, 256>& palette() const noexcept {
        return palette_;
    }

private:
    [[nodiscard]] std::array<std::uint32_t, 256> loadDosPalette(int resourceId) const;

    const AssetStore& assets_;
    std::array<std::uint32_t, 256> palette_{};
    std::array<std::uint32_t, 256> interplayPalette_{};
    std::array<std::uint32_t, 256> presagePalette_{};
    std::unordered_map<int, std::shared_ptr<PakSheet>> sheets_;
    std::unordered_map<std::uint64_t, Sprite> sprites_;
};

}  // namespace mf
