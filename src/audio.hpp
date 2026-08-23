#pragma once

#include "asset_store.hpp"

#include <chrono>
#include <future>
#include <mmsystem.h>
#include <windows.h>

namespace mf {

class Audio {
public:
    explicit Audio(const AssetStore& assets) : assets_(assets) {}
    ~Audio();

    void prewarmMusicOutput();
    bool playSound(int resourceId);
    bool playEffect(int resourceId);
    bool playMusic(int resourceId, bool loop = true);
    void tick(unsigned milliseconds);
    void stop();
    void stopMusic();
    void setEnabled(bool enabled);
    void setSoundEnabled(bool enabled);
    void setMusicEnabled(bool enabled);
    [[nodiscard]] std::size_t midiEventCount(int resourceId) const;
    [[nodiscard]] std::size_t soundWaveSize(int resourceId) const;
    [[nodiscard]] bool soundPlaying() const noexcept {
        return activeSoundRemainingMilliseconds_ != 0;
    }
    [[nodiscard]] bool enabled() const noexcept { return soundEnabled_ && musicEnabled_; }
    [[nodiscard]] bool soundEnabled() const noexcept { return soundEnabled_; }
    [[nodiscard]] bool musicEnabled() const noexcept { return musicEnabled_; }

private:
    static std::vector<std::uint8_t> makeWave(std::span<const std::uint8_t> sound);
    struct MidiEvent { std::uint64_t milliseconds; std::uint32_t message; };
    struct MidiSequence {
        std::vector<MidiEvent> events;
        std::uint64_t durationMilliseconds{};
    };
    static MidiSequence parseMidi(std::span<const std::uint8_t> midi);
    bool tryAcquireMusicOutput();
    void closeMusicOutput();
    struct Voice {
        std::vector<std::uint8_t> wave;
        WAVEFORMATEX format{};
        WAVEHDR header{};
        HWAVEOUT output{};
        bool tracked{};
    };
    bool startVoice(int resourceId, bool tracked);
    static void closeVoice(Voice& voice);
    void stopTrackedSound();
    void stopMusicOutput();

    const AssetStore& assets_;
    std::vector<std::unique_ptr<Voice>> voices_;
    std::vector<MidiEvent> midiEvents_;
    std::future<HMIDIOUT> midiOpenFuture_;
    HMIDIOUT midiOutput_{};
    std::size_t nextMidiEvent_{};
    std::uint64_t midiElapsed_{};
    std::uint64_t midiDuration_{};
    std::uint64_t activeSoundRemainingMilliseconds_{};
    int activeMusicResourceId_{-1};
    int requestedMusicResourceId_{-1};
    bool requestedMusicLoop_{true};
    bool midiLoop_{};
    bool soundEnabled_{true};
    bool musicEnabled_{true};
};

}  // namespace mf
