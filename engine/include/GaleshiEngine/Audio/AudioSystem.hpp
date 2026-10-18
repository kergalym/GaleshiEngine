#pragma once
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <string>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace Galeshi {

class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem() { Shutdown(); }

    bool Init() {
        if (ma_engine_init(NULL, &m_engine) != MA_SUCCESS) return false;
        m_initialized = true;
        return true;
    }
    void Shutdown() {
        if (!m_initialized) return;
        for (auto& kv : m_activeSounds) {
            ma_sound_uninit(kv.second);
            delete kv.second;
        }
        m_activeSounds.clear();
        ma_engine_uninit(&m_engine);
        m_initialized = false;
    }

    void PlaySound(const std::string& filepath, bool loop = false) {
        if (!m_initialized) return;
        StopSound(filepath);
        ma_sound* pSound = new ma_sound();
        ma_result result = ma_sound_init_from_file(
            &m_engine, filepath.c_str(), 0, NULL, NULL, pSound);
        if (result != MA_SUCCESS) {
            spdlog::error("[Audio] Failed to play '{}': {}", filepath, static_cast<int>(result));
            delete pSound;
            return;
        }
        ma_sound_set_looping(pSound, loop);
        ma_sound_start(pSound);
        m_activeSounds[filepath] = pSound;
        spdlog::info("[Audio] Playing '{}' (loop={})", filepath, loop);
    }

    void StopSound(const std::string& filepath) {
        auto it = m_activeSounds.find(filepath);
        if (it == m_activeSounds.end()) return;
        ma_sound_stop(it->second);
        ma_sound_uninit(it->second);
        delete it->second;
        m_activeSounds.erase(it);
    }

    bool IsSoundPlaying(const std::string& filepath) {
        auto it = m_activeSounds.find(filepath);
        if (it == m_activeSounds.end()) return false;
        return ma_sound_is_playing(it->second) == MA_TRUE;
    }

private:
    ma_engine m_engine;
    bool m_initialized = false;
    std::unordered_map<std::string, ma_sound*> m_activeSounds;
};

} // namespace Galeshi
