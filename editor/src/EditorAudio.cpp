#include "GaleshiEditorApp.hpp"



    // -------------------------------------------------------------------
    // Audio
    // -------------------------------------------------------------------
    void GaleshiEditorApp::UpdateAudio(float dt) {
        if (!m_audio.IsInitialized()) return;

        // Mixer values live on the scene so they are serialized with it.
        m_audio.ApplyMixerSettings(m_scene.AudioBuses());
        m_audio.PruneSources(m_scene.Registry());

        // Listener: a primary AudioListenerComponent wins; otherwise the
        // editor/game camera acts as the listener.
        glm::vec3 listenerPos = m_camera.Position();
        glm::vec3 listenerForward = -glm::vec3(m_camera.View()[0][2],
                                               m_camera.View()[1][2],
                                               m_camera.View()[2][2]);
        glm::vec3 listenerUp = glm::vec3(m_camera.View()[0][1],
                                         m_camera.View()[1][1],
                                         m_camera.View()[2][1]);

        auto listenerView = m_scene.Registry().view<Galeshi::AudioListenerComponent,
                                                     Galeshi::TransformComponent>();
        for (auto e : listenerView) {
            const auto& listener =
                listenerView.get<Galeshi::AudioListenerComponent>(e);
            if (!listener.primary) continue;

            const glm::mat4 world = EntityWorldMatrix(e);
            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat rotation;
            glm::decompose(world, scale, rotation, translation, skew, perspective);
            listenerPos = translation;
            listenerForward = glm::normalize(rotation * glm::vec3(0.0f, 0.0f, -1.0f));
            listenerUp = glm::normalize(rotation * glm::vec3(0.0f, 1.0f, 0.0f));
            break;
        }
        m_audio.SetListener(listenerPos, listenerForward, listenerUp, dt);

        auto view = m_scene.Registry().view<Galeshi::AudioSourceComponent>();
        for (auto e : view) {
            auto& source = view.get<Galeshi::AudioSourceComponent>(e);

            glm::vec3 position(0.0f);
            if (m_scene.Registry().all_of<Galeshi::TransformComponent>(e)) {
                const glm::mat4 world = EntityWorldMatrix(e);
                glm::vec3 scale, translation, skew;
                glm::vec4 perspective;
                glm::quat rotation;
                glm::decompose(world, scale, rotation, translation, skew, perspective);
                position = translation + rotation * source.offset;
            }

            const bool playing = m_audio.IsSourcePlaying(e);
            if (source.autoplay && !playing && !source.soundPath.empty()) {
                m_audio.PlaySource(e, source, position);
            } else if (playing) {
                m_audio.UpdateSource(e, source, position, dt);
            }
            source.playing = m_audio.IsSourcePlaying(e);
        }
    }



    void GaleshiEditorApp::DrawAudioMixer() {
        if (!m_showAudioMixer) return;
        if (ImGui::Begin("Audio Mixer", &m_showAudioMixer)) {
            auto& buses = m_scene.AudioBuses();
            ImGui::TextDisabled(
                "Audio Sources are routed through these buses. "
                "Values are saved with the scene.");
            ImGui::Separator();

            for (int i = 0; i < Galeshi::kAudioBusCount; ++i) {
                const auto bus = static_cast<Galeshi::AudioBus>(i);
                ImGui::PushID(i);
                ImGui::TextUnformatted(Galeshi::AudioBusName(bus));
                ImGui::SameLine(110.0f);
                ImGui::SetNextItemWidth(200.0f);
                float volume = buses[static_cast<size_t>(i)].volume;
                if (ImGui::SliderFloat("##Volume", &volume, 0.0f, 1.0f,
                                       "%.2f")) {
                    buses[static_cast<size_t>(i)].volume = volume;
                }
                ImGui::SameLine();
                bool muted = buses[static_cast<size_t>(i)].muted;
                if (ImGui::Checkbox("Mute", &muted)) {
                    buses[static_cast<size_t>(i)].muted = muted;
                }
                if (i == static_cast<int>(Galeshi::AudioBus::Master)) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(final output)");
                }
                ImGui::PopID();
            }

            ImGui::Separator();
            if (ImGui::Button("Reset Mixer")) {
                for (auto& bus : buses) {
                    bus.volume = 1.0f;
                    bus.muted = false;
                }
            }
        }
        ImGui::End();
    }

