#include "GaleshiEngine/Core/Application.hpp"
#include "GaleshiEngine/Renderer/Renderer.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Renderer/RenderSystem.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"

#include <imgui.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

class GaleshiEditorApp final : public Galeshi::Application {
public:
    GaleshiEditorApp()
        : Galeshi::Application({
              .name = "GaleshiEditor", .width = 1600, .height = 900, .editor = true}) {}

protected:
    void OnStart() override {
        m_assets = std::make_unique<Galeshi::AssetManager>();
        m_render = std::make_unique<Galeshi::RenderSystem>();
        m_render->Init(m_assets.get());

        // --- Свет ---
        auto sun = m_scene.CreateEntity("Sun");
        auto& dl = sun.AddComponent<Galeshi::DirectionalLightComponent>();
        dl.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.5f));
        dl.color     = {1.0f, 0.97f, 0.9f};
        dl.intensity = 3.0f;

        // --- Куб 1: центр ---
        auto e1 = m_scene.CreateEntity("Cube_Orange");
        {
            auto mesh = std::make_shared<Galeshi::Mesh>();
            BuildCube(mesh.get());
            e1.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh });

            auto mat = std::make_shared<Galeshi::Material>();
            mat->albedoFactor    = {0.85f, 0.55f, 0.25f, 1.0f};
            mat->metallicFactor  = 0.0f;
            mat->roughnessFactor = 0.4f;
            e1.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });

            auto& tc = e1.GetComponent<Galeshi::TransformComponent>();
            tc.position = {0.0f, 0.0f, 0.0f};
        }

        // --- Куб 2: металл ---
        auto e2 = m_scene.CreateEntity("Cube_Metal");
        {
            auto mesh = std::make_shared<Galeshi::Mesh>();
            BuildCube(mesh.get());
            e2.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh });

            auto mat = std::make_shared<Galeshi::Material>();
            mat->albedoFactor    = {0.95f, 0.95f, 0.95f, 1.0f};
            mat->metallicFactor  = 1.0f;
            mat->roughnessFactor = 0.15f;
            e2.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });

            auto& tc = e2.GetComponent<Galeshi::TransformComponent>();
            tc.position = {-2.5f, 0.0f, 0.0f};
        }

        // --- Куб 3: шершавый ---
        auto e3 = m_scene.CreateEntity("Cube_Rough");
        {
            auto mesh = std::make_shared<Galeshi::Mesh>();
            BuildCube(mesh.get());
            e3.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh });

            auto mat = std::make_shared<Galeshi::Material>();
            mat->albedoFactor    = {0.3f, 0.55f, 0.85f, 1.0f};
            mat->metallicFactor  = 0.0f;
            mat->roughnessFactor = 0.9f;
            e3.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });

            auto& tc = e3.GetComponent<Galeshi::TransformComponent>();
            tc.position = {2.5f, 0.0f, 0.0f};
        }
    }

    void OnUpdate(float dt) override {
        m_time += dt;
        const auto* r = GetRenderer();
        m_camera.SetPerspective(glm::radians(60.0f),
            static_cast<float>(r->Width()) / static_cast<float>(r->Height()),
            0.1f, 1000.0f);
        m_camera.Orbit(glm::vec3(0.0f), m_time * 0.4f, 0.35f, 8.0f);
    }

    void OnRender() override {
        auto* r = GetRenderer();
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                           0x1a1d21ff, 1.0f, 0);
        m_render->Render(m_scene, m_camera, 0,
                         static_cast<uint16_t>(r->Width()),
                         static_cast<uint16_t>(r->Height()));
    }

    void OnImGui() override {
        ImGui::Begin("Viewport");
        ImGui::Text("Stage 3 — PBR + Scene (EnTT)");
        ImGui::Text("Backend: %s", bgfx::getRendererName(bgfx::getRendererType()));
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();

        ImGui::Begin("World Outliner");
        for (auto& e : m_scene.AllEntities()) {
            auto& nc = e.GetComponent<Galeshi::NameComponent>();
            ImGui::BulletText("%s", nc.name.c_str());
        }
        ImGui::End();

        ImGui::Begin("Details");   ImGui::Text("(not wired yet)");  ImGui::End();
        ImGui::Begin("Content Browser"); ImGui::Text("assets/");   ImGui::End();
    }

    void OnShutdown() override {
        if (m_render) m_render->Shutdown();
        m_assets.reset();
        m_render.reset();
    }

private:
    void BuildCube(Galeshi::Mesh* mesh) {
        struct V { float x, y, z, nx, ny, nz, tx, ty, tz, u, v; };
        static const V verts[24] = {
            // +X
            { 1,-1,-1,  1,0,0, 0,0,-1, 0,0}, { 1, 1,-1,  1,0,0, 0,0,-1, 0,1},
            { 1, 1, 1,  1,0,0, 0,0,-1, 1,1}, { 1,-1, 1,  1,0,0, 0,0,-1, 1,0},
            // -X
            {-1,-1, 1, -1,0,0, 0,0,1, 0,0}, {-1, 1, 1, -1,0,0, 0,0,1, 0,1},
            {-1, 1,-1, -1,0,0, 0,0,1, 1,1}, {-1,-1,-1, -1,0,0, 0,0,1, 1,0},
            // +Y
            {-1, 1,-1,  0,1,0, 1,0,0, 0,0}, {-1, 1, 1,  0,1,0, 1,0,0, 0,1},
            { 1, 1, 1,  0,1,0, 1,0,0, 1,1}, { 1, 1,-1,  0,1,0, 1,0,0, 1,0},
            // -Y
            {-1,-1, 1,  0,-1,0,1,0,0, 0,0}, {-1,-1,-1,  0,-1,0,1,0,0, 0,1},
            { 1,-1,-1,  0,-1,0,1,0,0, 1,1}, { 1,-1, 1,  0,-1,0,1,0,0, 1,0},
            // +Z
            {-1,-1, 1,  0,0,1, 1,0,0, 0,0}, {-1, 1, 1,  0,0,1, 1,0,0, 0,1},
            { 1, 1, 1,  0,0,1, 1,0,0, 1,1}, { 1,-1, 1,  0,0,1, 1,0,0, 1,0},
            // -Z
            { 1,-1,-1,  0,0,-1,-1,0,0, 0,0}, { 1, 1,-1,  0,0,-1,-1,0,0, 0,1},
            {-1, 1,-1,  0,0,-1,-1,0,0, 1,1}, {-1,-1,-1,  0,0,-1,-1,0,0, 1,0},
        };
        static const uint32_t idx[36] = {
            0,1,2, 0,2,3,   4,5,6, 4,6,7,
            8,9,10, 8,10,11, 12,13,14, 12,14,15,
            16,17,18, 16,18,19, 20,21,22, 20,22,23,
        };

        std::vector<Galeshi::Vertex> vs;
        vs.reserve(24);
        for (auto& v : verts) {
            Galeshi::Vertex vert{};
            vert.position = {v.x, v.y, v.z};
            vert.normal   = {v.nx, v.ny, v.nz};
            vert.tangent  = {v.tx, v.ty, v.tz};
            vert.uv       = {v.u, v.v};
            vs.push_back(vert);
        }
        std::vector<uint32_t> is(idx, idx + 36);
        mesh->Upload(vs, is);
    }

    Galeshi::Scene       m_scene;
    Galeshi::Camera      m_camera;
    float                m_time = 0.0f;
    std::unique_ptr<Galeshi::AssetManager> m_assets;
    std::unique_ptr<Galeshi::RenderSystem> m_render;
};

int main(int, char**) {
    GaleshiEditorApp app;
    return app.Run();
}
