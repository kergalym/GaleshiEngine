#include "GaleshiEngine/Core/Application.hpp"
#include "GaleshiEngine/Renderer/Renderer.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Renderer/ShaderManager.hpp"

#include <imgui.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdint>

class GaleshiEditorApp final : public Galeshi::Application {
public:
    GaleshiEditorApp()
        : Galeshi::Application({
              .name   = "GaleshiEditor",
              .width  = 1600,
              .height = 900,
              .editor = true,
          }) {}

protected:
    void OnStart() override {
        auto* r = GetRenderer();
        m_camera.SetPerspective(
            glm::radians(60.0f),
            static_cast<float>(r->Width()) / static_cast<float>(r->Height()),
            0.1f, 1000.0f);

        BuildCube();

        m_program = Galeshi::ShaderManager::LoadProgram(
            "shaders/vs_cube.bin", "shaders/fs_cube.bin");

        u_albedo     = bgfx::createUniform("u_albedo",     bgfx::UniformType::Vec4);
        u_lightDir   = bgfx::createUniform("u_lightDir",   bgfx::UniformType::Vec4);
        u_lightColor = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
        u_cameraPos  = bgfx::createUniform("u_cameraPos",  bgfx::UniformType::Vec4);
    }

    void OnUpdate(float dt) override {
        m_time += dt;

        auto* r = GetRenderer();
        m_camera.SetPerspective(
            glm::radians(60.0f),
            static_cast<float>(r->Width()) / static_cast<float>(r->Height()),
            0.1f, 1000.0f);

        m_camera.Orbit(glm::vec3(0.0f), m_time * 0.4f, 0.45f, 4.5f);
    }

    void OnRender() override {
        if (!bgfx::isValid(m_program)) return;

        auto* r = GetRenderer();

        const glm::mat4 model = glm::rotate(
            glm::mat4(1.0f),
            m_time * 0.7f,
            glm::vec3(0.0f, 1.0f, 0.0f));

        bgfx::setTransform(glm::value_ptr(model));

        const glm::mat4 vp = m_camera.ViewProj();
        bgfx::setViewTransform(0, nullptr, glm::value_ptr(vp));
        bgfx::setViewRect(0, 0, 0,
            static_cast<uint16_t>(r->Width()),
            static_cast<uint16_t>(r->Height()));

        const float albedo[4]     = {0.85f, 0.55f, 0.25f, 1.0f};
        const float lightDir[4]   = {-0.4f, -0.8f, -0.5f, 1.2f};
        const float lightColor[4] = {1.00f, 0.97f, 0.90f, 1.0f};
        const glm::vec3 p         = m_camera.Position();
        const float camPos[4]     = {p.x, p.y, p.z, 1.0f};

        bgfx::setUniform(u_albedo,     albedo);
        bgfx::setUniform(u_lightDir,   lightDir);
        bgfx::setUniform(u_lightColor, lightColor);
        bgfx::setUniform(u_cameraPos,  camPos);

        bgfx::setVertexBuffer(0, m_vb);
        bgfx::setIndexBuffer(m_ib);

        // УБРАН BGFX_STATE_CULL_CW — он отсекал все видимые грани.
        bgfx::setState(
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
            BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_MSAA);

        bgfx::submit(0, m_program);
    }

    void OnImGui() override {
        ImGui::Begin("Viewport");
        ImGui::Text("GaleshiEngine — Stage 2 renderer");
        ImGui::Text("Backend: %s", bgfx::getRendererName(bgfx::getRendererType()));
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();

        ImGui::Begin("World Outliner");   ImGui::Text("(empty scene)");      ImGui::End();
        ImGui::Begin("Details");          ImGui::Text("No selection");       ImGui::End();
        ImGui::Begin("Content Browser");  ImGui::Text("assets/");            ImGui::End();
    }

    void OnShutdown() override {
        if (bgfx::isValid(m_vb))      bgfx::destroy(m_vb);
        if (bgfx::isValid(m_ib))      bgfx::destroy(m_ib);
        if (bgfx::isValid(m_program)) bgfx::destroy(m_program);

        if (bgfx::isValid(u_albedo))     bgfx::destroy(u_albedo);
        if (bgfx::isValid(u_lightDir))   bgfx::destroy(u_lightDir);
        if (bgfx::isValid(u_lightColor)) bgfx::destroy(u_lightColor);
        if (bgfx::isValid(u_cameraPos))  bgfx::destroy(u_cameraPos);
    }

private:
    void BuildCube() {
        struct V { float x, y, z, nx, ny, nz; };

        static const V verts[24] = {
            // +X
            { 1,-1,-1,  1,0,0}, { 1, 1,-1,  1,0,0}, { 1, 1, 1,  1,0,0}, { 1,-1, 1,  1,0,0},
            // -X
            {-1,-1, 1, -1,0,0}, {-1, 1, 1, -1,0,0}, {-1, 1,-1, -1,0,0}, {-1,-1,-1, -1,0,0},
            // +Y
            {-1, 1,-1,  0,1,0}, {-1, 1, 1,  0,1,0}, { 1, 1, 1,  0,1,0}, { 1, 1,-1,  0,1,0},
            // -Y
            {-1,-1, 1,  0,-1,0},{-1,-1,-1,  0,-1,0},{ 1,-1,-1,  0,-1,0},{ 1,-1, 1,  0,-1,0},
            // +Z
            {-1,-1, 1,  0,0,1}, {-1, 1, 1,  0,0,1}, { 1, 1, 1,  0,0,1}, { 1,-1, 1,  0,0,1},
            // -Z
            { 1,-1,-1,  0,0,-1},{ 1, 1,-1,  0,0,-1},{-1, 1,-1,  0,0,-1},{-1,-1,-1,  0,0,-1},
        };

        static const uint16_t idx[36] = {
            0,1,2, 0,2,3,   4,5,6, 4,6,7,
            8,9,10, 8,10,11, 12,13,14, 12,14,15,
            16,17,18, 16,18,19, 20,21,22, 20,22,23,
        };

        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal,   3, bgfx::AttribType::Float)
            .end();

        m_vb = bgfx::createVertexBuffer(bgfx::copy(verts, sizeof(verts)), layout);
        m_ib = bgfx::createIndexBuffer(bgfx::copy(idx, sizeof(idx)));
    }

    Galeshi::Camera m_camera;
    float m_time = 0.0f;

    bgfx::VertexBufferHandle m_vb      = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_ib      = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle      m_program = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle u_albedo     = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightDir   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cameraPos  = BGFX_INVALID_HANDLE;
};

int main(int, char**) {
    GaleshiEditorApp app;
    return app.Run();
}
