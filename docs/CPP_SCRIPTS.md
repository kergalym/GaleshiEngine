# C++ gameplay scripts

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
velocity access, plus `FInterp` and `VInterp`. Scripts are intentionally
compiled with a stable C ABI so the engine can be rebuilt independently.

Compilation uses `g++` by default; set `CXX` to override the compiler. The
compiler runs in a child process and is stopped after 30 seconds. Compile
diagnostics appear in the Code Editor **Output** and **Errors** tabs; invalid
modules are never loaded, and the last successfully loaded module keeps
running.
