#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mf {

constexpr int kLogicalWidth = 512;
constexpr int kLogicalHeight = 384;
constexpr int kDosLogicalWidth = 320;
constexpr int kDosLogicalHeight = 200;

enum class AssetDialect {
    Macintosh,
    Dos,
};

struct Point {
    int x{};
    int y{};
};

struct Rect {
    int left{};
    int top{};
    int right{};
    int bottom{};

    [[nodiscard]] bool contains(Point point) const noexcept {
        return point.x >= left && point.x < right && point.y >= top && point.y < bottom;
    }
    [[nodiscard]] int width() const noexcept { return right - left; }
    [[nodiscard]] int height() const noexcept { return bottom - top; }
};

inline std::uint16_t readBe16(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 2 > data.size()) throw std::runtime_error("truncated big-endian word");
    return static_cast<std::uint16_t>((data[offset] << 8U) | data[offset + 1]);
}

inline std::int16_t readBeS16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::int16_t>(readBe16(data, offset));
}

inline std::uint32_t readBe32(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("truncated big-endian long");
    return (static_cast<std::uint32_t>(data[offset]) << 24U) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(data[offset + 3]);
}

inline std::uint16_t readLe16(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 2 > data.size()) throw std::runtime_error("truncated little-endian word");
    return static_cast<std::uint16_t>(data[offset] | data[offset + 1] << 8U);
}

inline std::int16_t readLeS16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::int16_t>(readLe16(data, offset));
}

inline std::uint32_t readLe32(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("truncated little-endian long");
    return static_cast<std::uint32_t>(data[offset]) |
           static_cast<std::uint32_t>(data[offset + 1]) << 8U |
           static_cast<std::uint32_t>(data[offset + 2]) << 16U |
           static_cast<std::uint32_t>(data[offset + 3]) << 24U;
}

inline constexpr std::uint32_t rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return static_cast<std::uint32_t>(red) << 16U |
           static_cast<std::uint32_t>(green) << 8U | blue;
}

}  // namespace mf
