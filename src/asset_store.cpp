#include "asset_store.hpp"

#include <cstring>

namespace mf {
namespace {

std::uint16_t readLe16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0] | bytes[1] << 8U);
}

std::uint32_t readLe32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           static_cast<std::uint32_t>(bytes[1]) << 8U |
           static_cast<std::uint32_t>(bytes[2]) << 16U |
           static_cast<std::uint32_t>(bytes[3]) << 24U;
}

}  // namespace

AssetStore::AssetStore(HINSTANCE instance, int resourceId) {
    const HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) throw std::runtime_error("embedded asset pack is missing");
    const HGLOBAL loaded = LoadResource(instance, resource);
    if (!loaded) throw std::runtime_error("embedded asset pack could not be loaded");
    bytes_ = static_cast<const std::uint8_t*>(LockResource(loaded));
    size_ = SizeofResource(instance, resource);
    if (!bytes_ || size_ < 20) throw std::runtime_error("embedded asset pack is invalid");
    if (std::memcmp(bytes_, "MARIOFPK", 8) != 0) {
        throw std::runtime_error("embedded asset pack has an invalid signature");
    }
    const auto version = readLe32(bytes_ + 8);
    const auto count = readLe32(bytes_ + 12);
    const auto entrySize = readLe32(bytes_ + 16);
    if (version != 1 || entrySize != 20 || 20ULL + count * entrySize > size_) {
        throw std::runtime_error("unsupported embedded asset pack");
    }
    entries_.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint8_t* raw = bytes_ + 20 + index * entrySize;
        Entry entry;
        std::memcpy(entry.type.data(), raw, 4);
        entry.id = static_cast<std::int16_t>(readLe16(raw + 4));
        entry.attributes = readLe16(raw + 6);
        entry.offset = readLe32(raw + 8);
        entry.size = readLe32(raw + 12);
        if (static_cast<std::uint64_t>(entry.offset) + entry.size > size_) {
            throw std::runtime_error("asset pack entry exceeds pack bounds");
        }
        const std::string_view type(entry.type.data(), 4);
        lookup_.emplace(key(type, entry.id), entries_.size());
        entries_.push_back(entry);
    }
}

std::uint64_t AssetStore::key(std::string_view type, int id) {
    if (type.size() != 4) throw std::invalid_argument("resource type must be four bytes");
    std::uint64_t value = static_cast<std::uint16_t>(id);
    for (unsigned char byte : type) value = value << 8U | byte;
    return value;
}

std::span<const std::uint8_t> AssetStore::get(std::string_view type, int id) const {
    const auto found = lookup_.find(key(type, id));
    if (found == lookup_.end()) {
        throw std::runtime_error("requested Macintosh resource is absent");
    }
    const Entry& entry = entries_[found->second];
    return {bytes_ + entry.offset, entry.size};
}

bool AssetStore::contains(std::string_view type, int id) const {
    return lookup_.contains(key(type, id));
}

std::vector<int> AssetStore::ids(std::string_view type) const {
    if (type.size() != 4) throw std::invalid_argument("resource type must be four bytes");
    std::vector<int> result;
    for (const Entry& entry : entries_) {
        if (std::equal(entry.type.begin(), entry.type.end(), type.begin())) result.push_back(entry.id);
    }
    return result;
}

}  // namespace mf
