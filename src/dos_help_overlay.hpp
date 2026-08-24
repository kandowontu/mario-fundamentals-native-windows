#pragma once

#include "pak.hpp"

namespace mf {

// Source page order is Backgammon, Checkers, Dominoes, Go Fish, Yacht,
// with two pages per game.
[[nodiscard]] const Sprite& dosHelpOverlay(int sourcePage);

}  // namespace mf
