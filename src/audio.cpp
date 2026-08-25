#include "audio.hpp"

#include <mmsystem.h>

namespace mf {
namespace {

void appendLe16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendLe32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

std::uint32_t readVariable(std::span<const std::uint8_t> bytes, std::size_t& position,
                           std::size_t limit) {
    std::uint32_t result = 0;
    for (int count = 0; count < 4; ++count) {
        if (position >= limit) throw std::runtime_error("truncated MIDI variable integer");
        const std::uint8_t byte = bytes[position++];
        result = result << 7U | (byte & 0x7FU);
        if (!(byte & 0x80U)) return result;
    }
    throw std::runtime_error("invalid MIDI variable integer");
}

}  // namespace

Audio::~Audio() {
    stop();
    stopMusic();
    closeMusicOutput();
}

void Audio::prewarmMusicOutput() {
    if (midiOutput_ || midiOpenFuture_.valid()) return;
    // Windows' software MIDI mapper can take several seconds to initialize on
    // some machines.  Opening it on the UI thread used to freeze the startup
    // controller exactly between "proudly presents" and Mario's title stage.
    midiOpenFuture_ = std::async(std::launch::async, [] {
        HMIDIOUT output{};
        return midiOutOpen(&output, MIDI_MAPPER, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR
            ? output : HMIDIOUT{};
    });
}

bool Audio::tryAcquireMusicOutput() {
    if (midiOutput_) return true;
    if (!midiOpenFuture_.valid() ||
        midiOpenFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        return false;
    }
    midiOutput_ = midiOpenFuture_.get();
    return midiOutput_ != nullptr;
}

void Audio::closeMusicOutput() {
    if (!midiOutput_ && midiOpenFuture_.valid()) midiOutput_ = midiOpenFuture_.get();
    if (!midiOutput_) return;
    midiOutReset(midiOutput_);
    midiOutClose(midiOutput_);
    midiOutput_ = nullptr;
}

std::vector<std::uint8_t> Audio::makeWave(std::span<const std::uint8_t> sound,
                                          AssetDialect dialect) {
    if (dialect == AssetDialect::Dos) {
        if (sound.size() < 6) throw std::runtime_error("DOS sound resource is truncated");
        const std::uint16_t encoding = readLe16(sound, 0);
        const std::uint32_t sampleCount = readLe16(sound, 2);
        const std::uint32_t sampleRate = readLe16(sound, 4);
        if (encoding != 3 || !sampleRate || sampleCount + 6U != sound.size()) {
            throw std::runtime_error("unsupported DOS sound resource");
        }
        std::vector<std::uint8_t> wave;
        wave.reserve(44 + sampleCount + 1);
        wave.insert(wave.end(), {'R', 'I', 'F', 'F'});
        appendLe32(wave, 36 + sampleCount + (sampleCount & 1U));
        wave.insert(wave.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        appendLe32(wave, 16);
        appendLe16(wave, 1);
        appendLe16(wave, 1);
        appendLe32(wave, sampleRate);
        appendLe32(wave, sampleRate);
        appendLe16(wave, 1);
        appendLe16(wave, 8);
        wave.insert(wave.end(), {'d', 'a', 't', 'a'});
        appendLe32(wave, sampleCount);
        wave.insert(wave.end(), sound.begin() + 6, sound.end());
        if (sampleCount & 1U) wave.push_back(0);
        return wave;
    }
    if (sound.size() < 36) throw std::runtime_error("sound resource is truncated");
    const auto format = readBe16(sound, 0);
    std::size_t commandCountOffset{};
    if (format == 1) commandCountOffset = 4 + readBe16(sound, 2) * 6U;
    else if (format == 2 && readBe16(sound, 2) == 0) commandCountOffset = 4;
    else throw std::runtime_error("unsupported sound resource format");
    if (readBe16(sound, commandCountOffset) != 1) {
        throw std::runtime_error("unsupported sound command list");
    }
    const auto command = readBe16(sound, commandCountOffset + 2);
    const auto headerOffset = readBe32(sound, commandCountOffset + 6);
    if (command != 0x8050 && command != 0x8051) {
        throw std::runtime_error("unsupported sound command");
    }
    const std::uint32_t sampleCount = readBe32(sound, headerOffset + 4);
    const std::uint32_t sampleRate = readBe32(sound, headerOffset + 8) >> 16U;
    const std::size_t sampleOffset = headerOffset + 22;
    if (sampleOffset + sampleCount > sound.size() || sound[headerOffset + 20] != 0) {
        throw std::runtime_error("unsupported or truncated SoundHeader");
    }
    std::vector<std::uint8_t> wave;
    wave.reserve(44 + sampleCount + 1);
    wave.insert(wave.end(), {'R', 'I', 'F', 'F'});
    appendLe32(wave, 36 + sampleCount + (sampleCount & 1U));
    wave.insert(wave.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    appendLe32(wave, 16);
    appendLe16(wave, 1);
    appendLe16(wave, 1);
    appendLe32(wave, sampleRate);
    appendLe32(wave, sampleRate);
    appendLe16(wave, 1);
    appendLe16(wave, 8);
    wave.insert(wave.end(), {'d', 'a', 't', 'a'});
    appendLe32(wave, sampleCount);
    wave.insert(wave.end(), sound.begin() + static_cast<std::ptrdiff_t>(sampleOffset),
                sound.begin() + static_cast<std::ptrdiff_t>(sampleOffset + sampleCount));
    if (sampleCount & 1U) wave.push_back(0);
    return wave;
}

bool Audio::playSound(int resourceId) {
    // Preserve the requested source route even when verification has disabled
    // physical output. This mirrors requestedMusicResourceId_ and lets silent
    // controller regressions distinguish tracked Sound Manager calls from
    // concurrent direct effects.
    requestedSoundResourceId_ = resourceId;
    stopTrackedSound();
    return startVoice(resourceId, true);
}

bool Audio::playEffect(int resourceId) {
    return startVoice(resourceId, false);
}

bool Audio::startVoice(int resourceId, bool tracked) {
    const std::string_view type =
        assets_.dialect() == AssetDialect::Dos ? std::string_view("SND ")
                                               : std::string_view("snd ");
    if (!soundEnabled_ || !assets_.contains(type, resourceId)) return false;
    try {
        auto voice = std::make_unique<Voice>();
        voice->wave = makeWave(assets_.get(type, resourceId), assets_.dialect());
        const std::uint32_t sampleRate = static_cast<std::uint32_t>(voice->wave[24]) |
                                         static_cast<std::uint32_t>(voice->wave[25]) << 8U |
                                         static_cast<std::uint32_t>(voice->wave[26]) << 16U |
                                         static_cast<std::uint32_t>(voice->wave[27]) << 24U;
        const std::uint32_t sampleCount = static_cast<std::uint32_t>(voice->wave[40]) |
                                          static_cast<std::uint32_t>(voice->wave[41]) << 8U |
                                          static_cast<std::uint32_t>(voice->wave[42]) << 16U |
                                          static_cast<std::uint32_t>(voice->wave[43]) << 24U;
        if (!sampleRate) throw std::runtime_error("sound resource has a zero sample rate");
        voice->format.wFormatTag = WAVE_FORMAT_PCM;
        voice->format.nChannels = 1;
        voice->format.nSamplesPerSec = sampleRate;
        voice->format.nAvgBytesPerSec = sampleRate;
        voice->format.nBlockAlign = 1;
        voice->format.wBitsPerSample = 8;
        voice->tracked = tracked;
        voice->header.lpData = reinterpret_cast<LPSTR>(voice->wave.data() + 44);
        voice->header.dwBufferLength = sampleCount;
        if (waveOutOpen(&voice->output, WAVE_MAPPER, &voice->format, 0, 0,
                        CALLBACK_NULL) != MMSYSERR_NOERROR) {
            return false;
        }
        if (waveOutPrepareHeader(voice->output, &voice->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            waveOutClose(voice->output);
            return false;
        }
        if (waveOutWrite(voice->output, &voice->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            closeVoice(*voice);
            return false;
        }
        if (tracked) {
            activeSoundRemainingMilliseconds_ =
                (static_cast<std::uint64_t>(sampleCount) * 1000U + sampleRate - 1U) / sampleRate;
        } else {
            // CODE 1 $CAA feeds one direct-effect scheduler, and $B22 reports
            // that channel busy until its sample drains. Keep movie/effect
            // voices concurrent, but retain the longest live duration so
            // source controllers cannot enqueue overlapping guarded effects.
            activeDirectEffectRemainingMilliseconds_ = std::max(
                activeDirectEffectRemainingMilliseconds_,
                (static_cast<std::uint64_t>(sampleCount) * 1000U + sampleRate - 1U) /
                    sampleRate);
        }
        voices_.push_back(std::move(voice));
        return true;
    } catch (...) {
        if (tracked) activeSoundRemainingMilliseconds_ = 0;
        return false;
    }
}

void Audio::closeVoice(Voice& voice) {
    if (!voice.output) return;
    waveOutReset(voice.output);
    if (voice.header.dwFlags & WHDR_PREPARED) {
        waveOutUnprepareHeader(voice.output, &voice.header, sizeof(WAVEHDR));
    }
    waveOutClose(voice.output);
    voice.output = nullptr;
}

void Audio::stopTrackedSound() {
    std::erase_if(voices_, [](const std::unique_ptr<Voice>& voice) {
        if (!voice->tracked) return false;
        closeVoice(*voice);
        return true;
    });
    activeSoundRemainingMilliseconds_ = 0;
}

Audio::MidiSequence Audio::parseMidi(std::span<const std::uint8_t> midi) {
    if (midi.size() < 22 || readBe32(midi, 0) != 0x4D546864 || readBe32(midi, 4) != 6 ||
        readBe16(midi, 8) != 0 || readBe16(midi, 10) != 1) {
        throw std::runtime_error("unsupported MIDI header");
    }
    const std::uint16_t division = readBe16(midi, 12);
    if (!division || division & 0x8000U || readBe32(midi, 14) != 0x4D54726B) {
        throw std::runtime_error("unsupported MIDI timing or track structure");
    }
    const std::size_t trackEnd = 22ULL + readBe32(midi, 18);
    if (trackEnd > midi.size()) throw std::runtime_error("truncated MIDI track");

    MidiSequence result;
    std::size_t position = 22;
    std::uint8_t runningStatus = 0;
    std::uint64_t elapsedMicroseconds = 0;
    std::uint32_t tempo = 500000;
    while (position < trackEnd) {
        const std::uint32_t delta = readVariable(midi, position, trackEnd);
        elapsedMicroseconds += static_cast<std::uint64_t>(delta) * tempo / division;
        if (position >= trackEnd) throw std::runtime_error("truncated MIDI event");
        std::uint8_t status = midi[position];
        bool running = status < 0x80U;
        if (running) {
            if (runningStatus < 0x80U) throw std::runtime_error("MIDI running status is absent");
            status = runningStatus;
        } else {
            ++position;
            if (status < 0xF0U) runningStatus = status;
        }

        if (status == 0xFFU) {
            if (position >= trackEnd) throw std::runtime_error("truncated MIDI meta event");
            const std::uint8_t type = midi[position++];
            const std::size_t length = readVariable(midi, position, trackEnd);
            if (position + length > trackEnd) throw std::runtime_error("truncated MIDI meta payload");
            if (type == 0x51 && length == 3) {
                tempo = static_cast<std::uint32_t>(midi[position]) << 16U |
                        static_cast<std::uint32_t>(midi[position + 1]) << 8U |
                        midi[position + 2];
                if (!tempo) throw std::runtime_error("invalid zero MIDI tempo");
            }
            position += length;
            if (type == 0x2F) break;
            continue;
        }
        if (status == 0xF0U || status == 0xF7U) {
            const std::size_t length = readVariable(midi, position, trackEnd);
            if (position + length > trackEnd) throw std::runtime_error("truncated MIDI system event");
            position += length;
            continue;
        }
        if (status >= 0xF0U) throw std::runtime_error("unsupported MIDI system event");

        const int dataCount = (status & 0xE0U) == 0xC0U ? 1 : 2;
        std::uint8_t data1{};
        if (running) {
            data1 = midi[position++];
        } else {
            if (position >= trackEnd) throw std::runtime_error("truncated MIDI channel event");
            data1 = midi[position++];
        }
        std::uint8_t data2{};
        if (dataCount == 2) {
            if (position >= trackEnd) throw std::runtime_error("truncated MIDI channel event");
            data2 = midi[position++];
        }
        if (data1 & 0x80U || data2 & 0x80U) throw std::runtime_error("invalid MIDI data byte");
        result.events.push_back(
            {elapsedMicroseconds / 1000U,
             static_cast<std::uint32_t>(status) | static_cast<std::uint32_t>(data1) << 8U |
                 static_cast<std::uint32_t>(data2) << 16U});
    }
    result.durationMilliseconds = elapsedMicroseconds / 1000U + 1U;
    if (result.events.empty()) throw std::runtime_error("MIDI track has no channel events");
    return result;
}

Audio::MidiSequence Audio::parseXmi(std::span<const std::uint8_t> xmi) {
    if (xmi.size() < 32 || readBe32(xmi, 0) != 0x464F524DU ||
        readBe32(xmi, 8) != 0x58444952U) {
        throw std::runtime_error("unsupported XMI container");
    }
    std::size_t eventChunk = std::string_view(
        reinterpret_cast<const char*>(xmi.data()), xmi.size()).find("EVNT");
    if (eventChunk == std::string_view::npos || eventChunk + 8 > xmi.size()) {
        throw std::runtime_error("XMI event chunk is absent");
    }
    const std::size_t eventEnd = eventChunk + 8ULL + readBe32(xmi, eventChunk + 4);
    if (eventEnd > xmi.size()) throw std::runtime_error("truncated XMI event chunk");

    struct TickEvent {
        std::uint64_t tick{};
        std::uint32_t message{};
        std::size_t order{};
    };
    std::vector<TickEvent> tickEvents;
    std::size_t position = eventChunk + 8;
    std::uint64_t tick = 0;
    std::size_t order = 0;
    while (position < eventEnd) {
        while (position < eventEnd && xmi[position] < 0x80U) tick += xmi[position++];
        if (position >= eventEnd) break;
        const std::uint8_t status = xmi[position++];
        if (status == 0xFFU) {
            if (position >= eventEnd) throw std::runtime_error("truncated XMI meta event");
            const std::uint8_t type = xmi[position++];
            const std::size_t length = readVariable(xmi, position, eventEnd);
            if (position + length > eventEnd) throw std::runtime_error("truncated XMI meta payload");
            position += length;
            if (type == 0x2FU) break;
            continue;
        }
        if (status == 0xF0U || status == 0xF7U) {
            const std::size_t length = readVariable(xmi, position, eventEnd);
            if (position + length > eventEnd) throw std::runtime_error("truncated XMI system event");
            position += length;
            continue;
        }
        if (status < 0x80U || status >= 0xF0U) {
            throw std::runtime_error("unsupported XMI status byte");
        }
        const int dataCount = (status & 0xE0U) == 0xC0U ? 1 : 2;
        if (position + static_cast<std::size_t>(dataCount) > eventEnd) {
            throw std::runtime_error("truncated XMI channel event");
        }
        const std::uint8_t data1 = xmi[position++];
        const std::uint8_t data2 = dataCount == 2 ? xmi[position++] : 0;
        if (data1 & 0x80U || data2 & 0x80U) throw std::runtime_error("invalid XMI data byte");
        tickEvents.push_back(
            {tick, static_cast<std::uint32_t>(status) |
                       static_cast<std::uint32_t>(data1) << 8U |
                       static_cast<std::uint32_t>(data2) << 16U,
             order++});
        if ((status & 0xF0U) == 0x90U) {
            const std::uint32_t duration = readVariable(xmi, position, eventEnd);
            if (data2 != 0) {
                tickEvents.push_back(
                    {tick + duration,
                     static_cast<std::uint32_t>(0x80U | (status & 0x0FU)) |
                         static_cast<std::uint32_t>(data1) << 8U,
                     order++});
            }
        }
    }
    if (tickEvents.empty()) throw std::runtime_error("XMI sequence has no channel events");
    std::stable_sort(tickEvents.begin(), tickEvents.end(), [](const TickEvent& left,
                                                               const TickEvent& right) {
        if (left.tick != right.tick) return left.tick < right.tick;
        return left.order < right.order;
    });
    // Miles XMIDI delta units are fixed at 120 Hz.  The reference parser
    // represents this as 60 PPQN at a forced 500,000 us/qn and deliberately
    // ignores authored tempo meta-events.  Treating those values as ordinary
    // SMF tempo changes makes several shipped songs run 20-40 percent slow.
    constexpr std::uint32_t division = 60;
    constexpr std::uint32_t microsecondsPerQuarter = 500000;
    auto tickToMicroseconds = [](std::uint64_t target) {
        return target * microsecondsPerQuarter / division;
    };

    MidiSequence result;
    result.events.reserve(tickEvents.size());
    for (const TickEvent& event : tickEvents) {
        result.events.push_back({tickToMicroseconds(event.tick) / 1000U, event.message});
    }
    result.durationMilliseconds = tickToMicroseconds(tickEvents.back().tick) / 1000U + 1U;
    return result;
}

Audio::MidiSequence Audio::parseMusic(int resourceId) const {
    if (assets_.dialect() == AssetDialect::Dos) {
        return parseXmi(assets_.get("XMI ", resourceId));
    }
    return parseMidi(assets_.get("Midi", resourceId));
}

bool Audio::playMusic(int resourceId, bool loop) {
    const std::string_view type =
        assets_.dialect() == AssetDialect::Dos ? std::string_view("XMI ")
                                               : std::string_view("Midi");
    if (!assets_.contains(type, resourceId)) return false;
    requestedMusicResourceId_ = resourceId;
    requestedMusicLoop_ = loop;
    if (!musicEnabled_) return false;
    if (midiOutput_ && activeMusicResourceId_ == resourceId && midiLoop_ == loop) return true;
    try {
        MidiSequence sequence = parseMusic(resourceId);
        stopMusicOutput();
        midiEvents_ = std::move(sequence.events);
        midiDuration_ = sequence.durationMilliseconds;
        midiLoop_ = loop;
        activeMusicResourceId_ = resourceId;
        prewarmMusicOutput();
        (void)tryAcquireMusicOutput();
        return true;
    } catch (...) {
        stopMusicOutput();
        return false;
    }
}

void Audio::tick(unsigned milliseconds) {
    if (activeSoundRemainingMilliseconds_ <= milliseconds) {
        activeSoundRemainingMilliseconds_ = 0;
    } else {
        activeSoundRemainingMilliseconds_ -= milliseconds;
    }
    if (activeDirectEffectRemainingMilliseconds_ <= milliseconds) {
        activeDirectEffectRemainingMilliseconds_ = 0;
    } else {
        activeDirectEffectRemainingMilliseconds_ -= milliseconds;
    }
    std::erase_if(voices_, [](const std::unique_ptr<Voice>& voice) {
        if (!(voice->header.dwFlags & WHDR_DONE)) return false;
        closeVoice(*voice);
        return true;
    });
    if (!musicEnabled_ || midiEvents_.empty() || !tryAcquireMusicOutput()) return;
    midiElapsed_ += milliseconds;
    while (true) {
        while (nextMidiEvent_ < midiEvents_.size() &&
               midiEvents_[nextMidiEvent_].milliseconds <= midiElapsed_) {
            midiOutShortMsg(midiOutput_, midiEvents_[nextMidiEvent_].message);
            ++nextMidiEvent_;
        }
        if (nextMidiEvent_ < midiEvents_.size() || !midiLoop_ || !midiDuration_ ||
            midiElapsed_ < midiDuration_) {
            break;
        }
        midiOutReset(midiOutput_);
        midiElapsed_ -= midiDuration_;
        nextMidiEvent_ = 0;
    }
}

void Audio::stop() {
    for (const auto& voice : voices_) closeVoice(*voice);
    voices_.clear();
    activeSoundRemainingMilliseconds_ = 0;
    activeDirectEffectRemainingMilliseconds_ = 0;
}

bool Audio::sourceDirectSoundGateRegressionTest() {
    if (!voices_.empty() || soundPlaying() || directEffectPlaying()) return false;

    // snd 5003 is 1,152 samples at 11,025 Hz, so the source channel remains
    // busy for 105 integer milliseconds. Exercise the same 33 ms cadence used
    // by the menu controller without opening a WinMM output device.
    activeDirectEffectRemainingMilliseconds_ = 105;
    if (!directSoundBusy()) return false;
    tick(33);
    if (!directEffectPlaying()) return false;
    tick(33);
    if (!directEffectPlaying()) return false;
    tick(38);
    if (!directEffectPlaying()) return false;
    tick(1);
    if (directSoundBusy()) return false;

    activeSoundRemainingMilliseconds_ = 1;
    const bool trackedLineIsBusy = directSoundBusy();
    activeSoundRemainingMilliseconds_ = 0;
    return trackedLineIsBusy;
}

void Audio::stopMusic() {
    requestedMusicResourceId_ = -1;
    stopMusicOutput();
}

void Audio::stopMusicOutput() {
    if (midiOutput_) {
        midiOutReset(midiOutput_);
    }
    midiEvents_.clear();
    nextMidiEvent_ = 0;
    midiElapsed_ = 0;
    midiDuration_ = 0;
    midiLoop_ = false;
    activeMusicResourceId_ = -1;
}

std::size_t Audio::midiEventCount(int resourceId) const {
    return parseMusic(resourceId).events.size();
}

std::uint64_t Audio::midiDurationMilliseconds(int resourceId) const {
    return parseMusic(resourceId).durationMilliseconds;
}

std::size_t Audio::soundWaveSize(int resourceId) const {
    const std::string_view type =
        assets_.dialect() == AssetDialect::Dos ? std::string_view("SND ")
                                               : std::string_view("snd ");
    return makeWave(assets_.get(type, resourceId), assets_.dialect()).size();
}

void Audio::setEnabled(bool enabled) {
    setSoundEnabled(enabled);
    setMusicEnabled(enabled);
}

void Audio::setSoundEnabled(bool enabled) {
    soundEnabled_ = enabled;
    if (!soundEnabled_) stop();
}

void Audio::setMusicEnabled(bool enabled) {
    if (musicEnabled_ == enabled) return;
    musicEnabled_ = enabled;
    if (!musicEnabled_) {
        stopMusicOutput();
    } else if (requestedMusicResourceId_ >= 0) {
        playMusic(requestedMusicResourceId_, requestedMusicLoop_);
    }
}

}  // namespace mf
