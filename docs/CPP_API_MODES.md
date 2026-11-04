# C++ API Modes: Native vs Hot-Reload Scripts

This document explains the two C++ integration paths in GaleshiEngine and why
the hot-reload script path uses `extern "C"` entry points even though scripts
are written in C++.

See also:

- [CPP_API.md](CPP_API.md) — public API overview, subsystem coverage and roadmap
- [CPP_SCRIPTS.md](CPP_SCRIPTS.md) — hot-reload script workflow and examples

## At a glance

| | Native API | Hot-reload scripts |
|---|---|---|
| Header | `GaleshiEngine/GaleshiEngine.hpp` | `GaleshiEngine/Scripting/CppScriptApi.hpp` or `CppGameplayApi.hpp` |
| Links with the engine | Yes | No |
| Direct access to engine classes | Yes: `Scene`, `RenderSystem`, `EnginePhysicsSystem`, etc. | No: handles and API calls only |
| Hot reload | No | Yes |
| Process isolation | Runs inside the game/editor process | Runs in a separate `GaleshiScriptWorker` process |
| Use for | Games, plugins, tools that need real engine objects | Gameplay logic that should be edited and reloaded quickly |

Both paths are C++. The difference is where the code runs and what kind of
objects can cross the boundary.

## Native API

### What it is

The native API is the actual public C++ class surface of the engine. It is
exposed through:

```cpp
#include <GaleshiEngine/GaleshiEngine.hpp>
```

and linked through CMake:

```cmake
target_link_libraries(MyGame PRIVATE GaleshiEngine::GaleshiEngine)
```

The umbrella header includes the public engine classes:

- `Application`, `JobSystem`, `ProjectManager`
- `Scene`, `Entity`, ECS components, `SceneSerializer`
- `AssetManager`, `Mesh`, `Material`, `Texture`
- `EnginePhysicsSystem`, `NavigationSystem`, `AISystem`, behavior trees
- `InputAxisSystem`, `AnimationSystem`
- `AudioSystem` and audio types
- `Renderer`, `RenderSystem`, `PostProcessManager`, cameras, particles,
  groom, debug rendering
- `UIRuntime` and UI widget data
- `LandscapeComponent`, `LandscapeMesh`, `VegetationComponent`,
  `VegetationGenerator`
- `ScriptEngine`
- Stable script API and class wrappers

Editor-only classes such as `ContentBrowser`, `EditorState` and
`FilePickerDialog` are intentionally excluded.

### EngineContext

`Galeshi::Api::EngineContext` is an optional non-owning convenience struct.
It groups pointers to the subsystems a game normally owns:

```cpp
struct EngineContext {
    Renderer* renderer = nullptr;
    InputAxisSystem* input = nullptr;
    Scene* scene = nullptr;
    AssetManager* assets = nullptr;
    RenderSystem* renderSystem = nullptr;
    PostProcessManager* postProcess = nullptr;
    EnginePhysicsSystem* physics = nullptr;
    NavigationSystem* navigation = nullptr;
    AnimationSystem* animation = nullptr;
    AudioSystem* audio = nullptr;
    UIRuntime* ui = nullptr;
    ParticleSystem* particles = nullptr;
    GroomSystem* groom = nullptr;
    DebugRenderer* debugRenderer = nullptr;
    ScriptEngine* scripting = nullptr;
    SceneSerializer* serializer = nullptr;
    JobSystem* jobs = nullptr;
};
```

`EngineContext` does not create, own or update subsystems. The game keeps
explicit ownership and lifetime:

```cpp
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

### When to use it

Use the native API when the code needs:

- real engine objects and pointers;
- custom rendering or asset pipelines;
- direct `entt` access;
- engine subsystem lifetime control;
- in-process performance without worker RPC;
- a standalone game, editor extension, plugin or tool.

The trade-off is that the code is linked to the engine and is not hot-reloaded.

## Hot-reload scripts

### What they are

A hot-reload script is a `.cpp`, `.cc` or `.cxx` file placed in the project:

```text
<project>/scripts/
<project>/assets/scripts/
```

The editor compiles changed files into temporary shared libraries and loads
them into `GaleshiScriptWorker`. The worker process contains the scripts, but
not the editor scene, renderer or GPU resources.

A script module exports two C-linkage entry points:

```cpp
extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api);
extern "C" void GaleshiScriptUpdate(float dt);
```

Example using the class wrappers:

```cpp
#include "GaleshiEngine/Scripting/CppGameplayApi.hpp"

static Galeshi::Script::Api* g_api = nullptr;

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    static Galeshi::Script::Api wrapper(api);
    g_api = &wrapper;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    if (!g_api || !g_api->Compatible()) return;

    const uint32_t player = g_api->world.Find("Player");
    if (!g_api->world.Valid(player)) return;

    const Galeshi::Script::Vec3 velocity =
        g_api->physics.Velocity(player);
    g_api->physics.SetVelocity(player, {0.0f, velocity.y, -5.0f});
}
```

The class wrappers in `CppGameplayApi.hpp` are header-only. They do not link
the engine. They call function pointers supplied through `GaleshiScriptApi`.

### Lifecycle

```text
Editor / ScriptEngine (host process)
        │
        │ 1. compile changed script to .so
        │ 2. send Load request
        ▼
GaleshiScriptWorker (separate process)
        │
        │ 3. dlopen(.so)
        │ 4. dlsym("GaleshiScriptRegister")
        │ 5. GaleshiScriptRegister(&api)
        │
        │ 6. every frame:
        │    dlsym("GaleshiScriptUpdate")
        │    GaleshiScriptUpdate(dt)
        │
        │ 7. API calls become CppWorkerPacket requests
        ▼
Editor / ScriptEngine executes the request on the real scene
```

`GaleshiScriptRegister` runs once when the module is loaded. It receives a
pointer to the host API table and typically stores it for later frames.

`GaleshiScriptUpdate` runs once per frame with the delta time. It performs
gameplay work through the stored API table.

### Why the API is a function table

The worker process does not link `libGaleshiEngine`. It cannot call
`Scene::CreateEntity`, `RenderSystem::Render` or any other engine symbol
directly. Instead, the host passes a table of function pointers:

```cpp
struct GaleshiScriptApi {
    void* user = nullptr;
    void (*log)(void* user, const char* message) = nullptr;
    // ...
    uint32_t (*create_entity)(void* user, const char* name) = nullptr;
    // ...
};
```

The `user` pointer identifies the worker-side API context. Each callback
forwards a request to the host and returns the result. This provides:

- a stable, controlled boundary between the worker and the engine;
- no requirement to link the engine into the script module;
- process isolation for crashes and hangs;
- the ability to unload and reload modules;
- no raw engine pointers crossing process boundaries.

The table is append-only for compatibility. New functions are added at the
end. Scripts can check `abi_version` and `struct_size`, and the C++ wrapper
exposes `Api::Compatible()`.

## Why `extern "C"`?

`extern "C"` is a linkage specification. It does not mean:

- the file is C;
- the code must be written in C;
- C++ classes and templates cannot be used inside the module.

It means the exported symbol has C linkage and therefore a stable name.

A C++ function such as:

```cpp
void GaleshiScriptUpdate(float dt);
```

is emitted with a compiler- and namespace-dependent mangled name, for example:

```text
_Z19GaleshiScriptUpdatef
```

The worker loads modules with:

```cpp
void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
auto registerFn = reinterpret_cast<GaleshiScriptRegisterFn>(
    dlsym(library, "GaleshiScriptRegister"));
auto updateFn = reinterpret_cast<GaleshiScriptUpdateFn>(
    dlsym(library, "GaleshiScriptUpdate"));
```

`dlsym` looks for the exact symbol name. Without `extern "C"` the name depends
on the compiler, namespace and parameter types, so the lookup is not portable
or stable.

With:

```cpp
extern "C" void GaleshiScriptUpdate(float dt);
```

the symbol is exactly `GaleshiScriptUpdate`.

The module can still use C++ internally:

```cpp
#include <string>
#include <vector>

static std::string g_message = "hello";

extern "C" void GaleshiScriptUpdate(float dt) {
    (void)dt;
    std::vector<int> values{1, 2, 3};
    g_message += "!";
}
```

Only the boundary is C-like. C++ objects do not cross the `.so` boundary.

## Why two exported functions?

The two functions implement a minimal plugin lifecycle:

1. **Register** — called once per load.
   - Receives the host API table.
   - Lets the module initialize state.
   - Avoids a global symbol lookup for every API call.

2. **Update** — called every frame.
   - Receives `dt`.
   - Runs gameplay code.

This separation makes module loading and reloading straightforward. A new
module can be loaded, registered and updated without restarting the worker or
the editor.

An alternative design would export a C++ object with a virtual `Update`
method. That would require passing C++ vtables, RTTI, exceptions and allocator
ownership across a shared-library boundary. Two plain function pointers are
simpler and more stable.

## Why scripts cannot use engine classes directly

This may compile:

```cpp
#include <GaleshiEngine/Scene/Scene.hpp>

void GaleshiScriptUpdate(float dt) {
    // Missing engine symbol at runtime in the worker process.
    Galeshi::Scene scene;
    scene.CreateEntity("Player");
}
```

but it will fail at load time or at the first call because
`GaleshiScriptWorker` does not link the engine library. The engine objects and
GPU resources live in the editor process, not in the worker.

The hot-reload API therefore uses:

- entity handles (`uint32_t`) instead of `Entity*`;
- numbers, vectors and strings instead of `glm::vec3`/`std::string` objects
  crossing the binary boundary;
- explicit operations such as `create_entity`, `set_position` and
  `play_sound`.

If direct engine classes are required, use the native API instead.

## Choosing an integration path

Choose **native API** when:

- the code is part of the game executable or a native plugin;
- it needs real engine classes and pointers;
- it owns scene, physics, audio or rendering systems;
- hot reload is not required;
- maximum in-process performance matters.

Choose **hot-reload scripts** when:

- the code is gameplay or level logic;
- fast iteration and hot reload matter;
- crash isolation is useful;
- the available script API is sufficient;
- passing handles rather than raw engine pointers is acceptable.

A project can use both: native systems in the game application and
hot-reload scripts for gameplay behavior.

## Common mistakes

### Using engine headers in a hot-reload module

Including `GaleshiEngine.hpp` in a hot-reload module does not make the worker
link the engine. Use `CppScriptApi.hpp` or `CppGameplayApi.hpp` instead.

### Assuming `extern "C"` means C

The file remains a C++ translation unit. `extern "C"` only changes the
linkage and symbol names of the exported functions.

### Storing raw engine pointers across the worker boundary

The worker and editor are separate processes. A pointer allocated in the
editor process is not valid in the worker process. Use entity IDs and API
operations.

### Calling the API before registration

`g_api` must be assigned in `GaleshiScriptRegister`. Do not call API
callbacks from static initialization before registration.

### Ignoring ABI checks

New scripts should check the API version when using recently added methods:

```cpp
if (!api.Compatible()) return;
```

The wrappers also null-check individual function pointers, so older hosts
fail safely instead of crashing.

## Further reading

- [CPP_API.md](CPP_API.md) — full public API coverage and roadmap
- [CPP_SCRIPTS.md](CPP_SCRIPTS.md) — hot-reload workflow, templates and examples
- [THIRD_PERSON_DEMO.md](THIRD_PERSON_DEMO.md) — setup and tuning for the
  third-person sample
- [third_person_demo.cpp](third_person_demo.cpp) — complete gameplay script
- [LUA_API.md](LUA_API.md) — the Lua scripting equivalent
