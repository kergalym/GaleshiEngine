#pragma once
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "GaleshiEngine/Audio/AudioTypes.hpp"
#include "GaleshiEngine/Scene/Components.hpp"

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>

#include <spdlog/spdlog.h>

namespace Galeshi {

// Audio playback with 3D spatialization and a small mixer-bus tree:
//
//   engine endpoint
//     └── Master
//           ├── Music
//           ├── SFX
//           ├── Voice
//           └── Ambient
//
// Every sound is attached to one bus at creation time and can be re-routed
// later. Bus volume/mute values are owned by the scene and applied here.
class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem() { Shutdown(); }

    bool Init() {
        if (m_initialized) return true;
        if (ma_engine_init(nullptr, &m_engine) != MA_SUCCESS) {
            spdlog::error("[Audio] Failed to initialize miniaudio engine");
            return false;
        }

        // Master is the root bus; the rest are children of Master.
        if (ma_sound_group_init(&m_engine, 0, nullptr,
                                &m_buses[static_cast<int>(AudioBus::Master)]) != MA_SUCCESS) {
            spdlog::error("[Audio] Failed to create Master bus");
            ma_engine_uninit(&m_engine);
            return false;
        }
        m_busReady[static_cast<int>(AudioBus::Master)] = true;

        for (int i = 1; i < kAudioBusCount; ++i) {
            if (ma_sound_group_init(&m_engine, 0,
                                    &m_buses[static_cast<int>(AudioBus::Master)],
                                    &m_buses[i]) != MA_SUCCESS) {
                spdlog::error("[Audio] Failed to create '{}' bus",
                              AudioBusName(static_cast<AudioBus>(i)));
                for (int j = i - 1; j >= 0; --j) {
                    if (m_busReady[j]) {
                        ma_sound_group_uninit(&m_buses[j]);
                        m_busReady[j] = false;
                    }
                }
                ma_engine_uninit(&m_engine);
                return false;
            }
            m_busReady[i] = true;
        }

        for (int i = 0; i < kAudioBusCount; ++i) {
            m_appliedBusVolume[i] = -1.0f;
        }
        m_initialized = true;
        spdlog::info("[Audio] Engine and {} mixer buses initialized", kAudioBusCount);
        return true;
    }

    void Shutdown() {
        if (!m_initialized) return;

        StopAll();

        // Children before Master.
        for (int i = kAudioBusCount - 1; i >= 0; --i) {
            if (m_busReady[i]) {
                ma_sound_group_uninit(&m_buses[i]);
                m_busReady[i] = false;
            }
        }
        ma_engine_uninit(&m_engine);
        m_initialized = false;
    }

    bool IsInitialized() const { return m_initialized; }

    // Stops every 2D and entity sound. Used on Play stop / scene unload.
    void StopAll() {
        if (!m_initialized) return;

        for (auto& kv : m_activeSounds) {
            ma_sound_stop(kv.second);
            ma_sound_uninit(kv.second);
            delete kv.second;
        }
        m_activeSounds.clear();

        for (auto& kv : m_sourceSounds) {
            if (!kv.second.sound) continue;
            ma_sound_stop(kv.second.sound);
            ma_sound_uninit(kv.second.sound);
            delete kv.second.sound;
        }
        m_sourceSounds.clear();
        m_hasLastListenerPosition = false;
    }

    // ------------------------------------------------------------------
    // Legacy 2D playback (Lua / one-shots). Routed through the SFX bus.
    // ------------------------------------------------------------------
    void PlaySound(const std::string& filepath, bool loop = false) {
        if (!m_initialized) return;
        StopSound(filepath);

        ma_sound* pSound = new ma_sound();
        ma_result result = ma_sound_init_from_file(
            &m_engine, filepath.c_str(), 0, BusGroup(AudioBus::SFX), nullptr, pSound);
        if (result != MA_SUCCESS) {
            spdlog::error("[Audio] Failed to play '{}': {}",
                          filepath, static_cast<int>(result));
            delete pSound;
            return;
        }
        ma_sound_set_spatialization_enabled(pSound, MA_FALSE);
        ma_sound_set_looping(pSound, loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(pSound);
        m_activeSounds[filepath] = pSound;
        spdlog::info("[Audio] Playing '{}' (2D, loop={})", filepath, loop);
    }

    void StopSound(const std::string& filepath) {
        auto it = m_activeSounds.find(filepath);
        if (it == m_activeSounds.end()) return;
        ma_sound_stop(it->second);
        ma_sound_uninit(it->second);
        delete it->second;
        m_activeSounds.erase(it);
    }

    bool IsSoundPlaying(const std::string& filepath) const {
        auto it = m_activeSounds.find(filepath);
        if (it == m_activeSounds.end()) return false;
        return ma_sound_is_playing(it->second) == MA_TRUE;
    }

    // ------------------------------------------------------------------
    // Entity-based 3D playback.
    // ------------------------------------------------------------------
    bool PlaySource(entt::entity entity, const AudioSourceComponent& source,
                    const glm::vec3& position) {
        if (!m_initialized || source.soundPath.empty()) return false;
        StopSource(entity);

        ma_sound* sound = new ma_sound();
        ma_result result = ma_sound_init_from_file(
            &m_engine, source.soundPath.c_str(), 0,
            BusGroup(source.bus), nullptr, sound);
        if (result != MA_SUCCESS) {
            spdlog::error("[Audio] Failed to play source '{}': {}",
                          source.soundPath, static_cast<int>(result));
            delete sound;
            return false;
        }

        ConfigureSound(sound, source, position);

        SourceRuntime runtime;
        runtime.sound = sound;
        runtime.bus = source.bus;
        runtime.path = source.soundPath;
        runtime.lastPosition = position;
        runtime.hasLastPosition = true;
        m_sourceSounds[entity] = std::move(runtime);

        ma_sound_start(sound);
        spdlog::info("[Audio] Playing source '{}' (3D={}, bus={}, loop={})",
                     source.soundPath, source.spatial,
                     AudioBusName(source.bus), source.loop);
        return true;
    }

    void StopSource(entt::entity entity) {
        auto it = m_sourceSounds.find(entity);
        if (it == m_sourceSounds.end()) return;
        if (it->second.sound) {
            ma_sound_stop(it->second.sound);
            ma_sound_uninit(it->second.sound);
            delete it->second.sound;
        }
        m_sourceSounds.erase(it);
    }

    bool IsSourcePlaying(entt::entity entity) const {
        auto it = m_sourceSounds.find(entity);
        if (it == m_sourceSounds.end()) return false;
        return ma_sound_is_playing(it->second.sound) == MA_TRUE;
    }

    // Applies live changes (position, volume, pitch, bus, spatialization).
    void UpdateSource(entt::entity entity, const AudioSourceComponent& source,
                      const glm::vec3& position, float dt) {
        auto it = m_sourceSounds.find(entity);
        if (it == m_sourceSounds.end()) return;
        SourceRuntime& runtime = it->second;
        if (!runtime.sound) return;

        if (runtime.path != source.soundPath) {
            StopSource(entity);
            PlaySource(entity, source, position);
            return;
        }

        if (runtime.bus != source.bus) {
            ma_sound_group* group = BusGroup(source.bus);
            if (group) {
                ma_node_attach_output_bus(&runtime.sound->engineNode.baseNode, 0,
                                          &group->engineNode.baseNode, 0);
            }
            runtime.bus = source.bus;
        }

        ConfigureSound(runtime.sound, source, position);

        if (source.spatial && dt > 0.0f && runtime.hasLastPosition) {
            const glm::vec3 velocity = (position - runtime.lastPosition) / dt;
            ma_sound_set_velocity(runtime.sound, velocity.x, velocity.y, velocity.z);
        }
        runtime.lastPosition = position;
        runtime.hasLastPosition = true;
    }

    // Stops entity sounds whose entity/component no longer exists.
    void PruneSources(const entt::registry& registry) {
        for (auto it = m_sourceSounds.begin(); it != m_sourceSounds.end();) {
            if (!registry.valid(it->first) ||
                !registry.all_of<AudioSourceComponent>(it->first)) {
                if (it->second.sound) {
                    ma_sound_stop(it->second.sound);
                    ma_sound_uninit(it->second.sound);
                    delete it->second.sound;
                }
                it = m_sourceSounds.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ------------------------------------------------------------------
    // Listener (position / orientation / velocity for doppler).
    // ------------------------------------------------------------------
    void SetListener(const glm::vec3& position, const glm::vec3& forward,
                     const glm::vec3& up, float dt = 0.0f) {
        if (!m_initialized) return;

        const glm::vec3 safeForward = SafeNormalize(forward, {0.0f, 0.0f, -1.0f});
        const glm::vec3 safeUp = SafeNormalize(up, {0.0f, 1.0f, 0.0f});

        ma_engine_listener_set_position(&m_engine, 0,
                                        position.x, position.y, position.z);
        ma_engine_listener_set_direction(&m_engine, 0,
                                         safeForward.x, safeForward.y, safeForward.z);
        ma_engine_listener_set_world_up(&m_engine, 0,
                                        safeUp.x, safeUp.y, safeUp.z);

        if (dt > 0.0f && m_hasLastListenerPosition) {
            const glm::vec3 velocity = (position - m_lastListenerPosition) / dt;
            ma_engine_listener_set_velocity(&m_engine, 0,
                                            velocity.x, velocity.y, velocity.z);
        }
        m_lastListenerPosition = position;
        m_hasLastListenerPosition = true;
    }

    // ------------------------------------------------------------------
    // Mixer buses.
    // ------------------------------------------------------------------
    void ApplyMixerSettings(const std::array<AudioBusSettings, kAudioBusCount>& settings) {
        if (!m_initialized) return;
        for (int i = 0; i < kAudioBusCount; ++i) {
            const auto busIndex = static_cast<size_t>(i);
            const float target = settings[busIndex].muted
                ? 0.0f
                : std::clamp(settings[busIndex].volume, 0.0f, 1.0f);
            if (std::abs(target - m_appliedBusVolume[i]) > 1e-6f) {
                m_appliedBusVolume[i] = target;
                if (m_busReady[i]) {
                    ma_sound_group_set_volume(&m_buses[i], target);
                    spdlog::info("[Audio] Bus '{}' volume={:.2f}{}",
                                 AudioBusName(static_cast<AudioBus>(i)), target,
                                 settings[busIndex].muted ? " (muted)" : "");
                }
            }
        }
    }

private:
    struct SourceRuntime {
        ma_sound*   sound = nullptr;
        AudioBus    bus = AudioBus::SFX;
        std::string path;
        glm::vec3   lastPosition{0.0f};
        bool        hasLastPosition = false;
    };

    static glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
        const float len = glm::length(v);
        return len > 1e-6f ? v / len : fallback;
    }

    static ma_attenuation_model ToMiniaudioAttenuation(AudioAttenuationModel model) {
        switch (model) {
            case AudioAttenuationModel::Linear:      return ma_attenuation_model_linear;
            case AudioAttenuationModel::Exponential: return ma_attenuation_model_exponential;
            case AudioAttenuationModel::Inverse:
            default:                                 return ma_attenuation_model_inverse;
        }
    }

    ma_sound_group* BusGroup(AudioBus bus) {
        const int index = static_cast<int>(bus);
        if (index >= 0 && index < kAudioBusCount && m_busReady[index]) {
            return &m_buses[index];
        }
        return &m_buses[static_cast<int>(AudioBus::Master)];
    }

    void ConfigureSound(ma_sound* sound, const AudioSourceComponent& source,
                        const glm::vec3& position) {
        ma_sound_set_looping(sound, source.loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_spatialization_enabled(sound, source.spatial ? MA_TRUE : MA_FALSE);
        ma_sound_set_volume(sound, std::max(source.volume, 0.0f));
        ma_sound_set_pitch(sound, std::max(source.pitch, 0.01f));

        if (source.spatial) {
            ma_sound_set_positioning(sound, ma_positioning_absolute);
            ma_sound_set_position(sound, position.x, position.y, position.z);
            ma_sound_set_attenuation_model(
                sound, ToMiniaudioAttenuation(source.attenuation));
            ma_sound_set_min_distance(sound, std::max(source.minDistance, 0.0f));
            ma_sound_set_max_distance(
                sound, std::max(source.maxDistance, source.minDistance));
            ma_sound_set_rolloff(sound, std::max(source.rolloff, 0.01f));
            ma_sound_set_doppler_factor(
                sound, std::max(source.dopplerFactor, 0.0f));
        }
    }

    ma_engine m_engine{};
    ma_sound_group m_buses[kAudioBusCount]{};
    bool m_busReady[kAudioBusCount]{};
    float m_appliedBusVolume[kAudioBusCount]{};
    bool m_initialized = false;

    std::unordered_map<std::string, ma_sound*> m_activeSounds;
    std::unordered_map<entt::entity, SourceRuntime> m_sourceSounds;

    glm::vec3 m_lastListenerPosition{0.0f};
    bool m_hasLastListenerPosition = false;
};

} // namespace Galeshi
