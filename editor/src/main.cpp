#include "GaleshiEngine/Core/Application.hpp"
#include "GaleshiEngine/Renderer/Renderer.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Renderer/RenderSystem.hpp"
#include "GaleshiEngine/Renderer/Framebuffer.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/SceneSerializer.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Editor/EditorState.hpp"
#include "GaleshiEngine/Editor/ContentBrowser.hpp"
#include "GaleshiEngine/Editor/FilePickerDialog.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/spdlog.h>
#include <ImGuizmo.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <filesystem>

class GaleshiEditorApp final : public Galeshi::Application {
public:
    GaleshiEditorApp()
        : Galeshi::Application({
              .name = "GaleshiEditor", .width = 1600, .height = 900, .editor = true}) {}

protected:
    void OnStart() override {
        m_assets  = std::make_unique<Galeshi::AssetManager>();
        m_render  = std::make_unique<Galeshi::RenderSystem>();
        m_render->Init(m_assets.get());

        m_browser    = std::make_unique<Galeshi::ContentBrowser>("assets");
        m_serializer = std::make_unique<Galeshi::SceneSerializer>(m_scene, *m_assets);

        // Авто-загрузка сцены, если она есть. Иначе — тестовая сцена.
        namespace fs = std::filesystem;
        if (fs::exists("scene.json")) {
            if (m_serializer->Load("scene.json")) {
                spdlog::info("[Editor] Scene auto-loaded from scene.json");
            } else {
                spdlog::warn("[Editor] Failed to load scene.json, building default");
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
            static_cast<float>(GetRenderer()->Width()) /
            static_cast<float>(GetRenderer()->Height()),
            0.1f, 1000.0f);
    }

    void OnUpdate(float dt) override {
        m_time += dt;

        auto animView = m_scene.Registry().view<Galeshi::SkeletalComponent>();
        for (auto h : animView) {
            auto& skel = animView.get<Galeshi::SkeletalComponent>(h);
            if (skel.isAnimated) {
                skel.currentTime += dt * skel.animationSpeed;
                // Loop
                if (skel.currentTime > 10.0f) skel.currentTime = 0.0f;
            }
        }

        // Editor camera — только если ImGuizmo не захватил управление
        const bool cameraControllable =
            m_viewportHovered &&
            !m_editor.GizmoLocked() &&
            !ImGuizmo::IsUsing() &&
            !ImGuizmo::IsOver();

        if (cameraControllable && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            auto d = ImGui::GetIO().MouseDelta;
            m_editor.camYaw   -= d.x * 0.01f;
            m_editor.camPitch -= d.y * 0.01f;
            m_editor.camPitch  = glm::clamp(m_editor.camPitch, -1.5f, 1.5f);
        }

        const float aspect =
            m_viewportH ? static_cast<float>(m_viewportW) / m_viewportH
                        : 16.0f / 9.0f;
        m_camera.SetPerspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        m_camera.Orbit(m_editor.camTarget,
                       m_editor.camYaw,
                       m_editor.camPitch,
                       m_editor.camRadius);
    }

    void OnRender() override {}

    void OnImGui() override {
        // --- Меню ---
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Import Model...", "Ctrl+I")) OpenFileDialog();
                if (ImGui::MenuItem("Load Scene", "Ctrl+O")) LoadScene();
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
            ImGui::EndMainMenuBar();
        }

        // Горячие клавиши
        if (!ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) m_editor.SetMode(Galeshi::GizmoMode::Translate);
            if (ImGui::IsKeyPressed(ImGuiKey_E)) m_editor.SetMode(Galeshi::GizmoMode::Rotate);
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_editor.SetMode(Galeshi::GizmoMode::Scale);
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelected();

            if (ImGui::GetIO().KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_O)) LoadScene();
            }
        }

        DrawDockspace();
        DrawViewport();
        DrawOutliner();
        DrawDetails();
        DrawContentBrowser();

        // File picker — рисуем поверх всего
        std::string chosen = m_filePicker.Draw();
        if (!chosen.empty()) {
            SpawnFromModel(chosen, m_editor.camTarget);
        }
    }

    void OnShutdown() override {
        // Авто-сохранение сцены
        SaveScene();

        m_render.reset();
        m_framebuffer.reset();
        m_serializer.reset();
        m_browser.reset();
        m_assets.reset();
    }

private:
    // -------------------------------------------------------------------
    // Undo / Redo
    // -------------------------------------------------------------------
    void PushHistory() {
        if (m_isRestoringHistory) return;
        std::string snap = m_serializer->SaveToString();
        if (m_historyIndex >= 0 && m_history[static_cast<size_t>(m_historyIndex)] == snap) return;

        // Обрезаем redo-хвост
        m_history.resize(static_cast<size_t>(m_historyIndex + 1));
        m_history.push_back(snap);
        m_historyIndex = (int)m_history.size() - 1;

        // Лимит истории
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
        m_serializer->LoadFromString(m_history[static_cast<size_t>(m_historyIndex)]);
        m_isRestoringHistory = false;
    }

    void Redo() {
        if (m_historyIndex + 1 >= (int)m_history.size()) return;
        m_historyIndex++;
        m_isRestoringHistory = true;
        m_editor.ClearSelection();
        m_serializer->LoadFromString(m_history[static_cast<size_t>(m_historyIndex)]);
        m_isRestoringHistory = false;
    }

    // -------------------------------------------------------------------
    // Save / Load
    // -------------------------------------------------------------------
    void OpenFileDialog() {
        m_filePicker.Open("assets", "Import Model",
            { ".fbx", ".gltf", ".glb", ".obj", ".dae" });
    }

    void SaveScene() {
        if (m_serializer) m_serializer->Save("scene.json");
    }
    void LoadScene() {
        // Load Scene = импорт модели из любого места.
        const char* home = std::getenv("HOME");
        std::string start = home ? home : "/";
        m_filePicker.Open(start, "Load Model",
            { ".fbx", ".gltf", ".glb", ".obj", ".dae" });
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
    // Viewport
    // -------------------------------------------------------------------
    void DrawViewport() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport");

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

        // --- Рендер в framebuffer ---
        if (m_framebuffer && m_framebuffer->Valid()) {
            bgfx::setViewFrameBuffer(1, m_framebuffer->Handle());
            m_render->Render(m_scene, m_camera, 1, w, h);
        }

        // --- Image ---
        if (m_framebuffer && bgfx::isValid(m_framebuffer->Color())) {
            ImTextureID texId = static_cast<ImTextureID>(m_framebuffer->Color().idx);
            ImGui::Image(texId, ImVec2(static_cast<float>(w), static_cast<float>(h)),
                         ImVec2(0, 0), ImVec2(1, 1));
        }

        m_viewportHovered = ImGui::IsItemHovered();

        // --- Zoom ---
        if (m_viewportHovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                m_editor.camRadius -= wheel * 0.5f;
                m_editor.camRadius = glm::clamp(m_editor.camRadius, 0.5f, 200.0f);
            }
        }

        // --- Drag-n-drop target ---
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                const char* assetPath = static_cast<const char*>(payload->Data);
                OnAssetDropped(assetPath);
            }
            ImGui::EndDragDropTarget();
        }

        // --- ImGuizmo (только для первой выделенной) ---
        Galeshi::Entity sel = m_editor.Selected(&m_scene);
        if (sel && sel.HasComponent<Galeshi::TransformComponent>()) {
            DrawGizmo(sel);
        }

        // --- Overlay ---
        ImGui::SetCursorPos(ImVec2(10, 10));
        ImGui::Text("GaleshiEngine - Stage 3  |  FPS: %.1f",
                    static_cast<double>(ImGui::GetIO().Framerate));

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void DrawGizmo(Galeshi::Entity sel) {
        auto& tc = sel.GetComponent<Galeshi::TransformComponent>();

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

        const ImVec2 pos  = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);

        glm::mat4 model = tc.LocalMatrix();

        const float* viewPtr = glm::value_ptr(m_camera.View());
        const float* projPtr = glm::value_ptr(m_camera.Projection());

        ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
        switch (m_editor.Mode()) {
        case Galeshi::GizmoMode::Translate: op = ImGuizmo::TRANSLATE; break;
        case Galeshi::GizmoMode::Rotate:    op = ImGuizmo::ROTATE;    break;
        case Galeshi::GizmoMode::Scale:     op = ImGuizmo::SCALE;     break;
        }

        ImGuizmo::Manipulate(viewPtr, projPtr, op, ImGuizmo::LOCAL,
                             glm::value_ptr(model));

        m_editor.SetGizmoLocked(ImGuizmo::IsUsing() || ImGuizmo::IsOver());

        if (ImGuizmo::IsUsing()) {
            glm::vec3 t, r, s;
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model),
                glm::value_ptr(t), glm::value_ptr(r), glm::value_ptr(s));

            tc.position = t;
            tc.rotation = glm::quat(glm::radians(r));
            tc.scale    = s;
        }
    }

    // -------------------------------------------------------------------
    // Outliner (multi-select)
    // -------------------------------------------------------------------
    void DrawEntityNode(Galeshi::Entity e) {
        bool isSelected = m_editor.IsSelected(e);
        auto& nc = e.GetComponent<Galeshi::NameComponent>();
        
        std::vector<Galeshi::Entity> children;
        auto view = m_scene.Registry().view<Galeshi::ParentComponent>();
        for (auto h : view) {
            if (view.get<Galeshi::ParentComponent>(h).parent == e.Handle()) {
                children.emplace_back(h, &m_scene);
            }
        }

        ImGuiTreeNodeFlags flags = ((isSelected) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)e.Handle(), flags, "%s", nc.name.c_str());
        
        if (ImGui::IsItemClicked()) {
            if (ImGui::GetIO().KeyCtrl) {
                if (isSelected) m_editor.RemoveFromSelection(e); else m_editor.AddToSelection(e);
            } else {
                m_editor.Select(e);
            }
        }

        if (opened && !children.empty()) {
            for (auto c : children) DrawEntityNode(c);
            ImGui::TreePop();
        }
    }

    void DrawOutliner() {
        ImGui::Begin("World Outliner");
        auto entities = m_scene.AllEntities();
        for (auto& e : entities) {
            if (!e.HasComponent<Galeshi::ParentComponent>()) {
                DrawEntityNode(e);
            }
        }
        if (ImGui::BeginPopupContextWindow("outliner_ctx")) {
            if (ImGui::MenuItem("Create Empty")) m_editor.Select(m_scene.CreateEntity("New Entity"));
            if (ImGui::MenuItem("Delete Selected") && m_editor.HasSelection()) DeleteSelected();
            ImGui::EndPopup();
        }
        ImGui::End();
    }

    // -------------------------------------------------------------------
    // Details
    // -------------------------------------------------------------------
    void DrawDetails() {
        ImGui::Begin("Details");

        Galeshi::Entity sel = m_editor.Selected(&m_scene);
        if (!sel) {
            ImGui::TextDisabled("No selection");
            ImGui::End();
            return;
        }

        if (m_editor.SelectedAll(&m_scene).size() > 1) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1),
                "Multi-select: editing first entity");
            ImGui::Separator();
        }

        if (sel.HasComponent<Galeshi::NameComponent>()) {
            auto& nc = sel.GetComponent<Galeshi::NameComponent>();
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", nc.name.c_str());
            if (ImGui::InputText("Name", buf, sizeof(buf))) nc.name = buf;
        }

        if (sel.HasComponent<Galeshi::TransformComponent>()) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& tc = sel.GetComponent<Galeshi::TransformComponent>();
                ImGui::DragFloat3("Position", glm::value_ptr(tc.position), 0.05f);
                ImGui::DragFloat3("Scale",    glm::value_ptr(tc.scale), 0.05f, 0.01f, 100.0f);

                glm::vec3 euler = glm::degrees(glm::eulerAngles(tc.rotation));
                if (ImGui::DragFloat3("Rotation", glm::value_ptr(euler), 1.0f)) {
                    tc.rotation = glm::quat(glm::radians(euler));
                }
            }
        }

        if (sel.HasComponent<Galeshi::MaterialComponent>()) {
            if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& matc = sel.GetComponent<Galeshi::MaterialComponent>();
                if (matc.material) {
                    auto& m = *matc.material;
                    ImGui::ColorEdit4("Albedo", glm::value_ptr(m.albedoFactor));
                    ImGui::SliderFloat("Metallic",  &m.metallicFactor,  0.0f, 1.0f);
                    ImGui::SliderFloat("Roughness", &m.roughnessFactor, 0.04f, 1.0f);
                    ImGui::ColorEdit3("Emissive", glm::value_ptr(m.emissiveFactor));
                } else {
                    ImGui::TextDisabled("No material");
                }
            }
        }

        if (sel.HasComponent<Galeshi::DirectionalLightComponent>()) {
            if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& dl = sel.GetComponent<Galeshi::DirectionalLightComponent>();
                ImGui::DragFloat3("Direction", glm::value_ptr(dl.direction), 0.05f, -1.0f, 1.0f);
                ImGui::ColorEdit3("Color", glm::value_ptr(dl.color));
                ImGui::SliderFloat("Intensity", &dl.intensity, 0.0f, 10.0f);
            }
        }

        ImGui::End();
    }

    // -------------------------------------------------------------------
    // Content Browser
    // -------------------------------------------------------------------
    void DrawContentBrowser() {
        ImGui::Begin("Content Browser");
        std::string clicked = m_browser->Draw();
        if (!clicked.empty()) OnAssetDoubleClicked(clicked);
        ImGui::End();
    }

    void OnAssetDoubleClicked(const std::string& path) {
        spdlog::info("[Editor] Loading asset: {}", path);

        namespace fs = std::filesystem;
        std::string ext = fs::path(path).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));

        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
            SpawnFromModel(path, glm::vec3(0.0f));
        }
    }

    void OnAssetDropped(const std::string& path) {
        spdlog::info("[Editor] Dropped: {}", path);

        namespace fs = std::filesystem;
        std::string ext = fs::path(path).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));

        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
            // Позиция — цель камеры
            SpawnFromModel(path, m_editor.camTarget);
        }
    }

    void SpawnFromModel(const std::string& path, glm::vec3 pos) {
        PushHistory();
        auto model = m_assets->LoadModel(path);
        if (model.meshes.empty()) return;

        namespace fs = std::filesystem;
        const auto stem = fs::path(path).stem().string();

        auto root = m_scene.CreateEntity(stem);
        
        // Применяем матрицу Armature (rootTransform) к корневому Entity
        auto& tc = root.GetComponent<Galeshi::TransformComponent>();
        glm::mat4 rootMat = model.rootTransform;
        
        glm::vec3 t = glm::vec3(rootMat[3]);
        glm::vec3 sc(
            glm::length(glm::vec3(rootMat[0])),
            glm::length(glm::vec3(rootMat[1])),
            glm::length(glm::vec3(rootMat[2]))
        );
        if (sc.x == 0) sc.x = 1; if (sc.y == 0) sc.y = 1; if (sc.z == 0) sc.z = 1;
        
        glm::mat3 rotMat(
            glm::vec3(rootMat[0]) / sc.x,
            glm::vec3(rootMat[1]) / sc.y,
            glm::vec3(rootMat[2]) / sc.z
        );
        
        tc.position = pos + t;
        tc.rotation = glm::quat_cast(rotMat);
        tc.scale = sc;

        for (size_t i = 0; i < model.meshes.size(); ++i) {
            auto e = m_scene.CreateEntity(stem + "_" + std::to_string(i));
            e.AddComponent<Galeshi::ParentComponent>(Galeshi::ParentComponent{ root.Handle() });
            e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ model.meshes[i], path, "", static_cast<int>(i) });
            
            auto mat = model.materials.empty() ? std::make_shared<Galeshi::Material>() : model.materials[0];
            e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });

            if (model.totalBones > 0) {
                auto& skel = e.AddComponent<Galeshi::SkeletalComponent>();
                skel.isAnimated = true;
                skel.sourcePath = path;
                skel.boneMatrices.resize(256, glm::mat4(1.0f));
            }
        }
        m_editor.Select(root);
        m_editor.camTarget = pos;
        m_editor.camRadius = 5.0f;
    }

    // -------------------------------------------------------------------
    // Тестовая сцена
    // -------------------------------------------------------------------
    void BuildTestScene() {
        auto sun = m_scene.CreateEntity("Sun");
        auto& dl = sun.AddComponent<Galeshi::DirectionalLightComponent>();
        dl.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.5f));
        dl.color     = {1.0f, 0.97f, 0.9f};
        dl.intensity = 3.0f;

        auto mkCube = [&](const std::string& name, glm::vec3 pos,
                          glm::vec4 col, float met, float rough)
        {
            auto e = m_scene.CreateEntity(name);

            auto mesh = m_assets->CreateCubeMesh();
            e.AddComponent<Galeshi::MeshComponent>(
                Galeshi::MeshComponent{ mesh, "", "cube" });

            auto mat = std::make_shared<Galeshi::Material>();
            mat->albedoFactor    = col;
            mat->metallicFactor  = met;
            mat->roughnessFactor = rough;
            e.AddComponent<Galeshi::MaterialComponent>(
                Galeshi::MaterialComponent{ mat });

            e.GetComponent<Galeshi::TransformComponent>().position = pos;
        };

        mkCube("Cube_Orange", { 0.0f, 0.0f, 0.0f}, {0.85f, 0.55f, 0.25f, 1.0f}, 0.0f, 0.4f);
        mkCube("Cube_Metal",  {-2.5f, 0.0f, 0.0f}, {0.95f, 0.95f, 0.95f, 1.0f}, 1.0f, 0.15f);
        mkCube("Cube_Rough",  { 2.5f, 0.0f, 0.0f}, {0.30f, 0.55f, 0.85f, 1.0f}, 0.0f, 0.9f);
    }

    // --- Состояние ---
    Galeshi::Scene          m_scene;
    Galeshi::Camera         m_camera;
    Galeshi::EditorState    m_editor;
    float                   m_time = 0.0f;

    std::unique_ptr<Galeshi::AssetManager>    m_assets;
    std::unique_ptr<Galeshi::RenderSystem>    m_render;
    std::unique_ptr<Galeshi::Framebuffer>     m_framebuffer;
    std::unique_ptr<Galeshi::ContentBrowser>  m_browser;
    std::unique_ptr<Galeshi::SceneSerializer> m_serializer;

    uint16_t m_viewportW = 0, m_viewportH = 0;
    bool     m_viewportHovered = false;

    Galeshi::FilePickerDialog m_filePicker;

    std::vector<std::string> m_history;
    int  m_historyIndex = -1;
    bool m_isRestoringHistory = false;
};

int main(int, char**) {
    GaleshiEditorApp app;
    return app.Run();
}
