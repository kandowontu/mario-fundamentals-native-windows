#include "canvas.hpp"

namespace mf {

Canvas::Canvas(int width, int height) : width_(width), height_(height) {
    memoryDc_ = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width_;
    info.bmiHeader.biHeight = -height_;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(memoryDc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memoryDc_ || !bitmap_ || !bits) throw std::runtime_error("could not create render canvas");
    pixels_ = static_cast<std::uint32_t*>(bits);
    previousBitmap_ = SelectObject(memoryDc_, bitmap_);
    SetBkMode(memoryDc_, TRANSPARENT);
}

Canvas::~Canvas() {
    if (memoryDc_ && previousBitmap_) SelectObject(memoryDc_, previousBitmap_);
    if (bitmap_) DeleteObject(bitmap_);
    if (memoryDc_) DeleteDC(memoryDc_);
}

COLORREF Canvas::colorRef(std::uint32_t color) {
    return RGB(color >> 16U & 0xFFU, color >> 8U & 0xFFU, color & 0xFFU);
}

void Canvas::clear(std::uint32_t color) {
    std::fill(pixels_, pixels_ + static_cast<std::size_t>(width_) * height_, color);
}

void Canvas::fillRect(Rect rect, std::uint32_t color) {
    rect.left = std::clamp(rect.left, 0, width_);
    rect.right = std::clamp(rect.right, rect.left, width_);
    rect.top = std::clamp(rect.top, 0, height_);
    rect.bottom = std::clamp(rect.bottom, rect.top, height_);
    for (int y = rect.top; y < rect.bottom; ++y) {
        std::fill(pixels_ + static_cast<std::size_t>(y) * width_ + rect.left,
                  pixels_ + static_cast<std::size_t>(y) * width_ + rect.right,
                  color);
    }
}

void Canvas::swapColors(Rect rect, std::uint32_t first, std::uint32_t second) {
    rect.left = std::clamp(rect.left, 0, width_);
    rect.right = std::clamp(rect.right, rect.left, width_);
    rect.top = std::clamp(rect.top, 0, height_);
    rect.bottom = std::clamp(rect.bottom, rect.top, height_);
    for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left; x < rect.right; ++x) {
            std::uint32_t& pixel = pixels_[static_cast<std::size_t>(y) * width_ + x];
            if (pixel == first) pixel = second;
            else if (pixel == second) pixel = first;
        }
    }
}

void Canvas::outlineRect(Rect rect, std::uint32_t color, int thickness) {
    HPEN pen = CreatePen(PS_SOLID, thickness, colorRef(color));
    HGDIOBJ oldPen = SelectObject(memoryDc_, pen);
    HGDIOBJ oldBrush = SelectObject(memoryDc_, GetStockObject(NULL_BRUSH));
    Rectangle(memoryDc_, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(memoryDc_, oldBrush);
    SelectObject(memoryDc_, oldPen);
    DeleteObject(pen);
}

void Canvas::line(Point from, Point to, std::uint32_t color, int thickness) {
    HPEN pen = CreatePen(PS_SOLID, thickness, colorRef(color));
    HGDIOBJ oldPen = SelectObject(memoryDc_, pen);
    MoveToEx(memoryDc_, from.x, from.y, nullptr);
    LineTo(memoryDc_, to.x, to.y);
    SelectObject(memoryDc_, oldPen);
    DeleteObject(pen);
}

void Canvas::sourceText(const SourceBitmapFont& font, std::wstring_view value,
                        Point centerBaseline, std::uint32_t color, bool syntheticBold) {
    // CODE 5's center helper subtracts floor(StringWidth / 2). QuickDraw's
    // algorithmic bold is the original one-pixel horizontal smear, with one
    // extra pixel of pen advance for each character.
    int penX = centerBaseline.x - font.textWidth(value, syntheticBold) / 2;
    const int top = centerBaseline.y - font.ascent();
    for (const wchar_t character : value) {
        const int bearing = font.glyphBearing(character);
        const int glyphWidth = font.glyphBitmapWidth(character);
        for (int y = 0; y < font.height(); ++y) {
            const int targetY = top + y;
            if (targetY < 0 || targetY >= height_) continue;
            for (int x = 0; x < glyphWidth; ++x) {
                if (!font.glyphPixel(character, x, y)) continue;
                const int targetX = penX + bearing + x;
                if (targetX >= 0 && targetX < width_)
                    pixels_[static_cast<std::size_t>(targetY) * width_ + targetX] = color;
                if (syntheticBold && targetX + 1 >= 0 && targetX + 1 < width_)
                    pixels_[static_cast<std::size_t>(targetY) * width_ + targetX + 1] = color;
            }
        }
        penX += font.glyphAdvance(character) + (syntheticBold ? 1 : 0);
    }
}

namespace {

int pakGlyphIndex(wchar_t value) {
    const int index = static_cast<int>(value) - 0x20;
    return index >= 0 && index <= 0x5d ? index : 0x1f;
}

}  // namespace

int Canvas::pakTextWidth(GraphicsAssets& graphics, std::wstring_view value, int resourceId,
                         std::size_t maxCharacters) const {
    const std::size_t count = std::min(value.size(), maxCharacters);
    int width = 0;
    for (std::size_t index = 0; index < count; ++index) {
        // CODE 15 $76-$84 does not use the otherwise blank space frame zero.
        // It advances spaces by frame $49, the same narrow width as lower-case i.
        const int frame = value[index] == L' ' ? 0x49 : pakGlyphIndex(value[index]);
        width += graphics.sprite(resourceId, frame).width;
    }
    return width;
}

void Canvas::pakText(GraphicsAssets& graphics, std::wstring_view value, int resourceId,
                     Rect rect, unsigned format, std::size_t maxCharacters) {
    const std::size_t count = std::min(value.size(), maxCharacters);
    if (count == 0) return;

    const int textWidth = pakTextWidth(graphics, value, resourceId, maxCharacters);
    int x = rect.left;
    if (format & DT_RIGHT) x = rect.right - textWidth;
    else if (format & DT_CENTER) x = rect.left + (rect.width() - textWidth) / 2;

    int textHeight = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (value[index] == L' ') continue;
        textHeight = std::max(textHeight,
                              graphics.sprite(resourceId, pakGlyphIndex(value[index])).height);
    }
    if (textHeight == 0) textHeight = graphics.sprite(resourceId, 0x49).height;
    int y = rect.top;
    if (format & DT_BOTTOM) y = rect.bottom - textHeight;
    else if (format & DT_VCENTER) y = rect.top + (rect.height() - textHeight) / 2;

    for (std::size_t index = 0; index < count; ++index) {
        if (value[index] == L' ') {
            x += graphics.sprite(resourceId, 0x49).width;
            continue;
        }
        const Sprite& glyph = graphics.sprite(resourceId, pakGlyphIndex(value[index]));
        const Rect visible{
            std::clamp(rect.left - x, 0, glyph.width),
            std::clamp(rect.top - y, 0, glyph.height),
            std::clamp(rect.right - x, 0, glyph.width),
            std::clamp(rect.bottom - y, 0, glyph.height)};
        if (visible.right > visible.left && visible.bottom > visible.top)
            spriteRegion(glyph, visible, x + visible.left, y + visible.top);
        x += glyph.width;
    }
}

void Canvas::sprite(const Sprite& source, int x, int y, bool includeOrigin) {
    if (includeOrigin) { x += source.originX; y += source.originY; }
    for (int sourceY = 0; sourceY < source.height; ++sourceY) {
        const int targetY = y + sourceY;
        if (targetY < 0 || targetY >= height_) continue;
        for (int sourceX = 0; sourceX < source.width; ++sourceX) {
            const int targetX = x + sourceX;
            if (targetX < 0 || targetX >= width_) continue;
            const auto sourceOffset = static_cast<std::size_t>(sourceY) * source.width + sourceX;
            if (!source.alpha[sourceOffset]) continue;
            pixels_[static_cast<std::size_t>(targetY) * width_ + targetX] = source.colors[sourceOffset];
        }
    }
}

void Canvas::spriteRegion(const Sprite& sprite, Rect source, int x, int y) {
    source.left = std::clamp(source.left, 0, sprite.width);
    source.right = std::clamp(source.right, source.left, sprite.width);
    source.top = std::clamp(source.top, 0, sprite.height);
    source.bottom = std::clamp(source.bottom, source.top, sprite.height);
    for (int sourceY = source.top; sourceY < source.bottom; ++sourceY) {
        const int targetY = y + sourceY - source.top;
        if (targetY < 0 || targetY >= height_) continue;
        for (int sourceX = source.left; sourceX < source.right; ++sourceX) {
            const int targetX = x + sourceX - source.left;
            if (targetX < 0 || targetX >= width_) continue;
            const auto sourceOffset = static_cast<std::size_t>(sourceY) * sprite.width + sourceX;
            if (!sprite.alpha[sourceOffset]) continue;
            pixels_[static_cast<std::size_t>(targetY) * width_ + targetX] = sprite.colors[sourceOffset];
        }
    }
}

std::uint64_t Canvas::pixelHash(Rect rect) const noexcept {
    rect.left = std::clamp(rect.left, 0, width_);
    rect.right = std::clamp(rect.right, rect.left, width_);
    rect.top = std::clamp(rect.top, 0, height_);
    rect.bottom = std::clamp(rect.bottom, rect.top, height_);
    std::uint64_t hash = 14695981039346656037ULL;
    for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left; x < rect.right; ++x) {
            hash ^= pixels_[static_cast<std::size_t>(y) * width_ + x];
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

void Canvas::bitmap(HBITMAP source, int x, int y) {
    if (!source) return;
    BITMAP description{};
    GetObjectW(source, sizeof(description), &description);
    HDC sourceDc = CreateCompatibleDC(memoryDc_);
    HGDIOBJ previous = SelectObject(sourceDc, source);
    BitBlt(memoryDc_, x, y, description.bmWidth, description.bmHeight, sourceDc, 0, 0, SRCCOPY);
    SelectObject(sourceDc, previous);
    DeleteDC(sourceDc);
}

void Canvas::bitmap(HBITMAP source, Rect destination) {
    if (!source) return;
    BITMAP description{};
    GetObjectW(source, sizeof(description), &description);
    HDC sourceDc = CreateCompatibleDC(memoryDc_);
    HGDIOBJ previous = SelectObject(sourceDc, source);
    SetStretchBltMode(memoryDc_, COLORONCOLOR);
    StretchBlt(memoryDc_, destination.left, destination.top, destination.width(), destination.height(),
               sourceDc, 0, 0, description.bmWidth, description.bmHeight, SRCCOPY);
    SelectObject(sourceDc, previous);
    DeleteDC(sourceDc);
}

void Canvas::present(HDC target, Rect destination) const {
    const int destinationWidth = destination.width();
    const int destinationHeight = destination.height();
    if (destinationWidth <= 0 || destinationHeight <= 0) return;

    // GDI's fractional StretchBlt sampling can change phase between repaints on
    // DPI-scaled desktops, making an otherwise static 512x384 board appear to
    // move by a pixel. Scale with an explicit nearest-neighbour map and upload
    // the finished frame 1:1 so every source pixel always lands in the same
    // destination pixels.
    if (presentationWidth_ != destinationWidth || presentationHeight_ != destinationHeight) {
        presentationWidth_ = destinationWidth;
        presentationHeight_ = destinationHeight;
        presentationPixels_.resize(static_cast<std::size_t>(destinationWidth) * destinationHeight);
    }
    for (int y = 0; y < destinationHeight; ++y) {
        const int sourceY = y * height_ / destinationHeight;
        std::uint32_t* output = presentationPixels_.data() +
                                static_cast<std::size_t>(y) * destinationWidth;
        const std::uint32_t* input = pixels_ + static_cast<std::size_t>(sourceY) * width_;
        for (int x = 0; x < destinationWidth; ++x)
            output[x] = input[x * width_ / destinationWidth];
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = destinationWidth;
    info.bmiHeader.biHeight = -destinationHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(target, destination.left, destination.top,
                      static_cast<DWORD>(destinationWidth), static_cast<DWORD>(destinationHeight),
                      0, 0, 0, static_cast<UINT>(destinationHeight), presentationPixels_.data(),
                      &info, DIB_RGB_COLORS);
}

void Canvas::saveBmp(std::wstring_view path) const {
    const std::wstring ownedPath(path);
    HANDLE file = CreateFileW(ownedPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("could not create QA bitmap");

    const DWORD pixelBytes = static_cast<DWORD>(
        static_cast<std::size_t>(width_) * height_ * sizeof(std::uint32_t));
    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + pixelBytes;
    BITMAPINFOHEADER imageHeader{};
    imageHeader.biSize = sizeof(BITMAPINFOHEADER);
    imageHeader.biWidth = width_;
    imageHeader.biHeight = -height_;
    imageHeader.biPlanes = 1;
    imageHeader.biBitCount = 32;
    imageHeader.biCompression = BI_RGB;
    imageHeader.biSizeImage = pixelBytes;

    const auto write = [file](const void* data, DWORD bytes) {
        DWORD written = 0;
        if (!WriteFile(file, data, bytes, &written, nullptr) || written != bytes) {
            throw std::runtime_error("could not write QA bitmap");
        }
    };
    try {
        write(&fileHeader, sizeof(fileHeader));
        write(&imageHeader, sizeof(imageHeader));
        write(pixels_, pixelBytes);
    } catch (...) {
        CloseHandle(file);
        throw;
    }
    CloseHandle(file);
}

}  // namespace mf
