#pragma once

#include "common.hpp"
#include "pak.hpp"
#include "source_fonts.hpp"

#include <windows.h>

namespace mf {

class Canvas {
public:
    Canvas(int width, int height);
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    void clear(std::uint32_t color);
    void swapColors(Rect rect, std::uint32_t first, std::uint32_t second);
    void outlineRect(Rect rect, std::uint32_t color, int thickness = 1);
    void line(Point from, Point to, std::uint32_t color, int thickness = 1);
    void sourceText(const SourceBitmapFont& font, std::wstring_view value,
                    Point centerBaseline, std::uint32_t color,
                    bool syntheticBold = false);
    [[nodiscard]] int pakTextWidth(GraphicsAssets& graphics, std::wstring_view value,
                                   int resourceId,
                                   std::size_t maxCharacters = 128) const;
    void pakText(GraphicsAssets& graphics, std::wstring_view value, int resourceId, Rect rect,
                 unsigned format = DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                 std::size_t maxCharacters = 128);
    void sprite(const Sprite& sprite, int x, int y, bool includeOrigin = true);
    void spriteRegion(const Sprite& sprite, Rect source, int x, int y);
    void bitmap(HBITMAP bitmap, int x, int y);
    void bitmap(HBITMAP bitmap, Rect destination);
    void present(HDC target, Rect destination) const;
    void saveBmp(std::wstring_view path) const;
    [[nodiscard]] std::uint64_t pixelHash(Rect rect) const noexcept;

    [[nodiscard]] HDC dc() const noexcept { return memoryDc_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    static COLORREF colorRef(std::uint32_t color);

    int width_{};
    int height_{};
    HDC memoryDc_{};
    HBITMAP bitmap_{};
    HGDIOBJ previousBitmap_{};
    std::uint32_t* pixels_{};
    mutable std::vector<std::uint32_t> presentationPixels_;
    mutable int presentationWidth_{};
    mutable int presentationHeight_{};
};

}  // namespace mf
