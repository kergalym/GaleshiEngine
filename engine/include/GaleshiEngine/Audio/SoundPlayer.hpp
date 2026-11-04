#pragma once

#include "GaleshiEngine/Audio/AudioTypes.hpp"

#include <memory>
#include <string>

namespace Galeshi {

class AudioSystem;

// One independent WAV/MP3 playback instance. The AudioSystem owns the
// miniaudio engine and must outlive every SoundPlayer created from it.
//
// Unlike AudioSystem::PlaySound(), which is keyed by file path, a SoundPlayer
// keeps its own sound object, so the same file can be played by several
// players at once.
class SoundPlayer {
public:
    explicit SoundPlayer(AudioSystem& audio);
    ~SoundPlayer();

    SoundPlayer(SoundPlayer&&) noexcept;
    SoundPlayer& operator=(SoundPlayer&&) noexcept;
    SoundPlayer(const SoundPlayer&) = delete;
    SoundPlayer& operator=(const SoundPlayer&) = delete;

    bool Load(const std::string& path, AudioBus bus = AudioBus::SFX);
    void Unload();
    bool IsLoaded() const;
    const std::string& Path() const;

    bool Play(bool loop = false);
    bool Pause();
    bool Resume();
    void Stop();
    bool IsPlaying() const;

    void SetVolume(float volume);
    float Volume() const;
    void SetPitch(float pitch);
    float Pitch() const;
    void SetLooping(bool loop);
    bool IsLooping() const;

    bool SeekSeconds(float seconds);
    float DurationSeconds() const;
    float PositionSeconds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi
