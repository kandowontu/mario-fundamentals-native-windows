#include "movie.hpp"

#include "canvas.hpp"

namespace mf {

Movie::Movie(const AssetStore& assets, int resourceId) : id_(resourceId) {
    const auto header = assets.get("MuV ", id_);
    if (header.size() < 44) throw std::runtime_error("MuV header is truncated");
    // QuickDraw stores point/extent pairs in vertical, horizontal order.
    // Treating these fields as x, y displaced every registered movie and also
    // turned the Yacht hull's authored horizontal frame offsets into vertical
    // jumps.
    originY_ = readBeS16(header, 2);
    originX_ = readBeS16(header, 4);
    height_ = readBe16(header, 10);
    width_ = readBe16(header, 12);
    duration_ = readBe32(header, 14);
    timeScale_ = readBe16(header, 18);
    tickDuration_ = readBe16(header, 20);
    const std::size_t commandCount = readBe16(header, 22);
    imageSheetId_ = id_ < 10000 ? id_ : id_ / 1000 * 1000;

    const auto imageData = assets.get("Img ", imageSheetId_);
    if (imageData.size() % 12 != 0) throw std::runtime_error("Img table is misaligned");
    images_.reserve(imageData.size() / 12);
    for (std::size_t offset = 0; offset < imageData.size(); offset += 12) {
        ImageRecord image;
        image.yOffset = readBeS16(imageData, offset);
        image.xOffset = readBeS16(imageData, offset + 2);
        image.source = {readBeS16(imageData, offset + 6), readBeS16(imageData, offset + 4),
                        readBeS16(imageData, offset + 10), readBeS16(imageData, offset + 8)};
        if (image.source.right < image.source.left || image.source.bottom < image.source.top) {
            throw std::runtime_error("Img source rectangle is inverted");
        }
        images_.push_back(image);
    }

    const auto timeline = assets.get("Ply ", id_);
    if (commandCount * 16ULL > timeline.size()) throw std::runtime_error("Ply timeline is truncated");
    commands_.reserve(commandCount);
    for (std::size_t index = 0; index < commandCount; ++index) {
        const std::size_t offset = index * 16;
        Command command;
        command.opcode = timeline[offset];
        command.flags = timeline[offset + 1];
        command.parameter = readBe16(timeline, offset + 2);
        command.start = readBe32(timeline, offset + 4);
        command.duration = readBe32(timeline, offset + 8);
        if (command.opcode == 5 || command.opcode == 6) {
            command.offsetX = readBeS16(timeline, offset + 12);
            command.offsetY = readBeS16(timeline, offset + 14);
        }
        if (command.opcode != 2 && command.opcode != 3 && command.opcode != 4 &&
            command.opcode != 5 && command.opcode != 6 && command.opcode != 7 &&
            command.opcode != 10) {
            throw std::runtime_error("Ply timeline has an unknown opcode");
        }
        if (command.opcode >= 3 && command.opcode <= 6 && command.parameter >= images_.size()) {
            resolved_ = false;
        }
        commands_.push_back(command);
    }
    if (commands_.empty() || commands_.back().opcode != 10 ||
        commands_.back().start != duration_) {
        throw std::runtime_error("Ply timeline has an invalid end marker");
    }
    if (!assets.contains("Pak ", imageSheetId_)) resolved_ = false;
}

void Movie::render(Canvas& canvas, GraphicsAssets& graphics, std::uint32_t time,
                   int x, int y) const {
    if (!resolved_) return;
    if (duration_) time %= duration_;
    for (const Command& command : commands_) {
        if (command.opcode < 3 || command.opcode > 6 || command.duration == 0 ||
            time < command.start || time >= command.start + command.duration) {
            continue;
        }
        const ImageRecord& image = images_[command.parameter];
        const Sprite& sprite = graphics.sprite(imageSheetId_, command.parameter);
        const int commandX = command.opcode >= 5 ? command.offsetX : 0;
        const int commandY = command.opcode >= 5 ? command.offsetY : 0;
        canvas.spriteRegion(sprite, image.source,
                            x + originX_ + image.xOffset + commandX,
                            y + originY_ + image.yOffset + commandY);
    }
}

std::size_t Movie::activeImageCount(std::uint32_t time) const noexcept {
    if (!resolved_) return 0;
    if (duration_) time %= duration_;
    return static_cast<std::size_t>(std::count_if(
        commands_.begin(), commands_.end(), [time](const Command& command) {
            return command.opcode >= 3 && command.opcode <= 6 && command.duration != 0 &&
                   time >= command.start && time < command.start + command.duration;
        }));
}

std::vector<int> Movie::soundsBetween(std::uint32_t previous, std::uint32_t current) const {
    std::vector<int> result;
    if (current < previous) {
        const auto tail = soundsBetween(previous, duration_);
        const auto head = soundsBetween(0, current);
        result.insert(result.end(), tail.begin(), tail.end());
        result.insert(result.end(), head.begin(), head.end());
        return result;
    }
    for (const Command& command : commands_) {
        if (command.opcode == 7 && command.start > previous && command.start <= current) {
            result.push_back(command.parameter);
        }
    }
    return result;
}

std::vector<int> Movie::soundsAtStart() const {
    std::vector<int> result;
    for (const Command& command : commands_) {
        if (command.opcode == 7 && command.start == 0) result.push_back(command.parameter);
    }
    return result;
}

std::vector<int> Movie::soundCues() const {
    std::vector<int> result;
    for (const Command& command : commands_) {
        if (command.opcode == 7) result.push_back(command.parameter);
    }
    return result;
}

Rect Movie::visualBounds(int x, int y) const noexcept {
    Rect bounds{};
    bool initialized = false;
    for (const Command& command : commands_) {
        if (command.opcode < 3 || command.opcode > 6 ||
            command.parameter >= images_.size()) {
            continue;
        }
        const ImageRecord& image = images_[command.parameter];
        const int commandX = command.opcode >= 5 ? command.offsetX : 0;
        const int commandY = command.opcode >= 5 ? command.offsetY : 0;
        const Rect frame{
            x + originX_ + image.xOffset + commandX,
            y + originY_ + image.yOffset + commandY,
            x + originX_ + image.xOffset + commandX + image.source.width(),
            y + originY_ + image.yOffset + commandY + image.source.height()};
        if (!initialized) {
            bounds = frame;
            initialized = true;
        } else {
            bounds.left = std::min(bounds.left, frame.left);
            bounds.top = std::min(bounds.top, frame.top);
            bounds.right = std::max(bounds.right, frame.right);
            bounds.bottom = std::max(bounds.bottom, frame.bottom);
        }
    }
    return bounds;
}

Rect Movie::imageBounds(std::size_t index, int x, int y) const noexcept {
    if (index >= images_.size()) return {};
    const ImageRecord& image = images_[index];
    return {x + originX_ + image.xOffset,
            y + originY_ + image.yOffset,
            x + originX_ + image.xOffset + image.source.width(),
            y + originY_ + image.yOffset + image.source.height()};
}

}  // namespace mf
