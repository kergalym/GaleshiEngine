# C++ gameplay scripts

For a complete third-person gameplay sample, see
[THIRD_PERSON_DEMO.md](THIRD_PERSON_DEMO.md) and
[third_person_demo.cpp](third_person_demo.cpp).

For the distinction between native C++ and hot-reload scripts, and for a
detailed explanation of the `extern "C"` module ABI, see
[CPP_API_MODES.md](CPP_API_MODES.md).

Place `.cpp`, `.cc` or `.cxx` files in the project's `scripts/` or
`assets/scripts/` directory. The editor compiles changed files into temporary
shared libraries and loads them in a separate `GaleshiScriptWorker` process, so
crashes, hangs and compile failures cannot take down the editor.

A script module must export two C functions:

```cpp
#include "GaleshiEngine/Scripting/CppScriptApi.hpp"

static const GaleshiScriptApi* g_api = nullptr;

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    g_api = api;
    g_api->log(g_api->user, "script registered");
}

extern "C" void GaleshiScriptUpdate(float dt) {
    const uint32_t player = g_api->find_entity(g_api->user, "Player");
    if (!g_api->entity_valid(g_api->user, player)) return;

    const GaleshiScriptVec3 target{0.0f, 2.0f, 0.0f};
    const GaleshiScriptVec3 current = g_api->get_position(g_api->user, player);
    const float t = 1.0f - std::exp(-8.0f * dt);
    // FInterp/VInterp are also provided as inline helpers and API callbacks.
    const GaleshiScriptVec3 next = VInterp(current, target, dt, 8.0f);
    g_api->set_position(g_api->user, player, next);
}
```

The API provides logging, input, entity lookup/validation, position and physics
velocity access, gameplay camera mode switching, plus `FInterp` and `VInterp`.
Scripts are intentionally compiled with a stable C ABI so the engine can be
rebuilt independently.

Class-based wrappers are available from
`GaleshiEngine/Scripting/CppGameplayApi.hpp`:

```cpp
#include "GaleshiEngine/Scripting/CppGameplayApi.hpp"

static Galeshi::Script::Api* g_api = nullptr;

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    static Galeshi::Script::Api wrapper(api);
    g_api = &wrapper;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    (void)dt;
    const uint32_t player = g_api->world.Find("Player");
    if (!g_api->world.Valid(player)) return;
    g_api->physics.SetVelocity(player, {0.0f, 0.0f, -5.0f});
}
```

New modules can check `g_api->AbiVersion()` / `Compatible()`. The C++ API
surface and subsystem expansion plan are documented in
[CPP_API.md](CPP_API.md).

### Optional trigger overlap callbacks

A module may also export either function. They are optional, looked up when
the module loads, and can be omitted independently:

```cpp
extern "C" void OnTriggerBeginOverlap(uint32_t trigger, uint32_t other) {
    // trigger is the sensor collider, other is the overlapping entity.
}

extern "C" void OnTriggerEndOverlap(uint32_t trigger, uint32_t other) {
    // Fires after the last contact between the pair is removed.
}
```

The host dispatches queued physics events on the main thread before the next
`GaleshiScriptUpdate`, so both entry points can call the regular script API.
Every loaded module with the matching export receives the event. The hook is
module-level, not per-entity: inspect the trigger/other ids if one module
handles several volumes. Entities must have a collider with `Is Trigger`
enabled in the editor. Water bodies with the Water inspector's **Trigger**
checkbox enabled also dispatch these events with the water entity as
`trigger`; use `HasComponent(trigger, "water")` to identify it.

### Accessing an entity shown in the Outliner

The editor's Outliner selection is not exposed to scripts. There is no
`get_selected_entity()` in Lua or in the hot-reload C++ ABI. Gameplay code
must find entities by name or numeric id.

Lua:

```lua
local crate = Galeshi.world.find("Crate")
if not crate or not crate:valid() then return end

crate:set_position(0.0, 2.0, 0.0)
local position = crate:position()

-- Duplicate names: find_all returns every match.
local crates = Galeshi.world.find_all("Crate")
```

Native C++ (linked with the engine):

```cpp
Galeshi::Entity crate = scene.FindEntityByName("Crate");
if (!crate) return;

auto& transform = crate.GetComponent<Galeshi::TransformComponent>();
transform.position = {0.0f, 2.0f, 0.0f};
```

Hot-reload C++ script:

```cpp
const uint32_t crate = g_api->world.Find("Crate");
if (!g_api->world.Valid(crate)) return;

const GaleshiScriptVec3 position = g_api->world.Position(crate);
g_api->physics.SetVelocity(crate, {0.0f, 0.0f, 5.0f});
```

Notes:

- Names are the Outliner names (`NameComponent`) and are case-sensitive.
- Lua `find` and C++ `World::Find` return the first match. Keep Outliner
  names unique, or use Lua `find_all` / native registry views.
- Lua handles expose `parent()` and `children()`; C++ hot-reload scripts do
  not have child enumeration, so use stable names or native C++ for hierarchy
  traversal.
- Entity ids received by trigger callbacks are raw numeric ids. In Lua wrap
  them with `Galeshi.world.entity(id)`; in C++ pass them to `world.*` calls.
- Keep a cached handle/id but revalidate it (`valid()` / `Valid()`) before use:
  loading a scene or undo can destroy the entity.
- The editor's `m_editor.Selected(...)` is editor-only. Using the selected
  entity from a gameplay script requires adding a host binding for it.

World, input, physics, navigation and audio examples:

```cpp
extern "C" void GaleshiScriptUpdate(float dt) {
    (void)dt;

    const uint32_t crate = g_api->world.Create("Crate");
    g_api->world.SetPosition(crate, {0.0f, 4.0f, 0.0f});
    g_api->physics.AttachBox(crate, {0.5f, 0.5f, 0.5f});
    g_api->physics.ApplyImpulse(crate, {0.0f, 2.0f, 0.0f});

    // Ragdoll activation is explicit; call this on death/hit/input.
    const uint32_t player = g_api->world.Find("Player");
    if (g_api->physics.CreateRagdoll(player)) {
        g_api->physics.ApplyRagdollImpulse(player, {0.0f, 4.0f, 0.0f});
    }

    const float side = g_api->input.AxisRaw("Horizontal");
    g_api->camera.Movement(side, 0.0f);

    if (g_api->navigation.FindPath({0.0f, 0.0f, 0.0f},
                                   {10.0f, 0.0f, 10.0f})) {
        for (uint32_t i = 0; i < g_api->navigation.PathPointCount(); ++i) {
            const auto point = g_api->navigation.PathPoint(i);
            (void)point;
        }
    }

    g_api->audio.AttachSource(crate, "assets/sounds/hit.wav");
    g_api->audio.PlaySource(crate);

    // 2D file players support WAV and MP3. Controls are keyed by path.
    g_api->audio.PlaySound("assets/sounds/theme.mp3", true);
    g_api->audio.SetSoundVolume("assets/sounds/theme.mp3", 0.5f);
    g_api->audio.SeekSound("assets/sounds/theme.mp3", 4.0f);
}
```

```cpp
g_api->set_camera_mode(g_api->user, GALESHI_CAMERA_FIRST_PERSON);
const int mode = g_api->get_camera_mode(g_api->user);

const GaleshiScriptVec3 move =
    g_api->get_camera_movement(g_api->user, horizontal, vertical);

g_api->set_player_strafing(g_api->user, true);
const bool strafing = g_api->get_player_strafing(g_api->user);

const bool shaderOk = g_api->compile_shader(
    g_api->user, "scripts/my_effect.glsl");
const char* shaderError = g_api->get_shader_error(g_api->user);
```

Camera mode changes apply to the current player camera.
`get_camera_forward()` and `get_camera_right()` expose the horizontal camera
basis; `get_camera_movement()` combines them into a camera-relative direction
with length at most 1. Hosts without a gameplay camera leave the mode flag at
`GALESHI_CAMERA_THIRD_PERSON`. With strafing disabled, A/D turns the player;
with it enabled, A/D moves sideways.

Compilation uses `g++` by default; set `CXX` to override the compiler. The
compiler runs in a child process and is stopped after 30 seconds. Compile
diagnostics appear in the Code Editor **Output** and **Errors** tabs; invalid
modules are never loaded, and the last successfully loaded module keeps
running. While a `.cpp`, `.h` or `.lua` file is being edited, the Errors tab
runs a debounced live syntax check and shows the offending source line before
the compiler/Lua diagnostic.

On Linux, the worker additionally installs a seccomp deny-list for process
creation, signalling, networking and kernel-management syscalls, and attempts
network/IPC/UTS namespace isolation. Namespace setup is best-effort; seccomp
still applies when unprivileged namespaces are unavailable.
