#include "source_fonts.hpp"

#include "resource_ids.h"

namespace mf {

namespace {

std::int8_t highSignedByte(std::uint16_t value) {
    return static_cast<std::int8_t>(value >> 8U);
}

}  // namespace

SourceBitmapFont::SourceBitmapFont(std::vector<std::uint8_t> data) : data_(std::move(data)) {
    if (data_.size() < 26) throw std::runtime_error("truncated source NFNT header");
    firstChar_ = readBe16(data_, 2);
    lastChar_ = readBe16(data_, 4);
    height_ = readBe16(data_, 14);
    widthsOffset_ = 16U + static_cast<std::size_t>(readBe16(data_, 16)) * 2U;
    ascent_ = readBeS16(data_, 18);
    descent_ = readBeS16(data_, 20);
    leading_ = readBeS16(data_, 22);
    rowWords_ = readBe16(data_, 24);
    if (lastChar_ < firstChar_ || height_ <= 0 || ascent_ < 0 || descent_ < 0 ||
        leading_ < 0 || rowWords_ == 0) {
        throw std::runtime_error("invalid source NFNT metrics");
    }

    glyphCount_ = static_cast<std::size_t>(lastChar_ - firstChar_) + 1U;
    locationsOffset_ = bitmapOffset_ + static_cast<std::size_t>(rowWords_) * 2U * height_;
    // The location table has a terminator for every real glyph plus the
    // missing-glyph entry. The offset/width table has those glyph entries.
    const std::size_t locationsEnd = locationsOffset_ + (glyphCount_ + 2U) * 2U;
    const std::size_t widthsEnd = widthsOffset_ + (glyphCount_ + 1U) * 2U;
    if (locationsEnd > widthsOffset_ || widthsEnd > data_.size()) {
        throw std::runtime_error("truncated source NFNT tables");
    }
    if (readBe16(data_, locationsOffset_ + (glyphCount_ + 1U) * 2U) > rowWords_ * 16U) {
        throw std::runtime_error("source NFNT strike exceeds its row width");
    }
}

std::uint8_t SourceBitmapFont::macRoman(wchar_t value) const noexcept {
    if (value <= 0x7f) return static_cast<std::uint8_t>(value);
    if (value == L'\x00a9') return 0xa9;
    if (value == L'\x2122') return 0xaa;
    return static_cast<std::uint8_t>('?');
}

std::size_t SourceBitmapFont::glyphIndex(wchar_t value) const noexcept {
    const std::uint8_t code = macRoman(value);
    if (code < firstChar_ || code > lastChar_) return glyphCount_;
    return static_cast<std::size_t>(code - firstChar_);
}

int SourceBitmapFont::glyphAdvance(wchar_t value) const {
    const std::uint16_t entry = readBe16(data_, widthsOffset_ + glyphIndex(value) * 2U);
    return entry & 0xffU;
}

int SourceBitmapFont::glyphBearing(wchar_t value) const {
    const std::uint16_t entry = readBe16(data_, widthsOffset_ + glyphIndex(value) * 2U);
    return highSignedByte(entry);
}

int SourceBitmapFont::glyphBitmapWidth(wchar_t value) const {
    const std::size_t index = glyphIndex(value);
    const int left = readBe16(data_, locationsOffset_ + index * 2U);
    const int right = readBe16(data_, locationsOffset_ + (index + 1U) * 2U);
    return right - left;
}

bool SourceBitmapFont::glyphPixel(wchar_t value, int x, int y) const {
    if (y < 0 || y >= height_) return false;
    const std::size_t index = glyphIndex(value);
    const int left = readBe16(data_, locationsOffset_ + index * 2U);
    const int right = readBe16(data_, locationsOffset_ + (index + 1U) * 2U);
    if (x < 0 || left + x >= right) return false;
    const std::size_t bit = static_cast<std::size_t>(y) * rowWords_ * 16U + left + x;
    return (data_[bitmapOffset_ + bit / 8U] & (0x80U >> (bit % 8U))) != 0;
}

int SourceBitmapFont::textWidth(std::wstring_view value, bool syntheticBold) const {
    int width = 0;
    for (const wchar_t character : value)
        width += glyphAdvance(character) + (syntheticBold ? 1 : 0);
    return width;
}

std::vector<std::uint8_t> SourceFonts::load(HINSTANCE instance, int resourceId) {
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) throw std::runtime_error("embedded source NFNT resource was not found");
    HGLOBAL loaded = LoadResource(instance, resource);
    const DWORD size = SizeofResource(instance, resource);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    if (!bytes || size == 0) throw std::runtime_error("embedded source NFNT resource is empty");
    const auto* first = static_cast<const std::uint8_t*>(bytes);
    return {first, first + size};
}

SourceFonts::SourceFonts(HINSTANCE instance)
    : times14_(load(instance, IDR_FONT_TIMES_14)),
      geneva9_(load(instance, IDR_FONT_GENEVA_9)),
      monaco12_(load(instance, IDR_FONT_MONACO_12)) {}

const SourceBitmapFont& SourceFonts::font(SourceFontFace face) const noexcept {
    if (face == SourceFontFace::Times14) return times14_;
    if (face == SourceFontFace::Geneva9) return geneva9_;
    return monaco12_;
}

}  // namespace mf
