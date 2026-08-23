#pragma once

#include <array>

namespace mf::audio_catalog {

// SONG 130 is the main-menu score. Each game has a primary score used by
// its title/game controller and a second score used only for the player's
// authored victory sequence.
inline constexpr int kMenuMusic = 900;
inline constexpr std::array<int, 5> kPrimaryGameMusic{910, 904, 912, 906, 908};
inline constexpr std::array<int, 5> kPlayerWinMusic{911, 905, 913, 907, 909};

// The DOS PRS stores the SONG/XMI IDs directly rather than separate 900-series
// Midi resource IDs.
inline constexpr int kDosMenuMusic = 130;
inline constexpr std::array<int, 5> kDosPrimaryGameMusic{140, 134, 142, 136, 138};
inline constexpr std::array<int, 5> kDosPlayerWinMusic{141, 135, 143, 137, 139};

// CODE 12 $1B74 starts these directly rather than through a movie timeline.
inline constexpr int kBrainstormSound = 8038;
inline constexpr int kSteppingStoneSound = 8042;

// CODE 12 emits these directly from the live main-menu cast controllers.
// The selected game-piece movies carry their own synchronized cues.
inline constexpr int kMenuSelectionSound = 5003;
inline constexpr int kMenuLaunchSound = 5010;
inline constexpr int kMenuFootSound = 5004;
inline constexpr int kMenuBowTieSound = 5006;

// CODE 5's PICT 128 title/about and PICT 129 credits presentations.
inline constexpr int kAboutSound = 5057;
inline constexpr int kCreditsSound = 5072;

}  // namespace mf::audio_catalog
