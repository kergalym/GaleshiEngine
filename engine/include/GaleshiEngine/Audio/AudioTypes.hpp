#pragma once

namespace Galeshi {

// Mixer buses. Every sound is routed through one of these groups; groups are
// children of Master, which feeds the engine endpoint.
enum class AudioBus : int {
    Master = 0,
    Music,
    SFX,
    Voice,
    Ambient,
    Count
};

inline constexpr int kAudioBusCount = static_cast<int>(AudioBus::Count);

struct AudioBusSettings {
    float volume = 1.0f;
    bool  muted  = false;
};

enum class AudioAttenuationModel : int {
    Inverse = 0,
    Linear,
    Exponential
};

inline const char* AudioBusName(AudioBus bus) {
    switch (bus) {
        case AudioBus::Master:  return "Master";
        case AudioBus::Music:   return "Music";
        case AudioBus::SFX:     return "SFX";
        case AudioBus::Voice:   return "Voice";
        case AudioBus::Ambient: return "Ambient";
        default:                return "Unknown";
    }
}

inline const char* AudioAttenuationModelName(AudioAttenuationModel model) {
    switch (model) {
        case AudioAttenuationModel::Inverse:     return "Inverse";
        case AudioAttenuationModel::Linear:      return "Linear";
        case AudioAttenuationModel::Exponential: return "Exponential";
        default:                                 return "Unknown";
    }
}

} // namespace Galeshi
