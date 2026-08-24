#include "movie.hpp"

#include "canvas.hpp"

namespace mf {

Movie::Movie(const AssetStore& assets, int resourceId) : id_(resourceId) {
    const auto header = assets.get("MuV ", id_);
    if (header.size() < 44) throw std::runtime_error("MuV header is truncated");
    const bool dos = assets.dialect() == AssetDialect::Dos;
    const auto word = [dos](std::span<const std::uint8_t> data, std::size_t offset) {
        return dos ? readLe16(data, offset) : readBe16(data, offset);
    };
    const auto signedWord = [dos](std::span<const std::uint8_t> data, std::size_t offset) {
        return dos ? readLeS16(data, offset) : readBeS16(data, offset);
    };
    const auto longWord = [dos](std::span<const std::uint8_t> data, std::size_t offset) {
        return dos ? readLe32(data, offset) : readBe32(data, offset);
    };
    // The Macintosh resources preserve QuickDraw's vertical/horizontal field
    // order.  The DOS port rewrote these records as conventional x/y and
    // width/height structures.  Mixing the two dialects swaps every DOS movie
    // axis: source rectangles are then clipped, moving the board flip and
    // punching apparent holes through talking-head cels.
    if (dos) {
        originX_ = signedWord(header, 2);
        originY_ = signedWord(header, 4);
        width_ = word(header, 10);
        height_ = word(header, 12);
    } else {
        originY_ = signedWord(header, 2);
        originX_ = signedWord(header, 4);
        height_ = word(header, 10);
        width_ = word(header, 12);
    }
    duration_ = longWord(header, 14);
    timeScale_ = word(header, 18);
    tickDuration_ = word(header, 20);
    const std::size_t commandCount = word(header, 22);
    imageSheetId_ = id_ < 10000 ? id_ : id_ / 1000 * 1000;

    const auto imageData = assets.get("Img ", imageSheetId_);
    if (imageData.size() % 12 != 0) throw std::runtime_error("Img table is misaligned");
    images_.reserve(imageData.size() / 12);
    for (std::size_t offset = 0; offset < imageData.size(); offset += 12) {
        ImageRecord image;
        if (dos) {
            image.xOffset = signedWord(imageData, offset);
            image.yOffset = signedWord(imageData, offset + 2);
            image.source = {signedWord(imageData, offset + 4),
                            signedWord(imageData, offset + 6),
                            signedWord(imageData, offset + 8),
                            signedWord(imageData, offset + 10)};
        } else {
            image.yOffset = signedWord(imageData, offset);
            image.xOffset = signedWord(imageData, offset + 2);
            image.source = {signedWord(imageData, offset + 6),
                            signedWord(imageData, offset + 4),
                            signedWord(imageData, offset + 10),
                            signedWord(imageData, offset + 8)};
        }
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
        command.parameter = word(timeline, offset + 2);
        command.start = longWord(timeline, offset + 4);
        command.duration = longWord(timeline, offset + 8);
        if (command.opcode == 5 || command.opcode == 6) {
            // DOS converted the MuV and Img geometry records to x/y order,
            // but the two-word motion payload in Ply commands retained its
            // original vertical/horizontal order.  Treating Ply like MuV/Img
            // made every translated cel move on the wrong axis: the Yacht
            // sailed down off the screen, Checkers actors drifted sideways,
            // and the dice cup shook horizontally instead of vertically.
            if (dos) {
                command.offsetY = signedWord(timeline, offset + 12);
                command.offsetX = signedWord(timeline, offset + 14);
            } else {
                command.offsetX = signedWord(timeline, offset + 12);
                command.offsetY = signedWord(timeline, offset + 14);
            }
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

Rect Movie::activeVisualBounds(std::uint32_t time, int x, int y) const noexcept {
    Rect bounds{};
    bool initialized = false;
    if (!resolved_) return bounds;
    if (duration_) time %= duration_;
    for (const Command& command : commands_) {
        if (command.opcode < 3 || command.opcode > 6 || command.duration == 0 ||
            time < command.start || time >= command.start + command.duration ||
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
