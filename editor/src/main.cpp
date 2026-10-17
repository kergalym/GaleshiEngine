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

        BuildTestScene();

        m_camera.SetPerspective(glm::radians(60.0f),
            static_cast<float>(GetRenderer()->Width()) /
            static_cast<float>(GetRenderer()->Height()),
            0.1f, 1000.0f);
    }

    void OnUpdate(float dt) override {
        m_time += dt;

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
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
                if (ImGui::MenuItem("Load Scene", "Ctrl+O")) LoadScene();
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) std::exit(0);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
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
                if (ImGui::IsKeyPressed(ImGuiKey_S)) SaveScene();
                if (ImGui::IsKeyPressed(ImGuiKey_O)) LoadScene();
            }
        }

        DrawDockspace();
        DrawViewport();
        DrawOutliner();
        DrawDetails();
        DrawContentBrowser();
    }

    void OnShutdown() override {
        m_render.reset();
        m_framebuffer.reset();
        m_serializer.reset();
        m_browser.reset();
        m_assets.reset();
    }

private:
    // -------------------------------------------------------------------
    // Save / Load
    // -------------------------------------------------------------------
    void SaveScene() {
        m_serializer->Save("assets/scene.json");
    }
    void LoadScene() {
        namespace fs = std::filesystem;
        if (!fs::exists("assets/scene.json")) {
            spdlog::warn("[Editor] No scene.json found");
            return;
        }
        m_editor.ClearSelection();
        m_serializer->Load("assets/scene.json");
    }

    void DeleteSelected() {
        if (!m_editor.HasSelection()) return;
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

        glm::mat4 model = tc.Matrix();

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
    void DrawOutliner() {
        ImGui::Begin("World Outliner");

        const bool ctrlHeld = ImGui::GetIO().KeyCtrl;

        auto entities = m_scene.AllEntities();
        for (auto& e : entities) {
            auto& nc = e.GetComponent<Galeshi::NameComponent>();

            const bool isSelected = m_editor.IsSelected(e);
            if (ImGui::Selectable(nc.name.c_str(), isSelected)) {
                if (ctrlHeld) {
                    if (isSelected) m_editor.RemoveFromSelection(e);
                    else            m_editor.AddToSelection(e);
                } else {
                    m_editor.Select(e);
                }
            }
        }

        if (ImGui::BeginPopupContextWindow("outliner_ctx")) {
            if (ImGui::MenuItem("Create Empty")) {
                auto e = m_scene.CreateEntity("New Entity");
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Delete Selected") && m_editor.HasSelection()) {
                DeleteSelected();
            }
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
        auto model = m_assets->LoadModel(path);
        if (model.meshes.empty()) {
            spdlog::warn("[Editor] Model has no meshes: {}", path);
            return;
        }

        namespace fs = std::filesystem;
        auto name = fs::path(path).stem().string();
        auto e = m_scene.CreateEntity(name);

        e.AddComponent<Galeshi::MeshComponent>(
            Galeshi::MeshComponent{ model.meshes[0], path, "" });

        auto mat = model.materials.empty()
                 ? std::make_shared<Galeshi::Material>()
                 : model.materials[0];
        e.AddComponent<Galeshi::MaterialComponent>(
            Galeshi::MaterialComponent{ mat });

        e.GetComponent<Galeshi::TransformComponent>().position = pos;

        m_editor.Select(e);
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
};

int main(int, char**) {
    GaleshiEditorApp app;
    return app.Run();
}
