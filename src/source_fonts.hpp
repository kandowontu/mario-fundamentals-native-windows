#pragma once

#include "common.hpp"

#include <windows.h>

namespace mf {

enum class SourceFontFace { Times14, Geneva9, Monaco12 };

class SourceBitmapFont {
public:
    explicit SourceBitmapFont(std::vector<std::uint8_t> data);

    [[nodiscard]] int ascent() const noexcept { return ascent_; }
    [[nodiscard]] int descent() const noexcept { return descent_; }
    [[nodiscard]] int leading() const noexcept { return leading_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] int lineHeight() const noexcept { return ascent_ + descent_ + leading_; }
    [[nodiscard]] int textWidth(std::wstring_view value, bool syntheticBold = false) const;
    [[nodiscard]] int glyphAdvance(wchar_t value) const;
    [[nodiscard]] int glyphBearing(wchar_t value) const;
    [[nodiscard]] int glyphBitmapWidth(wchar_t value) const;
    [[nodiscard]] bool glyphPixel(wchar_t value, int x, int y) const;

private:
    [[nodiscard]] std::size_t glyphIndex(wchar_t value) const noexcept;
    [[nodiscard]] std::uint8_t macRoman(wchar_t value) const noexcept;

    std::vector<std::uint8_t> data_;
    std::uint16_t firstChar_{};
    std::uint16_t lastChar_{};
    std::uint16_t rowWords_{};
    std::size_t glyphCount_{};
    std::size_t bitmapOffset_{26};
    std::size_t locationsOffset_{};
    std::size_t widthsOffset_{};
    int height_{};
    int ascent_{};
    int descent_{};
    int leading_{};
};

class SourceFonts {
public:
    explicit SourceFonts(HINSTANCE instance);

    [[nodiscard]] const SourceBitmapFont& font(SourceFontFace face) const noexcept;

private:
    static std::vector<std::uint8_t> load(HINSTANCE instance, int resourceId);

    SourceBitmapFont times14_;
    SourceBitmapFont geneva9_;
    SourceBitmapFont monaco12_;
};

}  // namespace mf
