#pragma once

#include "canvas.hpp"
#include "source_fonts.hpp"

#include <windows.h>

namespace mf {

void renderSourceAbout(Canvas& canvas, const SourceFonts& fonts, HBITMAP title,
                       Point origin);

}  // namespace mf
