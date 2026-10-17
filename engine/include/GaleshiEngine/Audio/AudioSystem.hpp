#pragma once
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <string>

namespace Galeshi {
class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem() { Shutdown(); }
    bool Init() {
        if (ma_engine_init(NULL, &m_engine) != MA_SUCCESS) return false;
        m_initialized = true; return true;
    }
    void Shutdown() { if (m_initialized) { ma_engine_uninit(&m_engine); m_initialized = false; } }
    void PlaySound(const std::string& filepath) { if (m_initialized) ma_engine_play_sound(&m_engine, filepath.c_str(), NULL); }
private:
    ma_engine m_engine;
    bool m_initialized = false;
};
}
