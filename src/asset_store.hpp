#pragma once

#include "common.hpp"

#include <windows.h>

namespace mf {

class AssetStore {
public:
    AssetStore(HINSTANCE instance, int resourceId);

    [[nodiscard]] std::span<const std::uint8_t> get(std::string_view type, int id) const;
    [[nodiscard]] bool contains(std::string_view type, int id) const;
    [[nodiscard]] std::vector<int> ids(std::string_view type) const;
    [[nodiscard]] std::size_t count() const noexcept { return entries_.size(); }

private:
    struct Entry {
        std::array<char, 4> type{};
        std::int16_t id{};
        std::uint16_t attributes{};
        std::uint32_t offset{};
        std::uint32_t size{};
    };

    [[nodiscard]] static std::uint64_t key(std::string_view type, int id);

    const std::uint8_t* bytes_{};
    std::size_t size_{};
    std::vector<Entry> entries_;
    std::unordered_map<std::uint64_t, std::size_t> lookup_;
};

}  // namespace mf
