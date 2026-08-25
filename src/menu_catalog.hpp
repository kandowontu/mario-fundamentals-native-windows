#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mf::menu_catalog {

// CODE 12 stores the menu selection in presentation order rather than the
// native engine's game-class order.  The source keyboard mnemonics reveal the
// exact sequence: C, G, D, B, Y.
inline constexpr std::array<int, 5> kSourceSelectionToGameIndex{2, 3, 1, 0, 4};
inline constexpr std::array<int, 5> kGameIndexToSourceSelection{4, 3, 1, 2, 5};
inline constexpr std::array<int, 5> kSelectionMovies{1111, 1112, 1113, 1114, 1115};
// CODE 12 $ABC stores a per-movie tick multiplier (10/13/10/10/8),
// while $C34 multiplies it by the 60-unit Pak/MuV frame duration.  These are
// the exact source times at which the selected hand/object actor rests.
inline constexpr std::array<std::uint32_t, 5> kSelectionHoldSourceTimes{
    600U, 780U, 600U, 600U, 480U};
// MBAR 129-133 install Options MENU 301, 303, 302, 304, and 305 for
// Backgammon, Dominoes, Checkers, Go Fish, and Yacht respectively.
inline constexpr std::array<int, 5> kGameIndexToOptionsMenuId{301, 303, 302, 304, 305};

enum class FileQuitAction {
    QuitApplication,
    ExitGame,
};

[[nodiscard]] constexpr int gameIndex(int sourceSelection) noexcept {
    return sourceSelection >= 1 && sourceSelection <= 5
        ? kSourceSelectionToGameIndex[static_cast<std::size_t>(sourceSelection - 1)] : -1;
}

[[nodiscard]] constexpr int sourceSelection(int gameIndex) noexcept {
    return gameIndex >= 0 && gameIndex < 5
        ? kGameIndexToSourceSelection[static_cast<std::size_t>(gameIndex)] : -1;
}

[[nodiscard]] constexpr int movie(int sourceSelection) noexcept {
    return sourceSelection >= 1 && sourceSelection <= 5
        ? kSelectionMovies[static_cast<std::size_t>(sourceSelection - 1)] : -1;
}

[[nodiscard]] constexpr std::uint32_t holdSourceTime(int sourceSelection) noexcept {
    return sourceSelection >= 1 && sourceSelection <= 5
        ? kSelectionHoldSourceTimes[static_cast<std::size_t>(sourceSelection - 1)] : 0U;
}

[[nodiscard]] constexpr int gameContextIndex(int activeGameIndex,
                                             int pendingGameIndex) noexcept {
    return activeGameIndex >= 0 ? activeGameIndex : pendingGameIndex;
}

[[nodiscard]] constexpr int optionsMenuId(int gameIndex) noexcept {
    return gameIndex >= 0 && gameIndex < 5
        ? kGameIndexToOptionsMenuId[static_cast<std::size_t>(gameIndex)]
        : 300;
}

// MBAR 128 uses File MENU 200 (Quit/Q) on the main shell.  Each game MBAR
// 129-133 substitutes File MENU 201 (Exit Game/Q), including title and modal
// screens reached while a game destination is pending.
[[nodiscard]] constexpr FileQuitAction fileQuitAction(int activeGameIndex,
                                                       int pendingGameIndex) noexcept {
    return gameContextIndex(activeGameIndex, pendingGameIndex) >= 0
        ? FileQuitAction::ExitGame
        : FileQuitAction::QuitApplication;
}

}  // namespace mf::menu_catalog
