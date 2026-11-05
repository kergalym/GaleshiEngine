#include "GaleshiEngine/Audio/SoundPlayer.hpp"

#include "GaleshiEngine/Audio/AudioSystem.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <spdlog/spdlog.h>

namespace Galeshi {

struct SoundPlayer::Impl {
    AudioSystem* audio = nullptr;
    ma_sound sound{};
    std::string path;
    bool loaded = false;
    bool looping = false;
};

SoundPlayer::SoundPlayer(AudioSystem& audio) : m_impl(std::make_unique<Impl>()) {
    m_impl->audio = &audio;
    audio.m_players.insert(this);
}

SoundPlayer::~SoundPlayer() {
    Unload();
    if (m_impl && m_impl->audio) {
        m_impl->audio->m_players.erase(this);
    }
}

SoundPlayer::SoundPlayer(SoundPlayer&& other) noexcept
    : m_impl(std::move(other.m_impl)) {
    if (m_impl && m_impl->audio) {
        m_impl->audio->m_players.erase(&other);
        m_impl->audio->m_players.insert(this);
    }
}

SoundPlayer& SoundPlayer::operator=(SoundPlayer&& other) noexcept {
    if (this == &other) return *this;
    Unload();
    if (m_impl && m_impl->audio) {
        m_impl->audio->m_players.erase(this);
    }
    m_impl = std::move(other.m_impl);
    if (m_impl && m_impl->audio) {
        m_impl->audio->m_players.erase(&other);
        m_impl->audio->m_players.insert(this);
    }
    return *this;
}

bool SoundPlayer::Load(const std::string& path, AudioBus bus) {
    if (!m_impl || !m_impl->audio || !m_impl->audio->IsInitialized() ||
        path.empty()) {
        return false;
    }

    Unload();
    const std::string resolvedPath = m_impl->audio->ResolvePath(path);
    const ma_result result = ma_sound_init_from_file(
        &m_impl->audio->m_engine, resolvedPath.c_str(), 0,
        m_impl->audio->BusGroup(bus), nullptr, &m_impl->sound);
    if (result != MA_SUCCESS) {
        spdlog::error(
            "[Audio] Failed to load sound player '{}' (resolved '{}'): {}",
            path, resolvedPath, static_cast<int>(result));
        return false;
    }

    ma_sound_set_spatialization_enabled(&m_impl->sound, MA_FALSE);
    m_impl->path = path;
    m_impl->loaded = true;
    m_impl->looping = false;
    return true;
}

void SoundPlayer::Unload() {
    if (!m_impl) return;
    if (m_impl->loaded && m_impl->audio &&
        m_impl->audio->IsInitialized()) {
        ma_sound_stop(&m_impl->sound);
        ma_sound_uninit(&m_impl->sound);
    }
    m_impl->loaded = false;
    m_impl->looping = false;
    m_impl->path.clear();
}

bool SoundPlayer::IsLoaded() const {
    return m_impl && m_impl->loaded;
}

const std::string& SoundPlayer::Path() const {
    static const std::string empty;
    return m_impl ? m_impl->path : empty;
}

bool SoundPlayer::Play(bool loop) {
    if (!m_impl || !m_impl->loaded) return false;
    SetLooping(loop);
    if (ma_sound_at_end(&m_impl->sound) == MA_TRUE) {
        ma_sound_seek_to_pcm_frame(&m_impl->sound, 0);
    }
    return ma_sound_start(&m_impl->sound) == MA_SUCCESS;
}

bool SoundPlayer::Pause() {
    if (!m_impl || !m_impl->loaded) return false;
    return ma_sound_stop(&m_impl->sound) == MA_SUCCESS;
}

bool SoundPlayer::Resume() {
    if (!m_impl || !m_impl->loaded) return false;
    return ma_sound_start(&m_impl->sound) == MA_SUCCESS;
}

void SoundPlayer::Stop() {
    if (!m_impl || !m_impl->loaded) return;
    ma_sound_stop(&m_impl->sound);
    ma_sound_seek_to_pcm_frame(&m_impl->sound, 0);
}

bool SoundPlayer::IsPlaying() const {
    return m_impl && m_impl->loaded &&
           ma_sound_is_playing(&m_impl->sound) == MA_TRUE;
}

void SoundPlayer::SetVolume(float volume) {
    if (!m_impl || !m_impl->loaded) return;
    ma_sound_set_volume(&m_impl->sound, std::max(volume, 0.0f));
}

float SoundPlayer::Volume() const {
    return m_impl && m_impl->loaded
        ? ma_sound_get_volume(&m_impl->sound)
        : 0.0f;
}

void SoundPlayer::SetPitch(float pitch) {
    if (!m_impl || !m_impl->loaded) return;
    ma_sound_set_pitch(&m_impl->sound, std::max(pitch, 0.01f));
}

float SoundPlayer::Pitch() const {
    return m_impl && m_impl->loaded
        ? ma_sound_get_pitch(&m_impl->sound)
        : 1.0f;
}

void SoundPlayer::SetLooping(bool loop) {
    if (!m_impl || !m_impl->loaded) return;
    m_impl->looping = loop;
    ma_sound_set_looping(&m_impl->sound, loop ? MA_TRUE : MA_FALSE);
}

bool SoundPlayer::IsLooping() const {
    return m_impl && m_impl->loaded && m_impl->looping;
}

bool SoundPlayer::SeekSeconds(float seconds) {
    if (!m_impl || !m_impl->loaded) return false;
    return ma_sound_seek_to_second(
               &m_impl->sound, std::max(seconds, 0.0f)) == MA_SUCCESS;
}

float SoundPlayer::DurationSeconds() const {
    if (!m_impl || !m_impl->loaded) return 0.0f;
    float duration = 0.0f;
    return ma_sound_get_length_in_seconds(&m_impl->sound, &duration) ==
                   MA_SUCCESS
        ? duration
        : 0.0f;
}

float SoundPlayer::PositionSeconds() const {
    if (!m_impl || !m_impl->loaded) return 0.0f;
    float position = 0.0f;
    return ma_sound_get_cursor_in_seconds(&m_impl->sound, &position) ==
                   MA_SUCCESS
        ? position
        : 0.0f;
}

} // namespace Galeshi
