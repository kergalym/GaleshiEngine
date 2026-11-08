#include "GaleshiEditorApp.hpp"



    void GaleshiEditorApp::ApplySceneViewState() {
        const Galeshi::SceneCameraState& camera = m_scene.CameraState();
        if (camera.valid) {
            // Position + target are authoritative.  Reconstructing the orbit
            // controls from them guarantees the restored camera position even
            // if a hand-edited scene has inconsistent yaw/pitch/radius values.
            const glm::vec3 offset = camera.position - camera.target;
            const float offsetLength = glm::length(offset);
            if (offsetLength > 1e-4f) {
                m_editor.camTarget = camera.target;
                m_editor.camRadius = glm::clamp(offsetLength, 0.5f, 200.0f);
                m_editor.camPitch = glm::clamp(
                    std::asin(glm::clamp(offset.y / offsetLength, -1.0f, 1.0f)),
                    -1.5f, 1.5f);
                m_editor.camYaw = std::atan2(offset.x, offset.z);
            } else {
                m_editor.camTarget = camera.target;
                m_editor.camYaw = camera.yaw;
                m_editor.camPitch = glm::clamp(camera.pitch, -1.5f, 1.5f);
                m_editor.camRadius = glm::clamp(camera.radius, 0.5f, 200.0f);
            }

            const float aspect = GetRenderer() && GetRenderer()->Height() > 0
                ? static_cast<float>(GetRenderer()->Width()) /
                  static_cast<float>(GetRenderer()->Height())
                : 16.0f / 9.0f;
            m_camera.SetPerspective(
                glm::radians(m_preferences.cameraFovDegrees), aspect,
                0.1f, 1000.0f);
            m_camera.Orbit(m_editor.camTarget, m_editor.camYaw,
                           m_editor.camPitch, m_editor.camRadius);
            spdlog::info("[Scene] Restored camera position=({}, {}, {}) "
                         "target=({}, {}, {})",
                         m_camera.Position().x, m_camera.Position().y,
                         m_camera.Position().z, camera.target.x, camera.target.y,
                         camera.target.z);
        }

        if (m_scene.HasGlobalPostProcess()) {
            const Galeshi::PostProcessSettings& pp =
                m_scene.GlobalPostProcess();
            m_postProcess.SetBloomEnabled(pp.bloomEnabled);
            m_postProcess.SetDofEnabled(pp.dofEnabled);
            m_postProcess.SetMotionBlurEnabled(pp.motionBlurEnabled);
            m_postProcess.SetSsaoEnabled(pp.ssaoEnabled);
            m_globalAoMode = pp.aoMode;
            m_postProcess.SetSsrEnabled(pp.ssrEnabled);
            m_postProcess.SetSsgiEnabled(pp.ssgiEnabled);
            m_postProcess.SetFxaaEnabled(pp.fxaaEnabled);
            m_postProcess.SetSmaaEnabled(pp.smaaEnabled);
            m_postProcess.SetTaaEnabled(pp.taaEnabled);
            m_postProcess.SetGodRaysEnabled(pp.godRaysEnabled);
            m_postProcess.SetAtmosphereEnabled(pp.atmosphereEnabled);
            m_postProcess.SetFogEnabled(pp.fogEnabled);
            m_postProcess.SetFogVolumetric(pp.fogVolumetric);
            spdlog::info("[Scene] Restored global post-process settings "
                         "(environment palette={}, amount={})",
                         pp.environmentPaletteEnabled,
                         pp.environmentPaletteAmount);
        }

        // Apply pending procedural water erosion after the scene is fully
        // loaded. A saved scene with a carved landscape has terrainCarved set
        // and is skipped, so reloading cannot deepen the basin twice. The
        // water-owned underwater terrain is refreshed either way.
        for (auto h : m_scene.Registry().view<Galeshi::WaterComponent>()) {
            auto& water = m_scene.Registry().get<Galeshi::WaterComponent>(h);
            if (water.enabled && water.carveLandscape &&
                !water.terrainCarved) {
                ApplyWaterErosion(h);
            } else if (water.terrainCarved) {
                EnsureUnderwaterTerrain(h);
                RefreshWaterShorePaint(h);
            }
            ApplyWaterShoreSettings(h);
        }
        RebuildWaterVegetationMasks();

        // The view and effect set may have changed completely; never reuse a
        // TAA history from the previous scene/camera.
        m_postProcess.ResetTaaHistory();
        m_waterSubmerged.clear();
        m_bodyWaterSurfaceY.clear();
        m_bodyWaterEntity.clear();
        m_bodyWaterUV.clear();
        m_bubbleAccumulator = 0.0f;
        if (m_fluid) m_fluid->Reset();
    }



    float GaleshiEditorApp::PssmPcfRadius() const {
        const float sharpness = glm::clamp(m_pssmShadowSharpness, 0.0f, 1.0f);
        return (1.0f - sharpness) * 4.0f;
    }



    Galeshi::PostProcessSettings GaleshiEditorApp::DefaultPostProcessSettings() const {
        Galeshi::PostProcessSettings s =
            m_scene.HasGlobalPostProcess()
                ? m_scene.GlobalPostProcess()
                : Galeshi::PostProcessSettings{};
        s.bloomEnabled       = m_postProcess.BloomEnabled();
        s.dofEnabled         = m_postProcess.DofEnabled();
        s.motionBlurEnabled  = m_postProcess.MotionBlurEnabled();
        s.ssaoEnabled        = m_postProcess.SsaoEnabled();
        s.aoMode             = m_globalAoMode;
        s.ssrEnabled         = m_postProcess.SsrEnabled();
        s.ssgiEnabled        = m_postProcess.SsgiEnabled();
        s.fxaaEnabled        = m_postProcess.FxaaEnabled();
        s.smaaEnabled        = m_postProcess.SmaaEnabled();
        s.taaEnabled         = m_postProcess.TaaEnabled();
        s.godRaysEnabled     = m_postProcess.GodRaysEnabled();
        s.atmosphereEnabled  = m_postProcess.AtmosphereEnabled();
        s.fogEnabled         = m_postProcess.FogEnabled();
        s.fogVolumetric      = m_postProcess.FogVolumetric();
        return s;
    }



    // The active post-process source is the highest-priority enabled volume,
    // or the scene-global settings when no volume is enabled. Both the
    // Post-Process menu and the volume inspector edit this active source, so
    // the most recently changed control always wins instead of the menu being
    // silently shadowed by a volume.
    Galeshi::PostProcessVolumeComponent* GaleshiEditorApp::ActivePostProcessVolume() {
        auto view = m_scene.Registry()
            .view<Galeshi::PostProcessVolumeComponent>();
        Galeshi::PostProcessVolumeComponent* best = nullptr;
        int bestPriority = std::numeric_limits<int>::min();
        for (auto e : view) {
            auto& volume =
                view.get<Galeshi::PostProcessVolumeComponent>(e);
            if (!volume.enabled) continue;
            if (volume.priority > bestPriority) {
                bestPriority = volume.priority;
                best = &volume;
            }
        }
        return best;
    }



    Galeshi::PostProcessSettings& GaleshiEditorApp::ActivePostProcessSettings() {
        if (auto* volume = ActivePostProcessVolume())
            return volume->settings;

        // With no active volume the manager toggles are authoritative. Mirror
        // them into the scene-global settings so the menu always displays the
        // values that are actually being rendered.
        Galeshi::PostProcessSettings& global = m_scene.GlobalPostProcess();
        global.bloomEnabled      = m_postProcess.BloomEnabled();
        global.dofEnabled        = m_postProcess.DofEnabled();
        global.motionBlurEnabled = m_postProcess.MotionBlurEnabled();
        global.ssaoEnabled       = m_postProcess.SsaoEnabled();
        global.aoMode            = m_globalAoMode;
        global.ssrEnabled        = m_postProcess.SsrEnabled();
        global.ssgiEnabled       = m_postProcess.SsgiEnabled();
        global.fxaaEnabled       = m_postProcess.FxaaEnabled();
        global.smaaEnabled       = m_postProcess.SmaaEnabled();
        global.taaEnabled        = m_postProcess.TaaEnabled();
        global.godRaysEnabled    = m_postProcess.GodRaysEnabled();
        global.atmosphereEnabled = m_postProcess.AtmosphereEnabled();
        global.fogEnabled        = m_postProcess.FogEnabled();
        global.fogVolumetric     = m_postProcess.FogVolumetric();
        return global;
    }



    Galeshi::PostProcessSettings GaleshiEditorApp::ResolvePostProcessSettings() {
        if (auto* volume = ActivePostProcessVolume())
            return volume->settings;
        return DefaultPostProcessSettings();
    }



    glm::vec3 GaleshiEditorApp::CameraForward() const {
        return glm::normalize(-glm::vec3(
            m_camera.View()[0][2],
            m_camera.View()[1][2],
            m_camera.View()[2][2]));
    }



    float GaleshiEditorApp::ViewSpaceDepth(const glm::vec3& worldPoint) const {
        const glm::vec3 cameraPos = m_camera.Position();
        const float distance = glm::dot(worldPoint - cameraPos, CameraForward());
        return std::max(distance, 0.05f);
    }



    // Returns the world-space centre of all mesh bounds below a root entity.
    // For skeletal characters the meshes live on child entities, so the root
    // alone usually only has a feet-level transform.
    bool GaleshiEditorApp::EntityWorldBoundsCenter(entt::entity root,  glm::vec3& outCenter,
                                 glm::vec3* outMin,
                                 glm::vec3* outMax) {
        auto& reg = m_scene.Registry();
        glm::vec3 boundsMin(std::numeric_limits<float>::max());
        glm::vec3 boundsMax(-std::numeric_limits<float>::max());
        bool haveBounds = false;

        for (auto entity : m_scene.AllEntities()) {
            const entt::entity handle = entity.Handle();
            entt::entity current = handle;
            bool belongsToRoot = false;
            int depth = 0;
            while (current != entt::null && depth++ < 128) {
                if (current == root) {
                    belongsToRoot = true;
                    break;
                }
                if (reg.all_of<Galeshi::ParentComponent>(current)) {
                    current = reg.get<Galeshi::ParentComponent>(current).parent;
                } else {
                    break;
                }
            }
            if (!belongsToRoot || !reg.all_of<Galeshi::MeshComponent>(handle)) {
                continue;
            }

            const auto& mesh = reg.get<Galeshi::MeshComponent>(handle).mesh;
            if (!mesh || !mesh->Valid()) continue;

            glm::vec3 localMin, localMax;
            mesh->LocalAABB(localMin, localMax);
            const glm::mat4 world = EntityWorldMatrix(handle);
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 local{
                    (corner & 1) ? localMax.x : localMin.x,
                    (corner & 2) ? localMax.y : localMin.y,
                    (corner & 4) ? localMax.z : localMin.z,
                };
                const glm::vec3 point =
                    glm::vec3(world * glm::vec4(local, 1.0f));
                boundsMin = glm::min(boundsMin, point);
                boundsMax = glm::max(boundsMax, point);
            }
            haveBounds = true;
        }

        if (haveBounds) {
            outCenter = (boundsMin + boundsMax) * 0.5f;
            if (outMin) *outMin = boundsMin;
            if (outMax) *outMax = boundsMax;
            return true;
        }

        const glm::mat4 world = EntityWorldMatrix(root);
        outCenter = glm::vec3(world[3]);
        if (reg.all_of<Galeshi::SkeletalComponent>(root)) {
            outCenter.y += 1.0f;
        }
        if (outMin) *outMin = outCenter;
        if (outMax) *outMax = outCenter;
        return false;
    }



    entt::entity GaleshiEditorApp::FindAutoFocusEntity() {
        auto& reg = m_scene.Registry();

        // A tagged player is the intended subject in both editor preview and
        // gameplay.
        auto playerView =
            reg.view<Galeshi::PlayerTagComponent, Galeshi::TransformComponent>();
        for (auto entity : playerView) {
            return entity;
        }

        // A selected character or mesh is an explicit focus request. Ignore
        // volumes, lights and other editor helpers that happen to be selected.
        Galeshi::Entity selected = m_editor.Selected(&m_scene);
        if (selected) {
            const entt::entity handle = selected.Handle();
            if (reg.all_of<Galeshi::SkeletalComponent>(handle) ||
                reg.all_of<Galeshi::MeshComponent>(handle)) {
                return handle;
            }
        }

        // Otherwise focus the closest skeletal character. This keeps actors
        // sharp when the camera target is not exactly on them.
        const glm::vec3 cameraPos = m_camera.Position();
        const glm::vec3 forward = CameraForward();
        const glm::mat4 viewProj = m_camera.Projection() * m_camera.View();
        float bestDistance = std::numeric_limits<float>::max();
        entt::entity best = entt::null;

        auto characterView =
            reg.view<Galeshi::SkeletalComponent, Galeshi::TransformComponent>();
        for (auto entity : characterView) {
            const glm::mat4 world = EntityWorldMatrix(entity);
            // Root transforms are usually at the feet. Use a rough chest
            // position for visibility/nearest tests so a cropped close-up
            // still selects the character whose feet are below the frame.
            const glm::vec3 approxCenter =
                glm::vec3(world[3]) + glm::vec3(0.0f, 1.0f, 0.0f);
            const float depth = glm::dot(approxCenter - cameraPos, forward);
            if (depth <= 0.05f || depth >= bestDistance) continue;

            // Ignore characters clearly outside the visible frame. Keep a
            // generous margin because the exact AABB centre is computed later.
            const glm::vec4 clip = viewProj * glm::vec4(approxCenter, 1.0f);
            if (clip.w <= 0.0f) continue;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1.5f || std::abs(ndc.y) > 1.5f) continue;

            bestDistance = depth;
            best = entity;
        }
        return best;
    }



    float GaleshiEditorApp::AutoFocusDistance() {
        const entt::entity target = FindAutoFocusEntity();
        glm::vec3 focusPoint = m_editor.camTarget;

        if (target != entt::null) {
            EntityWorldBoundsCenter(target, focusPoint);
        }

        return ViewSpaceDepth(focusPoint);
    }



    float GaleshiEditorApp::ResolveDofFocusDistance(const Galeshi::PostProcessSettings& s) {
        return s.dofAutoFocus ? AutoFocusDistance() : s.dofFocusDistance;
    }



    void GaleshiEditorApp::ApplyEnvironmentPalettePreset(
        Galeshi::PostProcessSettings& settings,
        const EnvironmentPalettePreset& preset) {
        settings.environmentPaletteEnabled = true;
        settings.environmentPaletteAmount = preset.amount;
        settings.environmentPaletteShadow = preset.shadow;
        settings.environmentPaletteMid = preset.mid;
        settings.environmentPaletteHighlight = preset.highlight;
    }



    bool GaleshiEditorApp::DrawEnvironmentPaletteControls(Galeshi::PostProcessSettings& settings) {
        bool changed = false;
        changed |= ImGui::Checkbox("Enabled##environment_palette",
                                   &settings.environmentPaletteEnabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Apply a shadow/midtone/highlight palette to the final\n"
                "environment render while preserving luminance.");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        changed |= ImGui::SliderFloat("Amount##environment_palette",
                                      &settings.environmentPaletteAmount,
                                      0.0f, 1.0f, "%.2f");

        changed |= ImGui::ColorEdit3(
            "Shadows##environment_palette",
            glm::value_ptr(settings.environmentPaletteShadow));
        changed |= ImGui::ColorEdit3(
            "Midtones##environment_palette",
            glm::value_ptr(settings.environmentPaletteMid));
        changed |= ImGui::ColorEdit3(
            "Highlights##environment_palette",
            glm::value_ptr(settings.environmentPaletteHighlight));

        ImGui::TextDisabled("Presets");
        int index = 0;
        for (const EnvironmentPalettePreset& preset :
             kEnvironmentPalettePresets) {
            if ((index % 4) != 0) ImGui::SameLine();
            if (ImGui::Button(preset.name)) {
                ApplyEnvironmentPalettePreset(settings, preset);
                changed = true;
            }
            ++index;
        }
        return changed;
    }



    void GaleshiEditorApp::DrawEnvironmentPaletteWindow() {
        if (!m_showEnvironmentPalette) return;
        if (!ImGui::Begin("Environment Palette", &m_showEnvironmentPalette)) {
            ImGui::End();
            return;
        }

        ImGui::TextDisabled(
            "A colour palette applied to the final environment render. "
            "The filter runs in the color-correction pass after tonemapping, "
            "so it does not affect the editor UI.");
        auto* activeVolume = ActivePostProcessVolume();
        ImGui::TextDisabled("Editing: %s",
                            activeVolume ? "active Post-Process Volume"
                                         : "global Post-Process settings");
        ImGui::Separator();

        Galeshi::PostProcessSettings& settings =
            activeVolume ? activeVolume->settings
                         : m_scene.GlobalPostProcess();
        ImGui::PushID(&settings);
        const bool changed = DrawEnvironmentPaletteControls(settings);
        ImGui::PopID();
        if (changed && !activeVolume) m_scene.SetHasGlobalPostProcess(true);

        ImGui::Separator();
        ImGui::TextDisabled(
            "An enabled Post-Process Volume overrides these values for the "
            "entire scene. The highest-priority volume wins.");
        ImGui::End();
    }



    void GaleshiEditorApp::DrawPostProcessVolumeSettings(Galeshi::PostProcessVolumeComponent& volume) {
        auto& s = volume.settings;

        ImGui::Checkbox("Enabled", &volume.enabled);
        ImGui::DragInt("Priority", &volume.priority, 1.0f, -100, 100);
        ImGui::DragFloat3("Editor Bounds", glm::value_ptr(volume.size),
                          0.1f, 0.1f, 10000.0f);
        ImGui::Checkbox("Visible in Editor", &volume.visibleInEditor);

        if (!volume.enabled) {
            ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f),
                               "Status: disabled");
        } else if (ActivePostProcessVolume() == &volume) {
            ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.55f, 1.0f),
                               "Status: active (entire scene)");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f),
                               "Status: overridden by a higher-priority volume");
        }
        ImGui::TextDisabled(
            "Volumes affect the whole scene; the highest-priority enabled "
            "volume overrides global Post-Process settings. The Post-Process "
            "menu edits the active source. Editor Bounds are only a visual "
            "reference and do not limit the effect.");

        if (ImGui::TreeNode("Bloom")) {
            ImGui::Checkbox("Enabled##bloom", &s.bloomEnabled);
            ImGui::DragFloat("Threshold", &s.bloomThreshold,
                             0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("Intensity", &s.bloomIntensity,
                             0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Radius", &s.bloomRadius,
                             0.01f, 0.01f, 10.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Depth of Field")) {
            ImGui::Checkbox("Enabled##dof", &s.dofEnabled);
            ImGui::Checkbox("Auto Focus (Target)", &s.dofAutoFocus);
            if (ImGui::IsItemHovered() && s.dofAutoFocus) {
                ImGui::SetTooltip("Focus follows the tagged player, the selected\n"
                                  "character/mesh, or the nearest skeletal character.\n"
                                  "Falls back to the camera orbit target.");
            }

            if (s.dofAutoFocus) {
                float autoFocus = ResolveDofFocusDistance(s);
                ImGui::BeginDisabled();
                ImGui::DragFloat("Focus Distance##dof", &autoFocus,
                                 0.1f, 0.05f, 10000.0f, "%.3f");
                ImGui::EndDisabled();
                ImGui::TextDisabled("Auto: %.3f m", autoFocus);
            } else {
                ImGui::DragFloat("Focus Distance##dof", &s.dofFocusDistance,
                                 0.1f, 0.05f, 10000.0f, "%.3f");
            }
            ImGui::DragFloat("Blur (px)", &s.dofBlur,
                             0.1f, 0.0f, 64.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Motion Blur")) {
            ImGui::Checkbox("Enabled##motionblur", &s.motionBlurEnabled);
            ImGui::DragFloat("Amount", &s.motionBlurAmount,
                             0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Max Velocity (px)", &s.motionBlurMaxVelocity,
                             0.5f, 0.0f, 1000.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Tonemap")) {
            ImGui::DragFloat("Exposure", &s.exposure, 0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("Contrast", &s.contrast, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Saturation", &s.saturation, 0.01f, 0.0f, 5.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Color Correction")) {
            ImGui::DragFloat("Exposure##cc", &s.colorExposure,
                             0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("Contrast##cc", &s.colorContrast,
                             0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Saturation##cc", &s.colorSaturation,
                             0.01f, 0.0f, 5.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Environment Palette")) {
            ImGui::PushID(&s);
            DrawEnvironmentPaletteControls(s);
            ImGui::PopID();
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Vignette")) {
            ImGui::DragFloat("Intensity##vignette", &s.vignetteIntensity,
                             0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Smoothness", &s.vignetteSmoothness,
                             0.01f, 0.26f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Chromatic Aberration")) {
            ImGui::DragFloat("Intensity##chroma", &s.chromaticAberration,
                             0.0001f, 0.0f, 0.1f, "%.4f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Ambient Occlusion")) {
            ImGui::Checkbox("Enabled##ao", &s.ssaoEnabled);
            int aoMode = static_cast<int>(s.aoMode);
            const char* aoModes[] = { "SSAO", "HBAO", "GTAO" };
            if (ImGui::Combo("Method", &aoMode, aoModes, 3)) {
                if (aoMode < 0) aoMode = 0;
                if (aoMode > 2) aoMode = 2;
                s.aoMode = static_cast<Galeshi::AmbientOcclusionMode>(aoMode);
            }

            int aoResolution = s.aoResolutionScale <= 0.375f ? 0 : 1;
            const char* aoResolutions[] = { "Quarter (fast)", "Half (quality)" };
            if (ImGui::Combo("Resolution", &aoResolution, aoResolutions, 2)) {
                s.aoResolutionScale = aoResolution == 0 ? 0.25f : 0.5f;
            }

            if (s.aoMode == Galeshi::AmbientOcclusionMode::SSAO) {
                ImGui::DragFloat("Radius", &s.ssaoRadius,
                                 0.5f, 0.0f, 1000.0f);
                ImGui::DragFloat("Bias", &s.ssaoBias,
                                 0.0001f, 0.0f, 1.0f, "%.4f");
                ImGui::DragFloat("Max Distance", &s.ssaoMaxDistance,
                                 0.1f, 0.0f, 1000.0f);
                ImGui::DragFloat("Strength", &s.ssaoStrength,
                                 0.01f, 0.0f, 10.0f);
            } else {
                ImGui::DragFloat("Radius##ao", &s.aoRadius,
                                 0.01f, 0.01f, 100.0f);
                ImGui::DragFloat("Bias##ao", &s.aoBias,
                                 0.001f, 0.0f, 1.0f, "%.4f");
                if (s.aoMode == Galeshi::AmbientOcclusionMode::GTAO) {
                    ImGui::DragFloat("Thickness##ao", &s.aoThickness,
                                     0.01f, 0.01f, 20.0f);
                }
                ImGui::DragFloat("Max Distance##ao", &s.aoMaxDistance,
                                 0.1f, 0.1f, 10000.0f);
                ImGui::DragFloat("Strength##ao", &s.aoStrength,
                                 0.01f, 0.0f, 10.0f);
                if (s.aoMode == Galeshi::AmbientOcclusionMode::HBAO) {
                    ImGui::SliderInt("Directions", &s.aoDirections, 2, 16);
                    ImGui::SliderInt("Steps per Direction", &s.aoSteps, 1, 12);
                } else {
                    ImGui::SliderInt("Slices", &s.aoDirections, 2, 16);
                    ImGui::SliderInt("Steps per Side", &s.aoSteps, 1, 12);
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("SSR")) {
            ImGui::Checkbox("Enabled##ssr", &s.ssrEnabled);
            ImGui::DragFloat("Intensity##ssr", &s.ssrIntensity,
                             0.01f, 0.0f, 5.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Screen-Space GI")) {
            ImGui::Checkbox("Enabled##ssgi", &s.ssgiEnabled);
            ImGui::DragFloat("Intensity##ssgi", &s.ssgiIntensity,
                             0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Radius##ssgi", &s.ssgiRadius,
                             0.05f, 0.05f, 50.0f, "%.2f m");
            ImGui::DragFloat("Thickness##ssgi", &s.ssgiThickness,
                             0.001f, 0.001f, 1.0f, "%.3f");
            ImGui::SliderInt("Rays##ssgi", &s.ssgiRays, 1, 8);
            ImGui::SliderInt("Steps##ssgi", &s.ssgiSteps, 2, 24);
            ImGui::TextDisabled("Half-resolution diffuse bounce");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Anti-Aliasing")) {
            ImGui::Checkbox("FXAA", &s.fxaaEnabled);
            ImGui::Checkbox("SMAA", &s.smaaEnabled);
            ImGui::Checkbox("TAA", &s.taaEnabled);
            ImGui::DragFloat("TAA Blend", &s.taaBlend,
                             0.005f, 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("God Rays")) {
            ImGui::Checkbox("Enabled##godrays", &s.godRaysEnabled);
            ImGui::DragFloat("Intensity##godrays", &s.godRaysIntensity,
                             0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("Decay", &s.godRaysDecay,
                             0.005f, 0.0f, 1.0f);
            ImGui::DragFloat("Density", &s.godRaysDensity,
                             0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Weight", &s.godRaysWeight,
                             0.01f, 0.0f, 10.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Fog")) {
            ImGui::Checkbox("Enabled##fog", &s.fogEnabled);
            ImGui::SameLine();
            if (ImGui::Checkbox("Volumetric", &s.fogVolumetric) && s.fogVolumetric) {
                s.fogEnabled = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Ray-march the height fog and sample PSSM shadows\n"
                                  "for directional-light shafts.");
            }
            ImGui::ColorEdit3("Color##fog", glm::value_ptr(s.fogColor));
            ImGui::DragFloat("Density", &s.fogDensity,
                             0.0005f, 0.0f, 1.0f, "%.4f");
            ImGui::DragFloat("Height Falloff", &s.fogHeightFalloff,
                             0.001f, 0.0f, 1.0f, "%.4f");
            ImGui::DragFloat("Base Height", &s.fogBaseHeight,
                             0.1f, -1000.0f, 1000.0f);
            ImGui::DragFloat("Start Distance", &s.fogStartDistance,
                             0.5f, 0.0f, 10000.0f);
            ImGui::DragFloat("Max Distance", &s.fogMaxDistance,
                             1.0f, 1.0f, 10000.0f);
            ImGui::DragFloat("Sun Scattering", &s.fogSunScattering,
                             0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Anisotropy", &s.fogAnisotropy,
                             0.01f, -0.95f, 0.95f);
            if (s.fogVolumetric) {
                ImGui::SliderInt("Steps", &s.fogSteps, 4, 64);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Atmosphere")) {
            ImGui::Checkbox("Enabled##atmosphere", &s.atmosphereEnabled);
            ImGui::DragFloat("Intensity##atmosphere", &s.atmosphereIntensity,
                             0.1f, 0.0f, 1000.0f);
            ImGui::TreePop();
        }

        if (ImGui::Button("Reset Post-Process Settings")) {
            // Restore the full-effects defaults used when a volume is added.
            volume.settings = Galeshi::PostProcessVolumeComponent{}.settings;
        }
    }



    void GaleshiEditorApp::DrawWaterSettings(Galeshi::WaterComponent& water, entt::entity entity) {
        ImGui::Checkbox("Enabled##water", &water.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Visible in Editor", &water.visibleInEditor);
        ImGui::DragFloat("Width", &water.width, 0.1f, 0.1f, 10000.0f);
        ImGui::DragFloat("Depth", &water.depth, 0.1f, 0.1f, 10000.0f);
        {
            const char* bodyTypes[] = {"Sea", "River", "Ocean", "Lake"};
            int bodyType = static_cast<int>(water.bodyType);
            if (ImGui::Combo("Body Type", &bodyType, bodyTypes, 4)) {
                PushHistory();
                water.bodyType = static_cast<Galeshi::WaterBodyType>(
                    std::clamp(bodyType, 0, 3));
            }
        }
        ImGui::ColorEdit3("Color", glm::value_ptr(water.shallowColor));
        ImGui::DragFloat("Transparency", &water.transparency,
                         0.01f, 0.0f, 1.0f);
        ImGui::TextDisabled("The water surface is centered on the entity and "
                            "uses its Y position.");

        if (ImGui::TreeNode("Terrain Erosion")) {
            ImGui::Checkbox("Carve Landscape", &water.carveLandscape);
            ImGui::DragFloat("Basin Depth", &water.basinDepth,
                             0.05f, 0.0f, 100.0f);
            ImGui::DragFloat("Bank Width", &water.bankWidth,
                             0.05f, 0.05f, 100.0f);
            ImGui::DragFloat("Bank Erosion", &water.bankErosion,
                             0.01f, 0.0f, 2.0f);
            if (ImGui::Button("Apply Terrain Erosion")) {
                PushHistory();
                ApplyWaterErosion(entity);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Carves a basin under the water, builds an eroded bank, "
                    "rebuilds the landscape mesh and regenerates vegetation.");
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Shore Material")) {
            if (ImGui::DragFloat("Detail Size (m)",
                                 &water.shoreDetailSize,
                                 0.05f, 0.05f, 100.0f)) {
                ApplyWaterShoreSettings(entity);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "World-space size of one sand/gravel texture tile. "
                    "Smaller values add finer grain.");
            }
            if (ImGui::DragFloat("Detail Strength",
                                 &water.shoreDetailStrength,
                                 0.01f, 0.0f, 1.0f)) {
                ApplyWaterShoreSettings(entity);
            }
            if (ImGui::DragFloat("Detail Octave",
                                 &water.shoreDetailOctave,
                                 0.1f, 1.0f, 16.0f)) {
                ApplyWaterShoreSettings(entity);
            }
            if (ImGui::DragFloat("Normal Strength",
                                 &water.shoreNormalStrength,
                                 0.01f, 0.0f, 4.0f)) {
                ApplyWaterShoreSettings(entity);
            }
            ImGui::TextDisabled(
                "Applies to carved banks and the underwater bed.");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Underwater Terrain")) {
            bool underwaterTerrain = water.underwaterTerrain;
            if (ImGui::Checkbox("Create Underwater Terrain",
                                &underwaterTerrain)) {
                PushHistory();
                water.underwaterTerrain = underwaterTerrain;
                EnsureUnderwaterTerrain(entity);
                RebuildWaterVegetationMasks();
                ApplyWaterShoreSettings(entity);
            }
            ImGui::DragInt("Grass Count", &water.underwaterGrassCount,
                           1.0f, 0, 20000);
            ImGui::ColorEdit3("Grass Color",
                              glm::value_ptr(water.underwaterGrassColor));
            if (ImGui::Button("Rebuild Underwater Terrain")) {
                PushHistory();
                EnsureUnderwaterTerrain(entity);
                RebuildWaterVegetationMasks();
                ApplyWaterShoreSettings(entity);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Creates a child terrain at the basin bed with its own "
                    "vegetation instances. Land vegetation is excluded from "
                    "the submerged footprint.");
            }
            ImGui::TextDisabled(
                "The bed is a child entity and owns its own grass set.");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Underwater Bubbles")) {
            std::vector<std::pair<std::string, entt::entity>> characters;
            for (auto e : m_scene.Registry().view<
                     Galeshi::SkeletalComponent>()) {
                std::string name = "Character";
                if (const auto* nc = m_scene.Registry().try_get<
                        Galeshi::NameComponent>(e)) {
                    name = nc->name;
                }
                characters.emplace_back(std::move(name), e);
            }
            std::sort(characters.begin(), characters.end(),
                      [](const auto& a, const auto& b) {
                          return a.first < b.first;
                      });

            const std::string currentCharacter =
                water.bubbleCharacter.empty()
                    ? std::string("Auto (Player)")
                    : water.bubbleCharacter;
            if (ImGui::BeginCombo("Character##bubble",
                                  currentCharacter.c_str())) {
                if (ImGui::Selectable(
                        "Auto (Player)",
                        water.bubbleCharacter.empty())) {
                    PushHistory();
                    water.bubbleCharacter.clear();
                }
                for (const auto& entry : characters) {
                    const bool selected =
                        water.bubbleCharacter == entry.first;
                    if (ImGui::Selectable(entry.first.c_str(), selected)) {
                        PushHistory();
                        water.bubbleCharacter = entry.first;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Skeletal character used for the underwater bubble "
                    "emitter.");
            }

            entt::entity character = entt::null;
            for (const auto& entry : characters) {
                if (water.bubbleCharacter == entry.first) {
                    character = entry.second;
                    break;
                }
            }
            if (character == entt::null) {
                for (const auto& entry : characters) {
                    if (entry.first == "Player") {
                        character = entry.second;
                        break;
                    }
                }
            }
            if (character == entt::null && !characters.empty()) {
                character = characters.front().second;
            }

            std::vector<std::string> bones;
            if (character != entt::null && m_assets) {
                const auto& skeleton =
                    m_scene.Registry().get<Galeshi::SkeletalComponent>(
                        character);
                if (!skeleton.sourcePath.empty()) {
                    auto model = m_assets->LoadModel(skeleton.sourcePath);
                    bones.reserve(model.boneMap.size());
                    for (const auto& [name, index] : model.boneMap) {
                        if (index >= 0 && index < 255) {
                            bones.push_back(name);
                        }
                    }
                    std::sort(bones.begin(), bones.end());
                }
            }

            const std::string currentBone =
                water.bubbleBone.empty()
                    ? std::string("(Body Center)")
                    : water.bubbleBone;
            if (ImGui::BeginCombo("Bubble Bone##bubble",
                                  currentBone.c_str())) {
                if (ImGui::Selectable(
                        "(Body Center)", water.bubbleBone.empty())) {
                    PushHistory();
                    water.bubbleBone.clear();
                }
                for (const std::string& bone : bones) {
                    const bool selected = water.bubbleBone == bone;
                    if (ImGui::Selectable(bone.c_str(), selected)) {
                        PushHistory();
                        water.bubbleBone = bone;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The selected bone is the bubble spawn location while "
                    "the player is underwater.");
            }
            if (bones.empty()) {
                ImGui::TextDisabled(
                    "Select a skeletal character to list armature bones.");
            }
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Text("Presets");
        if (ImGui::Button("Calm Lake")) {
            PushHistory();
            water.bodyType = Galeshi::WaterBodyType::Lake;
            water.waveHeight = 0.12f;
            water.waveScale = 1.4f;
            water.waveSpeed = 0.55f;
            water.choppiness = 0.15f;
            water.detailStrength = 0.20f;
            water.refractionStrength = 0.28f;
            water.reflectionStrength = 0.82f;
            water.clarity = 0.72f;
            water.absorption = 0.32f;
            water.foamDistance = 0.45f;
            water.foamIntensity = 0.35f;
            water.shallowColor = {0.10f, 0.46f, 0.48f};
            water.deepColor = {0.015f, 0.10f, 0.16f};
            water.transparency = 0.88f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Ocean")) {
            PushHistory();
            water.bodyType = Galeshi::WaterBodyType::Ocean;
            water.waveHeight = 0.48f;
            water.waveScale = 0.75f;
            water.waveSpeed = 1.25f;
            water.choppiness = 0.55f;
            water.detailStrength = 0.55f;
            water.refractionStrength = 0.34f;
            water.reflectionStrength = 0.92f;
            water.clarity = 0.60f;
            water.absorption = 0.70f;
            water.foamDistance = 0.85f;
            water.foamIntensity = 0.78f;
            water.shallowColor = {0.04f, 0.24f, 0.34f};
            water.deepColor = {0.004f, 0.025f, 0.085f};
            water.transparency = 0.76f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Tropical")) {
            PushHistory();
            water.bodyType = Galeshi::WaterBodyType::Sea;
            water.waveHeight = 0.22f;
            water.waveScale = 1.15f;
            water.waveSpeed = 0.85f;
            water.choppiness = 0.28f;
            water.detailStrength = 0.38f;
            water.refractionStrength = 0.42f;
            water.reflectionStrength = 0.86f;
            water.clarity = 0.76f;
            water.absorption = 0.36f;
            water.foamDistance = 0.65f;
            water.foamIntensity = 0.62f;
            water.shallowColor = {0.03f, 0.62f, 0.58f};
            water.deepColor = {0.005f, 0.10f, 0.22f};
            water.transparency = 0.84f;
        }

        if (ImGui::TreeNode("Waves")) {
            ImGui::DragFloat("Height", &water.waveHeight,
                             0.005f, 0.0f, 5.0f);
            ImGui::DragFloat("Scale", &water.waveScale,
                             0.01f, 0.05f, 20.0f);
            ImGui::DragFloat("Speed", &water.waveSpeed,
                             0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Choppiness", &water.choppiness,
                             0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Detail Strength", &water.detailStrength,
                             0.01f, 0.0f, 2.0f);
            ImGui::DragFloat2("Flow Direction", glm::value_ptr(water.flowDirection),
                              0.01f, -10.0f, 10.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Optics")) {
            ImGui::Checkbox("Planar Reflections", &water.planarReflections);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Render a mirrored opaque scene for the nearest\n"
                                  "water plane. Other planes use the environment map.");
            }
            ImGui::DragFloat("Refraction", &water.refractionStrength,
                             0.005f, 0.0f, 2.0f);
            ImGui::DragFloat("Reflection", &water.reflectionStrength,
                             0.005f, 0.0f, 2.0f);
            ImGui::DragFloat("Fresnel Power", &water.fresnelPower,
                             0.05f, 1.0f, 20.0f);
            ImGui::DragFloat("Depth Fade", &water.depthFade,
                             0.05f, 0.05f, 100.0f);
            ImGui::DragFloat("Absorption", &water.absorption,
                             0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Clarity", &water.clarity,
                             0.01f, 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Foam & Sparkle")) {
            ImGui::DragFloat("Shore Distance", &water.foamDistance,
                             0.01f, 0.01f, 20.0f);
            ImGui::DragFloat("Foam Intensity", &water.foamIntensity,
                             0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Sparkle", &water.sparkle,
                             0.01f, 0.0f, 2.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Colors")) {
            ImGui::ColorEdit3("Deep", glm::value_ptr(water.deepColor));
            ImGui::ColorEdit3("Foam", glm::value_ptr(water.foamColor));
            ImGui::TreePop();
        }

        if (ImGui::Button("Reset Water Settings")) {
            PushHistory();
            water = Galeshi::WaterComponent{};
        }
    }



    void GaleshiEditorApp::DrawGroomSettings(Galeshi::GroomComponent& groom) {
        ImGui::Checkbox("Enabled##groom", &groom.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Simulate", &groom.simulate);
        if (groom.runtime) {
            ImGui::SameLine();
            ImGui::TextDisabled("%zu guides / %zu strands",
                                groom.runtime->guides.size(),
                                groom.runtime->strands.size());
        }

        char targetBuffer[128];
        std::snprintf(targetBuffer, sizeof(targetBuffer), "%s",
                      groom.targetMeshName.c_str());
        if (ImGui::InputText("Target Mesh", targetBuffer, sizeof(targetBuffer))) {
            groom.targetMeshName = targetBuffer;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Leave empty to use this entity's mesh, or the "
                              "highest descendant mesh of a character.");
        }

        if (ImGui::TreeNodeEx("Transform",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Position", glm::value_ptr(groom.position),
                              0.01f);
            glm::vec3 rotation =
                glm::degrees(glm::eulerAngles(glm::normalize(groom.rotation)));
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotation),
                                  1.0f)) {
                groom.rotation =
                    glm::normalize(glm::quat(glm::radians(rotation)));
            }
            ImGui::DragFloat3("Scale", glm::value_ptr(groom.scale),
                              0.01f, 0.001f, 100.0f);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Text("Presets");
        if (ImGui::Button("Short Fur")) {
            PushHistory();
            groom.guideCount = 160;
            groom.strandsPerGuide = 10;
            groom.segments = 6;
            groom.length = 0.075f;
            groom.width = 0.0035f;
            groom.clumpRadius = 0.008f;
            groom.clumping = 0.78f;
            groom.frizz = 0.025f;
            groom.curl = 0.018f;
            groom.volume = 0.18f;
            groom.scalpStart = 0.56f;
            groom.gravity = 0.18f;
            groom.stiffness = 0.85f;
            groom.damping = 0.32f;
            groom.windStrength = 0.08f;
            groom.rootColor = {0.09f, 0.05f, 0.03f};
            groom.tipColor = {0.25f, 0.14f, 0.075f};
            groom.opacity = 0.95f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Bob")) {
            PushHistory();
            groom.guideCount = 120;
            groom.strandsPerGuide = 14;
            groom.segments = 8;
            groom.length = 0.24f;
            groom.width = 0.0075f;
            groom.clumpRadius = 0.014f;
            groom.clumping = 0.68f;
            groom.frizz = 0.045f;
            groom.curl = 0.035f;
            groom.volume = 0.30f;
            groom.scalpStart = 0.60f;
            groom.gravity = 0.30f;
            groom.stiffness = 0.72f;
            groom.damping = 0.26f;
            groom.windStrength = 0.14f;
            groom.rootColor = {0.095f, 0.052f, 0.032f};
            groom.tipColor = {0.31f, 0.17f, 0.09f};
            groom.opacity = 0.90f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Long")) {
            PushHistory();
            groom.guideCount = 96;
            groom.strandsPerGuide = 12;
            groom.segments = 10;
            groom.length = 0.48f;
            groom.width = 0.008f;
            groom.clumpRadius = 0.018f;
            groom.clumping = 0.52f;
            groom.frizz = 0.065f;
            groom.curl = 0.055f;
            groom.volume = 0.34f;
            groom.scalpStart = 0.58f;
            groom.gravity = 0.42f;
            groom.stiffness = 0.58f;
            groom.damping = 0.22f;
            groom.windStrength = 0.22f;
            groom.rootColor = {0.08f, 0.045f, 0.03f};
            groom.tipColor = {0.36f, 0.20f, 0.11f};
            groom.opacity = 0.88f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Wet Look")) {
            PushHistory();
            groom.guideCount = 110;
            groom.strandsPerGuide = 14;
            groom.segments = 8;
            groom.length = 0.28f;
            groom.width = 0.0045f;
            groom.clumpRadius = 0.010f;
            groom.clumping = 0.90f;
            groom.frizz = 0.012f;
            groom.curl = 0.020f;
            groom.volume = 0.20f;
            groom.scalpStart = 0.58f;
            groom.gravity = 0.50f;
            groom.stiffness = 0.80f;
            groom.damping = 0.36f;
            groom.windStrength = 0.08f;
            groom.specular = 0.95f;
            groom.shininess = 80.0f;
            groom.ambient = 0.24f;
            groom.opacity = 0.82f;
            groom.rootColor = {0.055f, 0.03f, 0.022f};
            groom.tipColor = {0.19f, 0.10f, 0.06f};
        }

        if (ImGui::TreeNode("Groom")) {
            ImGui::DragInt("Guides", &groom.guideCount, 1.0f, 1, 4096);
            ImGui::DragInt("Strands / Guide", &groom.strandsPerGuide,
                           1.0f, 1, 64);
            ImGui::DragInt("Segments", &groom.segments, 0.2f, 2, 32);
            ImGui::DragFloat("Length", &groom.length, 0.002f, 0.001f, 5.0f);
            ImGui::DragFloat("Width", &groom.width, 0.0001f, 0.0001f, 0.1f);
            ImGui::DragFloat("Root Width", &groom.rootWidth, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Tip Width", &groom.tipWidth, 0.005f, 0.0f, 2.0f);
            ImGui::DragFloat("Clump Radius", &groom.clumpRadius,
                             0.0005f, 0.0f, 0.2f);
            ImGui::DragFloat("Clumping", &groom.clumping, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Frizz", &groom.frizz, 0.002f, 0.0f, 0.5f);
            ImGui::DragFloat("Curl", &groom.curl, 0.002f, 0.0f, 0.5f);
            ImGui::DragFloat("Volume", &groom.volume, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloatRange2("Scalp Band", &groom.scalpStart,
                                   &groom.scalpEnd, 0.005f, 0.0f, 1.0f);
            ImGui::DragFloat("Normal Threshold", &groom.normalThreshold,
                             0.01f, -1.0f, 1.0f);
            ImGui::DragFloat3("Groom Direction",
                              glm::value_ptr(groom.groomDirection), 0.01f);
            ImGui::InputScalar("Seed", ImGuiDataType_U32, &groom.seed);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Dynamics")) {
            ImGui::DragFloat("Gravity", &groom.gravity, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Stiffness", &groom.stiffness,
                             0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Damping", &groom.damping, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Wind Strength", &groom.windStrength,
                             0.005f, 0.0f, 2.0f);
            ImGui::DragFloat3("Wind Direction",
                              glm::value_ptr(groom.windDirection), 0.01f);
            ImGui::DragFloat("Wind Frequency", &groom.windFrequency,
                             0.01f, 0.0f, 10.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Shading")) {
            ImGui::ColorEdit3("Root Color", glm::value_ptr(groom.rootColor));
            ImGui::ColorEdit3("Tip Color", glm::value_ptr(groom.tipColor));
            ImGui::DragFloat("Specular", &groom.specular, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Shininess", &groom.shininess,
                             0.5f, 1.0f, 256.0f);
            ImGui::DragFloat("Ambient", &groom.ambient, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Opacity", &groom.opacity, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Random Tint", &groom.randomTint,
                             0.01f, 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::Button("Rebuild Groom")) {
            groom.runtime.reset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Groom Settings")) {
            PushHistory();
            auto target = groom.targetMeshName;
            groom = Galeshi::GroomComponent{};
            groom.targetMeshName = target;
        }
    }

