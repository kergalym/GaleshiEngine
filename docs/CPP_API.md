# GaleshiEngine C++ API

For a conceptual explanation of the two integration modes and the module ABI,
see [CPP_API_MODES.md](CPP_API_MODES.md).

The engine exposes two complementary C++ surfaces:

1. **Native API** — link against `GaleshiEngine::GaleshiEngine` and include
   `GaleshiEngine/GaleshiEngine.hpp`. This exposes the real engine classes and
   is intended for games, plugins and tools built in the same build.
2. **Hot-reload Script API** — include
   `GaleshiEngine/Scripting/CppScriptApi.hpp` (or the class wrappers in
   `CppGameplayApi.hpp`). Scripts are compiled into shared libraries and run
   in `GaleshiScriptWorker`; they never link the engine.

Editor-only classes (`ContentBrowser`, `EditorState`, `FilePickerDialog`) are
not part of the public API.

## Native API

```cpp
#include <GaleshiEngine/GaleshiEngine.hpp>

class MyGame final : public Galeshi::Application {
protected:
    void OnStart() override {
        m_physics.Init();
        m_context.scene = &m_scene;
        m_context.assets = m_assets.get();
        m_context.renderer = GetRenderer();
        m_context.input = &GetInput();
        m_context.physics = &m_physics;
        m_context.audio = &m_audio;
    }

    void OnUpdate(float dt) override {
        m_physics.Update(m_scene, dt);
        m_audio.Update(m_scene, dt);
    }

private:
    Galeshi::Scene m_scene;
    std::unique_ptr<Galeshi::AssetManager> m_assets =
        std::make_unique<Galeshi::AssetManager>();
    Galeshi::EnginePhysicsSystem m_physics;
    Galeshi::AudioSystem m_audio;
    Galeshi::Api::EngineContext m_context;
};
```

`Galeshi::Api::EngineContext` is a non-owning convenience struct: fill the
fields for the subsystems your game uses. Subsystems remain explicitly
constructed and updated by the game; the context does not hide lifetime or
threading rules.

The public umbrella currently includes:

- `Application`, `JobSystem`, `ProjectManager`
- `Scene`, `Entity`, all public ECS components, `SceneSerializer`
- `AssetManager`, `Mesh`, `Material`, `Texture`, landscape and vegetation
- `InputAxisSystem`
- `EnginePhysicsSystem`
- `NavigationSystem`, `AISystem`, behavior trees
- `AnimationSystem`
- `AudioSystem`, audio types and mixer buses
- `Renderer`, `RenderSystem`, `PostProcessManager`, camera, framebuffer,
  frustum, debug renderer, groom, particles, shader manager, ImGui bridge
- `UIRuntime` and UI widget data
- `ScriptEngine`
- stable script API and class wrappers

## Hot-reload C++ Script API

The existing stable ABI is extended append-only. New fields are added at the
end of `GaleshiScriptApi`; old modules keep their known offsets and continue
to work. New modules can check `abi_version` and `struct_size`.

```cpp
#include "GaleshiEngine/Scripting/CppGameplayApi.hpp"

static Galeshi::Script::Api* g_api = nullptr;

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    static Galeshi::Script::Api wrapper(api);
    g_api = &wrapper;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    const uint32_t player = g_api->world.Find("Player");
    if (!g_api->world.Valid(player)) return;
    g_api->physics.SetVelocity(player, {0.0f, 0.0f, -5.0f});
}
```

Current class wrappers cover:

- `World` — create/destroy/find/valid, names, local position/rotation/scale,
  parent/detach, component presence, velocity
- `Input` — key state, dead-zoned/raw axes, mouse buttons, position, delta,
  wheel
- `Physics` — linear velocity, attach box/sphere/capsule, destroy body,
  impulse
- `Navigation` — navmesh build, path query and path points
- `Audio` — 2D play/stop/state, 3D sources, stop-all and mixer buses
- `Camera` — mode, basis and camera-relative movement
- `Player` — strafing
- `MaterialAccess` — material factors, flags, colors and texture slots
- `Rendering` — renderer size and deferred/LOD/occlusion toggles
- `PostProcess` — scene-wide post-process flags, floats, ints and colors by name
- `UI` — canvas/widget lookup, creation/removal, common properties and event polling
- `AI` — nav agent/perception/behavior-tree properties and node editing
- `Landscape` — height/brush tools, layers, layer textures, paint and bake
- `Vegetation` — tree/grass settings, regeneration, clear and instance counts
- `Animation` — model spawn/apply, state machine parameters/states, clip playback and clip info
- `Assets` — existence checks, path resolution and model loading
- `Particles` / `Groom` — component property editing
- `Shader` — compile and error text

Every worker request is a `CppWorkerPacket`; the host answers in
`ScriptEngine::MakeWorkerResponse`. New API entries must add:

1. a function pointer at the end of `GaleshiScriptApi`;
2. a `CppWorkerRequest` value;
3. a worker-side callback that sends the request;
4. a host-side handler in `MakeWorkerResponse`;
5. a wrapper method in `CppGameplayApi.hpp`;
6. documentation and an example.

## Subtasks

### 1. API foundation — done

- Public umbrella header and `GaleshiEngine::GaleshiEngine` alias.
- Non-owning `Api::EngineContext` with all public subsystem pointers.
- API/script ABI version constants.
- `CppGameplayApi.hpp` class wrappers.
- Moved the miniaudio implementation out of the public header.
- Forward-declared Jolt's `JobSystem` in the public physics header.
- Compile-check translation unit (`PublicApi.cpp`).

### 2. World and ECS — done

Native surface: already available through `Scene`, `Entity`, `entt` and
components.

Script surface:

- create/destroy entity, find by name, enumerate by component;
- get/set name;
- local/world transform, rotation, scale and parent;
- component presence and typed component access;
- transform interpolation helpers;
- entity lifetime guards and generation-safe handles.

### 3. Input, camera and player — done

Native surface: `InputAxisSystem`, `Camera`, player/camera components.

Script surface:

- key and mouse state, mouse position/delta/wheel;
- raw and dead-zoned axes;
- camera mode, forward/right/movement;
- player strafing and gameplay-camera binding;
- action rebinding where the host allows it.

### 4. Physics — done (rigid bodies)

Native surface: `EnginePhysicsSystem`.

Script surface:

- create/destroy box, sphere, capsule and collider-backed entities;
- static/kinematic/dynamic modes, mass, gravity, friction/restitution;
- impulse, force, linear/angular velocity;
- sphere/ray casts and contact events;
- cloth creation/update;
- entity-to-body and body-to-entity mapping.

### 5. Navigation and AI — done

Native surface: `NavigationSystem`, `AISystem`, behavior trees and perception.

Script surface:

- build navmesh and query path results;
- NavAgent destination/speed/waypoints;
- behavior-tree parameters and state;
- perception queries and target selection;
- AI controller lifecycle.

### 6. Audio, including 3D — done

Native surface: `AudioSystem`, `AudioSourceComponent`,
`AudioListenerComponent` and scene mixer buses.

Script surface:

- 2D play/stop/state;
- 3D source play/stop/state with attenuation and offset;
- listener selection (camera or entity);
- bus volume/mute;
- one-shot and looping playback;
- entity-attached source controls.

### 7. Rendering and post-processing — done (material/renderer/post-process)

Native surface: `Renderer`, `RenderSystem`, `PostProcessManager`,
`PostProcessSettings` and material/texture classes.

Script surface:

- renderer resolution/vsync/debug information where safe;
- post-process toggles and scalar parameters;
- scene global post-process overrides;
- material map/factor access where the host permits it;
- debug draw primitives;
- shader compile/error/preview.

### 8. UI and widgets — done (configuration, events, text values)

Native surface: `UIRuntime`, `UICanvas`, `UIWidget`.

Script surface:

- create/remove canvases and widgets;
- position, size, text, color, value and visibility;
- event polling plus handler registration;
- pointer-over-UI query;
- runtime text-input values.

### 9. Landscape, vegetation and instancing — done

Native surface: `LandscapeComponent`, `LandscapeMesh`,
`VegetationComponent`, `VegetationGenerator`.

Script surface:

- height/weight sampling and brush application;
- landscape layer configuration and texture bake;
- tree/grass counts, seed, scale and slope;
- tree types and per-mesh leaf material sets;
- regenerate/clear instances;
- paint trees/grass.

### 10. Animation and assets — done

Native surface: `AnimationSystem`, `AssetManager`, skeletal components.

Script surface:

- load models and inspect meshes/clips;
- spawn skeletal actors or apply models to entities;
- play/stop/blend clips and set state-machine parameters;
- animation state and event queries;
- asset path resolution and availability checks.

### 11. Particles and groom — done

Native surface: `ParticleSystem`, `GroomSystem`.

Script surface:

- emitter parameters and runtime spawn/burst;
- particle counts and alive queries;
- groom enable/density/length/wind parameters.

### 12. Compatibility, tests and docs — done

- Add an API contract test that compiles the umbrella header and a sample
  script module.
- Add worker protocol round-trip tests for every new request.
- Keep append-only ABI rules and bump `kScriptApiVersion` when semantics change.
- Add one runnable example per subsystem under `examples/` and link it from
  this document.

## API smoke test

`scripts/check_cpp_api.sh` compiles `tests/cpp_api_smoke.cpp` against the
public script headers without linking the engine. This catches header,
wrapper and ABI-layout regressions.

`tests/cpp_worker_roundtrip.py` compiles a tiny script module, starts
`GaleshiScriptWorker`, answers a representative set of worker requests with
mock host responses, and verifies the protocol and callback payloads.
