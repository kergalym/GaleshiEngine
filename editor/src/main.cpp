#include "GaleshiEngine/Core/Application.hpp"
#include "GaleshiEngine/Renderer/Renderer.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Renderer/RenderSystem.hpp"
#include "GaleshiEngine/Renderer/Framebuffer.hpp"
#include "GaleshiEngine/Renderer/RayPick.hpp"
#include "GaleshiEngine/Renderer/DebugRenderer.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/SceneSerializer.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include "GaleshiEngine/Editor/EditorState.hpp"
#include "GaleshiEngine/Editor/ContentBrowser.hpp"
#include "GaleshiEngine/Editor/FilePickerDialog.hpp"
#include "GaleshiEngine/Physics/PhysicsSystem.hpp"
#include "GaleshiEngine/AI/NavigationSystem.hpp"
#include "GaleshiEngine/AI/AISystem.hpp"
#include "GaleshiEngine/Audio/AudioSystem.hpp"
#include "GaleshiEngine/Scripting/ScriptEngine.hpp"
#include "GaleshiEngine/Renderer/PostProcessManager.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/spdlog.h>
#include <ImGuizmo.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <bx/math.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <vector>

class GaleshiEditorApp final : public Galeshi::Application {
    Galeshi::AudioSystem m_audio;
    Galeshi::ScriptEngine m_scripts;
    Galeshi::PostProcessManager m_postProcess;

public:
    explicit GaleshiEditorApp(bool debugMode)
        : Galeshi::Application({
              .name = "GaleshiEditor", .width = 1600, .height = 900, .editor = true, .debug = debugMode}) {}

protected:
    // -------------------------------------------------------------------
    // LIFECYCLE
    // -------------------------------------------------------------------
    void OnStart() override {
        // --- Auto-inserted subsystem init ---
        spdlog::info("[Subsystem] m_audio.Init() begin");
        if (!m_audio.Init()) spdlog::error("[Subsystem] m_audio.Init() failed");
        else spdlog::info("[Subsystem] m_audio.Init() done");
        m_physics = std::make_unique<Galeshi::EnginePhysicsSystem>();
        spdlog::info("[Subsystem] m_physics->Init() begin");
        if (!m_physics->Init()) spdlog::error("[Subsystem] m_physics->Init() failed");
        else spdlog::info("[Subsystem] m_physics->Init() done");
        m_scripts.BindFunctions();
        m_scripts.BindECS(m_scene);
        m_scripts.BindAudio([this](const std::string& p){ m_audio.PlaySound(p); });
        m_scripts.BindPhysics([this](const glm::vec3& he, const glm::vec3& pos, entt::entity e) {
            return m_physics->CreateBox(he, pos, e);
        });
        spdlog::info("[Subsystem] m_scripts.RunScript(init.lua) begin");
        if (!m_scripts.RunScript("assets/scripts/init.lua"))
            spdlog::error("[Subsystem] m_scripts.RunScript(init.lua) failed");
        else
            spdlog::info("[Subsystem] m_scripts.RunScript(init.lua) done");
        // --- End auto-inserted ---

        m_assets  = std::make_unique<Galeshi::AssetManager>();
        m_physics = std::make_unique<Galeshi::EnginePhysicsSystem>();
        m_physics->Init();
        m_physics->SetOnContactCallback([this](entt::entity e1, entt::entity e2) {
            // Simple demo: play beep on any collision involving an AudioSource entity
            auto& reg = m_scene.Registry();
            for (entt::entity e : { e1, e2 }) {
                if (reg.valid(e) && reg.all_of<Galeshi::AudioSourceComponent>(e)) {
                    auto& a = reg.get<Galeshi::AudioSourceComponent>(e);
                    if (!a.soundPath.empty()) {
                        m_audio.PlaySound(a.soundPath, a.loop);
                        a.playing = true;
                    }
                }
            }
        });
        m_navSystem = std::make_unique<Galeshi::NavigationSystem>();
        m_navSystem->Init();
        m_debug = std::make_unique<Galeshi::DebugRenderer>();
        m_debug->Init();
        
        m_render  = std::make_unique<Galeshi::RenderSystem>();
        m_render->Init(m_assets.get());
        m_postProcess.Init();
        m_render->SetDeferredEnabled(true);
        // SSAO/SSR включим на 2-м кадре — чтобы первый кадр прошёл
        // без sampling render target (иначе Vulkan layout mismatch).
        m_pendingEffectsEnable = true;
        for (int i = 0; i < 3; ++i) {
            m_shadowMaps[i] = bgfx::createTexture2D(2048, 2048, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            m_shadowFbos[i] = bgfx::createFrameBuffer(1, &m_shadowMaps[i], false);
        }

        m_browser    = std::make_unique<Galeshi::ContentBrowser>("assets");
        m_serializer = std::make_unique<Galeshi::SceneSerializer>(m_scene, *m_assets);

        // Авто-загрузка сцены, если есть. Иначе — тестовая сцена.
        namespace fs = std::filesystem;
        if (fs::exists("scene.json")) {
            if (m_serializer->Load("scene.json")) {
                spdlog::info("[Editor] Scene auto-loaded from scene.json");
                // Fix upward-pointing sun for correct shadows
                auto lview = m_scene.Registry().view<Galeshi::DirectionalLightComponent>();
                for (auto e : lview) {
                    auto& dl = lview.get<Galeshi::DirectionalLightComponent>(e);
                    if (dl.direction.y > 0.0f || true) {
                        dl.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.5f));
                        spdlog::warn("[Editor] Sun was pointing up, flipped downwards for shadows");
                    }
                }
            } else {
                BuildTestScene();
            }
        } else {
            BuildTestScene();
        }

        // Первый снапшот для Undo/Redo
        m_history.clear();
        m_historyIndex = -1;
        PushHistory();

        m_camera.SetPerspective(glm::radians(60.0f),
            static_cast<float>(GetRenderer()->Width()) / static_cast<float>(GetRenderer()->Height()),
            0.1f, 1000.0f);
    }

    void OnUpdate(float dt) override {
        spdlog::trace("[Main] OnUpdate");
        // --- Deferred effect enable (frame 2+) ---
        m_renderFrameCounter++;
        if (m_pendingEffectsEnable && m_renderFrameCounter >= 2) {
            m_postProcess.SetSsaoEnabled(true);
            m_postProcess.SetSsrEnabled(true);
            m_pendingEffectsEnable = false;
            spdlog::info("[Effects] SSAO and SSR enabled on frame {}", m_renderFrameCounter);
        }

        // --- Auto-inserted physics update ---
        if (m_physics) {
            m_physics->Update(m_scene, dt);
            m_physics->SyncTransformsToECS(m_scene);
        }

        // Audio: auto-play & state sync
        {
            auto view = m_scene.Registry().view<Galeshi::AudioSourceComponent>();
            for (auto e : view) {
                auto& a = view.get<Galeshi::AudioSourceComponent>(e);
                bool playingNow = !a.soundPath.empty() && m_audio.IsSoundPlaying(a.soundPath);
                if (a.playing != playingNow) a.playing = playingNow;
                if (a.autoplay && !a.playing && !a.soundPath.empty()) {
                    m_audio.PlaySound(a.soundPath, a.loop);
                    a.playing = true;
                }
            }
        }
        // --- End auto-inserted ---

        m_time += dt;
        Galeshi::AISystem::Update(m_scene, dt);

        // Анимация скелета
        auto animView = m_scene.Registry().view<Galeshi::SkeletalComponent>();
        for (auto h : animView) {
            auto& skel = animView.get<Galeshi::SkeletalComponent>(h);
            if (skel.isAnimated) {
                skel.currentTime += dt * skel.animationSpeed;
                if (skel.currentTime > 10.0f) skel.currentTime = 0.0f;
            }
        }

        // Логика камеры
        const bool cameraControllable = m_viewportHovered && !m_editor.GizmoLocked() && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver();
        
        // Зум
        if (m_viewportHovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                m_editor.camRadius -= wheel * 0.5f;
                m_editor.camRadius = glm::clamp(m_editor.camRadius, 0.5f, 200.0f);
            }
        }

        // Вращение ПКМ
        if (cameraControllable && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            auto d = ImGui::GetIO().MouseDelta;
            m_editor.camYaw   -= d.x * 0.01f;
            m_editor.camPitch -= d.y * 0.01f;
            m_editor.camPitch  = glm::clamp(m_editor.camPitch, -1.5f, 1.5f);
        }

        // Перемещение стрелками
        if (cameraControllable && !ImGui::GetIO().WantTextInput) {
            float panSpeed = 10.0f * dt;
            float yaw = m_editor.camYaw;
            glm::vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
            glm::vec3 right(forward.z, 0.0f, -forward.x);

            if (ImGui::IsKeyDown(ImGuiKey_UpArrow))    m_editor.camTarget -= forward * panSpeed;
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow))  m_editor.camTarget += forward * panSpeed;
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))  m_editor.camTarget -= right * panSpeed;
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) m_editor.camTarget += right * panSpeed;
            
        }

        const float aspect = m_viewportH ? static_cast<float>(m_viewportW) / m_viewportH : 16.0f / 9.0f;
        m_camera.SetPerspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        m_camera.Orbit(m_editor.camTarget, m_editor.camYaw, m_editor.camPitch, m_editor.camRadius);
    }

    void OnRender() override {} // Всё во Viewport

    void OnImGui() override {
        DrawMainMenuBar();

        // Hotkeys
        if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) m_editor.SetMode(Galeshi::GizmoMode::Translate);
            if (ImGui::IsKeyPressed(ImGuiKey_E)) m_editor.SetMode(Galeshi::GizmoMode::Rotate);
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_editor.SetMode(Galeshi::GizmoMode::Scale);
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelected();
            if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
                spdlog::info("[Lua] Hot-reload init.lua");
                m_scripts.HotReload("assets/scripts/init.lua");
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                m_audio.PlaySound("assets/sounds/beep.wav");
            }

            if (ImGui::GetIO().KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_I)) OpenFileDialog();
                if (ImGui::IsKeyPressed(ImGuiKey_S)) SaveScene();
                if (ImGui::IsKeyPressed(ImGuiKey_O)) LoadScene();
                if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
                if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
            }
        }

        DrawDockspace();
        DrawViewport();
        DrawOutliner();
        DrawDetails();
        DrawContentBrowser();

        // File Picker (модальный)
        
        // --- File pickers (модальные диалоги) ---
        std::string chosenOpen = m_filePicker.Draw();
        if (!chosenOpen.empty()) {
            SpawnFromModel(chosenOpen, m_editor.camTarget);
        }

        std::string chosenSave = m_filePickerSave.Draw();
        if (!chosenSave.empty()) {
            if (m_serializer) {
                if (m_serializer->Save(chosenSave)) {
                    spdlog::info("[Editor] Scene saved to {}", chosenSave);
                } else {
                    spdlog::error("[Editor] Failed to save scene to {}", chosenSave);
                }
            }
        }

        // Load Scene dialog (.json)
        std::string chosenLoad = m_scenePicker.Draw();
        if (!chosenLoad.empty()) {
            m_editor.ClearSelection();
            if (m_serializer->Load(chosenLoad)) {
                spdlog::info("[Editor] Scene loaded from {}", chosenLoad);
            } else {
                spdlog::error("[Editor] Failed to load scene: {}", chosenLoad);
            }
        }
    }

    void OnShutdown() override {
        spdlog::info("[Editor] OnShutdown: begin");

        // Автосохранение ОТКЛЮЧЕНО. Сцена сохраняется только вручную
        // через File -> Save Scene (Ctrl+S).

        // Выходим без очистки GPU (иначе NVIDIA deadlock)
        spdlog::info("[Editor] OnShutdown: exit (skip GPU cleanup)");
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }

private:
    // -------------------------------------------------------------------
    // UI: Menu Bar
    // -------------------------------------------------------------------
    void DrawMainMenuBar() {
        if (!ImGui::BeginMainMenuBar()) return;

        // Play/Stop
        if (ImGui::Button(m_editor.isPlaying ? "Stop" : "Play", ImVec2(60, 0))) {
            if (!m_editor.isPlaying) {
                PushHistory();
                m_editor.isPlaying = true;
            } else {
                m_editor.isPlaying = false;
                m_physics->Reset();
                if (m_historyIndex >= 0) m_serializer->LoadFromString(m_history[m_historyIndex]);
            }
        }
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();

                if (ImGui::BeginMenu("Post-Process")) {
            bool inv = m_postProcess.InvertEnabled();
            if (ImGui::MenuItem("Invert Colors", nullptr, &inv)) m_postProcess.SetInvertEnabled(inv);
            bool fxaa = m_postProcess.FxaaEnabled();
            if (ImGui::MenuItem("FXAA", nullptr, &fxaa)) m_postProcess.SetFxaaEnabled(fxaa);
            bool ssao = m_postProcess.SsaoEnabled();
            if (ImGui::MenuItem("SSAO", nullptr, &ssao)) m_postProcess.SetSsaoEnabled(ssao);
            bool smaa = m_postProcess.SmaaEnabled();
            if (ImGui::MenuItem("SMAA", nullptr, &smaa)) m_postProcess.SetSmaaEnabled(smaa);
            bool taa = m_postProcess.TaaEnabled();
            if (ImGui::MenuItem("TAA", nullptr, &taa)) m_postProcess.SetTaaEnabled(taa);
            bool ssr = m_postProcess.SsrEnabled();
            if (ImGui::MenuItem("SSR", nullptr, &ssr)) m_postProcess.SetSsrEnabled(ssr);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset All Effects")) {
                m_postProcess.SetInvertEnabled(false);
                m_postProcess.SetFxaaEnabled(false);
                m_postProcess.SetSsaoEnabled(true);
                m_postProcess.SetSsrEnabled(true);
            }
            ImGui::EndMenu();
        }
if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Import Model...", "Ctrl+I")) OpenFileDialog();
            if (ImGui::MenuItem("Load Scene", "Ctrl+O")) LoadScene();
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Scene")) {
                m_editor.ClearSelection();
                m_scene.Registry().clear();
                BuildTestScene();
                PushHistory();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) std::exit(0);
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) Redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Translate", "W")) m_editor.SetMode(Galeshi::GizmoMode::Translate);
            if (ImGui::MenuItem("Rotate",    "E")) m_editor.SetMode(Galeshi::GizmoMode::Rotate);
            if (ImGui::MenuItem("Scale",     "R")) m_editor.SetMode(Galeshi::GizmoMode::Scale);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Empty Entity"))
                m_editor.Select(m_scene.CreateEntity("New Entity"));
            
            if (ImGui::MenuItem("Landscape")) {
                auto e = m_scene.CreateEntity("Landscape");
                auto& lc = e.AddComponent<Galeshi::LandscapeComponent>();
                lc.sizeX = 100; lc.sizeZ = 100; lc.scale = 1.0f;
                lc.heights.resize(lc.sizeX * lc.sizeZ, 0.0f);
                
                auto mesh = std::make_shared<Galeshi::Mesh>();
                Galeshi::LandscapeMesh::Rebuild(lc, *mesh);
                
                e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "landscape" });
                auto mat = std::make_shared<Galeshi::Material>();
                mat->albedoFactor = {0.4f, 0.6f, 0.3f, 1.0f};
                mat->roughnessFactor = 0.9f;
                e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
                
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Cube")) {
                auto e = MakeCube("Cube", {0, 0, 0});
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Physics Sphere")) {
                auto e = SpawnPhysicsSphere("PhysicsSphere", {0, 5, 0}, 0.5f);
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Physics Capsule")) {
                auto e = SpawnPhysicsCapsule("PhysicsCapsule", {0, 5, 0}, 0.5f, 0.5f);
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Cloth (SoftBody)")) {
                auto e = m_scene.CreateEntity("Cloth");
                auto& sb = e.AddComponent<Galeshi::SoftBodyComponent>();
                sb.gridX = 10; sb.gridY = 10;
                sb.origin = {0, 5, 0};
                sb.softBodyId = m_physics->CreateCloth(sb.gridX, sb.gridY, sb.spacing, sb.origin, static_cast<entt::entity>(e));

                auto mesh = std::make_shared<Galeshi::Mesh>();
                std::vector<Galeshi::Vertex> verts;
                std::vector<uint32_t> idxs;
                for (int y = 0; y < sb.gridY; ++y) {
                    for (int x = 0; x < sb.gridX; ++x) {
                        Galeshi::Vertex v{};
                        v.position = sb.origin + glm::vec3(x * sb.spacing, 0, y * sb.spacing);
                        v.normal = glm::vec3(0, 1, 0);
                        v.uv = glm::vec2(float(x)/sb.gridX, float(y)/sb.gridY);
                        verts.push_back(v);
                    }
                }
                for (int y = 0; y < sb.gridY - 1; ++y) {
                    for (int x = 0; x < sb.gridX - 1; ++x) {
                        uint32_t i0 = y * sb.gridX + x;
                        uint32_t i1 = i0 + 1;
                        uint32_t i2 = i0 + sb.gridX;
                        uint32_t i3 = i2 + 1;
                        idxs.push_back(i0); idxs.push_back(i2); idxs.push_back(i1);
                        idxs.push_back(i1); idxs.push_back(i2); idxs.push_back(i3);
                    }
                }
                mesh->Upload(verts, idxs);
                e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "cloth" });
                auto mat = std::make_shared<Galeshi::Material>();
                mat->albedoFactor = {0.8f, 0.2f, 0.2f, 1.0f};
                e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });

                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Physics Cube")) {
                auto e = SpawnPhysicsCube("PhysicsCube", {0, 5, 0});
                e.AddComponent<Galeshi::BoxColliderComponent>();
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("AI Agent")) {
                    auto e = m_scene.CreateEntity("AI_Agent");
                    auto mesh = std::make_shared<Galeshi::Mesh>();
                    auto cube = m_assets->CreateCubeMesh();
                    // Переиспользуем куб
                    e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ cube, "", "cube" });
                    
                    auto mat = std::make_shared<Galeshi::Material>();
                    mat->albedoFactor = {0.2f, 0.8f, 0.2f, 1.0f}; // Зеленый
                    e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
                    
                    auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                    tc.position = {0, 1, 0}; // Над землей
                    tc.scale = {0.5f, 0.5f, 0.5f};
                    
                    e.AddComponent<Galeshi::NavAgentComponent>();
                    m_editor.Select(e);
                }
                if (ImGui::MenuItem("Build NavMesh")) {
                    spdlog::info("[Editor] Build NavMesh menu clicked");
                    if (!m_navSystem) {
                        spdlog::error("[Editor] m_navSystem is NULL");
                    } else {
                        if (m_navSystem->BuildFromScene(m_scene)) {
                            spdlog::info("[Editor] NavMesh generated");
                        } else {
                            spdlog::error("[Editor] BuildFromScene failed");
                        }
                    }
                    spdlog::info("[Editor] Build NavMesh menu done");
                }
                if (ImGui::MenuItem("Directional Light")) {
                auto e = m_scene.CreateEntity("DirectionalLight");
                auto& dl = e.HasComponent<Galeshi::DirectionalLightComponent>()
                           ? e.GetComponent<Galeshi::DirectionalLightComponent>()
                           : e.AddComponent<Galeshi::DirectionalLightComponent>();
                dl.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.5f));
                dl.intensity = 3.0f;
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Point Light")) {
                auto e = m_scene.CreateEntity("PointLight");
                auto& pl = e.HasComponent<Galeshi::PointLightComponent>()
                           ? e.GetComponent<Galeshi::PointLightComponent>()
                           : e.AddComponent<Galeshi::PointLightComponent>();
                auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                tc.position = {0, 2, 0};
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Spot Light")) {
                auto e = m_scene.CreateEntity("SpotLight");
                auto& sl = e.HasComponent<Galeshi::SpotLightComponent>()
                           ? e.GetComponent<Galeshi::SpotLightComponent>()
                           : e.AddComponent<Galeshi::SpotLightComponent>();
                auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                tc.position = {0, 3, 0};
                m_editor.Select(e);
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Stats")) {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Entities: %d", (int)m_scene.AllEntities().size());
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    // -------------------------------------------------------------------
    // Undo / Redo
    // -------------------------------------------------------------------
    void PushHistory() {
        if (m_isRestoringHistory) return;
        std::string snap = m_serializer->SaveToString();
        if (m_historyIndex >= 0 && m_history[m_historyIndex] == snap) return;

        m_history.resize(m_historyIndex + 1);
        m_history.push_back(snap);
        m_historyIndex = (int)m_history.size() - 1;

        if (m_history.size() > 50) {
            m_history.erase(m_history.begin());
            m_historyIndex--;
        }
    }

    void Undo() {
        if (m_historyIndex <= 0) return;
        m_historyIndex--;
        m_isRestoringHistory = true;
        m_editor.ClearSelection();
        m_serializer->LoadFromString(m_history[m_historyIndex]);
        m_isRestoringHistory = false;
    }

    void Redo() {
        if (m_historyIndex + 1 >= (int)m_history.size()) return;
        m_historyIndex++;
        m_isRestoringHistory = true;
        m_editor.ClearSelection();
        m_serializer->LoadFromString(m_history[m_historyIndex]);
        m_isRestoringHistory = false;
    }

    // -------------------------------------------------------------------
    // Save / Load / Delete
    // -------------------------------------------------------------------
    void SaveScene() {
        const char* home = std::getenv("HOME");
        std::string start = home ? home : "/";
        m_filePickerSave.SaveAs(start, "scene.json", "Save Scene", { ".json" });
    }
    
    void LoadScene() {
        // Load Scene -> модальный диалог для .json файлов
        const char* home = std::getenv("HOME");
        std::string start = home ? home : "/";
        m_scenePicker.Open(start, "Load Scene", { ".json" });
    }

    void DeleteSelected() {
        if (!m_editor.HasSelection()) return;
        PushHistory();
        auto entities = m_editor.SelectedAll(&m_scene);
        for (auto e : entities) m_scene.DestroyEntity(e);
        m_editor.ClearSelection();
    }

    // -------------------------------------------------------------------
    // Dockspace
    // -------------------------------------------------------------------
    void DrawDockspace() {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        ImGui::Begin("DockSpace", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

        ImGui::PopStyleVar(3);

        const ImGuiID dockspaceId = ImGui::GetID("GaleshiDockspace");
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, vp->Size);

            ImGuiID center = dockspaceId;
            ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
            ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,  0.30f, nullptr, &center);
            ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.20f, nullptr, &center);

            ImGui::DockBuilderDockWindow("Viewport",        center);
            ImGui::DockBuilderDockWindow("World Outliner",  left);
            ImGui::DockBuilderDockWindow("Details",         right);
            ImGui::DockBuilderDockWindow("Content Browser", bottom);
            ImGui::DockBuilderFinish(dockspaceId);
        }
        ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
        ImGui::End();
    }

    // -------------------------------------------------------------------
    // Viewport & Picking & Sculpting
    // -------------------------------------------------------------------
    void DrawViewport() {
        spdlog::trace("[Main] DrawViewport");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport");
        m_viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        ImGui::PopStyleVar();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const uint16_t w = static_cast<uint16_t>(avail.x);
        const uint16_t h = static_cast<uint16_t>(avail.y);

        if (w > 0 && h > 0) {
            if (!m_framebuffer || m_framebuffer->Width() != w || m_framebuffer->Height() != h) {
                if (!m_framebuffer) m_framebuffer = std::make_unique<Galeshi::Framebuffer>();
                m_framebuffer->Resize(w, h);
            }
        }

        m_viewportW = w;
        m_viewportH = h;

        if (m_framebuffer && m_framebuffer->Valid()) {
            
            // --- SHADOW PASS ---
            auto lview = m_scene.Registry().view<Galeshi::DirectionalLightComponent>();
            glm::mat4 lightMtcs[3];
            if (!lview.empty()) {
                auto lh = *lview.begin();
                auto& dl = lview.get<Galeshi::DirectionalLightComponent>(lh);
                glm::vec3 center = m_editor.camTarget;
                float extents[3] = { 15.0f, 40.0f, 100.0f };
                for (int i = 0; i < 3; ++i) {
                    glm::vec3 eye = center - dl.direction * (extents[i] * 2.0f);
                    glm::mat4 lightView = glm::lookAt(eye, center, glm::vec3(0,1,0));
                    glm::mat4 lightProj;
                    bx::mtxOrtho(glm::value_ptr(lightProj), -extents[i], extents[i], -extents[i], extents[i], 0.1f, 400.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
                    lightMtcs[i] = lightProj * lightView;
                    m_render->RenderShadowMap(m_scene, lightMtcs[i], 6 + i, m_shadowFbos[i]);
                }
            } else {
                for (int i = 0; i < 3; ++i) {
                    glm::mat4 lightView = glm::lookAt(glm::vec3(0,20,0), glm::vec3(0), glm::vec3(0,1,0));
                    glm::mat4 lightProj;
                    bx::mtxOrtho(glm::value_ptr(lightProj), -15.0f, 15.0f, -15.0f, 15.0f, 0.1f, 50.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
                    lightMtcs[i] = lightProj * lightView;
                    m_render->RenderShadowMap(m_scene, lightMtcs[i], 6 + i, m_shadowFbos[i]);
                }
            }

bgfx::setViewFrameBuffer(1, m_framebuffer->Handle());
            m_render->Render(m_scene, m_camera, 1, w, h);

            if (m_render->DeferredEnabled()) {
                glm::vec3 ldir{-0.4f,-0.8f,-0.5f}, lcol{1.0f};
                float lint = 1.2f;
                auto lview = m_scene.Registry().view<Galeshi::DirectionalLightComponent>();
                if (!lview.empty()) {
                    auto h = *lview.begin();
                    ldir = lview.get<Galeshi::DirectionalLightComponent>(h).direction;
                    lcol = lview.get<Galeshi::DirectionalLightComponent>(h).color;
                    lint = lview.get<Galeshi::DirectionalLightComponent>(h).intensity;
                }
                glm::vec3 cp = m_camera.Position();
                float camPos[4] = {cp.x, cp.y, cp.z, 1.0f};
                float lDirA[4]  = {ldir.x, ldir.y, ldir.z, 0.0f};
                float lColA[4]  = {lcol.r, lcol.g, lcol.b, lint};
                
                float plPosA[8][4] = {};
                float plColA[8][4] = {};
                int plCount = 0;
                auto plview = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::PointLightComponent>();
                for (auto h : plview) {
                    if (plCount >= 8) break;
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
                    slDir[slCount][3] = cos(glm::radians(sl.innerConeAngle));
                    slCol[slCount][0] = sl.color.r;
                    slCol[slCount][1] = sl.color.g;
                    slCol[slCount][2] = sl.color.b;
                    slCol[slCount][3] = cos(glm::radians(sl.outerConeAngle));
                    slCount++;
                }

                glm::mat4 proj = m_camera.Projection();
                glm::mat4 invProj = glm::inverse(proj);
                glm::mat4 invViewProj = glm::inverse(proj * m_camera.View());
                

                m_postProcess.RenderLighting(
                    m_framebuffer->Color(),
                    m_framebuffer->Normal(),
                    m_framebuffer->WorldPos(),
                    m_shadowMaps[0], m_shadowMaps[1], m_shadowMaps[2],
                    lightMtcs[0], lightMtcs[1], lightMtcs[2],
                    invViewProj,
                    camPos, lDirA, lColA,
                    plPosA, plColA, plCount,
                    slPos, slDir, slCol, slCount,
                    2, w, h);




                // Bloom + Tonemap (applied before SSAO/SSR to keep color space linear)
                // m_postProcess.RenderBloom(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 3, w, h, 1.0f, 0.5f);
                // m_postProcess.SwapFbos();
                m_postProcess.RenderTonemap(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 3, w, h, 0.0f, 1.0f, 1.0f);
                m_postProcess.SwapFbos();

                if (m_postProcess.SsaoEnabled()) {
                    bgfx::TextureHandle depth = m_framebuffer->Depth();
                    bgfx::TextureHandle normal = m_framebuffer->Normal();
                    bgfx::TextureHandle color = m_postProcess.OutputTexture();
                    m_postProcess.RenderSSAO(color, depth, normal,
                        m_postProcess.PingFbo(), proj, invProj, 3, w, h);
                    m_postProcess.SwapFbos();
                }
                if (m_postProcess.SsrEnabled()) {
                    bgfx::TextureHandle depth = m_framebuffer->Depth();
                    bgfx::TextureHandle normal = m_framebuffer->Normal();
                    bgfx::TextureHandle color = m_postProcess.OutputTexture();
                    m_postProcess.RenderSSR(color, normal, depth,
                        m_postProcess.PingFbo(), invProj, camPos, 3, w, h);
                    m_postProcess.SwapFbos();
                }

                if (m_postProcess.SmaaEnabled()) {
                    m_postProcess.RenderSmaa(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 3, w, h);
                    m_postProcess.SwapFbos();
                }

                if (m_postProcess.TaaEnabled()) {
                    glm::mat4 prevVP = m_postProcess.GetPrevViewProj(); // Need to expose this
                    glm::mat4 currVP = m_camera.Projection() * m_camera.View();
                    m_postProcess.RenderTaa(m_postProcess.OutputTexture(), m_framebuffer->Depth(), prevVP, glm::inverse(currVP), m_postProcess.PingFbo(), 3, w, h);
                    m_postProcess.SwapFbos();
                    m_postProcess.SetPrevViewProj(currVP);
                }

                if (m_postProcess.FxaaEnabled()) {
                    m_postProcess.RenderFxaa(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 3, w, h);
                    m_postProcess.SwapFbos();
                }

                if (m_postProcess.InvertEnabled()) {
                    m_postProcess.RenderInvert(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 3, w, h);
                    m_postProcess.SwapFbos();
                }
            } else {
                m_postProcess.Render(2, m_framebuffer->Color(), w, h);
            }
            
            // Forward Pass для стекла (рисуется поверх)
            if (m_postProcess.OutputFbo().idx != bgfx::kInvalidHandle) {
                bgfx::setViewClear(4, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
                bgfx::setViewFrameBuffer(4, m_postProcess.OutputFbo());
                m_render->RenderGlass(m_scene, m_camera, 4, w, h);
            }
        }

        // Debug rendering (grid, navmesh, selection box)
        if (m_debug) {
            m_debug->Begin();
            
            // Grid (100x100 на полу)
            m_debug->AddGrid(50.0f, 2.0f, {0.3f, 0.3f, 0.3f, 0.4f});
            
            // Оси в origin
            m_debug->AddAxes({0, 0.05f, 0}, 2.0f);
            
            // NavMesh debug (если есть)
            if (m_navSystem) {
                const auto& navTris = m_navSystem->GetDebugTriangles();
                if (!navTris.empty()) {
                    // Просто рисуем как линии
                    for (size_t i = 0; i + 2 < navTris.size(); i += 3) {
                        const glm::vec4 c = {0.2f, 0.8f, 0.4f, 0.5f};
                        m_debug->AddLine(navTris[i], navTris[i+1], c);
                        m_debug->AddLine(navTris[i+1], navTris[i+2], c);
                        m_debug->AddLine(navTris[i+2], navTris[i], c);
                    }
                }
            }
            
            // Selection box (вокруг выделенного)
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (sel && sel.HasComponent<Galeshi::TransformComponent>() && !sel.HasComponent<Galeshi::SkeletalComponent>()) {
                auto& tc = sel.GetComponent<Galeshi::TransformComponent>();
                m_debug->AddBox(tc.position, tc.scale + glm::vec3(0.1f), {1, 1, 0, 1});
            }
            
            if (m_postProcess.OutputFbo().idx != bgfx::kInvalidHandle) {
                bgfx::setViewFrameBuffer(5, m_postProcess.OutputFbo());
                m_debug->Render(m_camera, 5, w, h);
            }
        }

        if (m_framebuffer && bgfx::isValid(m_framebuffer->Color())) {
            bgfx::TextureHandle __ppTex = m_postProcess.OutputTexture();
            bgfx::TextureHandle __disp = bgfx::isValid(__ppTex)
                ? __ppTex : m_framebuffer->Color();
            // Force a blit to a BLIT_DST texture to ensure correct image layout for ImGui (Vulkan)
            if (bgfx::isValid(__ppTex)) {
                m_postProcess.BlitTexture(__ppTex, m_postProcess.BlitTextureA(), 3);
                __disp = m_postProcess.BlitTextureA();
            }
            ImTextureID texId = static_cast<ImTextureID>(__disp.idx);
            ImGui::Image(texId, ImVec2(static_cast<float>(w), static_cast<float>(h)), ImVec2(0, 0), ImVec2(1, 1));
            ImVec2 imgMin = ImGui::GetItemRectMin();
            ImVec2 imgMax = ImGui::GetItemRectMax();

            // --- 1. Drag (только ЛКМ, screen-space, без стрелок W/E/R) ---
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            const bool selHasTf = sel && sel.HasComponent<Galeshi::TransformComponent>();

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

            // --- Visual gizmo: X/Y/Z arrows drawn, but result IGNORED. ---
            // Only our LMB drag below is allowed to move the object.
            // ImGuizmo is invoked purely to render the affordance; the
            // transformed matrix is discarded and never written back to ECS.
            if (selHasTf) {
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(imgMin.x, imgMin.y, w, h);
                ImGuizmo::SetGizmoSizeClipSpace(0.12f);
                glm::mat4 visView  = m_camera.View();
                glm::mat4 visProj  = m_camera.Projection();
                auto& tcVis = sel.GetComponent<Galeshi::TransformComponent>();
                glm::mat4 visModel = tcVis.LocalMatrix();
                ImGuizmo::Manipulate(glm::value_ptr(visView), glm::value_ptr(visProj),
                                     ImGuizmo::TRANSLATE, ImGuizmo::LOCAL,
                                     glm::value_ptr(visModel));
                // visModel intentionally not applied.
            }

            const float centerRadiusPx = 24.0f;
            ImVec2 mouse = ImGui::GetMousePos();
            const bool overCenter = selOnScreen &&
                std::hypot(mouse.x - selScreen.x, mouse.y - selScreen.y) < centerRadiusPx;

            // Begin drag on LMB down over center
            if (ImGui::IsItemHovered() && !m_isDragging &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) && overCenter) {
                m_isDragging = true;
                m_dragStartMouse = { mouse.x, mouse.y };
                m_dragStartObjWorld = selWorldPos;
            }

            // Perform drag while LMB held
            if (m_isDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && selHasTf) {
                const float dx = mouse.x - m_dragStartMouse.x;
                const float dy = mouse.y - m_dragStartMouse.y;

                glm::vec3 camPos = m_camera.Position();
                float dist = glm::length(selWorldPos - camPos);
                dist = std::max(dist, 0.5f);
                const float fovY = glm::radians(60.0f);  // matches SetPerspective
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
            if (selOnScreen) {
                ImU32 hintColor = overCenter ? IM_COL32(255, 220, 40, 220)
                                             : IM_COL32(255, 255, 255, 110);
                ImGui::GetWindowDrawList()->AddCircle(
                    ImVec2(selScreen.x, selScreen.y),
                    centerRadiusPx * 0.5f, hintColor, 24, 2.0f);
            }

            // --- 2. ЛКМ Выделение (Raycast по AABB) ---
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_isDragging && !overCenter && !ImGuizmo::IsOver()) {
                ImVec2 mouse = ImGui::GetMousePos();
                float mx = mouse.x - imgMin.x;
                float my = mouse.y - imgMin.y;
                float ndcX = (mx / w) * 2.0f - 1.0f;
                float ndcY = 1.0f - (my / h) * 2.0f;

                glm::mat4 invVP = glm::inverse(m_camera.Projection() * m_camera.View());
                glm::vec4 rayStart = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
                glm::vec4 rayEnd   = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
                rayStart /= rayStart.w; rayEnd /= rayEnd.w;
                
                glm::vec3 rayDir = glm::normalize(glm::vec3(rayEnd - rayStart));
                glm::vec3 rayOrigin = glm::vec3(rayStart);

                Galeshi::Entity bestHit;
                float bestDist = 99999.0f;

                auto view = m_scene.Registry().view<Galeshi::TransformComponent>();
                for (auto e : view) {
                    auto& tc = view.get<Galeshi::TransformComponent>(e);
                    
                    // 1. Вычисляем МИРОВУЮ позицию (проходим вверх по родителям)
                    glm::vec3 worldPos = tc.position;
                    entt::entity curr = e;
                    int __depth = 0; while (__depth++ < 128 && m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                        curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                        if (m_scene.Registry().all_of<Galeshi::TransformComponent>(curr)) {
                            worldPos += m_scene.Registry().get<Galeshi::TransformComponent>(curr).position;
                        }
                    }

                    // 2. Увеличиваем коробку для скелетных моделей
                    float size = 0.5f;
                    if (m_scene.Registry().all_of<Galeshi::SkeletalComponent>(curr)) {
                        size = 1.5f; // Персонажи обычно больше кубов
                    }

                    glm::vec3 minB = worldPos - glm::vec3(size);
                    glm::vec3 maxB = worldPos + glm::vec3(size);
                    
                    // 3. Ray-AABB
                    float tmin = (minB.x - rayOrigin.x) / rayDir.x;
                    float tmax = (maxB.x - rayOrigin.x) / rayDir.x;
                    if (tmin > tmax) std::swap(tmin, tmax);
                    
                    float tymin = (minB.y - rayOrigin.y) / rayDir.y;
                    float tymax = (maxB.y - rayOrigin.y) / rayDir.y;
                    if (tymin > tymax) std::swap(tymin, tymax);
                    
                    if ((tmin > tymax) || (tymin > tmax)) continue;
                    if (tymin > tmin) tmin = tymin;
                    if (tymax < tmax) tmax = tymax;
                    
                    float tzmin = (minB.z - rayOrigin.z) / rayDir.z;
                    float tzmax = (maxB.z - rayOrigin.z) / rayDir.z;
                    if (tzmin > tzmax) std::swap(tzmin, tzmax);
                    
                    if ((tmin > tzmax) || (tzmin > tmax)) continue;
                    if (tzmin > tmin) tmin = tzmin;
                    if (tzmax < tmax) tmax = tzmax;

                    if (tmax < 0) continue; // За спиной
                    
                    if (tmin < bestDist) {
                        bestDist = tmin;
                        bestHit = Galeshi::Entity(curr, &m_scene); // Выделяем корень, а не кость
                    }
                }

                if (bestHit) m_editor.Select(bestHit);
                else m_editor.ClearSelection();
            }

                    }
        ImGui::End();
    }

    // -------------------------------------------------------------------
    // Details
    // -------------------------------------------------------------------
    void DrawTextureSlot(const char* label, std::shared_ptr<Galeshi::Texture>& slot) {
        ImGui::PushID(label);
        ImGui::Text("%s", label);

        if (slot && bgfx::isValid(slot->Handle())) {
            ImTextureID texId = static_cast<ImTextureID>(slot->Handle().idx);
            ImGui::Image(texId, ImVec2(64, 64));
            ImGui::SameLine();
            ImGui::BeginGroup();
            std::string path = slot->Path();
            auto slash = path.find_last_of('/');
            std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
            ImGui::TextWrapped("%s", name.c_str());
            if (ImGui::Button("Clear")) {
                slot.reset();
            }
            ImGui::EndGroup();
        } else {
            ImGui::Button("Empty (drop texture here)", ImVec2(200, 64));
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                const char* path = static_cast<const char*>(payload->Data);
                std::string p = path;
                auto dotPos = p.find_last_of('.');
                std::string ext = (dotPos != std::string::npos) ? p.substr(dotPos) : "";
                for (auto& c : ext) c = static_cast<char>(std::tolower(c));
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                    ext == ".bmp" || ext == ".tga") {
                    slot = m_assets->LoadTexture(p);
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopID();
    }

        void DrawOutliner() {
        ImGui::Begin("World Outliner");
        auto entities = m_scene.AllEntities();
        for (auto e : entities) {
            // Скрываем субмеши (объекты с родителем) из общего списка
            if (e.HasComponent<Galeshi::ParentComponent>()) continue;
            std::string name = "Entity " + std::to_string((uint32_t)e.Handle());
            if (e.HasComponent<Galeshi::NameComponent>()) {
                name = e.GetComponent<Galeshi::NameComponent>().name;
            }
            if (e.HasComponent<Galeshi::SkeletalComponent>()) {
                name = "[Skeletal] " + name; // Пометка для скелетных моделей
            }
            bool isSelected = m_editor.IsSelected(e);
            if (ImGui::Selectable(name.c_str(), isSelected)) {
                m_editor.Select(e);
            }
        }
        ImGui::End();
    }

void DrawDetails() {
        ImGui::Begin("Details");
        auto selected = m_editor.SelectedAll(&m_scene);
        if (selected.empty()) { ImGui::TextDisabled("No selection"); ImGui::End(); return; }

        if (!selected.empty() && selected[0].HasComponent<Galeshi::NameComponent>()) {
            auto& nc = selected[0].GetComponent<Galeshi::NameComponent>();
            char buf[256]; std::snprintf(buf, sizeof(buf), "%s", nc.name.c_str());
            if (ImGui::InputText("Name", buf, sizeof(buf))) nc.name = buf;
        }

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 pos = selected[0].HasComponent<Galeshi::TransformComponent>() ? selected[0].GetComponent<Galeshi::TransformComponent>().position : glm::vec3(0.0f);
            glm::vec3 scl = selected[0].HasComponent<Galeshi::TransformComponent>() ? selected[0].GetComponent<Galeshi::TransformComponent>().scale : glm::vec3(1.0f);
            glm::vec3 rot = selected[0].HasComponent<Galeshi::TransformComponent>() ? glm::degrees(glm::eulerAngles(selected[0].GetComponent<Galeshi::TransformComponent>().rotation)) : glm::vec3(0.0f);

            if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 0.05f)) { for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().position = pos; }


        
            if (ImGui::DragFloat3("Scale", glm::value_ptr(scl), 0.05f, 0.01f, 100.0f)) { for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().scale = scl; }
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(rot), 1.0f)) { glm::quat q = glm::quat(glm::radians(rot)); for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().rotation = q; }
        }

        if (auto sel = m_editor.Selected(&m_scene); sel) {
            if (!sel.HasComponent<Galeshi::AudioSourceComponent>()) {
                if (ImGui::Button("+ Add Audio Source")) {
                    sel.AddComponent<Galeshi::AudioSourceComponent>();
                }
            } else {
                auto& audio = sel.GetComponent<Galeshi::AudioSourceComponent>();
                if (ImGui::CollapsingHeader("Audio Source")) {
                    char buf[256];
                    snprintf(buf, sizeof buf, "%s", audio.soundPath.c_str());
                    if (ImGui::InputText("Sound Path", buf, sizeof buf)) {
                        audio.soundPath = buf;
                    }
                    ImGui::Checkbox("Autoplay", &audio.autoplay);
                    ImGui::Checkbox("Loop", &audio.loop);
                    if (ImGui::Button(audio.playing ? "Stop" : "Play")) {
                        if (audio.playing) {
                            m_audio.StopSound(audio.soundPath);
                            audio.playing = false;
                        } else {
                            m_audio.PlaySound(audio.soundPath, audio.loop);
                            audio.playing = true;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text("State: %s", audio.playing ? "playing" : "idle");
                    if (ImGui::Button("Remove Audio Source")) {
                        sel.RemoveComponent<Galeshi::AudioSourceComponent>();
                    }
                }
            }
        }

        if (selected[0].HasComponent<Galeshi::LandscapeComponent>() && ImGui::CollapsingHeader("Landscape (Sculpting)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Use Left Click in viewport to sculpt");
            if (ImGui::Button("Toggle Raise Brush")) {
                m_editor.toolMode = (m_editor.toolMode == Galeshi::EditorState::ToolMode::Raise) ? Galeshi::EditorState::ToolMode::Select : Galeshi::EditorState::ToolMode::Raise;
            }
            ImGui::Text("Current Tool: %s", m_editor.toolMode == Galeshi::EditorState::ToolMode::Raise ? "Raise" : "Select");
        }

        // Helper lambda to draw material UI safely
        auto DrawMaterialUI = [&](const char* label, Galeshi::MaterialComponent& matc, uint32_t eid) {
            if (!matc.material) return;
            auto& m = *matc.material;
            ImGui::PushID(eid);
            if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::ColorEdit4("Albedo Factor", glm::value_ptr(m.albedoFactor));
                ImGui::SliderFloat("Metallic Factor",  &m.metallicFactor,  0.0f, 1.0f);
                ImGui::Checkbox("Glass (Transparent)", &m.isGlass);
                ImGui::SliderFloat("Roughness Factor", &m.roughnessFactor, 0.04f, 1.0f);
                ImGui::ColorEdit3("Emissive", glm::value_ptr(m.emissiveFactor));

                ImGui::Separator();
                ImGui::Text("Texture Maps (drag from Content Browser):");

                DrawTextureSlot("Albedo Map",           m.albedoMap);
                DrawTextureSlot("Normal Map",           m.normalMap);
                DrawTextureSlot("Metallic/Roughness",   m.metallicRoughnessMap);
            }
            ImGui::PopID();
        };

        // 1. Draw material for the selected entity itself (if any)
        if (selected[0].HasComponent<Galeshi::MaterialComponent>()) {
            DrawMaterialUI("Material", selected[0].GetComponent<Galeshi::MaterialComponent>(), (uint32_t)selected[0].Handle());
        }

        // 2. Draw materials for all children (e.g., skeletal mesh sub-meshes)
        for (auto e : m_scene.AllEntities()) {
            if (e.HasComponent<Galeshi::ParentComponent>() && e.GetComponent<Galeshi::ParentComponent>().parent == selected[0].Handle()) {
                if (e.HasComponent<Galeshi::MaterialComponent>()) {
                    std::string label = "Material Slot [" + std::to_string((uint32_t)e.Handle()) + "]";
                    if (e.HasComponent<Galeshi::NameComponent>()) {
                        label = e.GetComponent<Galeshi::NameComponent>().name;
                    }
                    DrawMaterialUI(label.c_str(), e.GetComponent<Galeshi::MaterialComponent>(), (uint32_t)e.Handle());
                }
            }
        }

        if (selected[0].HasComponent<Galeshi::NavAgentComponent>() && ImGui::CollapsingHeader("AI Agent", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& agent = selected[0].GetComponent<Galeshi::NavAgentComponent>();
            ImGui::DragFloat("Speed", &agent.speed, 0.1f, 0.1f, 20.0f);
            ImGui::Text("Path Waypoints: %zu", agent.path.size());
            ImGui::Text("Use RMB in viewport to set target");
        }

        if (selected[0].HasComponent<Galeshi::SkeletalComponent>() && ImGui::CollapsingHeader("Skeletal Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& skel = selected[0].GetComponent<Galeshi::SkeletalComponent>();
            ImGui::Checkbox("Playing", &skel.isAnimated);
            ImGui::DragFloat("Speed", &skel.animationSpeed, 0.1f, 0.0f, 10.0f);
        }
        
        if (selected[0].HasComponent<Galeshi::DirectionalLightComponent>() && ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& dl = selected[0].GetComponent<Galeshi::DirectionalLightComponent>();
            ImGui::DragFloat3("Direction", glm::value_ptr(dl.direction), 0.05f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Color", glm::value_ptr(dl.color));
            ImGui::SliderFloat("Intensity", &dl.intensity, 0.0f, 10.0f);
        }

        if (selected[0].HasComponent<Galeshi::PointLightComponent>() && ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& pl = m_scene.Registry().get<Galeshi::PointLightComponent>(selected[0].Handle());
            ImGui::ColorEdit3("Point Color", glm::value_ptr(pl.color));
            {
                float oldInt = pl.intensity;
                if (ImGui::DragFloat("Point Intensity", &pl.intensity, 0.1f, 0.0f, 100.0f)) {
                }
            }
            ImGui::DragFloat("Point Radius", &pl.radius, 0.1f, 0.1f, 100.0f);
        }

        if (selected[0].HasComponent<Galeshi::SpotLightComponent>() && ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& sl = m_scene.Registry().get<Galeshi::SpotLightComponent>(selected[0].Handle());
            ImGui::DragFloat3("Spot Direction", glm::value_ptr(sl.direction), 0.05f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Spot Color", glm::value_ptr(sl.color));
            ImGui::SliderFloat("Spot Intensity", &sl.intensity, 0.0f, 20.0f);
            ImGui::DragFloat("Spot Radius", &sl.radius, 0.1f, 0.1f, 100.0f);
            ImGui::SliderFloat("Inner Cone", &sl.innerConeAngle, 0.0f, 90.0f);
            ImGui::SliderFloat("Outer Cone", &sl.outerConeAngle, 0.0f, 90.0f);
        }

        ImGui::End();
    }

    // -------------------------------------------------------------------
    // Content Browser & Assets
    // -------------------------------------------------------------------
    void DrawContentBrowser() {
        ImGui::Begin("Content Browser");
        std::string clicked = m_browser->Draw();
        if (!clicked.empty()) OnAssetDoubleClicked(clicked);
        ImGui::End();
    }

    void OpenFileDialog() {
        const char* home = std::getenv("HOME");
        std::string start = home ? home : "/";
        m_filePicker.Open(start, "Load Model", { ".fbx", ".gltf", ".glb", ".obj", ".dae" });
    }

    void OnAssetDoubleClicked(const std::string& path) {
        namespace fs = std::filesystem;
        std::string ext = fs::path(path).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));

        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
            SpawnFromModel(path, glm::vec3(0.0f));
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                   ext == ".bmp" || ext == ".tga") {
            // Применяем к выделенному объекту
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (sel && sel.HasComponent<Galeshi::MaterialComponent>()) {
                auto& matc = sel.GetComponent<Galeshi::MaterialComponent>();
                if (matc.material) {
                    matc.material->albedoMap = m_assets->LoadTexture(path);
                    spdlog::info("[Editor] Applied texture to selected: {}", path);
                }
            }
        }
    }

    void OnAssetDropped(const std::string& path, glm::vec3 pos) {
        namespace fs = std::filesystem;
        std::string ext = fs::path(path).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));

        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
            SpawnFromModel(path, pos);
            return;
        }

        // Если бросили текстуру на объект с материалом — установить в albedo
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".bmp" || ext == ".tga") {
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (sel && sel.HasComponent<Galeshi::MaterialComponent>()) {
                auto& matc = sel.GetComponent<Galeshi::MaterialComponent>();
                if (matc.material) {
                    matc.material->albedoMap = m_assets->LoadTexture(path);
                    spdlog::info("[Editor] Set albedo map: {}", path);
                }
            }
        }
    }

    void SpawnFromModel(const std::string& path, glm::vec3 pos) {
        PushHistory();
        auto model = m_assets->LoadModel(path);
        if (model.meshes.empty()) return;
        namespace fs = std::filesystem;
        const auto stem = fs::path(path).stem().string();

        auto root = m_scene.CreateEntity(stem);
        auto& rootTf = root.GetComponent<Galeshi::TransformComponent>();
        
        // 1. Force clean TransformComponent (0,0,0) as requested
        rootTf.position = glm::vec3(0.0f);
        rootTf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        rootTf.scale = glm::vec3(1.0f);
        
        // 2. If the model imports flipped (Z-up), uncomment the next line:
        rootTf.rotation = glm::angleAxis(glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        if (model.hasAnimation) {
            auto& skel = root.AddComponent<Galeshi::SkeletalComponent>();
            skel.isAnimated = true; skel.sourcePath = path; skel.boneMatrices.resize(256, glm::mat4(1.0f));
        }

        for (size_t i = 0; i < model.meshes.size(); ++i) {
            auto e = m_scene.CreateEntity(stem + "_" + std::to_string(i));
            
            // NOTE: We do NOT reset child transforms here. Assimp gives them local offsets.
            
            e.AddComponent<Galeshi::ParentComponent>(Galeshi::ParentComponent{ root.Handle() });
            e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ model.meshes[i], path, "", static_cast<int>(i) });
            
            std::shared_ptr<Galeshi::Material> mat;
            if (i < model.meshMaterialIndex.size()) {
                int matIdx = model.meshMaterialIndex[i];
                if (matIdx >= 0 && matIdx < static_cast<int>(model.materials.size()))
                    mat = model.materials[static_cast<size_t>(matIdx)];
            }
            if (!mat && !model.materials.empty())
                mat = model.materials[0];
            if (!mat)
                mat = std::make_shared<Galeshi::Material>();
            mat->isGlass = false; // Force opaque for characters
            e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
        }
        m_editor.Select(root);
        m_editor.camTarget = pos;
        m_editor.camRadius = 5.0f;
    }

    // -------------------------------------------------------------------
    // Scene Helpers
    // -------------------------------------------------------------------
    Galeshi::Entity MakeCube(const std::string& name, glm::vec3 pos) {
        auto e = m_scene.CreateEntity(name);
        auto mesh = m_assets->CreateCubeMesh();
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "cube" });
        auto mat = std::make_shared<Galeshi::Material>();
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
        e.GetComponent<Galeshi::TransformComponent>().position = pos;
        return e;
    }

    // -------------------------------------------------------------------
    // Spawn a cube with a Jolt rigid body attached.
    // Always use this for physics cubes — it wires RigidBodyComponent,
    // motionType, mass, AND the Jolt body (CreateBox), so SyncTransformsToECS
    // will pick it up.
    // -------------------------------------------------------------------
    Galeshi::Entity SpawnPhysicsCube(const std::string& name, glm::vec3 pos,
                                     glm::vec3 scale = glm::vec3(1.0f)) {
        auto e = MakeCube(name, pos);
        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.scale = scale;

        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
        rb.mass = 1.0f;
        rb.bodyId = m_physics->CreateBox(scale * 0.5f, pos, static_cast<entt::entity>(e));

        spdlog::info("[Physics] Spawned {} bodyId=0x{:08x}", name, rb.bodyId);
        return e;
    }

    Galeshi::Entity SpawnPhysicsSphere(const std::string& name, glm::vec3 pos, float radius) {
        auto e = m_scene.CreateEntity(name);
        auto mat = std::make_shared<Galeshi::Material>();
        mat->albedoFactor = {0.7f, 0.5f, 0.5f, 1.0f};
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{mat});
        auto mesh = m_assets->CreateSphereMesh();
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "sphere" });
        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.position = pos;
        tf.scale = glm::vec3(radius * 2.0f);  // unit sphere (r=0.5) * scale = diameter
        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
        rb.mass = 1.0f;
        rb.bodyId = m_physics->CreateSphere(radius, pos, static_cast<entt::entity>(e), false);
        spdlog::info("[Physics] Spawned {} bodyId=0x{:08x}", name, rb.bodyId);
        return e;
    }

    Galeshi::Entity SpawnPhysicsCapsule(const std::string& name, glm::vec3 pos, float halfHeight, float radius) {
        auto e = m_scene.CreateEntity(name);
        auto mat = std::make_shared<Galeshi::Material>();
        mat->albedoFactor = {0.5f, 0.7f, 0.5f, 1.0f};
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{mat});
        auto mesh = m_assets->CreateCapsuleMesh(halfHeight, radius);
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "capsule" });
        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.position = pos;
        tf.scale = glm::vec3(1.0f);  // mesh baked with correct size
        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
        rb.mass = 1.0f;
        rb.bodyId = m_physics->CreateCapsule(halfHeight, radius, pos, static_cast<entt::entity>(e), false);
        spdlog::info("[Physics] Spawned {} bodyId=0x{:08x}", name, rb.bodyId);
        return e;
    }

    void BuildTestScene() {
        auto sun = m_scene.CreateEntity("Sun");
        auto& dl = sun.HasComponent<Galeshi::DirectionalLightComponent>()
                   ? sun.GetComponent<Galeshi::DirectionalLightComponent>()
                   : sun.AddComponent<Galeshi::DirectionalLightComponent>();
        dl.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.5f));
        dl.color = {1.0f, 0.97f, 0.9f}; dl.intensity = 3.0f;
        // Добавляем большой пол (чтобы тени падали на него)
        auto ground = MakeCube("Ground", {0.0f, -5.0f, 0.0f});
        ground.GetComponent<Galeshi::TransformComponent>().scale = { 100.0f, 10.0f, 100.0f };
        ground.GetComponent<Galeshi::MaterialComponent>().material->albedoFactor = { 0.3f, 0.3f, 0.3f, 1.0f };
        ground.GetComponent<Galeshi::MaterialComponent>().material->roughnessFactor = 0.8f;
        {
            auto& rb = ground.AddComponent<Galeshi::RigidBodyComponent>();
            rb.motionType = Galeshi::RigidBodyComponent::MotionType::Static;
            rb.bodyId = m_physics->CreateBox(glm::vec3(100.0f, 10.0f, 100.0f) * 0.5f, ground.GetComponent<Galeshi::TransformComponent>().position, static_cast<entt::entity>(ground), true);
        }
        // Кубки Dynamic (как PhysicsCube) — падают с y=3.0 на землю
        auto spawnDynamicCube = [this](const std::string& name, glm::vec3 pos, glm::vec3 color) {
            auto e = MakeCube(name, pos);
            auto& mat = *e.GetComponent<Galeshi::MaterialComponent>().material;
            mat.albedoFactor = glm::vec4(color, 1.0f);
            auto& tf = e.GetComponent<Galeshi::TransformComponent>();
            auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
            rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
            rb.mass = 1.0f;
            rb.bodyId = m_physics->CreateBox(tf.scale * 0.5f, pos, static_cast<entt::entity>(e), false);
            return e;
        };
        spawnDynamicCube("Cube_Orange", { 0.0f, 3.0f, 0.0f}, {0.85f, 0.55f, 0.25f});
        spawnDynamicCube("Cube_Metal",  {-2.5f, 3.5f, 0.0f}, {0.95f, 0.95f, 0.95f});
        spawnDynamicCube("Cube_Rough",  { 2.5f, 4.0f, 0.0f}, {0.30f, 0.55f, 0.85f});


        auto pl = m_scene.CreateEntity("PointLight");
        if (!pl.HasComponent<Galeshi::PointLightComponent>())
            pl.AddComponent<Galeshi::PointLightComponent>();
        pl.GetComponent<Galeshi::TransformComponent>().position = {0.0f, 2.0f, 0.0f};
    }

    // --- Состояние ---
    Galeshi::Scene          m_scene;
    Galeshi::Camera         m_camera;
    Galeshi::EditorState    m_editor;
    float                   m_time = 0.0f;
    int                     m_renderFrameCounter = 0;
    bool                    m_pendingEffectsEnable = false;

    std::unique_ptr<Galeshi::AssetManager>    m_assets;
    std::unique_ptr<Galeshi::RenderSystem>    m_render;
    std::unique_ptr<Galeshi::Framebuffer>     m_framebuffer;
    std::unique_ptr<Galeshi::ContentBrowser>  m_browser;
    std::unique_ptr<Galeshi::SceneSerializer> m_serializer;
    std::unique_ptr<Galeshi::EnginePhysicsSystem>   m_physics;
    std::unique_ptr<Galeshi::NavigationSystem> m_navSystem;
    std::unique_ptr<Galeshi::DebugRenderer> m_debug;
    bgfx::TextureHandle m_shadowMaps[3] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::FrameBufferHandle m_shadowFbos[3] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };

    uint16_t    m_viewportW = 0, m_viewportH = 0;
    bool        m_viewportHovered = false;
    Galeshi::FilePickerDialog m_filePicker;
    Galeshi::FilePickerDialog m_filePickerSave;
    Galeshi::FilePickerDialog m_scenePicker;

    std::vector<std::string> m_history;
    int  m_historyIndex = -1;
    bool m_isRestoringHistory = false;
    bool m_wasUsingGizmo = false;

    // Screen-space translate drag (LMB-only, no W/E/R arrows)
    bool      m_isDragging = false;
    glm::vec2 m_dragStartMouse{0.0f, 0.0f};
    glm::vec3 m_dragStartObjWorld{0.0f};
};

int main(int argc, char** argv) {
    bool debugMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--debug") debugMode = true;
    }
    if (debugMode) {
        spdlog::set_level(spdlog::level::debug);
    }
    // Принудительно небуферизированный stdout — чтобы логи
    // появлялись до любого crash.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // ВАЖНО: создаём app через new и НИКОГДА не удаляем.
    // Причина: ~Application вызывает bgfx::shutdown() и SDL_Quit(),
    // которые падают в deadlock внутри libnvidia-glcore при dlclose.
    // OnShutdown() (в конце Run) уже сохраняет сцену и делает cleanup.
    // OS освободит GPU-ресурсы при завершении процесса.
    GaleshiEditorApp* app = new GaleshiEditorApp(debugMode);
    int exitCode = app->Run();

    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(exitCode);  // нет деструкторов, нет dlclose, нет NVIDIA deadlock
}
