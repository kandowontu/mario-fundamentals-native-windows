#pragma once

#include <windows.h>

namespace mf {

enum class GameEdition {
    Cancel,
    Macintosh,
    Dos,
};

GameEdition chooseGameEdition(HINSTANCE instance, int showCommand);

}  // namespace mf
