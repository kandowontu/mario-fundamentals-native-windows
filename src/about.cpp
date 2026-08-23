#include "about.hpp"

namespace mf {

void renderSourceAbout(Canvas& canvas, const SourceFonts& fonts, HBITMAP title,
                       Point origin) {
    // CODE 5 $A6-$2B8. PICT 128 is its 447x215 color window; every coordinate
    // below is the source QuickDraw port coordinate used by that routine.
    canvas.bitmap(title, origin.x, origin.y);

    constexpr std::array<std::wstring_view, 6> lines{
        L"Jon made the stuff work,",
        L"Jason drew pictures,",
        L"(Steve and Mike helped Jason,)",
        L"Mike, Brian, and Matt made noises,",
        L"Tom and Fred told us if they",
        L"liked it or not"};
    constexpr std::uint32_t yellow = rgb(255, 255, 0);
    constexpr std::uint32_t gray = rgb(0x4c, 0x4c, 0x4c);

    const SourceBitmapFont& times = fonts.font(SourceFontFace::Times14);
    const int mainLineHeight = times.lineHeight() + 2;
    int baseline = 65;
    for (std::size_t index = 0; index < 4; ++index) {
        canvas.sourceText(times, lines[index],
                          {origin.x + 265, origin.y + baseline}, yellow, true);
        baseline += mainLineHeight;
    }

    // The fourth baseline is followed by 9 pixels, then QuickDraw Line(240,0).
    const int dividerY = baseline - mainLineHeight + 9;
    canvas.line({origin.x + 145, origin.y + dividerY},
                {origin.x + 385, origin.y + dividerY}, yellow);
    baseline = dividerY + mainLineHeight + 2;
    for (std::size_t index = 4; index < lines.size(); ++index) {
        canvas.sourceText(times, lines[index],
                          {origin.x + 265, origin.y + baseline}, yellow, true);
        baseline += mainLineHeight;
    }

    // CODE 5 anchors these three Geneva line-heights above portRect.bottom.
    // It then advances one Geneva line before switching to Monaco for version.
    const SourceBitmapFont& geneva = fonts.font(SourceFontFace::Geneva9);
    const SourceBitmapFont& monaco = fonts.font(SourceFontFace::Monaco12);
    baseline = 215 - 3 * geneva.lineHeight();
    canvas.sourceText(geneva, L"\x00a9" L"1996 BrainStorm\x2122 Inc.",
                      {origin.x + 75, origin.y + baseline}, gray);
    baseline += geneva.lineHeight();
    canvas.sourceText(monaco, L"v. 1.1",
                      {origin.x + 75, origin.y + baseline}, gray);
}

}  // namespace mf
