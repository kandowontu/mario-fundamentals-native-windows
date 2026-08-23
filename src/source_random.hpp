#pragma once

#include "common.hpp"

#include <limits>

namespace mf {

// Source-compatible implementation of the classic QuickDraw Random trap and
// Mario's Fundamentals CODE 1 $352C range scaler. QuickDraw updates randSeed
// with the Park-Miller recurrence and returns the signed low word. The game
// translates that word to 0..65534 and takes the high word of limit * value.
class SourceRandom {
public:
    explicit SourceRandom(std::uint32_t seed = 1) noexcept { setSeed(seed); }

    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed % kModulus;
    }

    [[nodiscard]] std::uint32_t seed() const noexcept { return seed_; }

    [[nodiscard]] std::int16_t next() noexcept {
        seed_ = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(seed_) * kMultiplier) % kModulus);
        return static_cast<std::int16_t>(seed_ & 0xffffU);
    }

    [[nodiscard]] std::uint16_t below(std::uint16_t limit) noexcept {
        // CODE 1 $352C sign-extends Random's word, adds 0x7fff, performs a
        // signed 32-bit multiply, then divides by 65536 with truncation toward
        // zero.  Keeping that signed product matters for Random == -32768:
        // its translated source value is -1 and the source routine returns
        // bucket zero.  Widening -1 to uint32_t first instead yields 65535.
        const std::int32_t source = static_cast<std::int32_t>(next()) + 32767;
        const std::int64_t wideProduct =
            static_cast<std::int64_t>(source) * static_cast<std::int32_t>(limit);
        const std::uint32_t productBits = static_cast<std::uint32_t>(wideProduct);
        const std::int32_t signedProduct = std::bit_cast<std::int32_t>(productBits);
        return static_cast<std::uint16_t>(signedProduct / 65536);
    }

    void discard(std::uint32_t count) noexcept {
        while (count-- != 0) (void)next();
    }

private:
    static constexpr std::uint32_t kMultiplier = 16807;
    static constexpr std::uint32_t kModulus = 2147483647;
    std::uint32_t seed_{1};
};

template <typename RandomAccessContainer>
void sourceShuffle(RandomAccessContainer& values, SourceRandom& random) {
    if (values.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("source shuffle exceeds the 16-bit item count");
    }
    std::size_t remaining = values.size();
    while (remaining > 1) {
        const std::size_t selected = random.below(static_cast<std::uint16_t>(remaining));
        if (selected >= remaining) {
            throw std::runtime_error("source shuffle selected an invalid item");
        }
        --remaining;
        std::swap(values[selected], values[remaining]);
    }
}

}  // namespace mf
