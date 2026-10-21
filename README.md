![alt](./screenshoots/initial3.png)

# GaleshiEngine

A minimalist game engine inspired by Unreal Engine 5 with ECS-based Scene Graph. The goal is a cross-platform editor with a Vulkan renderer, physics, AI, and Lua Scripting.

Project is under prealpha state, renderer is still unstable and sometimes may hang your GPU. Use at own risk!
---

## Features

| System             | Status  | Library                              |
| ------------------ | :-----: | ------------------------------------ |
| Window and input   | Done    | SDL3                                 |
| Renderer (Vulkan)  | Done    | bgfx + bimg + bx                     |
| Editor UI          | Done    | Dear ImGui (docking)                 |
| Gizmo              | Done    | ImGuizmo (linked, not wired)         |
| PBR shader         | Done    | Basic model (Lambert + Blinn-Phong)  |
| FBX/glTF import    | Done    | Assimp (linked)                      |
| Scene / ECS        | Done    | EnTT (linked)                        |
| Physics            | Done    | Jolt Physics (rigid + soft)          |
| AI / NavMesh       | Done    | Recast and Detour                    |
| Scripting          | WIP | Lua                                      |

---

## Building

### Requirements

- CMake 3.24 or newer (on CMake 4.x requires `CMAKE_POLICY_VERSION_MINIMUM=3.5`)
- C++20 compiler (GCC 13+, Clang 15+, MSVC 19.35+)
- Ninja (or Make)
- Vulkan SDK (`vulkaninfo` must work)
- SDL3 is pulled automatically via FetchContent

### Linux 

```bash
git clone <repo> GaleshiEngine
cd GaleshiEngine
cmake --preset debug --fresh
cmake --build --preset debug --target GaleshiEditor -j$(nproc)

./build/debug/bin/GaleshiEditor
or 
./build/debug/bin/GaleshiEditor --debug
```

Technology stack
Component	Library	License
Window / input	SDL3	zlib
Rendering	bgfx	BSD-2
UI	Dear ImGui	MIT
Gizmo	ImGuizmo	MIT
Math	GLM	MIT
Model import	Assimp	BSD-3
Physics	Jolt Physics	MIT
Navigation	RecastNavigation	zlib
ECS	EnTT	MIT
Logging	spdlog	MIT


## Roadmap

### P0 — Physics ↔ ECS
- [✔] `PhysicsSystem::SyncTransformsToECS()` — read body positions from Jolt, write to `TransformComponent`
- [✔] Call `SyncTransformsToECS()` from `OnUpdate()` after `PhysicsSystem::Update()`
- [✔] Spawn test scene: 5 dynamic cubes (`CreateBox()` + `RigidBodyComponent`)
- [✔] Verify cubes fall and settle on the floor

### P1 — Scripting & Audio
- [✔] Lua ↔ ECS: pass entity IDs from C++ to Lua (`new_entity()`)
- [✔] Implement real `set_position(entity, x, y, z)` on `TransformComponent`
- [✔] Add `get_position`, `translate` bindings
- [✔] Hot-reload `init.lua` on keypress
- [✔] `AudioSystem::PlaySound(path)` via miniaudio
- [✔] `AudioSourceComponent` in ECS
- [✔] Trigger sound on keypress / collision / script event

### P2 — Post-processing
- [✔] Off-screen FBO: color + depth + normal + position
- [✔] `PostProcessManager::Render(fbo)` — takes scene textures


### P3 — Navigation, Editor, Serialization
- [✔] Recast NavMesh generation from `MeshComponent` scene
- [✔] `NavAgentComponent` + Detour pathfinding
- [✔] AI Controller in Lua (`RunAIScripts`, isolated `sol::environment` for per-agent state)
- [✔] Scene hierarchy panel (drag & drop)
- [✔] Inspector: edit `Transform`, `RigidBody`, `Mesh`
- [ ] ImGuizmo translate/rotate/scale *(postponed — requires applying visModel to ECS)*
- [✔] Undo/Redo (`m_history`, `m_historyIndex`)
- [✔] `Scene::Save/Load(path)` via nlohmann/json (`*.gscn`)

### P4 — Long-term
- [✔] Multi-threaded ECS job system (integrated with `JPH::JobSystemThreadPool`)
- [✔] Parallel skeletal animation (`Galeshi::ParallelFor` + `Barrier` API)
- [✔] Asset pipeline: `.fbx` / `.gltf`
- [✔] Advanced Material Property Panel (Metal/Plastic/Rubber/Gold presets, Normal Strength, Emissive Map)
- [✔] Scripting over Lua
- [✔] Packaging: `GaleshiEditor --package Game.gproj`
- [✔] Standalone mode: `--game` (clean fullscreen without editor UI)
- [✔] CLI: `galeshi build`, `galeshi run`, `galeshi package`, `galeshi editor`

### P5 — Rendering & Optimization
- [✔] Frustum Culling (`Frustum.hpp` — Gribb-Hartmann plane extraction + sphere test)
- [ ] SSAO from depth + normal
- [ ] SSR as optional pass
- [ ] TAA / FXAA / SMAA selectable in UI *(FXAA stable; TAA/SMAA layout fixes moved to P5)*
- [✔] PSSM (Parallel Split Shadow Maps) — 3 cascades, log-linear split scheme, tight ortho projections
- [ ] Instancing (`bgfx::setInstanceDataBuffer` for cubes and other repeating meshes)
- [ ] Occlusion Culling (Hi-Z or CPU raycast for large scenes)
- [ ] TAA with Motion Vectors (via G-Buffer `s_motionVec`, ping-pong history + projection jitter)
- [ ] SMAA (precomputed AreaTex/SearchTex + 3 passes)
- [ ] LOD system (auto-switch `MeshComponent` by distance)
- [ ] Bloom improvements (brightness threshold, multi-pass downsampling)
- [ ] Shadow acne / peter-panning tuning in PSSM (bias + normal offset per cascade)
- [ ] Mobile/quality scaling via BGFX `setViewRect` + dynamic resolution



Known limitations

    Linux/X11 only at the moment. The Wayland path in bgfx is untested.

    PBR is currently basic: Lambert + Blinn-Phong, no IBL, no metallic/roughness maps.


License

MIT. See LICENSE.

All third-party libraries are distributed under their own licenses (see the table above).
Credits

    bgfx for a solid cross-API renderer

    Dear ImGui for the tooling that makes an editor possible

    Jolt Physics for a modern physics engine with soft body support

    All library authors listed above

