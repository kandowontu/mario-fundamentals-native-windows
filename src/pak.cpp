#include "pak.hpp"

namespace mf {
namespace {

std::vector<std::uint8_t> decodeOuter(std::span<const std::uint8_t> source) {
    if (source.size() < 5) throw std::runtime_error("Pak resource is too short");
    const std::size_t expected = readBe32(source, 0);
    std::vector<std::uint8_t> output;
    output.reserve(expected);
    std::size_t position = 4;
    while (position < source.size() && output.size() < expected) {
        std::uint8_t flags = source[position++];
        for (int bit = 0; bit < 8 && output.size() < expected; ++bit, flags >>= 1U) {
            if (position >= source.size()) break;
            if (flags & 1U) {
                const std::uint16_t token = readBe16(source, position);
                position += 2;
                const std::size_t distance = (token & 0x0FFFU) + 1U;
                const std::size_t count = (token >> 12U) + 3U;
                if (distance > output.size()) throw std::runtime_error("invalid Pak LZSS distance");
                for (std::size_t index = 0; index < count; ++index) {
                    output.push_back(output[output.size() - distance]);
                }
            } else {
                output.push_back(source[position++]);
            }
        }
    }
    if (output.size() != expected) throw std::runtime_error("Pak LZSS size mismatch");
    return output;
}

}  // namespace

PakSheet::PakSheet(std::span<const std::uint8_t> compressed) : unpacked_(decodeOuter(compressed)) {
    const std::span<const std::uint8_t> data(unpacked_);
    flags_ = readBe16(data, 0);
    if ((flags_ & 0x7FFFU) != 2) throw std::runtime_error("unsupported Pak span encoding");
    const std::uint16_t count = readBe16(data, 2);
    if (6ULL + count * 4ULL > data.size()) throw std::runtime_error("truncated Pak frame table");
    offsets_.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        const auto offset = readBe32(data, 6 + index * 4);
        if (offset >= data.size()) throw std::runtime_error("invalid Pak frame offset");
        offsets_.push_back(offset);
    }
}

Sprite PakSheet::decodeFrame(int index, const std::array<std::uint32_t, 256>& palette) const {
    if (index < 0 || index >= frameCount()) throw std::out_of_range("Pak frame index");
    const std::span<const std::uint8_t> data(unpacked_);
    std::size_t position = offsets_[static_cast<std::size_t>(index)];
    const std::size_t end = index + 1 < frameCount()
                                ? offsets_[static_cast<std::size_t>(index + 1)]
                                : data.size();
    Sprite result;
    if (flags_ & 0x8000U) {
        result.originX = readBeS16(data, position);
        result.originY = readBeS16(data, position + 2);
        position += 4;
    }
    result.width = readBe16(data, position);
    result.height = readBe16(data, position + 2);
    position += 4;
    if (result.width <= 0 || result.height <= 0) throw std::runtime_error("invalid Pak dimensions");
    result.colors.assign(static_cast<std::size_t>(result.width) * result.height, 0);
    result.alpha.assign(result.colors.size(), 0);
    int x = 0;
    int y = 0;
    struct Repeat { std::size_t source; int remaining; };
    std::vector<Repeat> repeats;
    while (true) {
        if (position >= end) throw std::runtime_error("unterminated Pak span stream");
        const std::uint8_t command = data[position++];
        if (command & 0x80U) { ++y; x = 0; }
        int count = command & 0x1FU;
        if (count == 0) {
            count = readBe16(data, position);
            position += 2;
        }
        const int operation = command >> 5U & 3U;
        if (operation == 0) {
            if (count > 1) { repeats.push_back({position, count - 1}); continue; }
            if (repeats.empty()) break;
            if (--repeats.back().remaining >= 0) position = repeats.back().source;
            else repeats.pop_back();
            continue;
        }
        auto check = [&](int amount) {
            if (y < 0 || y >= result.height || x < 0 || x + amount > result.width) {
                throw std::runtime_error("Pak span exceeds declared frame");
            }
        };
        if (operation == 1) { check(count); x += count; continue; }
        if (operation == 2) {
            if (count == 1) break;
            if (position >= end) throw std::runtime_error("truncated Pak fill span");
            const auto color = palette[data[position++]];
            check(count);
            for (int item = 0; item < count; ++item) {
                const auto target = static_cast<std::size_t>(y) * result.width + x++;
                result.colors[target] = color;
                result.alpha[target] = 255;
            }
            continue;
        }
        if (position + static_cast<std::size_t>(count) > end) {
            throw std::runtime_error("truncated Pak literal span");
        }
        check(count);
        for (int item = 0; item < count; ++item) {
            const auto target = static_cast<std::size_t>(y) * result.width + x++;
            result.colors[target] = palette[data[position++]];
            result.alpha[target] = 255;
        }
    }
    return result;
}

GraphicsAssets::GraphicsAssets(const AssetStore& assets) : assets_(assets) {
    const auto clut = assets_.get("clut", 1000);
    if (clut.size() < 8) throw std::runtime_error("main color table is invalid");
    const std::size_t count = readBe16(clut, 6) + 1U;
    if (8 + count * 8 > clut.size() || count > palette_.size()) {
        throw std::runtime_error("main color table is truncated");
    }
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t offset = 8 + index * 8;
        palette_[index] = rgb(
            static_cast<std::uint8_t>(readBe16(clut, offset + 2) >> 8U),
            static_cast<std::uint8_t>(readBe16(clut, offset + 4) >> 8U),
            static_cast<std::uint8_t>(readBe16(clut, offset + 6) >> 8U));
    }
}

const Sprite& GraphicsAssets::sprite(int resourceId, int frame) {
    const std::uint64_t key = static_cast<std::uint64_t>(static_cast<std::uint32_t>(resourceId)) << 32U |
                              static_cast<std::uint32_t>(frame);
    if (const auto found = sprites_.find(key); found != sprites_.end()) return found->second;
    auto& sheet = sheets_[resourceId];
    if (!sheet) sheet = std::make_shared<PakSheet>(assets_.get("Pak ", resourceId));
    return sprites_.emplace(key, sheet->decodeFrame(frame, palette_)).first->second;
}

}  // namespace mf
