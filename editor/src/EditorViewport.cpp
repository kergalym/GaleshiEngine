#include "GaleshiEditorApp.hpp"

namespace {

struct DirectionalLightSelection {
    entt::entity primary = entt::null;
    entt::entity secondary = entt::null;
};

// A scene can author a sun plus a weaker fill. The renderer used to take the
// first registry entry, which made the fill cast the only shadow and left the
// sun ignored. Pick the brightest light as the primary and keep the next one
// as an unshadowed fill.
DirectionalLightSelection SelectDirectionalLights(Galeshi::Scene& scene) {
    auto view = scene.Registry()
        .view<Galeshi::DirectionalLightComponent>();
    DirectionalLightSelection result;
    float bestScore = -1.0f;
    float secondScore = -1.0f;
    for (auto e : view) {
        const auto& dl = view.get<Galeshi::DirectionalLightComponent>(e);
        const float luminance =
            0.2126f * dl.color.r + 0.7152f * dl.color.g +
            0.0722f * dl.color.b;
        const float score = std::max(dl.intensity, 0.0f) * luminance;
        if (score > bestScore) {
            result.secondary = result.primary;
            secondScore = bestScore;
            result.primary = e;
            bestScore = score;
        } else if (score > secondScore) {
            result.secondary = e;
            secondScore = score;
        }
    }
    return result;
}

bool RayTriangleIntersect(const glm::vec3& origin, const glm::vec3& direction,
                          const glm::vec3& a, const glm::vec3& b,
                          const glm::vec3& c, float& outT) {
    constexpr float kEpsilon = 1.0e-7f;
    const glm::vec3 edge1 = b - a;
    const glm::vec3 edge2 = c - a;
    const glm::vec3 p = glm::cross(direction, edge2);
    const float determinant = glm::dot(edge1, p);
    if (std::abs(determinant) < kEpsilon) return false;

    const float inverse = 1.0f / determinant;
    const glm::vec3 t = origin - a;
    const float u = glm::dot(t, p) * inverse;
    if (u < 0.0f || u > 1.0f) return false;

    const glm::vec3 q = glm::cross(t, edge1);
    const float v = glm::dot(direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f) return false;

    const float distance = glm::dot(edge2, q) * inverse;
    if (distance <= kEpsilon) return false;
    outT = distance;
    return true;
}

bool RaycastMeshLocal(const Galeshi::Mesh& mesh, const glm::vec3& origin,
                      const glm::vec3& direction, float& outDistance,
                      glm::vec3& outHit) {
    const auto& vertices = mesh.Vertices();
    const auto& indices = mesh.Indices();
    float best = std::numeric_limits<float>::max();
    bool hit = false;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() ||
            i2 >= vertices.size()) {
            continue;
        }
        float distance = 0.0f;
        if (RayTriangleIntersect(origin, direction,
                                 vertices[i0].position,
                                 vertices[i1].position,
                                 vertices[i2].position, distance) &&
            distance < best) {
            best = distance;
            hit = true;
        }
    }
    if (!hit) return false;
    outDistance = best;
    outHit = origin + direction * best;
    return true;
}

} // namespace


    void GaleshiEditorApp::EnsurePlayerCameraPreviewResources() {
        if (bgfx::isValid(m_playerCameraPreviewFbo) &&
            bgfx::isValid(m_playerCameraPreviewOutputFbo) &&
            bgfx::isValid(m_playerCameraPreviewColor) &&
            bgfx::isValid(m_playerCameraPreviewOutput)) {
            return;
        }

        ShutdownPlayerCameraPreview();
        const uint64_t rtFlags =
            BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

        m_playerCameraPreviewColor = bgfx::createTexture2D(
            m_playerCameraPreviewW, m_playerCameraPreviewH, false, 1,
            bgfx::TextureFormat::RGBA16F, rtFlags);
        m_playerCameraPreviewDepth = bgfx::createTexture2D(
            m_playerCameraPreviewW, m_playerCameraPreviewH, false, 1,
            bgfx::TextureFormat::D32F, rtFlags);
        if (bgfx::isValid(m_playerCameraPreviewColor) &&
            bgfx::isValid(m_playerCameraPreviewDepth)) {
            bgfx::TextureHandle attachments[] = {
                m_playerCameraPreviewColor, m_playerCameraPreviewDepth
            };
            m_playerCameraPreviewFbo = bgfx::createFrameBuffer(
                2, attachments, false);
        }

        m_playerCameraPreviewOutput = bgfx::createTexture2D(
            m_playerCameraPreviewW, m_playerCameraPreviewH, false, 1,
            bgfx::TextureFormat::RGBA8, rtFlags);
        if (bgfx::isValid(m_playerCameraPreviewOutput)) {
            m_playerCameraPreviewOutputFbo = bgfx::createFrameBuffer(
                1, &m_playerCameraPreviewOutput, false);
        }

        if (!bgfx::isValid(m_playerCameraPreviewFbo) ||
            !bgfx::isValid(m_playerCameraPreviewOutputFbo)) {
            ShutdownPlayerCameraPreview();
        }
    }


    void GaleshiEditorApp::ShutdownPlayerCameraPreview() {
        if (bgfx::isValid(m_playerCameraPreviewFbo)) {
            bgfx::destroy(m_playerCameraPreviewFbo);
            m_playerCameraPreviewFbo = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_playerCameraPreviewOutputFbo)) {
            bgfx::destroy(m_playerCameraPreviewOutputFbo);
            m_playerCameraPreviewOutputFbo = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_playerCameraPreviewColor)) {
            bgfx::destroy(m_playerCameraPreviewColor);
            m_playerCameraPreviewColor = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_playerCameraPreviewDepth)) {
            bgfx::destroy(m_playerCameraPreviewDepth);
            m_playerCameraPreviewDepth = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_playerCameraPreviewOutput)) {
            bgfx::destroy(m_playerCameraPreviewOutput);
            m_playerCameraPreviewOutput = BGFX_INVALID_HANDLE;
        }
    }


    bool GaleshiEditorApp::RenderPlayerCameraPreview(
        entt::entity cameraEntity, bool renderThisFrame) {
        if (!m_render || !m_scene.Registry().valid(cameraEntity)) return false;
        const auto* camera =
            m_scene.Registry().try_get<Galeshi::CameraComponent>(cameraEntity);
        if (!camera) return false;

        EnsurePlayerCameraPreviewResources();
        if (!bgfx::isValid(m_playerCameraPreviewFbo) ||
            !bgfx::isValid(m_playerCameraPreviewOutputFbo)) {
            return false;
        }

        if (renderThisFrame) {
            const glm::mat4 world = EntityWorldMatrix(cameraEntity);
            glm::vec3 eye = glm::vec3(world[3]);
            glm::vec3 forward = glm::vec3(
                world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
            glm::vec3 up = glm::vec3(
                world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
            if (glm::dot(forward, forward) < 1e-6f ||
                glm::dot(up, up) < 1e-6f) {
                forward = {0.0f, 0.0f, -1.0f};
                up = {0.0f, 1.0f, 0.0f};
            } else {
                forward = glm::normalize(forward);
                up = glm::normalize(up);
            }
            if (std::abs(glm::dot(forward, up)) > 0.999f) {
                up = std::abs(forward.y) > 0.9f
                    ? glm::vec3(0.0f, 0.0f, 1.0f)
                    : glm::vec3(0.0f, 1.0f, 0.0f);
            }

            Galeshi::Camera previewCamera;
            previewCamera.SetPerspective(
                glm::radians(glm::clamp(camera->fovY, 1.0f, 179.0f)),
                static_cast<float>(m_playerCameraPreviewW) /
                    static_cast<float>(m_playerCameraPreviewH),
                std::max(camera->nearZ, 1e-4f),
                std::max(camera->farZ, camera->nearZ + 0.001f));
            previewCamera.SetView(eye, eye + forward, up);

            // The preview is deliberately forward-rendered: it must not
            // touch the main deferred targets or the post-process chain.
            const bool oldDeferred = m_render->DeferredEnabled();
            const bool oldOcclusion = m_render->OcclusionCullingEnabled();
            m_render->SetDeferredEnabled(false);
            m_render->SetOcclusionCullingEnabled(false);
            bgfx::setViewFrameBuffer(m_playerCameraPreviewView,
                                     m_playerCameraPreviewFbo);
            m_render->Render(m_scene, previewCamera,
                             m_playerCameraPreviewView,
                             m_playerCameraPreviewW, m_playerCameraPreviewH);
            m_render->SetDeferredEnabled(oldDeferred);
            m_render->SetOcclusionCullingEnabled(oldOcclusion);

            const Galeshi::PostProcessSettings pp =
                ResolvePostProcessSettings();
            m_postProcess.RenderTonemap(
                m_playerCameraPreviewColor, m_playerCameraPreviewOutputFbo,
                m_playerCameraPreviewTonemapView,
                m_playerCameraPreviewW, m_playerCameraPreviewH,
                std::exp2(pp.exposureEv) * pp.exposure *
                    pp.colorExposure,
                pp.contrast, pp.saturation,
                pp.colorContrast, pp.colorSaturation,
                pp.colorGain, pp.colorGamma, pp.colorOffset,
                pp.grainStrength, pp.grainScale,
                static_cast<float>(m_time),
                pp.autoExposureEnabled,
                m_postProcess.AutoExposureTexture(),
                pp.colorTemperature, pp.colorTint,
                pp.gradeShadows, pp.gradeMidtones,
                pp.gradeHighlights);
        }

        return bgfx::isValid(m_playerCameraPreviewOutput);
    }


    void GaleshiEditorApp::DrawCollisionShapeGizmos() {
        if (!m_debug) return;
        Galeshi::Entity selected = m_editor.Selected(&m_scene);
        auto colliderView =
            m_scene.Registry().view<Galeshi::ColliderComponent>();
        for (auto e : colliderView) {
            const auto& collider =
                colliderView.get<Galeshi::ColliderComponent>(e);
            if (!collider.visibleInEditor) continue;

            const bool isSelected = selected && selected.Handle() == e;
            const glm::vec4 color = isSelected
                ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)
                : (collider.isTrigger
                       ? glm::vec4(0.30f, 0.60f, 1.00f, 0.90f)
                       : glm::vec4(0.20f, 1.00f, 0.40f, 0.75f));

            const glm::mat4 world = EntityWorldMatrix(e);
            auto toWorld = [&world](const glm::vec3& p) {
                return glm::vec3(world * glm::vec4(p, 1.0f));
            };
            const glm::vec3 c = collider.offset;

            switch (collider.shapeType) {
                case Galeshi::ColliderShapeType::Box: {
                    const glm::vec3 h = collider.halfExtents;
                    glm::vec3 p[8] = {
                        toWorld(c + glm::vec3(-h.x, -h.y, -h.z)),
                        toWorld(c + glm::vec3( h.x, -h.y, -h.z)),
                        toWorld(c + glm::vec3( h.x, -h.y,  h.z)),
                        toWorld(c + glm::vec3(-h.x, -h.y,  h.z)),
                        toWorld(c + glm::vec3(-h.x,  h.y, -h.z)),
                        toWorld(c + glm::vec3( h.x,  h.y, -h.z)),
                        toWorld(c + glm::vec3( h.x,  h.y,  h.z)),
                        toWorld(c + glm::vec3(-h.x,  h.y,  h.z)),
                    };
                    auto edge = [&](int a, int b) {
                        m_debug->AddLine(p[a], p[b], color);
                    };
                    edge(0, 1); edge(1, 2); edge(2, 3); edge(3, 0);
                    edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 4);
                    edge(0, 4); edge(1, 5); edge(2, 6); edge(3, 7);
                    break;
                }
                case Galeshi::ColliderShapeType::Sphere: {
                    const int segments = 32;
                    const float r = collider.radius;
                    for (int i = 0; i < segments; ++i) {
                        const float a1 = static_cast<float>(i) / segments * 6.2831853f;
                        const float a2 = static_cast<float>(i + 1) / segments * 6.2831853f;
                        m_debug->AddLine(
                            toWorld(c + glm::vec3(std::cos(a1) * r, std::sin(a1) * r, 0.0f)),
                            toWorld(c + glm::vec3(std::cos(a2) * r, std::sin(a2) * r, 0.0f)),
                            color);
                        m_debug->AddLine(
                            toWorld(c + glm::vec3(std::cos(a1) * r, 0.0f, std::sin(a1) * r)),
                            toWorld(c + glm::vec3(std::cos(a2) * r, 0.0f, std::sin(a2) * r)),
                            color);
                        m_debug->AddLine(
                            toWorld(c + glm::vec3(0.0f, std::cos(a1) * r, std::sin(a1) * r)),
                            toWorld(c + glm::vec3(0.0f, std::cos(a2) * r, std::sin(a2) * r)),
                            color);
                    }
                    break;
                }
                case Galeshi::ColliderShapeType::Capsule: {
                    const int segments = 24;
                    const float r = collider.radius;
                    const float hh = collider.halfHeight;
                    for (int ring = 0; ring < 2; ++ring) {
                        const float y = c.y + (ring == 0 ? -hh : hh);
                        for (int i = 0; i < segments; ++i) {
                            const float a1 = static_cast<float>(i) / segments * 6.2831853f;
                            const float a2 = static_cast<float>(i + 1) / segments * 6.2831853f;
                            m_debug->AddLine(
                                toWorld({c.x + std::cos(a1) * r, y, c.z + std::sin(a1) * r}),
                                toWorld({c.x + std::cos(a2) * r, y, c.z + std::sin(a2) * r}),
                                color);
                        }
                    }
                    for (int i = 0; i < 4; ++i) {
                        const float a = static_cast<float>(i) * 1.5707963f;
                        m_debug->AddLine(
                            toWorld({c.x + std::cos(a) * r, c.y - hh, c.z + std::sin(a) * r}),
                            toWorld({c.x + std::cos(a) * r, c.y + hh, c.z + std::sin(a) * r}),
                            color);
                    }
                    break;
                }
            }
        }

        // Painted cloth vertices are shown as small green crosses so the
        // brush result is visible before rebuilding the soft body.
        const Galeshi::Entity clothEntity = FindClothMeshTarget(selected);
        if (clothEntity && clothEntity.HasComponent<Galeshi::SoftBodyComponent>() &&
            clothEntity.HasComponent<Galeshi::MeshComponent>()) {
            const auto& cloth =
                clothEntity.GetComponent<Galeshi::SoftBodyComponent>();
            const auto& meshComponent =
                clothEntity.GetComponent<Galeshi::MeshComponent>();
            if (cloth.meshCloth && meshComponent.mesh) {
                const glm::mat4 world = EntityWorldMatrix(clothEntity.Handle());
                const auto& vertices = meshComponent.mesh->Vertices();
                const size_t count =
                    std::min(vertices.size(), cloth.paintWeights.size());
                for (size_t i = 0; i < count; ++i) {
                    if (cloth.paintWeights[i] <= 0.001f) continue;
                    const glm::vec3 p =
                        glm::vec3(world * glm::vec4(vertices[i].position, 1.0f));
                    const float size = 0.03f;
                    m_debug->AddLine(p - glm::vec3(size, 0, 0),
                                     p + glm::vec3(size, 0, 0),
                                     glm::vec4(0.3f, 1.0f, 0.4f, 0.9f));
                    m_debug->AddLine(p - glm::vec3(0, size, 0),
                                     p + glm::vec3(0, size, 0),
                                     glm::vec4(0.3f, 1.0f, 0.4f, 0.9f));
                }
            }
        }
    }



    // -------------------------------------------------------------------
    // Viewport & Picking & Sculpting
    // -------------------------------------------------------------------
    void GaleshiEditorApp::SetViewportInputFocus(bool focused) {
        m_viewportInputFocused = focused;
        if (m_isGameMode) return;
        if (focused) {
            ImGui::SetWindowFocus("Viewport");
        } else {
            ImGui::SetWindowFocus(nullptr);
        }
    }


    void GaleshiEditorApp::SetViewportViewMode(ViewportViewMode mode) {
        if (mode == m_viewportViewMode) return;

        // Keep the renderer setup from before the diagnostic view. While
        // F1/F2 are active their pass restores this snapshot after every
        // frame; F3 restores it as the active state again.
        if (m_viewportViewMode == ViewportViewMode::Lit &&
            mode != ViewportViewMode::Lit && m_render) {
            // The renderer mode can be left stale by a previous diagnostic
            // pass; save the actual normal state, not that stale mode.
            m_render->SetViewMode(Galeshi::RenderSystem::ViewMode::Lit);
            m_savedRenderState = m_render->SaveState();
            m_viewModeStateSaved = true;
        }
        m_viewportViewMode = mode;

        if (mode == ViewportViewMode::Lit && m_viewModeStateSaved) {
            if (m_render) {
                m_render->RestoreState(m_savedRenderState);
                // RestoreState is the source of truth for the normal pass.
                // Keep the renderer's view mode explicitly in sync so a
                // diagnostic pass can never survive F3.
                m_render->SetViewMode(Galeshi::RenderSystem::ViewMode::Lit);
            }
            m_viewModeStateSaved = false;
            m_postProcess.ResetTaaHistory();
            m_lastViewportTexture = BGFX_INVALID_HANDLE;
        }
    }


    void GaleshiEditorApp::DrawViewport() {
        spdlog::trace("[Main] DrawViewport");
        m_uiOverlayHovered = false;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (m_isGameMode) {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
            ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size);
            ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
            m_viewportHovered = true;
        } else {
            ImGui::Begin("Viewport");
            m_viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
            if (m_editor.isPlaying && ImGui::GetIO().KeyShift &&
                ImGui::IsKeyPressed(ImGuiKey_F1)) {
                SetViewportInputFocus(false);
            }
        }
        if (!m_isGameMode && !ImGui::GetIO().WantTextInput) {
            // Keep view-mode keys global to the editor viewport. Requiring
            // hover/focus made F3 unreliable after the viewport had been
            // clicked or the mouse moved away. Shift+F1 remains the focus
            // release shortcut, so F1 only switches when Shift is not held.
            // F1 = Wireframe, F2 = Unlit, F3 = Lit (the default).
            if (!ImGui::GetIO().KeyShift &&
                ImGui::IsKeyPressed(ImGuiKey_F1)) {
                SetViewportViewMode(ViewportViewMode::Wireframe);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
                SetViewportViewMode(ViewportViewMode::Unlit);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
                SetViewportViewMode(ViewportViewMode::Lit);
            }
        }
        // Guard the normal path against a stale diagnostic view mode left in
        // the renderer by an interrupted transition.
        if (m_viewportViewMode == ViewportViewMode::Lit && m_render) {
            m_render->SetViewMode(Galeshi::RenderSystem::ViewMode::Lit);
        }
        ImGui::PopStyleVar();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const uint16_t w = static_cast<uint16_t>(avail.x);
        const uint16_t h = static_cast<uint16_t>(avail.y);

        // The UI/viewport stays at native size; the complete scene and
        // post-process chain render at this scaled internal resolution.
        const float activeRenderScale = std::clamp(
            m_dynamicResolution ? m_dynamicScale : m_renderScale, 0.5f, 1.0f);
        const uint16_t renderW = static_cast<uint16_t>(std::max(
            2, static_cast<int>(std::lround(static_cast<float>(w) * activeRenderScale))));
        const uint16_t renderH = static_cast<uint16_t>(std::max(
            2, static_cast<int>(std::lround(static_cast<float>(h) * activeRenderScale))));

        // Performance mode only removes expensive optional GPU work. The
        // viewport, camera and scene keep rendering and simulating.
        const bool skipHeavyGpu = m_performanceMode && !m_isGameMode;

        if (w > 0 && h > 0) {
            if (!m_framebuffer || m_framebuffer->Width() != renderW ||
                m_framebuffer->Height() != renderH) {
                // Recreating an FBO that bgfx still considers bound can make it
                // skip the image-layout transition on the next bind (the new
                // handle may reuse the old index). Touch the backbuffer first
                // so the next view always binds a different framebuffer.
                bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
                bgfx::setViewRect(0, 0, 0, 1, 1);
                bgfx::setViewClear(0, BGFX_CLEAR_NONE);
                bgfx::touch(0);

                if (!m_framebuffer) m_framebuffer = std::make_unique<Galeshi::Framebuffer>();
                m_framebuffer->Resize(renderW, renderH);
            }
        }

        m_viewportW = w;
        m_viewportH = h;

        float postFxAccumMs = 0.0f;
        ProfileClock::time_point postFxSegmentStart{};
        bool postFxProfiling = false;

        if (m_framebuffer && m_framebuffer->Valid()) {

            const auto pssmStart = ProfileClock::now();
            // --- PSSM SHADOW PASS ---
            const DirectionalLightSelection dirLights =
                SelectDirectionalLights(m_scene);
            glm::mat4 lightMtx0, lightMtx1, lightMtx2;
            float cascadeSplits[4] = {15.0f, 40.0f, 100.0f, 0.0f}; // Fallback
            float cascadeNormalOffsets[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float cascadeDepthBias[4] = {0.0f, 0.0f, 0.0f, 0.0f};

            if (dirLights.primary != entt::null) {
                auto& dl = m_scene.Registry()
                    .get<Galeshi::DirectionalLightComponent>(dirLights.primary);
                glm::vec3 lightDir = glm::normalize(dl.direction);

                // 1. Calculate Cascade Splits (Practical Split Scheme).
                // Follow the adaptive camera clip range so shadows do not
                // disappear or become unstable at large orbit distances.
                float nearPlane = std::max(m_camera.NearPlane(), 0.05f);
                float farPlane = std::max(m_camera.FarPlane(), 1000.0f);
                float lambda = 0.5f;
                float splits[3];
                for (int i = 0; i < 3; ++i) {
                    float p = (i + 1) / 3.0f;
                    float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
                    float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
                    splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
                }
                cascadeSplits[0] = splits[0];
                cascadeSplits[1] = splits[1];
                cascadeSplits[2] = splits[2];

                // 2. Extract Frustum Corners
                glm::vec3 camPos = m_camera.Position();
                glm::mat4 invViewProj = glm::inverse(m_camera.Projection() * m_camera.View());
                glm::vec3 rays[4];
                for (int i = 0; i < 4; ++i) {
                    float x = (i == 0 || i == 3) ? -1.0f : 1.0f;
                    float y = (i < 2) ? -1.0f : 1.0f;
                    glm::vec4 pt = invViewProj * glm::vec4(x, y, 1.0f, 1.0f);
                    glm::vec3 farWorld = glm::vec3(pt) / pt.w;
                    rays[i] = glm::normalize(farWorld - camPos);
                }

                // 3. Render Cascades
                for (int i = 0; i < 3; ++i) {
                    float prevSplit = (i == 0) ? nearPlane : splits[i-1];
                    float currSplit = splits[i];

                    glm::vec3 corners[8];
                    for (int j = 0; j < 4; ++j) {
                        corners[j] = camPos + rays[j] * prevSplit;
                        corners[j+4] = camPos + rays[j] * currSplit;
                    }

                    glm::vec3 center = glm::vec3(0.0f);
                    for (int j = 0; j < 8; ++j) center += corners[j];
                    center /= 8.0f;

                    glm::vec3 eye = center - lightDir * (farPlane * 0.5f);
                    glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0,1,0))) > 0.99f) ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
                    glm::mat4 lightView = glm::lookAt(eye, center, up);

                    float minX = std::numeric_limits<float>::max();
                    float maxX = std::numeric_limits<float>::lowest();
                    float minY = std::numeric_limits<float>::max();
                    float maxY = std::numeric_limits<float>::lowest();
                    float minZ = std::numeric_limits<float>::max();
                    float maxZ = std::numeric_limits<float>::lowest();

                    for (int j = 0; j < 8; ++j) {
                        glm::vec4 trf = lightView * glm::vec4(corners[j], 1.0f);
                        minX = std::min(minX, trf.x); maxX = std::max(maxX, trf.x);
                        minY = std::min(minY, trf.y); maxY = std::max(maxY, trf.y);
                        minZ = std::min(minZ, trf.z); maxZ = std::max(maxZ, trf.z);
                    }

                    // Snap the light-space bounds to the shadow-map texel
                    // grid. Without this, small camera movements shift every
                    // cascade by a sub-texel amount and distant shadows appear
                    // to flicker/shimmer.
                    const float extentX = std::max(maxX - minX, 1e-3f);
                    const float extentY = std::max(maxY - minY, 1e-3f);
                    const float extent = std::max(extentX, extentY);
                    const float texelWorld = extent / 2048.0f;
                    const float centerX = std::floor(
                        ((minX + maxX) * 0.5f) / texelWorld) * texelWorld;
                    const float centerY = std::floor(
                        ((minY + maxY) * 0.5f) / texelWorld) * texelWorld;
                    minX = centerX - extent * 0.5f;
                    maxX = centerX + extent * 0.5f;
                    minY = centerY - extent * 0.5f;
                    maxY = centerY + extent * 0.5f;
                    const float depthRange =
                        std::max((-minZ + 10.0f) - (-maxZ - 10.0f), 1e-3f);
                    const float ndcDepthScale = bgfx::getCaps()->homogeneousDepth
                        ? 2.0f : 1.0f;
                    cascadeNormalOffsets[i] = texelWorld * m_pssmNormalOffsetTexels;
                    cascadeDepthBias[i] =
                        (texelWorld * m_pssmDepthBiasTexels / depthRange) *
                        ndcDepthScale;

                    glm::mat4 lightProj;
                    bx::mtxOrtho(glm::value_ptr(lightProj), minX, maxX, minY, maxY,
                                  -maxZ - 10.0f, -minZ + 10.0f, 0.0f,
                                  bgfx::getCaps()->homogeneousDepth,
                                  bx::Handedness::Right);

                    const glm::mat4 lightMtx = lightProj * lightView;
                    if (i == 0) lightMtx0 = lightMtx;
                    else if (i == 1) lightMtx1 = lightMtx;
                    else lightMtx2 = lightMtx;

                    m_render->RenderShadowMap(m_scene, lightMtx, 2 + i, m_shadowFbos[i]);
                }
            } else {
                // Fallback without a directional light.
                cascadeSplits[0] = 15.0f; cascadeSplits[1] = 40.0f; cascadeSplits[2] = 100.0f;
                for (int i = 0; i < 3; ++i) {
                    glm::mat4 lightView = glm::lookAt(glm::vec3(0,20,0), glm::vec3(0), glm::vec3(0,0,1));
                    glm::mat4 lightProj;
                    bx::mtxOrtho(glm::value_ptr(lightProj), -15.0f, 15.0f, -15.0f, 15.0f,
                                  0.1f, 50.0f, 0.0f,
                                  bgfx::getCaps()->homogeneousDepth,
                                  bx::Handedness::Right);
                    const float texelWorld = 30.0f / 2048.0f;
                    const float depthRange = 49.9f;
                    const float ndcDepthScale = bgfx::getCaps()->homogeneousDepth
                        ? 2.0f : 1.0f;
                    cascadeNormalOffsets[i] = texelWorld * m_pssmNormalOffsetTexels;
                    cascadeDepthBias[i] =
                        (texelWorld * m_pssmDepthBiasTexels / depthRange) *
                        ndcDepthScale;
                    const glm::mat4 lightMtx = lightProj * lightView;
                    if (i == 0) lightMtx0 = lightMtx;
                    else if (i == 1) lightMtx1 = lightMtx;
                    else lightMtx2 = lightMtx;
                    m_render->RenderShadowMap(m_scene, lightMtx, 2 + i, m_shadowFbos[i]);
                }
            }
            m_cpuProfile.pssmMs = ProfileElapsedMs(pssmStart, ProfileClock::now());

            // --- POINT LIGHT SHADOW PASS ---
            // shortcut: fs_lighting never samples the point cube map, so these six full scene passes per frame produce unused data; enable when point shadows are sampled.
            constexpr bool kPointShadowsSampled = false;
            const auto pointShadowStart = ProfileClock::now();
            auto plview = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::PointLightComponent>();
            if (kPointShadowsSampled && plview.begin() != plview.end()) {
                auto firstLight = *plview.begin();
                auto& tc = plview.get<Galeshi::TransformComponent>(firstLight);

                glm::vec3 lightPos = tc.position;

                glm::mat4 proj;
                bx::mtxProj(glm::value_ptr(proj), 90.0f, 1.0f, 0.1f, 50.0f, bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
                glm::mat4 views[6] = {
                    glm::lookAt(lightPos, lightPos + glm::vec3( 1, 0, 0), glm::vec3(0,-1,0)),
                    glm::lookAt(lightPos, lightPos + glm::vec3(-1, 0, 0), glm::vec3(0,-1,0)),
                    glm::lookAt(lightPos, lightPos + glm::vec3( 0, 1, 0), glm::vec3(0, 0,1)),
                    glm::lookAt(lightPos, lightPos + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
                    glm::lookAt(lightPos, lightPos + glm::vec3( 0, 0, 1), glm::vec3(0,-1,0)),
                    glm::lookAt(lightPos, lightPos + glm::vec3( 0, 0,-1), glm::vec3(0,-1,0))
                };

                for (int i = 0; i < 6; ++i) {
                    m_render->RenderPointShadowMap(m_scene, proj * views[i], lightPos, 5 + i, m_postProcess.PointShadowFbo(i));
                }
            }
            m_cpuProfile.pointShadowsMs = ProfileElapsedMs(pointShadowStart, ProfileClock::now());

            // --- SPOT LIGHT SHADOW PASS ---
            const auto spotShadowStart = ProfileClock::now();
            auto slview = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::SpotLightComponent>();
            int slIdx = 0;
            glm::mat4 spotLightVPs[4];
            for (auto e : slview) {
                if (slIdx >= 4) break;
                auto& tc = slview.get<Galeshi::TransformComponent>(e);
                auto& sl = slview.get<Galeshi::SpotLightComponent>(e);
                glm::vec3 lightPos = tc.position;
                glm::vec3 dir = glm::normalize(sl.direction);

                glm::mat4 lightView = glm::lookAt(lightPos, lightPos + dir, glm::vec3(0,1,0));
                glm::mat4 lightProj;
                float fov = glm::radians(sl.outerConeAngle * 2.0f);
                bx::mtxProj(glm::value_ptr(lightProj), fov, 1.0f, 0.1f, 50.0f, bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
                spotLightVPs[slIdx] = lightProj * lightView;

                m_render->RenderShadowMap(m_scene, spotLightVPs[slIdx], 11 + slIdx, m_postProcess.SpotShadowFbo(slIdx));
                slIdx++;
            }
            m_cpuProfile.spotShadowsMs = ProfileElapsedMs(spotShadowStart, ProfileClock::now());

            Galeshi::PostProcessSettings pp =
                ResolvePostProcessSettings();
            if (skipHeavyGpu) {
                // Keep the viewport live, but skip optional full-screen work
                // that dominates GPU time on integrated and mid-range cards.
                pp.ssgiEnabled = false;
                pp.ssrEnabled = false;
                pp.ssaoEnabled = false;
                pp.atmosphereEnabled = false;
                pp.godRaysEnabled = false;
                pp.fogVolumetric = false;
                pp.dofEnabled = false;
                pp.motionBlurEnabled = false;
            }

            // Do not carry stale history across a period where TAA was off.
            if (!pp.taaEnabled) m_postProcess.ResetTaaHistory();

            // Reprojection is done in the unjittered screen space. The
            // scene itself uses the jittered projection, but history samples
            // must land on the same pixel grid; otherwise edges acquire dark
            // one-pixel ghosts from the previous jitter offset.
            const glm::mat4 taaVP = m_camera.Projection() * m_camera.View();
            const glm::mat4 taaInvVP = glm::inverse(taaVP);

            // TAA needs a fresh sub-pixel projection offset every frame. The
            // offset is applied only to the scene camera; editor picking,
            // gizmos and auto-focus keep using the unjittered camera.
            Galeshi::Camera renderCamera = m_camera;
            if (m_render->DeferredEnabled() && pp.taaEnabled) {
                auto halton = [](uint32_t index, uint32_t base) {
                    float f = 1.0f;
                    float r = 0.0f;
                    while (index > 0) {
                        f /= static_cast<float>(base);
                        r += f * static_cast<float>(index % base);
                        index /= base;
                    }
                    return r;
                };
                const uint32_t jitterIndex =
                    static_cast<uint32_t>(m_renderFrameCounter % 8) + 1u;
                const float jx = halton(jitterIndex, 2u) - 0.5f;
                const float jy = halton(jitterIndex, 3u) - 0.5f;
                glm::mat4 jitteredProj = m_camera.Projection();
                jitteredProj[2][0] += (2.0f * jx) / static_cast<float>(w);
                jitteredProj[2][1] += (2.0f * jy) / static_cast<float>(h);
                renderCamera.SetProjection(jitteredProj);
            }

            // View 0 is a small depth-only occluder pass. Hardware queries
            // are read on the next frame, so this must run before the scene
            // view without stalling the GPU.
            const auto occlusionStart = ProfileClock::now();
            m_render->RenderOcclusion(m_scene, renderCamera, 0, renderW, renderH);
            m_cpuProfile.occlusionMs = ProfileElapsedMs(occlusionStart, ProfileClock::now());

            // Object motion vectors must use the same unjittered history
            // matrix that TAA uses, otherwise jitter is baked into velocity.
            const glm::mat4 motionPrevVP = m_postProcess.HasPrevViewProj()
                ? m_postProcess.GetPrevViewProj() : taaVP;
            m_render->SetPreviousViewProj(motionPrevVP);

            bgfx::setViewFrameBuffer(1, m_framebuffer->Handle());
            const auto gbufferStart = ProfileClock::now();
            m_render->Render(m_scene, renderCamera, 1, renderW, renderH);
            m_cpuProfile.gbufferMs = ProfileElapsedMs(gbufferStart, ProfileClock::now());

            // Mirrored opaque scene for the nearest water plane. The water
            // pass samples this HDR target at the same screen UV as the
            // visible surface. Half resolution is sufficient for a rippled
            // reflection and keeps the extra scene pass affordable.
            const auto planarStart = ProfileClock::now();
            if (skipHeavyGpu) {
                // Clear the stale mirror target; water falls back to the
                // environment cubemap instead of rendering the scene twice.
                m_render->RenderPlanarReflection(m_scene, m_camera, 19, 0, 0);
            } else {
                const uint16_t reflectionW = static_cast<uint16_t>(
                    std::max(2, static_cast<int>(renderW / 2)));
                const uint16_t reflectionH = static_cast<uint16_t>(
                    std::max(2, static_cast<int>(renderH / 2)));
                m_render->RenderPlanarReflection(m_scene, m_camera, 19,
                                                 reflectionW, reflectionH);
            }
            m_cpuProfile.planarReflectionMs = ProfileElapsedMs(planarStart,
                                                               ProfileClock::now());

            postFxSegmentStart = ProfileClock::now();
            postFxProfiling = true;

            // Water lighting uses the same directional light as the deferred
            // pass. Keep a copy outside the deferred block for the water pass
            // that runs after TAA.
            float waterSunDir[4] = {-0.4f, -0.8f, -0.5f, 0.0f};
            float waterSunColor[4] = {1.0f, 1.0f, 1.0f, 1.2f};

            if (m_render->DeferredEnabled()) {
                glm::vec3 ldir{-0.4f,-0.8f,-0.5f}, lcol{1.0f};
                float lint = 1.2f;
                glm::vec3 ldir2{0.0f,-1.0f,0.0f}, lcol2{0.0f};
                float lint2 = 0.0f;
                if (dirLights.primary != entt::null) {
                    const auto& dl = m_scene.Registry()
                        .get<Galeshi::DirectionalLightComponent>(dirLights.primary);
                    ldir = dl.direction;
                    lcol = dl.color;
                    lint = dl.intensity;
                }
                if (dirLights.secondary != entt::null) {
                    const auto& dl = m_scene.Registry()
                        .get<Galeshi::DirectionalLightComponent>(dirLights.secondary);
                    ldir2 = dl.direction;
                    lcol2 = dl.color;
                    lint2 = dl.intensity;
                }
                waterSunDir[0] = -ldir.x;
                waterSunDir[1] = -ldir.y;
                waterSunDir[2] = -ldir.z;
                waterSunDir[3] = 0.0f;
                waterSunColor[0] = lcol.r;
                waterSunColor[1] = lcol.g;
                waterSunColor[2] = lcol.b;
                waterSunColor[3] = lint;
                glm::vec3 cp = m_camera.Position();
                float camPos[4] = {cp.x, cp.y, cp.z, 1.0f};
                float lDirA[4]  = {ldir.x, ldir.y, ldir.z, 0.0f};
                float lColA[4]  = {lcol.r, lcol.g, lcol.b, lint};
                float lDirB[4]  = {ldir2.x, ldir2.y, ldir2.z, 0.0f};
                float lColB[4]  = {lcol2.r, lcol2.g, lcol2.b, lint2};

                // Rebuild the sky IBL when the sun direction changes. Views
                // 190/200+ are sorted after the lighting pass, so the current
                // frame uses the previous (already valid) cubemaps.
                {
                    glm::vec3 sunWorld = glm::normalize(-ldir);
                    if (!m_iblGenerated || glm::distance(sunWorld, m_lastIblSun) > 0.02f) {
                        m_postProcess.GenerateIBL(lDirA);
                        m_lastIblSun = sunWorld;
                        m_iblGenerated = true;
                    }
                }

                float plPosA[64][4] = {};
                float plColA[64][4] = {};
                int plCount = 0;
                auto plview = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::PointLightComponent>();
                for (auto h : plview) {
                    if (plCount >= 64) break;
                    auto& tc = plview.get<Galeshi::TransformComponent>(h);
                    auto& pl = plview.get<Galeshi::PointLightComponent>(h);
                    plPosA[plCount][0] = tc.position.x;
                    plPosA[plCount][1] = tc.position.y;
                    plPosA[plCount][2] = tc.position.z;
                    plPosA[plCount][3] = pl.radius;
                    plColA[plCount][0] = pl.color.r;
                    plColA[plCount][1] = pl.color.g;
                    plColA[plCount][2] = pl.color.b;
                    plColA[plCount][3] = pl.intensity;
                    plCount++;
                }

                float slPos[4][4] = {};
                float slDir[4][4] = {};
                float slCol[4][4] = {};
                float slCone[4][4] = {};
                int slCount = 0;
                auto slview = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::SpotLightComponent>();
                for (auto h : slview) {
                    if (slCount >= 4) break;
                    auto& tc = slview.get<Galeshi::TransformComponent>(h);
                    auto& sl = slview.get<Galeshi::SpotLightComponent>(h);
                    slPos[slCount][0] = tc.position.x;
                    slPos[slCount][1] = tc.position.y;
                    slPos[slCount][2] = tc.position.z;
                    slPos[slCount][3] = sl.radius;
                    glm::vec3 dir = glm::normalize(sl.direction);
                    slDir[slCount][0] = dir.x;
                    slDir[slCount][1] = dir.y;
                    slDir[slCount][2] = dir.z;
                    float cosInner = cos(glm::radians(sl.innerConeAngle));
                    float cosOuter = cos(glm::radians(sl.outerConeAngle));
                    slDir[slCount][3] = cosInner;
                    slCone[slCount][0] = cosOuter;
                    slCone[slCount][1] = 1.0f / std::max(cosInner - cosOuter, 1e-4f);
                    slCol[slCount][0] = sl.color.r;
                    slCol[slCount][1] = sl.color.g;
                    slCol[slCount][2] = sl.color.b;
                    slCol[slCount][3] = sl.intensity;
                    slCount++;
                }

                glm::mat4 proj = renderCamera.Projection();
                glm::mat4 invProj = glm::inverse(proj);
                glm::mat4 invViewProj = glm::inverse(proj * m_camera.View());
                glm::mat4 currVP = proj * m_camera.View(); // True VP matrix for post-processing


                // Pass spot shadow matrices
                static float spotVPs[16] = {0};
                for(int i=0; i<4 && i<slCount; ++i) {
                    float* p = glm::value_ptr(spotLightVPs[i]);
                    for(int j=0; j<4; ++j) spotVPs[i*4 + j] = p[j];
                }
                // We will need to pass this to RenderLighting, but for now, we just set it as a uniform directly.
                // The binding happens inside RenderLighting, but since we need the matrices, we'll add a setUniform here (hacky but works).
                // Actually, PostProcessManager doesn't have u_spotLightVP exposed. We'll do it in RenderLighting.
                // Since we pass slVP to it? No, we can just set it in PostProcessManager::RenderLighting using the data from a static array.

                {
                    if (slCount > 0) bgfx::setUniform(m_postProcess.u_spotLightVP0, glm::value_ptr(spotLightVPs[0]));
                    if (slCount > 1) bgfx::setUniform(m_postProcess.u_spotLightVP1, glm::value_ptr(spotLightVPs[1]));
                    if (slCount > 2) bgfx::setUniform(m_postProcess.u_spotLightVP2, glm::value_ptr(spotLightVPs[2]));
                    if (slCount > 3) bgfx::setUniform(m_postProcess.u_spotLightVP3, glm::value_ptr(spotLightVPs[3]));
                }
                // Ambient occlusion must be resolved before lighting so the
                // result can modulate ambient/IBL and not replace final color.
                Galeshi::AmbientOcclusionParams ao;
                ao.enabled = pp.ssaoEnabled;
                ao.mode = static_cast<int>(pp.aoMode);
                if (pp.aoMode == Galeshi::AmbientOcclusionMode::SSAO) {
                    ao.radius      = pp.ssaoRadius;
                    ao.bias        = pp.ssaoBias;
                    ao.maxDistance = pp.ssaoMaxDistance;
                    ao.strength    = pp.ssaoStrength;
                } else {
                    ao.radius      = pp.aoRadius;
                    ao.bias        = pp.aoBias;
                    ao.thickness   = pp.aoThickness;
                    ao.maxDistance = pp.aoMaxDistance;
                    ao.strength    = pp.aoStrength;
                    ao.directions  = pp.aoDirections;
                    ao.steps       = pp.aoSteps;
                }
                ao.resolutionScale = pp.aoResolutionScale;
                ao.temporalEnabled = pp.aoTemporalEnabled;
                ao.temporalBlend = pp.aoTemporalBlend;
                m_postProcess.RenderAmbientOcclusion(
                    m_framebuffer->WorldPos(), m_framebuffer->Normal(),
                    m_camera.View(), proj, 15, renderW, renderH, ao);

                // Eye adaptation reads the lit HDR (view 45 runs after
                // tonemap, so this frame's lighting is already there); the
                // adapted value is consumed by the next frame's tonemap.
                if (pp.autoExposureEnabled) {
                    m_postProcess.RenderAutoExposure(
                        m_postProcess.OutputTexture(), renderW, renderH,
                        m_frameDelta,
                        pp.autoExposureCalibration,
                        pp.autoExposureMinEv, pp.autoExposureMaxEv,
                        pp.autoExposureSpeedUp, pp.autoExposureSpeedDown, 45,
                        pp.autoExposureHistogram);
                }

                m_postProcess.RenderLightCulling(plCount, plPosA, currVP, renderW, renderH);
                const float pssmParams[4] = {
                    m_pssmSlopeBias, PssmPcfRadius(),
                    m_pssmNormalOffsetSlope,
                    bgfx::getCaps()->originBottomLeft ? 0.0f : 1.0f
                };
                const float fogDensity = pp.fogEnabled ? std::max(pp.fogDensity, 0.0f) : 0.0f;
                const float fogColorDensity[4] = {
                    pp.fogColor.r, pp.fogColor.g, pp.fogColor.b, fogDensity
                };
                const float fogParams[4] = {
                    pp.fogHeightFalloff, pp.fogBaseHeight,
                    pp.fogStartDistance, pp.fogMaxDistance
                };
                const float fogParams2[4] = {
                    pp.fogSunScattering, pp.fogAnisotropy,
                    pp.fogVolumetric ? 1.0f : 0.0f,
                    static_cast<float>(std::clamp(pp.fogSteps, 1, 64))
                };
                const float pcssParams[4] = {
                    pp.pcssEnabled ? 1.0f : 0.0f,
                    std::max(pp.shadowLightSourceSize, 0.01f),
                    24.0f,  // blocker search radius in shadow-map texels
                    48.0f   // maximum penumbra in shadow-map texels
                };
                const float contactShadowParams[4] = {
                    pp.contactShadowsEnabled ? 1.0f : 0.0f,
                    std::max(pp.contactShadowLength, 0.0f),
                    std::max(pp.contactShadowThickness, 0.0001f),
                    0.0f
                };
                m_postProcess.RenderLighting(
                    m_framebuffer->Color(),
                    m_framebuffer->Normal(),
                    m_framebuffer->WorldPos(),
                    m_framebuffer->Material(),
                    m_framebuffer->Features(),
                    m_shadowMaps[0], m_shadowMaps[1], m_shadowMaps[2],
                    lightMtx0, lightMtx1, lightMtx2,
                    invViewProj,
                    cascadeSplits,
                    cascadeNormalOffsets, cascadeDepthBias, pssmParams,
                    camPos, lDirA, lColA, lDirB, lColB,
                    fogColorDensity, fogParams, fogParams2,
                    plPosA, plColA, plCount,
                    slPos, slDir, slCol, slCone, slCount,
                    currVP, pcssParams, contactShadowParams,
                    20, renderW, renderH);

                // Particles are drawn after lighting into the HDR output
                // target (view 21), so bloom/tonemapping pick them up.
                if (m_particles) {
                    m_particles->Render(m_scene, m_camera, 21,
                                        m_postProcess.OutputFbo(), renderW, renderH);
                }

                // The post-processing passes are submitted in increasing view
                // ID order. The ping-pong swaps happen on the CPU, so the call
                // order must match the GPU view order exactly.

                // 1. SSGI trace (View 22) and SSGI/SSR composite (View 24).
                // The trace is half-resolution; the composite performs a
                // depth-aware upsample and adds the diffuse bounce before
                // applying the specular SSR pass.
                Galeshi::SSGIParams ssgi;
                ssgi.enabled = pp.ssgiEnabled;
                ssgi.intensity = pp.ssgiIntensity;
                ssgi.radius = pp.ssgiRadius;
                ssgi.thickness = pp.ssgiThickness;
                ssgi.rays = pp.ssgiRays;
                ssgi.steps = pp.ssgiSteps;

                bgfx::TextureHandle ssgiTex = BGFX_INVALID_HANDLE;
                if (ssgi.enabled) {
                    ssgiTex = m_postProcess.RenderSSGI(
                        m_postProcess.OutputTexture(), m_framebuffer->Normal(),
                        m_framebuffer->WorldPos(), m_framebuffer->Depth(),
                        m_camera.View(), proj, 22, renderW, renderH, ssgi);
                }

                if (pp.ssrEnabled || ssgi.enabled) {
                    const uint16_t ssgiW = static_cast<uint16_t>(
                        std::max<int>(1, renderW / 2));
                    const uint16_t ssgiH = static_cast<uint16_t>(
                        std::max<int>(1, renderH / 2));
                    if (bgfx::isValid(m_postProcess.RenderScreenSpaceLighting(
                            m_postProcess.OutputTexture(), m_framebuffer->Normal(),
                            m_framebuffer->Depth(), m_framebuffer->WorldPos(),
                            m_framebuffer->Color(), m_framebuffer->Material(),
                            m_postProcess.AOTexture(), ssgiTex,
                            m_postProcess.PingFbo(), m_camera.View(), proj,
                            invProj, camPos, 24, renderW, renderH,
                            pp.ssrEnabled, pp.ssrIntensity,
                            ssgi.enabled, pp.ssgiIntensity,
                            ssgiW, ssgiH))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 2. Motion Blur (velocity view 23, blur view 25)
                if (pp.motionBlurEnabled && m_postProcess.HasPrevViewProj()) {
                    if (bgfx::isValid(m_postProcess.RenderMotionBlur(
                            m_postProcess.OutputTexture(), m_framebuffer->WorldPos(),
                            m_framebuffer->Velocity(),
                            m_postProcess.GetPrevViewProj(), taaInvVP,
                            m_postProcess.PingFbo(), 23, 25, renderW, renderH, m_frameDelta,
                            pp.motionBlurAmount, pp.motionBlurMaxVelocity))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 3. Atmosphere (View 26)
                if (pp.atmosphereEnabled) {
                    float sunDir[4] = { -lDirA[0], -lDirA[1], -lDirA[2], 20.0f };
                    if (bgfx::isValid(m_postProcess.RenderAtmosphere(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            glm::inverse(currVP), camPos, sunDir, m_postProcess.PingFbo(),
                            26, renderW, renderH, pp.atmosphereIntensity))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 4. Volumetric Light / god rays (View 27)
                if (pp.godRaysEnabled) {
                    float sunDir[4] = { -lDirA[0], -lDirA[1], -lDirA[2], 20.0f };
                    if (bgfx::isValid(m_postProcess.RenderGodRays(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            currVP, camPos, sunDir, m_postProcess.PingFbo(),
                            27, renderW, renderH, pp.godRaysIntensity, pp.godRaysDecay,
                            pp.godRaysDensity, pp.godRaysWeight))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 5. Depth of Field (Views 28..31)
                if (pp.dofEnabled) {
                    const float dofFocusDistance = ResolveDofFocusDistance(pp);
                    if (bgfx::isValid(m_postProcess.RenderDof(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            proj, invProj, m_postProcess.PingFbo(), 28, renderW, renderH,
                            dofFocusDistance, pp.dofBlur,
                            pp.dofBokehBlades, pp.dofBokehRadius))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 6. Bloom (Views 32..35). Lens flare rides this pass, so it
                // needs bloom on. The sun is projected with the lit-scene VP;
                // occluders darken its brightness sample, so no depth test.
                glm::vec2 flareUv(0.5f);
                float flareVisibility = 0.0f;
                const glm::vec3 toSunRaw = -glm::vec3(lDirA[0], lDirA[1], lDirA[2]);
                if (pp.lensFlareIntensity > 0.0f && glm::length(toSunRaw) > 1e-4f) {
                    const glm::vec3 toSun = glm::normalize(toSunRaw);
                    const glm::vec3 eye = m_camera.Position();
                    const glm::vec4 clip = currVP * glm::vec4(eye + toSun * 1000.0f, 1.0f);
                    if (clip.w > 0.0f) {
                        flareUv = glm::vec2(clip.x, clip.y) / clip.w * 0.5f + 0.5f;
                        if (!bgfx::getCaps()->originBottomLeft) flareUv.y = 1.0f - flareUv.y;
                        const glm::vec2 edge = glm::abs(flareUv - 0.5f) * 2.0f;
                        flareVisibility = 1.0f - glm::smoothstep(0.85f, 1.15f, std::max(edge.x, edge.y));
                    }
                }
                if (pp.bloomEnabled) {
                    if (bgfx::isValid(m_postProcess.RenderBloom(
                            m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                            32, renderW, renderH, pp.bloomThreshold, pp.bloomIntensity,
                            pp.bloomRadius, flareUv, flareVisibility,
                            pp.lensFlareIntensity, pp.lensFlareThreshold))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 7. TAA (Views 36..37)
                if (pp.taaEnabled) {
                    if (bgfx::isValid(m_postProcess.RenderTaa(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            m_framebuffer->Velocity(),
                            m_postProcess.GetPrevViewProj(), taaInvVP,
                            m_postProcess.PingFbo(), 36, renderW, renderH, pp.taaBlend,
                            pp.taaSharpen))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 8. Water (copy scene color/depth at View 38, water at
                // View 39). Rendering after TAA avoids smearing the animated
                // normals while keeping the water in the HDR tonemap chain.
                postFxAccumMs += ProfileElapsedMs(postFxSegmentStart,
                                                   ProfileClock::now());
                const auto waterStart = ProfileClock::now();
                {
                    m_postProcess.BlitTexture(m_postProcess.OutputTexture(),
                                              m_postProcess.BlitTextureA(), 38);
                    m_postProcess.BlitDepth(m_framebuffer->Depth(), 38);
                    bgfx::setViewFrameBuffer(39, m_postProcess.OutputFbo());
                    bgfx::setViewRect(39, 0, 0, renderW, renderH);
                    const bgfx::TextureHandle env =
                        bgfx::isValid(m_postProcess.EnvCubeMap())
                            ? m_postProcess.EnvCubeMap()
                            : m_postProcess.BlackCube();
                    m_render->RenderWater(
                        m_scene, m_camera, m_postProcess.BlitTextureA(),
                        m_postProcess.BlitDepth(), env,
                        m_render->PlanarReflectionTexture(),
                        m_render->PlanarReflectionPlaneY(),
                        waterSunDir, waterSunColor, 39, renderW, renderH,
                        static_cast<float>(m_time), m_camera.NearPlane(),
                        m_camera.FarPlane());
                }
                m_cpuProfile.waterMs = ProfileElapsedMs(waterStart, ProfileClock::now());

                // 8b. Groom hair: an independent forward transparent pass.
                // It reuses the HDR target and the copied opaque depth, and
                // is submitted before tonemapping/AA like other transparent
                // surfaces.
                const auto hairStart = ProfileClock::now();
                {
                    const bgfx::TextureHandle env =
                        bgfx::isValid(m_postProcess.EnvCubeMap())
                            ? m_postProcess.EnvCubeMap()
                            : m_postProcess.BlackCube();
                    bgfx::setViewFrameBuffer(39, m_postProcess.OutputFbo());
                    bgfx::setViewRect(39, 0, 0, renderW, renderH);
                    m_render->RenderHair(
                        m_scene, m_camera, m_postProcess.BlitDepth(), env,
                        waterSunDir, waterSunColor, 39, renderW, renderH,
                        m_camera.NearPlane(), m_camera.FarPlane());
                }
                m_cpuProfile.hairMs = ProfileElapsedMs(hairStart, ProfileClock::now());

                // 8c. Lit translucency: alpha-blended forward pass over the
                // HDR target, using the copied opaque depth for testing and
                // the PSSM shadow maps for shadow/transmission depth.
                {
                    const bgfx::TextureHandle env =
                        bgfx::isValid(m_postProcess.EnvCubeMap())
                            ? m_postProcess.EnvCubeMap()
                            : m_postProcess.BlackCube();
                    bgfx::setViewFrameBuffer(39, m_postProcess.OutputFbo());
                    bgfx::setViewRect(39, 0, 0, renderW, renderH);
                    const float transLightDir[4] = {
                        -waterSunDir[0], -waterSunDir[1], -waterSunDir[2], 0.0f
                    };
                    const float translucencyPssm[4] = {
                        m_pssmSlopeBias, PssmPcfRadius(),
                        m_pssmNormalOffsetSlope,
                        bgfx::getCaps()->originBottomLeft ? 0.0f : 1.0f
                    };
                    m_render->RenderTranslucency(
                        m_scene, m_camera, m_postProcess.BlitDepth(), env,
                        m_shadowMaps[0], m_shadowMaps[1], m_shadowMaps[2],
                        lightMtx0, lightMtx1, lightMtx2,
                        cascadeSplits, translucencyPssm,
                        transLightDir, waterSunColor, 39, renderW, renderH);
                }
                postFxSegmentStart = ProfileClock::now();

                // 9. Scene-referred grading + ACES tonemap (View 40)
                m_postProcess.RenderTonemap(
                    m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                    40, renderW, renderH,
                    std::exp2(pp.exposureEv) * pp.exposure *
                        pp.colorExposure,
                    pp.contrast, pp.saturation,
                    pp.colorContrast, pp.colorSaturation,
                    pp.colorGain, pp.colorGamma, pp.colorOffset,
                    pp.grainStrength, pp.grainScale,
                    static_cast<float>(m_time),
                    pp.autoExposureEnabled,
                    m_postProcess.AutoExposureTexture(),
                    pp.colorTemperature, pp.colorTint,
                    pp.gradeShadows, pp.gradeMidtones,
                    pp.gradeHighlights);
                m_postProcess.SwapFbos();

                // Colour grading LUT. Reloaded only when the path changes; a
                // texture that is not N*N wide by N high is ignored.
                if (pp.colorLutPath != m_colorLutPath) {
                    m_colorLutPath = pp.colorLutPath;
                    m_colorLut = (m_assets && !m_colorLutPath.empty())
                        ? m_assets->LoadTexture(m_colorLutPath, false, nullptr, false) : nullptr;
                    const bool lutValid = m_colorLut && m_colorLut->Valid() &&
                        m_colorLut->Height() >= 2 &&
                        static_cast<uint32_t>(m_colorLut->Width()) ==
                            static_cast<uint32_t>(m_colorLut->Height()) * m_colorLut->Height();
                    if (m_colorLut && !lutValid) {
                        spdlog::warn("[LUT] {} is {}x{}, expected N*N by N",
                                     m_colorLutPath, m_colorLut->Width(), m_colorLut->Height());
                        m_colorLut.reset();
                    }
                }
                bgfx::TextureHandle lutHandle = BGFX_INVALID_HANDLE;
                float lutSize = 0.0f;
                if (m_colorLut) {
                    lutHandle = m_colorLut->Handle();
                    lutSize = static_cast<float>(m_colorLut->Height());
                }

                // 10. LUT-style environment palette (View 41), after tonemap
                m_postProcess.RenderColorCorrection(
                    m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                    41, renderW, renderH, 1.0f, 1.0f, 1.0f,
                    pp.environmentPaletteEnabled
                        ? pp.environmentPaletteAmount
                        : 0.0f,
                    pp.environmentPaletteShadow, pp.environmentPaletteMid,
                    pp.environmentPaletteHighlight, lutHandle, lutSize,
                    pp.colorLutStrength);
                m_postProcess.SwapFbos();

                // 11. Vignette (View 42)
                m_postProcess.RenderVignette(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 42, renderW, renderH, pp.vignetteIntensity, pp.vignetteSmoothness);
                m_postProcess.SwapFbos();

                // 12. Chromatic Aberration (View 43)
                m_postProcess.RenderChromatic(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 43, renderW, renderH, pp.chromaticAberration);
                m_postProcess.SwapFbos();

                // 13. SMAA (Views 44..45)
                if (pp.smaaEnabled) {
                    if (bgfx::isValid(m_postProcess.RenderSmaa(
                            m_postProcess.OutputTexture(), 44, renderW, renderH))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 14. FXAA (View 46)
                if (pp.fxaaEnabled) {
                    if (bgfx::isValid(m_postProcess.RenderFxaa(
                            m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                            46, renderW, renderH))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // Store the unjittered camera matrix for the next frame's
                // motion blur and TAA reprojection.
                m_postProcess.SetPrevViewProj(taaVP);
            } else {
                // View 2 is reserved for the first directional shadow cascade.
                m_postProcess.Render(46, m_framebuffer->Color(), renderW, renderH);
            }
        }

        // Stop PostFX before editor debug gizmos; the final scene copy and
        // debug overlay are presentation/editor work and land in Other.
        if (postFxProfiling) {
            postFxAccumMs += ProfileElapsedMs(postFxSegmentStart,
                                               ProfileClock::now());
            m_cpuProfile.postFxMs = postFxAccumMs;
        }

        // Debug rendering (View 50). Editor-only overlays (grid, axes,
        // nav/AI debug, lights and post-process-volume bounds) are hidden
        // while Play is active, but collision-shape wireframes remain visible.
        if (m_debug && !m_isGameMode && m_framebuffer &&
            m_viewportViewMode == ViewportViewMode::Lit) {
            m_debug->Begin();
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (!m_editor.isPlaying) {
            m_debug->AddGrid(50.0f, 2.0f, {0.3f, 0.3f, 0.3f, 0.4f});
            m_debug->AddAxes({0, 0.05f, 0}, 2.0f);
            if (m_navSystem) {
                const auto& navTris = m_navSystem->GetDebugTriangles();
                if (!navTris.empty()) {
                    for (size_t i = 0; i + 2 < navTris.size(); i += 3) {
                        const glm::vec4 c = {0.2f, 0.8f, 0.4f, 0.5f};
                        m_debug->AddLine(navTris[i], navTris[i+1], c);
                        m_debug->AddLine(navTris[i+1], navTris[i+2], c);
                        m_debug->AddLine(navTris[i+2], navTris[i], c);
                    }
                }
            }
            auto navView = m_scene.Registry().view<Galeshi::NavAgentComponent, Galeshi::TransformComponent>();
            for (auto e : navView) {
                auto& tc = navView.get<Galeshi::TransformComponent>(e);
                auto& nav = navView.get<Galeshi::NavAgentComponent>(e);
                glm::vec3 worldPos = tc.position;
                entt::entity curr = e;
                int depth = 0;
                while (depth++ < 128 && m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                    curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                    if (m_scene.Registry().all_of<Galeshi::TransformComponent>(curr))
                        worldPos += m_scene.Registry().get<Galeshi::TransformComponent>(curr).position;
                }
                m_debug->AddBox(worldPos, glm::vec3(0.5f), {0.2f, 1.0f, 0.2f, 1.0f});
                glm::vec3 prev = worldPos;
                for (const auto& pt : nav.path) {
                    m_debug->AddLine(prev, pt, {1.0f, 1.0f, 0.0f, 1.0f});
                    prev = pt;
                }
            }
            if (m_showAIDebug) {
                auto perceptionView = m_scene.Registry().view<Galeshi::PerceptionComponent,
                                                               Galeshi::TransformComponent>();
                for (auto e : perceptionView) {
                    const auto& perception =
                        perceptionView.get<Galeshi::PerceptionComponent>(e);
                    const auto& tc =
                        perceptionView.get<Galeshi::TransformComponent>(e);
                    glm::vec3 origin = tc.position;
                    entt::entity curr = e;
                    int depth = 0;
                    while (depth++ < 128 &&
                           m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                        curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                        if (m_scene.Registry().all_of<Galeshi::TransformComponent>(curr)) {
                            origin += m_scene.Registry().get<Galeshi::TransformComponent>(curr).position;
                        }
                    }

                    const glm::vec4 ringColor = perception.canSeeTarget
                        ? glm::vec4(0.3f, 1.0f, 0.3f, 0.9f)
                        : glm::vec4(0.9f, 0.7f, 0.2f, 0.55f);
                    const float y = origin.y + 0.04f;
                    for (int i = 0; i < 40; ++i) {
                        const float a1 = static_cast<float>(i) / 40.0f * 6.2831853f;
                        const float a2 = static_cast<float>(i + 1) / 40.0f * 6.2831853f;
                        m_debug->AddLine(
                            {origin.x + std::cos(a1) * perception.sightRadius, y,
                             origin.z + std::sin(a1) * perception.sightRadius},
                            {origin.x + std::cos(a2) * perception.sightRadius, y,
                             origin.z + std::sin(a2) * perception.sightRadius},
                            ringColor);
                    }

                    if (perception.targetEntity != entt::null &&
                        m_scene.Registry().valid(perception.targetEntity) &&
                        m_scene.Registry().all_of<Galeshi::TransformComponent>(
                            perception.targetEntity)) {
                        glm::vec3 target =
                            m_scene.Registry().get<Galeshi::TransformComponent>(
                                perception.targetEntity).position;
                        target.y += 1.0f;
                        const glm::vec3 eye = origin + glm::vec3(0.0f, perception.eyeHeight, 0.0f);
                        m_debug->AddLine(eye, target,
                            perception.canSeeTarget ? glm::vec4(1.0f, 0.2f, 0.2f, 1.0f)
                                                    : glm::vec4(0.5f, 0.5f, 0.5f, 0.7f));
                        m_debug->AddBox(perception.lastKnownPosition + glm::vec3(0.0f, 0.2f, 0.0f),
                                        glm::vec3(0.2f),
                                        glm::vec4(0.2f, 0.8f, 1.0f, 0.8f));
                    }
                }
            }
            if (!m_editor.isPlaying) {
                auto plviewViz = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::PointLightComponent>();
                for (auto e : plviewViz) {
                    auto& tc = plviewViz.get<Galeshi::TransformComponent>(e);
                    auto& pl = plviewViz.get<Galeshi::PointLightComponent>(e);
                    float r = pl.radius;
                    glm::vec4 c = {1.0f, 1.0f, 0.0f, 1.0f};
                    for (int i = 0; i < 32; ++i) {
                        float a1 = i * 2.0f * 3.14159f / 32.0f;
                        float a2 = (i + 1) * 2.0f * 3.14159f / 32.0f;
                        m_debug->AddLine(tc.position + glm::vec3(cos(a1), 0, sin(a1))*r, tc.position + glm::vec3(cos(a2), 0, sin(a2))*r, c);
                        m_debug->AddLine(tc.position + glm::vec3(0, cos(a1), sin(a1))*r, tc.position + glm::vec3(0, cos(a2), sin(a2))*r, c);
                        m_debug->AddLine(tc.position + glm::vec3(cos(a1), sin(a1), 0)*r, tc.position + glm::vec3(cos(a2), sin(a2), 0)*r, c);
                    }
                }
                auto slviewViz = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::SpotLightComponent>();
                for (auto e : slviewViz) {
                    auto& tc = slviewViz.get<Galeshi::TransformComponent>(e);
                    auto& sl = slviewViz.get<Galeshi::SpotLightComponent>(e);
                    glm::vec3 dir = glm::normalize(sl.direction);
                    float r = sl.radius;
                    float outer = glm::radians(sl.outerConeAngle);
                    float coneR = tan(outer) * r;
                    glm::vec3 endPos = tc.position + dir * r;
                    glm::vec3 up = glm::vec3(0, 1, 0);
                    if (abs(dot(dir, up)) > 0.99f) up = glm::vec3(1, 0, 0);
                    glm::vec3 right = glm::normalize(glm::cross(dir, up));
                    glm::vec3 forward = glm::normalize(glm::cross(right, dir));
                    glm::vec4 c = {1.0f, 0.5f, 0.0f, 1.0f};
                    for (int i = 0; i < 16; ++i) {
                        float a1 = i * 2.0f * 3.14159f / 16.0f;
                        float a2 = (i + 1) * 2.0f * 3.14159f / 16.0f;
                        glm::vec3 p1 = endPos + (cos(a1)*right + sin(a1)*forward) * coneR;
                        glm::vec3 p2 = endPos + (cos(a2)*right + sin(a2)*forward) * coneR;
                        m_debug->AddLine(tc.position, p1, c);
                        m_debug->AddLine(p1, p2, c);
                    }
                }
            }
            if (!m_editor.isPlaying && sel && sel.HasComponent<Galeshi::TransformComponent>() && !sel.HasComponent<Galeshi::SkeletalComponent>()) {
                auto& tc = sel.GetComponent<Galeshi::TransformComponent>();
                m_debug->AddBox(tc.position, tc.scale + glm::vec3(0.1f), {1, 1, 0, 1});
            }

            // Yellow outline of the selected particle emitter's spawn volume.
            // Matches the ParticleSystem: the spawn area is centred on the
            // emitter's world position (parent chain applied) and is either an
            // axis-aligned box or a sphere.
            if (!m_editor.isPlaying && sel &&
                sel.HasComponent<Galeshi::ParticleEmitterComponent>() &&
                sel.HasComponent<Galeshi::TransformComponent>()) {
                const auto& pe = sel.GetComponent<Galeshi::ParticleEmitterComponent>();

                glm::mat4 world(1.0f);
                std::vector<entt::entity> chain;
                entt::entity cur = sel.Handle();
                int depth = 0;
                while (cur != entt::null && depth++ < 128) {
                    chain.push_back(cur);
                    if (m_scene.Registry().all_of<Galeshi::ParentComponent>(cur)) {
                        cur = m_scene.Registry().get<Galeshi::ParentComponent>(cur).parent;
                    } else {
                        break;
                    }
                }
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    if (m_scene.Registry().all_of<Galeshi::TransformComponent>(*it)) {
                        world = world * m_scene.Registry().get<Galeshi::TransformComponent>(*it).LocalMatrix();
                    }
                }
                const glm::vec3 origin(world[3]);
                const glm::vec4 yellow = {1.0f, 1.0f, 0.0f, 1.0f};

                const bool useBox = pe.spawnBoxSize.x > 0.0f ||
                                    pe.spawnBoxSize.y > 0.0f ||
                                    pe.spawnBoxSize.z > 0.0f;
                if (useBox) {
                    // AddBox() expects the full size, spawnBoxSize holds half extents.
                    m_debug->AddBox(origin, pe.spawnBoxSize * 2.0f, yellow);
                } else if (pe.spawnRadius > 0.0f) {
                    const int segments = 48;
                    for (int i = 0; i < segments; ++i) {
                        const float a1 = static_cast<float>(i) / segments * 6.2831853f;
                        const float a2 = static_cast<float>(i + 1) / segments * 6.2831853f;
                        const float c1 = std::cos(a1) * pe.spawnRadius;
                        const float s1 = std::sin(a1) * pe.spawnRadius;
                        const float c2 = std::cos(a2) * pe.spawnRadius;
                        const float s2 = std::sin(a2) * pe.spawnRadius;
                        m_debug->AddLine(origin + glm::vec3(c1, s1, 0.0f),
                                         origin + glm::vec3(c2, s2, 0.0f), yellow);
                        m_debug->AddLine(origin + glm::vec3(c1, 0.0f, s1),
                                         origin + glm::vec3(c2, 0.0f, s2), yellow);
                        m_debug->AddLine(origin + glm::vec3(0.0f, c1, s1),
                                         origin + glm::vec3(0.0f, c2, s2), yellow);
                    }
                }
            }

            } // !isPlaying: editor-only overlays

            // Brush outline: draw the actual brush footprint on the height
            // field so radius is visible before painting/sculpting.
            if (!m_editor.isPlaying && m_lastBrushHitValid && sel &&
                sel.HasComponent<Galeshi::LandscapeComponent>() &&
                m_editor.toolMode != Galeshi::EditorState::ToolMode::Select) {
                const auto& landscape =
                    sel.GetComponent<Galeshi::LandscapeComponent>();
                const glm::mat4 world = EntityWorldMatrix(sel.Handle());
                const float radius = std::max(0.05f, m_lastBrushRadius);
                const glm::vec4 color =
                    m_editor.toolMode == Galeshi::EditorState::ToolMode::PaintTrees
                        ? glm::vec4(0.30f, 1.00f, 0.30f, 1.0f)
                        : m_editor.toolMode == Galeshi::EditorState::ToolMode::PaintGrass
                            ? glm::vec4(0.35f, 0.85f, 0.25f, 1.0f)
                            : glm::vec4(1.00f, 0.80f, 0.20f, 1.0f);
                constexpr int segments = 64;
                glm::vec3 previous(0.0f);
                for (int i = 0; i <= segments; ++i) {
                    const float a = static_cast<float>(i) / segments *
                                    6.2831853f;
                    const float x = m_lastBrushLocal.x + std::cos(a) * radius;
                    const float z = m_lastBrushLocal.z + std::sin(a) * radius;
                    const float y = Galeshi::LandscapeMesh::SampleHeight(
                        landscape, x, z) + 0.035f;
                    const glm::vec3 point = glm::vec3(
                        world * glm::vec4(x, y, z, 1.0f));
                    if (i > 0) m_debug->AddLine(previous, point, color);
                    previous = point;
                }
            }

            // Camera gizmos (editor only): a small frustum makes the camera
            // object visible and clearly shows its viewing direction.
            if (!m_editor.isPlaying) {
                auto cameraView = m_scene.Registry()
                    .view<Galeshi::CameraComponent,
                          Galeshi::TransformComponent>();
                const float aspect = m_viewportH
                    ? static_cast<float>(m_viewportW) / m_viewportH
                    : 16.0f / 9.0f;
                for (auto cameraEntity : cameraView) {
                    const auto& cameraComponent =
                        cameraView.get<Galeshi::CameraComponent>(cameraEntity);
                    const glm::mat4 world = EntityWorldMatrix(cameraEntity);
                    const glm::vec3 eye = glm::vec3(world[3]);
                    glm::vec3 forward = glm::vec3(
                        world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
                    glm::vec3 up = glm::vec3(
                        world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
                    forward = glm::dot(forward, forward) < 1e-6f
                        ? glm::vec3(0,0,-1) : glm::normalize(forward);
                    up = glm::dot(up, up) < 1e-6f
                        ? glm::vec3(0,1,0) : glm::normalize(up);
                    glm::vec3 right = glm::normalize(glm::cross(forward, up));
                    up = glm::normalize(glm::cross(right, forward));
                    const bool selectedCamera = sel && sel.Handle() == cameraEntity;
                    const glm::vec4 color = selectedCamera
                        ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)
                        : glm::vec4(0.25f, 0.8f, 1.0f, 0.85f);
                    const float nearPlane = 0.45f;
                    const float halfH = std::tan(glm::radians(
                        glm::clamp(cameraComponent.fovY, 1.0f, 179.0f)) * 0.5f) *
                        nearPlane;
                    const float halfW = halfH * aspect;
                    const glm::vec3 center = eye + forward * nearPlane;
                    const glm::vec3 corners[4] = {
                        center - right * halfW - up * halfH,
                        center + right * halfW - up * halfH,
                        center + right * halfW + up * halfH,
                        center - right * halfW + up * halfH,
                    };
                    for (int i = 0; i < 4; ++i) {
                        m_debug->AddLine(eye, corners[i], color);
                        m_debug->AddLine(corners[i], corners[(i + 1) % 4], color);
                    }
                    m_debug->AddLine(eye, eye + forward * 0.9f, color);
                }
            }

            // Collision shape gizmos stay visible across Play/Stop. Only the
            // per-collider Visible in Editor flag controls the wireframe.
            DrawCollisionShapeGizmos();

            // Post-process volume bounds.
            if (!m_editor.isPlaying) {
                auto volumeView = m_scene.Registry()
                    .view<Galeshi::PostProcessVolumeComponent>();
                for (auto e : volumeView) {
                    const auto& volume =
                        volumeView.get<Galeshi::PostProcessVolumeComponent>(e);
                    if (!volume.visibleInEditor) continue;

                    const bool isSelected = sel && sel.Handle() == e;
                    const glm::vec4 color = isSelected
                        ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)
                        : glm::vec4(0.65f, 0.30f, 1.00f, 0.85f);

                    const glm::mat4 world = EntityWorldMatrix(e);
                    glm::vec3 worldScale, translation, worldSkew;
                    glm::vec4 worldPerspective;
                    glm::quat rotation;
                    glm::decompose(world, worldScale, rotation, translation,
                                   worldSkew, worldPerspective);

                    const glm::vec3 h = volume.size * 0.5f;
                    auto toWorld = [&](const glm::vec3& p) {
                        return translation + rotation * p;
                    };
                    glm::vec3 p[8] = {
                        toWorld({-h.x, -h.y, -h.z}),
                        toWorld({ h.x, -h.y, -h.z}),
                        toWorld({ h.x, -h.y,  h.z}),
                        toWorld({-h.x, -h.y,  h.z}),
                        toWorld({-h.x,  h.y, -h.z}),
                        toWorld({ h.x,  h.y, -h.z}),
                        toWorld({ h.x,  h.y,  h.z}),
                        toWorld({-h.x,  h.y,  h.z}),
                    };
                    auto edge = [&](int a, int b) {
                        m_debug->AddLine(p[a], p[b], color);
                    };
                    edge(0, 1); edge(1, 2); edge(2, 3); edge(3, 0);
                    edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 4);
                    edge(0, 4); edge(1, 5); edge(2, 6); edge(3, 7);
                }
            }
            if (m_framebuffer && bgfx::isValid(m_framebuffer->Handle())) {
                // Draw directly into the G-buffer FBO: it already owns the
                // scene depth, and its color attachment has been filled with
                // the post-processed image at view 48. The G-buffer color is
                // no longer sampled after the lighting pass.
                bgfx::setViewFrameBuffer(50, m_framebuffer->Handle());
                // The diagnostic pass at view 50 leaves a dark CLEAR on this
                // view; without this reset, returning to Lit would wipe the
                // lit image every frame and show only the debug lines.
                bgfx::setViewClear(50, BGFX_CLEAR_NONE);
                m_debug->Render(m_camera, 50, renderW, renderH);
            }
        }

        // Copy the final HDR color into the G-buffer color attachment, then
        // blend the transparent layer over it (view 48, before glass at 49,
        // debug at 50 and the ImGui blit at 51). Reusing the persistent
        // G-buffer FBO avoids temporary FBOs whose layout can get out of sync.
        if (m_framebuffer &&
            bgfx::isValid(m_framebuffer->Color()) &&
            m_postProcess.OutputFbo().idx != bgfx::kInvalidHandle) {
            m_postProcess.BlitTexture(m_postProcess.OutputTexture(), m_framebuffer->Color(), 48);
            bgfx::setViewFrameBuffer(49, m_framebuffer->Handle());
            bgfx::setViewRect(49, 0, 0, renderW, renderH);
            m_render->RenderGlass(m_scene, m_camera, 49, renderW, renderH);
        }

        // View modes are diagnostic passes over the final scene image. They
        // reuse the viewport color/depth targets at view 50 (after post and
        // glass, before the ImGui copy at 51), and deliberately omit shadows,
        // lighting and post-processing.
        if (m_framebuffer &&
            bgfx::isValid(m_framebuffer->Handle()) &&
            m_viewportViewMode != ViewportViewMode::Lit) {
            const Galeshi::RenderSystem::State savedState =
                m_viewModeStateSaved
                    ? m_savedRenderState
                    : m_render->SaveState();
            m_render->SetViewMode(
                static_cast<Galeshi::RenderSystem::ViewMode>(
                    m_viewportViewMode));
            bgfx::setViewFrameBuffer(50, m_framebuffer->Handle());
            m_render->Render(m_scene, m_camera, 50, renderW, renderH);
            m_render->RestoreState(savedState);
        }

        // Set when the 2D UI canvas overlay handles the click this frame.
        bool uiOverlayConsumed = false;

        // Blit for ImGui (View 51). This runs every frame; performance mode
        // only skips optional heavy passes inside the scene render.
        bgfx::TextureHandle __disp = BGFX_INVALID_HANDLE;
        if (m_framebuffer && bgfx::isValid(m_framebuffer->Color())) {
            bgfx::TextureHandle __ppTex = m_framebuffer->Color();
            __disp = __ppTex;
            m_postProcess.BlitTexture(__ppTex, m_postProcess.BlitTextureA(), 51);
            __disp = m_postProcess.BlitTextureA();
            m_lastViewportTexture = __disp;
        } else if (bgfx::isValid(m_lastViewportTexture)) {
            __disp = m_lastViewportTexture;
        }

        if (bgfx::isValid(__disp)) {
            ImTextureID texId = static_cast<ImTextureID>(__disp.idx);
            ImGui::Image(texId, ImVec2(static_cast<float>(w), static_cast<float>(h)), ImVec2(0, 0), ImVec2(1, 1));
            m_viewportImagePos = ImGui::GetItemRectMin();
            m_viewportImageSize = ImGui::GetItemRectSize();

            // Keep the lightweight viewport stats visible instead of burying
            // them in a menu. Draw them after the image so they overlay the
            // rendered scene without becoming interactive viewport widgets.
            {
                char fpsText[64];
                char entitiesText[64];
                std::snprintf(fpsText, sizeof(fpsText), "FPS: %.1f",
                              ImGui::GetIO().Framerate);
                std::snprintf(entitiesText, sizeof(entitiesText), "Entities: %d",
                              static_cast<int>(m_scene.AllEntities().size()));

                const char* viewModeName =
                    m_viewportViewMode == ViewportViewMode::Wireframe
                        ? "Wireframe"
                        : (m_viewportViewMode == ViewportViewMode::Unlit
                               ? "Unlit"
                               : "Lit");
                char renderStatus[128];
                std::snprintf(
                    renderStatus, sizeof(renderStatus),
                    "View: %s | Effects: %s", viewModeName,
                    skipHeavyGpu ? "Reduced (live)" : "Full");
                const char* statusText = renderStatus;

                const float lineHeight = ImGui::GetTextLineHeight();
                const ImVec2 padding(8.0f, 5.0f);
                const ImVec2 textPos(m_viewportImagePos.x + padding.x,
                                     m_viewportImagePos.y + padding.y);
                const float textWidth = std::max(
                    std::max(ImGui::CalcTextSize(fpsText).x,
                             ImGui::CalcTextSize(entitiesText).x),
                    ImGui::CalcTextSize(statusText).x);
                const ImVec2 panelMin = m_viewportImagePos;
                const ImVec2 panelMax(
                    m_viewportImagePos.x + textWidth + padding.x * 2.0f,
                    m_viewportImagePos.y + lineHeight * 3.0f +
                        padding.y * 2.0f);

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(panelMin, panelMax,
                                        IM_COL32(10, 12, 16, 150), 5.0f);
                drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f),
                                  IM_COL32(0, 0, 0, 180), fpsText);
                drawList->AddText(textPos, IM_COL32(235, 240, 248, 255), fpsText);
                drawList->AddText(ImVec2(textPos.x + 1.0f,
                                         textPos.y + lineHeight + 1.0f),
                                  IM_COL32(0, 0, 0, 180), entitiesText);
                drawList->AddText(ImVec2(textPos.x, textPos.y + lineHeight),
                                  IM_COL32(235, 240, 248, 255), entitiesText);
                drawList->AddText(
                    ImVec2(textPos.x + 1.0f, textPos.y + lineHeight * 2.0f + 1.0f),
                    IM_COL32(0, 0, 0, 180), statusText);
                drawList->AddText(
                    ImVec2(textPos.x, textPos.y + lineHeight * 2.0f),
                    skipHeavyGpu
                        ? IM_COL32(255, 190, 90, 255)
                        : IM_COL32(140, 220, 150, 255),
                    statusText);
            }

            // A left click inside the gameplay view restores the viewport
            // input focus after Shift+F1 released it.
            if (m_editor.isPlaying && !m_isGameMode &&
                m_viewportImageSize.x > 0.0f &&
                m_viewportImageSize.y > 0.0f &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                ImGui::IsMouseHoveringRect(
                    m_viewportImagePos,
                    ImVec2(m_viewportImagePos.x + m_viewportImageSize.x,
                           m_viewportImagePos.y + m_viewportImageSize.y),
                    false) &&
                !m_uiRuntime.IsPointerOverUI()) {
                SetViewportInputFocus(true);
            }

            // A selected player camera gets a floating live preview in the
            // lower-right corner. It is rendered into its own small target,
            // so it does not disturb the editor camera or the main post chain.
            if (!m_isGameMode &&
                m_viewportImageSize.x > 120.0f &&
                m_viewportImageSize.y > 80.0f) {
                Galeshi::Entity selectedCamera = m_editor.Selected(&m_scene);
                if (selectedCamera &&
                    selectedCamera.HasComponent<Galeshi::CameraComponent>() &&
                    IsGameplayCameraAttachedToPlayer(
                        selectedCamera.Handle()) &&
                    RenderPlayerCameraPreview(
                        selectedCamera.Handle(), true)) {
                    const float maxWidth = std::min(
                        320.0f, m_viewportImageSize.x * 0.38f);
                    const float maxHeight = m_viewportImageSize.y * 0.38f;
                    const float previewScale = std::min(
                        maxWidth / static_cast<float>(m_playerCameraPreviewW),
                        maxHeight / static_cast<float>(m_playerCameraPreviewH));
                    if (previewScale > 0.1f) {
                        const float previewW =
                            static_cast<float>(m_playerCameraPreviewW) *
                            previewScale;
                        const float previewH =
                            static_cast<float>(m_playerCameraPreviewH) *
                            previewScale;
                        const float margin = 16.0f;
                        const ImVec2 previewMin(
                            m_viewportImagePos.x + m_viewportImageSize.x -
                                margin - previewW,
                            m_viewportImagePos.y + m_viewportImageSize.y -
                                margin - previewH);
                        const ImVec2 previewMax(
                            previewMin.x + previewW,
                            previewMin.y + previewH);
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        drawList->AddRectFilled(
                            ImVec2(previewMin.x - 4.0f, previewMin.y - 4.0f),
                            ImVec2(previewMax.x + 4.0f, previewMax.y + 4.0f),
                            IM_COL32(8, 10, 14, 220), 6.0f);
                        drawList->AddImage(
                            static_cast<ImTextureID>(
                                m_playerCameraPreviewOutput.idx),
                            previewMin, previewMax);
                        drawList->AddRect(
                            previewMin, previewMax,
                            IM_COL32(235, 242, 250, 230), 4.0f);
                        const char* label = "Player Camera";
                        drawList->AddText(
                            ImVec2(previewMin.x + 8.0f,
                                   previewMin.y + 7.0f),
                            IM_COL32(0, 0, 0, 180), label);
                        drawList->AddText(
                            ImVec2(previewMin.x + 7.0f,
                                   previewMin.y + 6.0f),
                            IM_COL32(245, 250, 255, 255), label);
                    }
                }
            }

            // Accept assets dragged from the Content Browser. Models are
            // spawned under the cursor; textures are still applied to the
            // selected material by OnAssetDropped().
            const ImVec2 dropImageMin = ImGui::GetItemRectMin();
            const ImVec2 dropImageMax = ImGui::GetItemRectMax();
            if (!m_isGameMode) {
                if (const ImGuiPayload* active = ImGui::GetDragDropPayload();
                    active && active->IsDataType("GALESHI_ASSET_PATH") &&
                    IsDragDropModelAssetPath(static_cast<const char*>(active->Data)) &&
                    ImGui::IsItemHovered(
                        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                    ImGui::SetTooltip(
                        "Drop to place this model in the scene as a new entity.");
                }
            }
            if (!m_isGameMode && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                    const char* path = static_cast<const char*>(payload->Data);
                    if (path && *path) {
                        const glm::vec3 dropPos = ViewportDropWorldPosition(
                            ImGui::GetMousePos(), dropImageMin, w, h);
                        OnAssetDropped(path, dropPos);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // 2D UI Studio canvas overlay.  It draws the authored canvas on
            // top of the viewport and handles selection/move/resize before
            // the 3D picking below gets a chance to run.
            if (!m_isGameMode && !m_editor.isPlaying && m_uiCanvasOverlay) {
                if (Galeshi::UICanvas* overlayCanvas = ActiveUICanvas()) {
                    uiOverlayConsumed = DrawUICanvasOverlay(
                        *overlayCanvas, dropImageMin, dropImageMax);
                }
            }
        } else {
            // Keep the viewport item geometry valid for gizmos, picking and
            // layout while there is no completed frame to display yet.
            ImGui::Dummy(ImVec2(static_cast<float>(w),
                                static_cast<float>(h)));
            m_viewportImagePos = ImGui::GetItemRectMin();
            m_viewportImageSize = ImGui::GetItemRectSize();
        }

        if (bgfx::isValid(__disp)) {
            if (m_isGameMode) {
                ImGui::End();
                return; // Skip gizmo and mouse picking in game mode.
            }

            ImVec2 imgMin = ImGui::GetItemRectMin();
            ImVec2 imgMax = ImGui::GetItemRectMax();

            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            const bool selHasTf = sel && sel.HasComponent<Galeshi::TransformComponent>();
            const bool selectedIsUICanvas =
                sel && sel.HasComponent<Galeshi::UICanvasComponent>();
            Galeshi::Entity clothEntity = FindClothMeshTarget(sel);
            const bool landscapeSculptActive =
                !m_isGameMode && selHasTf &&
                sel.HasComponent<Galeshi::LandscapeComponent>() &&
                m_editor.toolMode != Galeshi::EditorState::ToolMode::Select &&
                m_editor.toolMode != Galeshi::EditorState::ToolMode::PaintCloth &&
                !uiOverlayConsumed &&
                (!m_editor.isPlaying || !m_uiRuntime.IsPointerOverUI());
            const bool clothPaintActive =
                !m_isGameMode && selHasTf && clothEntity &&
                clothEntity.HasComponent<Galeshi::SoftBodyComponent>() &&
                clothEntity.HasComponent<Galeshi::MeshComponent>() &&
                m_editor.toolMode ==
                    Galeshi::EditorState::ToolMode::PaintCloth &&
                !uiOverlayConsumed &&
                (!m_editor.isPlaying || !m_uiRuntime.IsPointerOverUI());

            // Landscape sculpting uses the same camera ray as picking, then
            // raycasts the actual height field instead of entity AABBs.
            if (landscapeSculptActive) {
                const ImVec2 sculptMouse = ImGui::GetMousePos();
                const bool overImage =
                    ImGui::IsWindowHovered() &&
                    sculptMouse.x >= imgMin.x && sculptMouse.x <= imgMax.x &&
                    sculptMouse.y >= imgMin.y && sculptMouse.y <= imgMax.y;
                const float ndcX = ((sculptMouse.x - imgMin.x) / w) * 2.0f - 1.0f;
                const float ndcY = 1.0f - ((sculptMouse.y - imgMin.y) / h) * 2.0f;
                const glm::mat4 invVP = glm::inverse(m_camera.Projection() * m_camera.View());
                glm::vec4 rayStart4 = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                glm::vec4 rayEnd4   = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
                rayStart4 /= rayStart4.w;
                rayEnd4 /= rayEnd4.w;
                const glm::vec3 rayOrigin = glm::vec3(rayStart4);
                const glm::vec3 rayDir = glm::normalize(glm::vec3(rayEnd4 - rayStart4));

                glm::mat4 landscapeWorld(1.0f);
                {
                    entt::entity curr = sel.Handle();
                    std::vector<entt::entity> chain;
                    int depth = 0;
                    while (curr != entt::null && depth++ < 128) {
                        chain.push_back(curr);
                        if (m_scene.Registry().all_of<Galeshi::ParentComponent>(curr))
                            curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                        else
                            break;
                    }
                    for (size_t ci = chain.size(); ci-- > 0;) {
                        if (m_scene.Registry().all_of<Galeshi::TransformComponent>(chain[ci]))
                            landscapeWorld = landscapeWorld *
                                m_scene.Registry().get<Galeshi::TransformComponent>(chain[ci]).LocalMatrix();
                    }
                }

                auto& landscape = sel.GetComponent<Galeshi::LandscapeComponent>();
                Galeshi::LandscapeRaycastHit hit;
                m_lastBrushHitValid = false;
                if (overImage &&
                    Galeshi::LandscapeMesh::Raycast(landscape, landscapeWorld,
                                                    rayOrigin, rayDir, hit)) {
                    m_lastSculptHeight = hit.height;
                    m_lastBrushLocal = hit.localPosition;
                    m_lastBrushRadius = m_editor.brushRadius;
                    m_lastBrushHitValid = true;

                    const bool paintTrees =
                        m_editor.toolMode ==
                        Galeshi::EditorState::ToolMode::PaintTrees;
                    const bool paintGrass =
                        m_editor.toolMode ==
                        Galeshi::EditorState::ToolMode::PaintGrass;
                    const bool paintLandscape =
                        m_editor.toolMode ==
                        Galeshi::EditorState::ToolMode::PaintLandscape;
                    const bool paintMode = paintTrees || paintGrass;
                    const bool leftClicked =
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left);

                    if (leftClicked) {
                        PushHistory();
                        m_wasSculpting = true;
                        m_paintStampValid = false;
                    }
                    if (paintLandscape &&
                        (leftClicked ||
                         (m_wasSculpting &&
                          ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
                        const bool erase = ImGui::GetIO().KeyAlt;
                        const int vertexChanged =
                            Galeshi::LandscapeMesh::ApplyPaintBrush(
                                landscape, landscape.activeLayer,
                                hit.localPosition.x,
                                hit.localPosition.z, m_editor.brushRadius,
                                m_editor.brushStrength,
                                m_editor.brushFalloff,
                                ImGui::GetIO().DeltaTime, erase);
                        const bool maskChanged = PaintLandscapeLayerMask(
                            sel, landscape.activeLayer,
                            hit.localPosition.x, hit.localPosition.z,
                            m_editor.brushRadius, m_editor.brushStrength,
                            m_editor.brushFalloff,
                            ImGui::GetIO().DeltaTime, erase);
                        if (vertexChanged > 0 || maskChanged) {
                            if (sel.HasComponent<Galeshi::MeshComponent>()) {
                                auto& meshComponent =
                                    sel.GetComponent<Galeshi::MeshComponent>();
                                if (meshComponent.mesh) {
                                    Galeshi::LandscapeMesh::UpdatePaintWeights(
                                        landscape, *meshComponent.mesh);
                                }
                            }
                            // Preview the active layer in the shader while
                            // painting; the deferred bake below still resolves
                            // all layers into the final texture set.
                            if (auto* materialComponent =
                                    m_scene.Registry()
                                        .try_get<Galeshi::MaterialComponent>(
                                            sel.Handle())) {
                                if (materialComponent->material &&
                                    landscape.activeLayer >= 0 &&
                                    landscape.activeLayer <
                                        static_cast<int>(
                                            landscape.layers.size())) {
                                    const auto& layer = landscape.layers[
                                        static_cast<size_t>(
                                            landscape.activeLayer)];
                                    auto& material =
                                        *materialComponent->material;
                                    material.landscapeAlbedoMap =
                                        layer.albedoMap;
                                    material.landscapeOrmMap = layer.ormMap;
                                    material.landscapeNormalMap =
                                        layer.normalMap;
                                    material.landscapeAlbedoSrgb =
                                        layer.albedoSrgb;
                                    material.landscapeOrmSrgb = layer.ormSrgb;
                                    material.landscapeNormalSrgb =
                                        layer.normalSrgb;
                                    material.landscapeBlend =
                                        layer.albedoMap || layer.ormMap ||
                                        layer.normalMap;
                                }
                            }
                            // Bake after the stroke so sources are decoded at
                            // most once per mouse release, even if the cursor
                            // leaves the viewport.
                            m_pendingLandscapeBake = sel.Handle();
                        }
                    } else if (paintMode &&
                        (leftClicked ||
                         (m_wasSculpting &&
                          ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
                        const float minStep =
                            std::max(0.05f, m_editor.brushRadius * 0.2f);
                        const float dx =
                            hit.localPosition.x - m_lastPaintLocal.x;
                        const float dz =
                            hit.localPosition.z - m_lastPaintLocal.z;
                        if (!m_paintStampValid ||
                            dx * dx + dz * dz >= minStep * minStep) {
                            PaintVegetationAt(sel, landscape,
                                              hit.localPosition,
                                              paintGrass);
                            m_lastPaintLocal = hit.localPosition;
                            m_paintStampValid = true;
                        }
                    } else if (m_wasSculpting &&
                               ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        Galeshi::LandscapeBrushSettings settings;
                        switch (m_editor.toolMode) {
                            case Galeshi::EditorState::ToolMode::Raise:
                                settings.tool = Galeshi::LandscapeBrush::Raise; break;
                            case Galeshi::EditorState::ToolMode::Lower:
                                settings.tool = Galeshi::LandscapeBrush::Lower; break;
                            case Galeshi::EditorState::ToolMode::Smooth:
                                settings.tool = Galeshi::LandscapeBrush::Smooth; break;
                            case Galeshi::EditorState::ToolMode::Flatten:
                                settings.tool = Galeshi::LandscapeBrush::Flatten; break;
                            case Galeshi::EditorState::ToolMode::Noise:
                                settings.tool = Galeshi::LandscapeBrush::Noise; break;
                            case Galeshi::EditorState::ToolMode::Terrace:
                                settings.tool = Galeshi::LandscapeBrush::Terrace; break;
                            case Galeshi::EditorState::ToolMode::Sharpen:
                                settings.tool = Galeshi::LandscapeBrush::Sharpen; break;
                            default: break;
                        }
                        settings.radius = m_editor.brushRadius;
                        settings.strength = m_editor.brushStrength;
                        settings.falloff = m_editor.brushFalloff;
                        settings.flattenHeight = m_editor.flattenHeight;
                        settings.noiseFrequency = m_editor.noiseFrequency;
                        settings.terraceStep = m_editor.terraceStep;
                        if (Galeshi::LandscapeMesh::ApplyBrush(
                                landscape, settings,
                                hit.localPosition.x,
                                hit.localPosition.z,
                                ImGui::GetIO().DeltaTime) > 0) {
                            if (sel.HasComponent<Galeshi::MeshComponent>()) {
                                auto& meshComponent =
                                    sel.GetComponent<Galeshi::MeshComponent>();
                                if (meshComponent.mesh) {
                                    Galeshi::LandscapeMesh::UpdateMesh(
                                        landscape, *meshComponent.mesh);
                                }
                            }
                        }
                    }

                    if (m_wasSculpting &&
                        !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        m_wasSculpting = false;
                        m_paintStampValid = false;
                        if (!paintMode && !paintLandscape &&
                            sel.HasComponent<Galeshi::VegetationComponent>()) {
                            sel.GetComponent<Galeshi::VegetationComponent>().dirty = true;
                        }
                    }
                }

                if (m_wasSculpting &&
                    !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    m_wasSculpting = false;
                    m_paintStampValid = false;
                }
            } else if (clothPaintActive) {
                const ImVec2 paintMouse = ImGui::GetMousePos();
                const bool overImage =
                    ImGui::IsWindowHovered() &&
                    paintMouse.x >= imgMin.x && paintMouse.x <= imgMax.x &&
                    paintMouse.y >= imgMin.y && paintMouse.y <= imgMax.y;
                if (overImage) {
                    const float ndcX =
                        ((paintMouse.x - imgMin.x) / w) * 2.0f - 1.0f;
                    const float ndcY =
                        1.0f - ((paintMouse.y - imgMin.y) / h) * 2.0f;
                    const glm::mat4 invVP =
                        glm::inverse(m_camera.Projection() * m_camera.View());
                    glm::vec4 rayStart4 = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                    glm::vec4 rayEnd4 = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                    rayStart4 /= rayStart4.w;
                    rayEnd4 /= rayEnd4.w;
                    const glm::vec3 rayOrigin = glm::vec3(rayStart4);
                    const glm::vec3 rayDir =
                        glm::normalize(glm::vec3(rayEnd4 - rayStart4));

                    auto& meshComponent =
                        clothEntity.GetComponent<Galeshi::MeshComponent>();
                    auto& cloth =
                        clothEntity.GetComponent<Galeshi::SoftBodyComponent>();
                    if (meshComponent.mesh) {
                        const glm::mat4 world =
                            EntityWorldMatrix(clothEntity.Handle());
                        const glm::mat4 inverseWorld = glm::inverse(world);
                        const glm::vec3 localOrigin = glm::vec3(
                            inverseWorld * glm::vec4(rayOrigin, 1.0f));
                        const glm::vec3 localDirection = glm::normalize(
                            glm::vec3(inverseWorld * glm::vec4(rayDir, 0.0f)));
                    float hitDistance = 0.0f;
                    glm::vec3 localHit(0.0f);
                    if (RaycastMeshLocal(*meshComponent.mesh, localOrigin,
                                         localDirection, hitDistance,
                                         localHit)) {
                        const auto& vertices = meshComponent.mesh->Vertices();
                        if (cloth.paintWeights.size() != vertices.size()) {
                            cloth.paintWeights.resize(vertices.size(), 0.0f);
                        }
                        const glm::vec3 scaleX(world[0]);
                        const glm::vec3 scaleY(world[1]);
                        const glm::vec3 scaleZ(world[2]);
                        const float worldScale = std::max(
                            0.001f, std::max({glm::length(scaleX),
                                              glm::length(scaleY),
                                              glm::length(scaleZ)}));
                        const float localRadius =
                            m_editor.brushRadius / worldScale;
                        const bool erase = ImGui::GetIO().KeyAlt;
                        const bool leftClicked =
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                        if (leftClicked) {
                            PushHistory();
                            m_wasSculpting = true;
                            m_paintStampValid = false;
                        }
                        if (m_wasSculpting &&
                            ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                            const float minStep =
                                std::max(0.01f, localRadius * 0.2f);
                            const glm::vec3 delta = localHit - m_lastPaintLocal;
                            if (!m_paintStampValid ||
                                glm::dot(delta, delta) >= minStep * minStep) {
                                for (size_t i = 0; i < vertices.size(); ++i) {
                                    const float distance = glm::length(
                                        vertices[i].position - localHit);
                                    if (distance > localRadius) continue;
                                    if (erase) {
                                        cloth.paintWeights[i] = 0.0f;
                                    } else {
                                        const float falloff = std::clamp(
                                            1.0f - distance / localRadius,
                                            0.0f, 1.0f);
                                        cloth.paintWeights[i] = std::max(
                                            cloth.paintWeights[i],
                                            std::clamp(m_editor.brushStrength *
                                                           falloff,
                                                       0.0f, 1.0f));
                                    }
                                }
                                m_lastPaintLocal = localHit;
                                m_paintStampValid = true;
                            }
                        }
                    }
                    }
                }
                if (m_wasSculpting &&
                    !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    // A model/skeletal root redirects to a descendant mesh;
                    // rebuild that mesh, not the selected authoring root.
                    RebuildCloth(clothEntity);
                    m_wasSculpting = false;
                    m_paintStampValid = false;
                }
            } else {
                m_wasSculpting = false;
            }

            // World position of the selected object (walk up parents)
            glm::vec3 selWorldPos{0.0f};
            if (selHasTf) {
                selWorldPos = sel.GetComponent<Galeshi::TransformComponent>().position;
                entt::entity curr = sel.Handle();
                int __depth = 0; while (__depth++ < 128 && m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                    curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                    if (m_scene.Registry().all_of<Galeshi::TransformComponent>(curr))
                        selWorldPos += m_scene.Registry().get<Galeshi::TransformComponent>(curr).position;
                }
            }

            // Project selected object to screen (center hit-test)
            glm::vec2 selScreen{0.0f, 0.0f};
            bool selOnScreen = false;
            if (selHasTf) {
                glm::vec4 clip = m_camera.Projection() * m_camera.View() * glm::vec4(selWorldPos, 1.0f);
                if (clip.w > 0.0001f) {
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    selScreen.x = imgMin.x + (ndc.x * 0.5f + 0.5f) * static_cast<float>(w);
                    selScreen.y = imgMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(h);
                    selOnScreen = true;
                }
            }

            // --- Gizmo: apply translation/rotation/scale to the ECS. ---
            if (selHasTf && !landscapeSculptActive && !clothPaintActive &&
                !selectedIsUICanvas &&
                !uiOverlayConsumed && !m_editor.isPlaying) {
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(imgMin.x, imgMin.y, w, h);
                ImGuizmo::SetGizmoSizeClipSpace(0.12f);
                glm::mat4 visView  = m_camera.View();
                glm::mat4 visProj  = m_camera.Projection();

                // Compute the world matrix and the parent world matrix
                // separately so the manipulated world transform can be
                // converted back into the entity's local transform.
                glm::mat4 worldMatrix(1.0f);
                glm::mat4 parentWorld(1.0f);
                entt::entity curr = sel.Handle();
                std::vector<entt::entity> chain;
                int depth = 0;
                while (curr != entt::null && depth++ < 128) {
                    chain.push_back(curr);
                    if (m_scene.Registry().all_of<Galeshi::ParentComponent>(curr))
                        curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                    else
                        break;
                }
                // chain[0] is the selected entity, the rest are ancestors.
                for (size_t ci = chain.size(); ci-- > 0; ) {
                    if (!m_scene.Registry().all_of<Galeshi::TransformComponent>(chain[ci]))
                        continue;
                    const glm::mat4 local =
                        m_scene.Registry().get<Galeshi::TransformComponent>(chain[ci]).LocalMatrix();
                    worldMatrix = worldMatrix * local;
                    if (ci > 0) parentWorld = parentWorld * local;
                }

                ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
                switch (m_editor.Mode()) {
                    case Galeshi::GizmoMode::Rotate: operation = ImGuizmo::ROTATE; break;
                    case Galeshi::GizmoMode::Scale:  operation = ImGuizmo::SCALE;  break;
                    case Galeshi::GizmoMode::Translate:
                    default:                         operation = ImGuizmo::TRANSLATE; break;
                }

                glm::mat4 visModel = worldMatrix;
                const bool manipulated = ImGuizmo::Manipulate(
                    glm::value_ptr(visView), glm::value_ptr(visProj),
                    operation, ImGuizmo::WORLD, glm::value_ptr(visModel));
                const bool usingGizmo = manipulated || ImGuizmo::IsUsing();

                if (usingGizmo) {
                    // Snapshot the pre-drag state on the first frame that
                    // actually changes the matrix. A plain click on a handle
                    // must not create a redundant undo entry.
                    if (manipulated && !m_wasUsingGizmo) {
                        PushHistory();
                        m_wasUsingGizmo = true;
                    }

                    if (manipulated) {
                        glm::vec3 localTranslation, localScale, localSkew;
                        glm::vec4 localPerspective;
                        glm::quat localRotation;
                        const glm::mat4 localMatrix = glm::inverse(parentWorld) * visModel;
                        if (glm::decompose(localMatrix, localScale, localRotation,
                                           localTranslation, localSkew, localPerspective)) {
                            auto& tcGizmo = sel.GetComponent<Galeshi::TransformComponent>();
                            tcGizmo.position = localTranslation;
                            tcGizmo.rotation = glm::normalize(localRotation);
                            tcGizmo.scale = localScale;

                            if (sel.HasComponent<Galeshi::RigidBodyComponent>()) {
                                auto& rb = sel.GetComponent<Galeshi::RigidBodyComponent>();
                                glm::vec3 worldTranslation, worldScale, worldSkew;
                                glm::vec4 worldPerspective;
                                glm::quat worldRotation;
                                if (glm::decompose(visModel, worldScale, worldRotation,
                                                   worldTranslation, worldSkew, worldPerspective)) {
                                    if (rb.bodyId != 0xFFFFFFFF) {
                                        m_physics->SetBodyTransform(rb.bodyId, worldTranslation,
                                                                    glm::normalize(worldRotation));
                                    }
                                }
                            }
                        }
                    }
                } else if (m_wasUsingGizmo) {
                    // Store the final transform so Redo can restore the drag.
                    PushHistory();
                    m_wasUsingGizmo = false;
                }
            } else {
                m_wasUsingGizmo = false;
            }

            const float centerRadiusPx = 24.0f;
            ImVec2 mouse = ImGui::GetMousePos();
            const bool overCenter = selOnScreen &&
                std::hypot(mouse.x - selScreen.x, mouse.y - selScreen.y) < centerRadiusPx;

            // Begin drag on LMB down over center
            if (!landscapeSculptActive && !clothPaintActive &&
                !selectedIsUICanvas &&
                !uiOverlayConsumed && !m_editor.isPlaying &&
                ImGui::IsItemHovered() && !m_isDragging &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) && overCenter &&
                !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
                PushHistory();
                m_isDragging = true;
                m_dragStartMouse = { mouse.x, mouse.y };
                m_dragStartObjWorld = selWorldPos;
            }

            // Perform drag while LMB held
            if (m_isDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && selHasTf &&
                !m_editor.isPlaying && !ImGuizmo::IsUsing()) {
                const float dx = mouse.x - m_dragStartMouse.x;
                const float dy = mouse.y - m_dragStartMouse.y;

                glm::vec3 camPos = m_camera.Position();
                float dist = glm::length(selWorldPos - camPos);
                dist = std::max(dist, 0.5f);
                const float fovY = glm::radians(m_preferences.cameraFovDegrees);  // matches SetPerspective
                const float worldPerPx = (2.0f * std::tan(fovY * 0.5f) * dist) /
                                         static_cast<float>(h);

                glm::mat4 v = m_camera.View();
                glm::vec3 right = glm::vec3(v[0]);  // view column 0 = camera right (world)
                glm::vec3 up    = glm::vec3(v[1]);  // view column 1 = camera up    (world)

                glm::vec3 deltaWorld = right * ( dx * worldPerPx)
                                     + up    * (-dy * worldPerPx);
                glm::vec3 newWorld = m_dragStartObjWorld + deltaWorld;

                // Convert to local if the object has a parent
                glm::vec3 parentOffset{0.0f};
                {
                    entt::entity curr = sel.Handle();
                    int __depth = 0; while (__depth++ < 128 && m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                        curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                        if (m_scene.Registry().all_of<Galeshi::TransformComponent>(curr))
                            parentOffset += m_scene.Registry().get<Galeshi::TransformComponent>(curr).position;
                    }
                }

                auto& tcSel = sel.GetComponent<Galeshi::TransformComponent>();
                tcSel.position = newWorld - parentOffset;

                if (sel.HasComponent<Galeshi::RigidBodyComponent>()) {
                    auto& rb = sel.GetComponent<Galeshi::RigidBodyComponent>();
                    if (rb.bodyId != 0xFFFFFFFF)
                        m_physics->SetBodyTransform(rb.bodyId, tcSel.position, tcSel.rotation);
                }
            }

            if (m_isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                m_isDragging = false;
                PushHistory();
            }

            // Center hint circle (drag affordance)
            if (selOnScreen && !selectedIsUICanvas && !m_editor.isPlaying) {
                ImU32 hintColor = overCenter ? IM_COL32(255, 220, 40, 220)
                                             : IM_COL32(255, 255, 255, 110);
                ImGui::GetWindowDrawList()->AddCircle(
                    ImVec2(selScreen.x, selScreen.y),
                    centerRadiusPx * 0.5f, hintColor, 24, 2.0f);
            }

            if (!landscapeSculptActive && !clothPaintActive &&
                !uiOverlayConsumed &&
                ImGui::IsItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_isDragging &&
                !overCenter && !ImGuizmo::IsOver()) {
                ImVec2 mouse = ImGui::GetMousePos();
                float mx = mouse.x - imgMin.x;
                float my = mouse.y - imgMin.y;
                float ndcX = (mx / w) * 2.0f - 1.0f;
                float ndcY = 1.0f - (my / h) * 2.0f;

                glm::mat4 invVP = glm::inverse(m_camera.Projection() * m_camera.View());
                glm::vec4 rayStart = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                glm::vec4 rayEnd   = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
                rayStart /= rayStart.w; rayEnd /= rayEnd.w;

                glm::vec3 rayDir = glm::normalize(glm::vec3(rayEnd - rayStart));
                glm::vec3 rayOrigin = glm::vec3(rayStart);

                Galeshi::Entity bestHit;
                float bestDist = 99999.0f;

                auto view = m_scene.Registry().view<Galeshi::TransformComponent>();
                for (auto e : view) {
                    // UI canvases are viewport overlays, not 3D scene geometry.
                    if (m_scene.Registry().all_of<Galeshi::UICanvasComponent>(e))
                        continue;
                    // Invisible standalone collision shapes must not be
                    // selectable during Play.
                    if (m_editor.isPlaying &&
                        m_scene.Registry().all_of<Galeshi::ColliderComponent>(e) &&
                        !m_scene.Registry().all_of<Galeshi::MeshComponent>(e)) {
                        continue;
                    }
                    auto& tc = view.get<Galeshi::TransformComponent>(e);
                    entt::entity curr = e;
                    int __depth = 0;
                    while (__depth++ < 128 &&
                           m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                        curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                    }

                    glm::vec3 boundsCenter, boundsExtents;
                    if (!MeshEntityWorldBounds(e, boundsCenter, boundsExtents)) {
                        // Standalone colliders have their own precise test below.
                        if (m_scene.Registry().all_of<Galeshi::ColliderComponent>(e))
                            continue;
                        boundsCenter = glm::vec3(EntityWorldMatrix(e)[3]);
                        boundsExtents = glm::vec3(0.5f);
                    }

                    const glm::vec3 minB = boundsCenter - boundsExtents;
                    const glm::vec3 maxB = boundsCenter + boundsExtents;
                    float tmin = -std::numeric_limits<float>::infinity();
                    float tmax = std::numeric_limits<float>::infinity();
                    bool miss = false;
                    for (int axis = 0; axis < 3 && !miss; ++axis) {
                        if (std::abs(rayDir[axis]) < 1e-8f) {
                            if (rayOrigin[axis] < minB[axis] ||
                                rayOrigin[axis] > maxB[axis]) {
                                miss = true;
                            }
                        } else {
                            float t1 = (minB[axis] - rayOrigin[axis]) /
                                       rayDir[axis];
                            float t2 = (maxB[axis] - rayOrigin[axis]) /
                                       rayDir[axis];
                            if (t1 > t2) std::swap(t1, t2);
                            tmin = std::max(tmin, t1);
                            tmax = std::min(tmax, t2);
                            if (tmin > tmax) miss = true;
                        }
                    }
                    if (miss || tmax < 0.0f) continue;

                    const float hitDistance = tmin > 0.0f ? tmin : tmax;
                    if (hitDistance < bestDist) {
                        bestDist = hitDistance;
                        bestHit = Galeshi::Entity(curr, &m_scene); // Select the root, not the bone.
                    }
                }

                // Collision shapes have no mesh, so pick their authored volume
                // directly. This also makes standalone shapes selectable.
                auto colliderPickView = m_scene.Registry()
                    .view<Galeshi::ColliderComponent, Galeshi::TransformComponent>();
                for (auto e : colliderPickView) {
                    if (m_editor.isPlaying) continue;
                    const auto& collider =
                        colliderPickView.get<Galeshi::ColliderComponent>(e);
                    if (!collider.visibleInEditor) continue;

                    const glm::mat4 world = EntityWorldMatrix(e);
                    const glm::mat4 invWorld = glm::inverse(world);
                    const glm::vec3 localOrigin =
                        glm::vec3(invWorld * glm::vec4(rayOrigin, 1.0f));
                    const glm::vec3 localDir =
                        glm::vec3(invWorld * glm::vec4(rayDir, 0.0f));

                    glm::vec3 half;
                    switch (collider.shapeType) {
                        case Galeshi::ColliderShapeType::Sphere:
                            half = glm::vec3(collider.radius);
                            break;
                        case Galeshi::ColliderShapeType::Capsule:
                            half = glm::vec3(collider.radius,
                                             collider.halfHeight + collider.radius,
                                             collider.radius);
                            break;
                        case Galeshi::ColliderShapeType::Box:
                        default:
                            half = collider.halfExtents;
                            break;
                    }
                    const glm::vec3 minB = collider.offset - half;
                    const glm::vec3 maxB = collider.offset + half;

                    float tmin = -std::numeric_limits<float>::infinity();
                    float tmax =  std::numeric_limits<float>::infinity();
                    bool miss = false;
                    for (int axis = 0; axis < 3 && !miss; ++axis) {
                        if (std::abs(localDir[axis]) < 1e-8f) {
                            if (localOrigin[axis] < minB[axis] ||
                                localOrigin[axis] > maxB[axis]) {
                                miss = true;
                            }
                        } else {
                            float t1 = (minB[axis] - localOrigin[axis]) / localDir[axis];
                            float t2 = (maxB[axis] - localOrigin[axis]) / localDir[axis];
                            if (t1 > t2) std::swap(t1, t2);
                            tmin = std::max(tmin, t1);
                            tmax = std::min(tmax, t2);
                            if (tmin > tmax) miss = true;
                        }
                    }
                    if (miss || tmax < 0.0f) continue;

                    const float tHit = tmin > 0.0f ? tmin : tmax;
                    const glm::vec3 localHit = localOrigin + localDir * tHit;
                    const glm::vec3 worldHit =
                        glm::vec3(world * glm::vec4(localHit, 1.0f));
                    const float worldDist = glm::length(worldHit - rayOrigin);
                    if (worldDist < bestDist) {
                        bestDist = worldDist;
                        bestHit = Galeshi::Entity(e, &m_scene);
                    }
                }

                if (bestHit) m_editor.Select(bestHit);
                else m_editor.ClearSelection();


            }

                    }
        ImGui::End();
    }

