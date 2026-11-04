// Compile-only smoke test for the public hot-reload C++ API.
//
// The project disables the regular test suite, so this file is checked by
// scripts/check_cpp_api.sh and by the CheckCppApi CMake target.
#include "GaleshiEngine/Scripting/CppGameplayApi.hpp"

static void ExerciseApi(const GaleshiScriptApi* raw) {
    Galeshi::Script::Api api(raw);
    if (!api.Valid() || !api.Compatible()) return;

    const uint32_t entity = api.world.Create("Box");
    api.world.SetPosition(entity, {0.0f, 1.0f, 0.0f});
    api.world.SetRotation(entity, {0.0f, 0.0f, 0.0f, 1.0f});
    api.world.SetScale(entity, {1.0f, 1.0f, 1.0f});
    api.world.SetParent(entity, UINT32_MAX);
    api.world.HasComponent(entity, "transform");

    api.input.Axis("Horizontal");
    api.input.AxisRaw("Horizontal");
    api.input.MouseDown("left");
    api.input.MousePosition();
    api.input.MouseDelta();
    api.input.MouseWheel();

    api.physics.AttachBox(entity, {0.5f, 0.5f, 0.5f});
    api.physics.ApplyImpulse(entity, {0.0f, 1.0f, 0.0f});
    api.physics.DestroyBody(entity);

    api.navigation.FindPath({0.0f, 0.0f, 0.0f},
                            {10.0f, 0.0f, 10.0f});
    api.navigation.PathPointCount();

    api.audio.AttachSource(entity, "assets/sounds/hit.wav");
    api.audio.PlaySource(entity);
    api.audio.StopSource(entity);

    api.postProcess.SetFlag("bloom", true);
    api.postProcess.SetFloat("exposure", 1.0f);
    api.postProcess.SetVec3("fog_color", {0.5f, 0.6f, 0.7f});

    const uint32_t canvas = api.ui.CreateCanvas("HUD", "HUD");
    api.ui.AddWidget(canvas, "label", "text");
    api.ui.SetWidgetString(canvas, "label", "label", "Hello");
    api.ui.SetWidgetVec2(canvas, "label", "position", {20.0f, 20.0f});
    GaleshiScriptUIEvent event;
    api.ui.PollEvent(event);

    const uint32_t landscape = api.landscape.Find();
    api.landscape.SampleHeight(0.0f, 0.0f);
    api.landscape.SetHeight(0.0f, 0.0f, 1.0f);
    api.landscape.ApplyBrush(
        Galeshi::Script::LandscapeTool::Raise,
        0.0f, 0.0f, 4.0f, 3.0f, 0.65f, 0.016f);
    const uint32_t vegetation = api.vegetation.Find();
    api.vegetation.SetInt(vegetation, "tree_count", 100);
    api.vegetation.Regenerate(vegetation);

    api.animation.SetState(entity, "Idle");
    api.animation.SetParameter(entity, "Speed", 0.0f);
    api.animation.ClipCount(entity);
    api.assets.Exists("assets/models/character.fbx");

    const uint32_t particles = api.particles.Find();
    api.particles.SetFloat(particles, "emit_rate", 20.0f);
    api.particles.SetVec3(particles, "gravity", {0.0f, -9.8f, 0.0f});
    const uint32_t groom = api.groom.Find();
    api.groom.SetFloat(groom, "length", 0.2f);
    api.groom.SetVec3(groom, "wind_direction", {1.0f, 0.0f, 0.0f});

    api.world.Destroy(entity);
}

// Keep the TU alive without linking the engine. The script exports are the
// actual entry points expected by ScriptEngine.
static const GaleshiScriptApi* g_api = nullptr;

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    g_api = api;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    (void)dt;
    ExerciseApi(g_api);
}
