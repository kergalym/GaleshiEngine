![GaleshiEngineLogo](./engine/assets/icons/galeshi_engine_logo.png)

![GaleshiEngine](./screenshoots/scrot-2026-10-31-05:55.png)

# GaleshiEngine

GaleshiEngine is a minimalist, ECS-based game engine and editor written in C++20. It targets Linux/X11 and uses a Vulkan renderer through bgfx. The project is intended as a compact engine for learning, prototyping, and building small games.

> **Status: pre-alpha (0.2.0).** The editor and most engine subsystems are functional, but the renderer is still under active development. The post-processing stack is functional on the tested Vulkan path, and some editor tools are incomplete. The engine can crash or hang the GPU on some drivers. Use at your own risk.

---

## Features

| System | Status | Notes |
| --- | --- | --- |
| Window and input | Working | SDL3 |
| Input axes | Working | Keyboard, mouse movement, wheel; tunable in Edit -> Preferences |
| Scene and ECS | Working | EnTT, parent-child transforms, entity lifecycle |
| Renderer core | Working | bgfx with the Vulkan backend |
| Deferred PBR | Working | Metallic-roughness PBR with G-Buffer |
| IBL | Working | Irradiance and prefiltered environment cubemaps |
| SSGI | Working | Half-resolution screen-space indirect diffuse lighting with bilateral upsample |
| Shadow maps | Working | 3-cascade directional PSSM, point and spot shadows |
| Tiled light culling | Working | 16x16 tiles, up to 64 point lights in the deferred path |
| Post-processing | Working | DoF, motion blur, atmosphere, fog, and the full stack are tested; effects can be isolated with `GALESHI_EFFECTS` |
| Physics | Working | Jolt rigid bodies and cloth/soft-body simulation |
| Particles | Working | CPU-simulated billboard emitters, editable in Details and serialized in scenes |
| AI and navigation | Working | Recast/Detour NavMesh, path following, behavior trees, and perception |
| Landscape and vegetation | Working | Heightfield sculpting tools and deterministic instanced trees/grass |
| Scripting | Working | Lua 5.4 through sol2, hot reload, animation/AI state control |
| Audio | Working | miniaudio, 2D playback and looping |
| Asset import | Working | Assimp FBX/glTF, PBR materials, skeletal animation |
| Editor | Working | Docking UI, viewport, outliner, details, content browser, and Preferences |
| UI Designer | ✅ Done | Canvas overlay, widget palette (Panel, Text, Button, Image, Slider, Checkbox, Progress, Input, Dropdown, Scroll, Toggle, Layout), live preview, and serialized UI widgets |
| Gizmo | Partial | ImGuizmo is rendered, but its transform is not applied |
| Glass material | WIP | Shader and UI flag exist, render pass is not connected |
| Scene serialization | Working | ECS, materials, landscape/vegetation, animation, AI components, editor camera view, and global post-process settings/volumes |
| Runtime and packaging | ✅ Done | GameMode is a real editor runtime (`--game <gproj>`) that opens the project, loads `startScene`, and starts playing; AppImage packaging is available |
| Automated tests | None | `BUILD_TESTING` is disabled |

---

## Rendering

The primary rendering path is deferred:

- G-Buffer: albedo/metallic, normal/roughness, world position, per-object motion vectors, and depth.
- Metallic-roughness PBR: GGX distribution, Smith geometry, Schlick Fresnel.
- IBL: generated irradiance and prefiltered environment cubemaps. The deferred lighting shader evaluates the split-sum BRDF analytically to stay within the Vulkan descriptor budget.
- SSGI: half-resolution screen-space diffuse global illumination. A cosine-weighted hemisphere trace samples the lit HDR buffer, and a depth-aware bilateral composite applies receiver albedo, AO, and metallic response.
- Directional shadows: three-cascade PSSM with log-linear split selection and tight orthographic projections.
- Point shadows: a cubemap for the first point light.
- Spot shadows: up to four spot lights.
- Tiled light culling through a compute shader.
- Frustum culling for scene meshes.
- Animated procedural water with planar scene reflections, Fresnel environment fallback, screen-space refraction, depth absorption, shoreline/crest foam, and sun sparkle. Planar reflections use a clipped mirrored opaque pass for the nearest water plane and can be disabled per water entity.
- Automatic instancing for repeated static meshes in the main, G-buffer, and shadow passes.
- Automatic mesh LOD generation and distance-based LOD selection.
- Conservative occlusion culling with GPU occlusion queries and a low-resolution occluder depth map.
- Cascade-aware PSSM depth bias, normal-offset tuning, and texel-snapped cascades for stable distant shadows.
- Adjustable PSSM shadow sharpness, including hard single-tap shadows and a Sharp Shadows preset.
- Per-object motion vectors for Motion Blur and TAA, with camera fallback for sky.
- Scalable render resolution with optional dynamic-resolution scaling.
- Analytical height fog plus an optional volumetric ray-marched mode with PSSM shadow shafts.
- Procedural groom hair with guide curves, dense skin-aware strands, spring/wind simulation, and anisotropic ribbon shading.
- Skybox and debug line rendering.

There is also a forward PBR fallback in `RenderSystem`, but the deferred path is the primary one and is enabled by default.

Implemented post-processing passes:

- SSAO, HBAO, and GTAO ambient occlusion with quarter/half-resolution AO evaluation and a bilateral upsample
- SSR, using an approximate screen-space raymarch
- SSGI, half-resolution screen-space diffuse indirect lighting with depth-aware upsample
- Bloom
- TAA
- SMAA
- FXAA
- Depth of Field, off globally and enabled by post-process volumes, with automatic focus on the player/nearest character or selected mesh
- Motion Blur
- Atmosphere
- God Rays
- ACES tonemapping
- Color correction
- Environment color palette filter with luminance-preserving shadow/midtone/highlight grading and presets
- Vignette
- Chromatic aberration
- Invert
- Scene-wide post-processing volumes with per-effect tuning for bloom, DoF, motion blur, tonemap, color correction, vignette, ambient occlusion (SSAO/HBAO/GTAO), SSR, anti-aliasing, god rays, atmosphere, and fog. The highest-priority enabled volume applies to the entire scene; bounds are editor-only visualization. The Post-Process menu edits the active source (volume or global settings), so menu and volume controls override each other on the last change.
- Realtime Rendering toggle in the Rendering menu pauses only scene, shadow, reflection and post-process GPU render passes while keeping the editor responsive and displaying the last completed viewport frame. Physics, animation, scripts, AI, particles and scene time keep updating normally.

---

## Physics

- Jolt Physics 5.2.
- Static, dynamic, and kinematic rigid bodies.
- Box, sphere, and capsule shapes.
- Gravity, impulses, linear velocity, and contact callbacks.
- Transform synchronization from Jolt to ECS.
- Cloth/soft-body grid simulation.
- Multithreaded job system.

Joint and constraint interfaces are not exposed yet.

## AI and navigation

- Recast/Detour NavMesh generation from scene meshes and landscape geometry.
- Pathfinding between world-space points and NavAgent waypoint following.
- Direct movement fallback for scenes without a NavMesh.
- Serializable behavior trees with Selector, Sequence, Condition, and Action nodes.
- Default behavior tree with patrol, target acquisition, chase, and attack branches.
- Perception with sight radius, field of view, hearing radius, memory, and AABB line-of-sight checks.
- Player targeting through `PlayerTagComponent` or an entity-name filter.
- Per-entity Lua AI controllers with an `update_ai` callback.
- Debug visualization of the generated NavMesh and AI state.
- Editor inspectors for perception, behavior trees, and player tags.

Squad coordination and shared blackboards are not implemented.

## Scripting

- Lua 5.4 with sol2.
- Hot reload with F5.
- Lifecycle callbacks: `RunOnce()` runs once after the script is loaded (and again after each successful hot reload), then `RunAlways(dt)` runs every frame. The legacy `update(dt)` callback is used as a fallback when `RunAlways` is not defined.
- Entity creation and destruction.
- Transform access: `set_position`, `get_position`, `translate`.
- Input axes and mouse helpers. `get_axis(name)` and `get_axis_raw(name)` read the configured axes; `get_mouse_position()`, `get_mouse_delta()`, `get_mouse_wheel()`, and `is_mouse_button_down(button)` expose raw mouse state.
- Character roots without a live Jolt body use the script fallback controller; it supports script velocities, gravity and impulses, so `add_impulse`-based Jump works for those roots too.
- The default axes are `Horizontal` (`A = -1`, `D = +1`), `Vertical` (`W = +1`, `S = -1`), `Jump` (`Space`), `Mouse X`, `Mouse Y`, and `Mouse ScrollWheel`. Keyboard bindings can be rebound per axis in Edit -> Preferences; changes are stored in the project's `input.json`. The same page has `Reset Keys to Defaults`.
- Impulse and velocity control.
- Sound playback.
- Navigation commands for AI agents.
- Entities created by scripts are tagged with `LuaEntityComponent` and removed on hot reload.
- Unified `Galeshi` API: namespaced gameplay modules (`world`, `input`, `physics`, `audio`, `animation`, `ai`, `navigation`, `ui`, `particles`, `engine`) plus entity handles with methods. The legacy global functions remain available for existing scripts. See [docs/LUA_API.md](docs/LUA_API.md).
- Skeletal animation from Lua: declare an imported model with `Galeshi.animation.model(path)`, spawn its mesh hierarchy, or pass it to `animation.play()` / `apply()` for playback and state-machine control.

```lua
local player = nil

function RunOnce()
    player = Galeshi.physics.create_box("Player", 0.0, 5.0, 0.0, 2.0)
end

function RunAlways(dt)
    if not player or not player:valid() then return end
    local velocity = Galeshi.physics.get_velocity(player)
    local horizontal = Galeshi.input.axis("Horizontal")
    Galeshi.physics.set_velocity(player, horizontal * 8.0, velocity.y, 0.0)
end
```

## Audio

- miniaudio.
- File-based playback, looping, stopping, and playback state checks.
- `AudioSourceComponent` in ECS.
- 3D positional audio with inverse, linear, and exponential attenuation, min/max distance, rolloff, doppler, and a local offset.
- Listener follows the camera or an entity with `AudioListenerComponent`.
- Mixer buses: Master, Music, SFX, Voice, and Ambient, with per-source routing and volume/mute controls in the Audio Mixer window.
- Playback can be triggered from Lua, editor actions, or collision callbacks.

## Assets

- Assimp import for FBX and glTF.
- Meshes, PBR materials, and textures with caching.
- Primitive cube, sphere, and capsule meshes.
- Landscape heightfield generation with Raise, Lower, Smooth, Flatten, and Noise brushes, with a terrain-following brush outline.
- Deterministic procedural tree and grass generation, rendered with per-instance matrices and instanced shadow passes.
- Multiple draggable tree species for landscape vegetation; each has its own model, density, size and texture override row, and can be painted or generated together. Dropped tree models default to Size `0.010`. Density scales the species' generated instance count, while Size scales the model itself. Models can be dropped from the Content Browser or viewport; each species' Tree Textures row accepts Albedo/Normal/MR/Emissive/Occlusion/Opacity/Specular drops, while the Landscape keeps its own material texture row.
- Tree and grass painting brushes with adjustable density and instance size. A single click stamps foliage and dragging continues the stroke. Painted instances are serialized with the scene, and `Hide Default Instances` hides generated procedural trees/grass while keeping authored tree species and painted vegetation visible.
- Skeletal animation clip sampling with weighted pose blending.
- Serializable animation state machines with parameters, triggers, transitions, and blend durations. Each state can use clips embedded in the character FBX or a separate animation FBX; separate clips are retargeted onto the character skeleton by bone name. Runtime retargeting also remaps namespace prefixes such as `mixamorig:` or `Korlan:` onto the selected character's root-bone prefix, and provides an `Animation Scale` multiplier for unit conversion (for example `0.01` for UE4 centimeter clips on a meter character).
- PBR material parameters and texture slots.

## Editor

- Docking ImGui interface.
- Viewport with orbit camera, selection, and drag movement. Camera navigation ignores collision shapes and their transform gizmos while moving, so it cannot be blocked by hovering them.
- World Outliner and Details panels, with independent collision/physics child nodes. Detaching is available only for children of collision shapes; skeletal-model children stay locked, and detach preserves their world transform. Parented static collider bodies follow their moving parent, so cameras attached below collision shapes remain stable during player movement.
- Collision-shape wireframes keep the same visibility across Play/Stop and are controlled only by their per-collider Visible in Editor flag; attached meshes are never hidden by Play mode.
- Content Browser with model import and a right-click menu for creating folders/Lua scripts, renaming, moving and deleting files and directories. Models, textures and audio can also be dragged in from any file manager; drops on the browser import into the current folder, other drops go to `assets/`.
- `File -> New Scene` replaces the current document with an empty scene and releases the previous scene's mesh, material and texture GPU resources before continuing. `File -> Recent Scenes` keeps up to ten recently loaded/saved scenes per project for quick loading.
- The Details Transform section includes a Uniform Scale control that updates X, Y and Z together for the selection.
- Create entities, lights, physics primitives, cloth, landscape, AI agents, and gameplay cameras.
- Camera entities expose FOV, clip planes, primary/priority selection and local Transform offset. A camera parented to a character follows it; during Play the selected primary camera overrides the editor viewport, and Stop restores the orbit camera and its previous view.
- `Testing -> Build Scene` rebuilds the PBR test scene: 12 colored spheres (3 albedo samples and a 3x3 metallic/roughness grid) plus the physics demo objects and ground.
- When `scene.json` is missing, the editor starts with an empty scene; the test scene is not built automatically.
- PBR material editing with Metal, Plastic, Rubber, and Gold presets.
- Landscape sculpting with brush settings, undo snapshots, and vegetation regeneration.
- Animation state machine editor with states, clips, parameters, and transitions.
- Perception and behavior-tree inspectors, plus player-tag and AI debug controls.
- NavMesh generation from the Create menu.
- Undo/Redo through scene snapshots.
- Scene save/load in JSON format.
- Play/Stop mode with physics reset and scene restoration.
- Debug overlay for grids, axes, light volumes, and NavMesh.

The ImGuizmo manipulator is currently visual only. Dragging the center marker moves the selected object, but ImGuizmo's returned transform is not applied to the ECS.

---

## Building

### Requirements

- Linux/X11. The Wayland path is not tested.
- CMake 3.24 or newer.
- C++20 compiler: GCC 13+, Clang 15+, or MSVC 19.35+.
- Ninja.
- A working Vulkan driver and Vulkan SDK.
- `shaderc` available in `PATH` or in the Vulkan SDK.
- X11 development headers.
- FreeType development files.
- Network access during the first configure, because dependencies are fetched with FetchContent.

### Debug build

```bash
git clone <repo> GaleshiEngine
cd GaleshiEngine
cmake --preset debug --fresh
cmake --build --preset debug -j"$(nproc)"
```

```bash
bash scripts/compile_shaders.sh build/debug shaders build/debug/bin/shaders
```

### Running

```bash
./build/debug/bin/GaleshiEditor
```

Optional flags:

```bash
./GaleshiEditor --debug
./GaleshiEditor --game
```

`--debug` enables verbose logging. `--game` hides the editor panels and runs the viewport in game mode, but it still uses the editor executable.

### GaleshiRuntime

```bash
cmake --preset debug --fresh
cmake --build --preset debug -j"$(nproc)"
./build/debug/bin/GaleshiRuntime
```

`GaleshiRuntime` is a standalone editor runtime target. It accepts `--game <path>.gproj` to open a project and load its `startScene`, and runs the scene directly without the full editor UI.

### Release build

```bash
cmake --preset release --fresh
cmake --build --preset release -j"$(nproc)"
./build/release/bin/GaleshiEditor
```

### AppImage (single-file editor)

The editor can be packaged as a single-file AppImage that contains the executable, engine assets, shaders, the bundled DejaVu Sans font, and the non-system shared libraries:

```bash
./scripts/package_engine_appimage.sh
./dist/GaleshiEngine-x86_64.AppImage
```

The script builds the release preset by default. Set `SKIP_BUILD=1` to reuse an existing `build/release`. On first use it downloads `linuxdeploy` and `appimagetool` into `.tools/` and produces the image in `dist/`.

Requirements and caveats:

- Linux x86_64 with a working Vulkan driver and X11 or XWayland. GPU drivers, Vulkan ICDs, FreeType, and core system libraries are host-provided and cannot be bundled.
- Direct execution uses FUSE. On systems without it, run the image with `--appimage-extract-and-run` or set `APPIMAGE_EXTRACT_AND_RUN=1`.
- The image is built against the host glibc. For distribution to older systems, build it in an older environment such as Ubuntu 22.04.
- The AppImage bundles the editor, not a game project. Project files remain external and are opened through the Project Manager.

### CLI helper

The repository contains a small wrapper for packaging and running builds. It expects an existing debug build:

```bash
./galeshi package Game.gproj
./galeshi run Game.gproj
```

The wrapper's `editor` command starts the binary from the repository root. Because the renderer loads `shaders/` and `assets/` relative to the working directory, use `cd build/debug/bin && ./GaleshiEditor` for direct editor launches.

---

## Project files and packaging

A project file is a JSON document such as `Game.gproj`:

```json
{
  "name": "MyGame",
  "startScene": "scene.json"
}
```

Packaging is experimental:

```bash
./galeshi package Game.gproj
```

---

## Controls

| Input | Action |
| --- | --- |
| Right mouse drag | Orbit the editor camera |
| Mouse wheel | Move the editor camera forward/back along the view direction |
| W / A / S / D | Move the player (when the scene is playing) |
| Esc | Stop the running scene |
| Left click | Select an entity |
| Left drag on center marker | Move the selected entity |
| W / E / R | Switch gizmo mode (translate / rotate / scale) |
| F | Focus the camera on the selected entity |
| Delete | Delete the selected entity |
| Ctrl+I | Import a model |
| Ctrl+S | Save the scene |
| Ctrl+O | Load a scene |
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| Ctrl+, | Open Preferences |
| F5 | Hot-reload `assets/scripts/init.lua` |

The ImGuizmo handles are visible, but only the center-marker drag currently modifies the scene.

## Known limitations

- The renderer is pre-alpha and may crash or hang the GPU on some drivers.
- Linux/X11 is the only tested platform. Wayland is untested.

## Roadmap

### Stability

- [ ] Run the Vulkan validation layers on all render passes.
- [x] Wire the glass render pass.
- [x] Apply ImGuizmo transforms to the ECS.

### Editor and data

- [x] Complete scene serialization for all ECS components.
- [x] Landscape formation tools and tree/grass instancing
- [x] Scene loading progress bar dialog
- [x] Separate project implementation and resources (show project creating or selecting window at startup)
- [x] Import content (fbx, glTF, textures, wav, mp3 sounds) into project
- [x] Lua game logic scripting system and editor under viewport (tab system on the right side area of Content browser). Lua bindings to C++ functions
- [x] Add logo to the left up corner of an editor
- [x] Add naming labels to Outliner and Properties panels
- [x] Show small splash window at startup
- [x] Add input axis system for keyboard and mouse
- [x] Add RunOnce and RunAlways system functions
- [x] UI designing system
- [x] Standalone one file container build (AppImage for the editor)

### Rendering

- [ ] Improve SSR quality and projection.
- [x] Add instancing for repeated meshes.
- [x] Add occlusion culling and LOD.
- [x] Tune PSSM bias and normal offsets.
- [x] Add per-object motion vectors for Motion Blur.
- [x] Add TAA motion vectors.
- [x] Add quality scaling and dynamic resolution.
- [x] Add texture compression and compressed texture loading/reading
- [x] Add two sided mode for normals option as checkbox to Material properties
- [x] Rename material slots to their names in FBX file format
- [x] Add Particles system
- [x] Add Postprocessing volume
- [x] Add analytical and volumetric height fog

### Physics 
- [x] Add various physics properties to collision shapes
- [x] Allow move collission shapes on XY coordinates
- [x] Set collission shapes invisible when user press play

### Content systems

- [x] Animation blending and state machines.
- [x] 3D audio and mixer buses.
- [x] Improve AI with behavior trees and perception.

---

## Third-party libraries

| Component | Library | License |
| --- | --- | --- |
| Window and input | [SDL3](https://github.com/libsdl-org/SDL) | zlib |
| Rendering | [bgfx](https://github.com/bkaradzic/bgfx) | BSD-2-Clause |
| Image loading | [bimg](https://github.com/bkaradzic/bimg) | BSD-2-Clause |
| Base library | [bx](https://github.com/bkaradzic/bx) | BSD-2-Clause |
| UI | [Dear ImGui](https://github.com/ocornut/imgui) | MIT |
| Gizmo | [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | MIT |
| Math | [GLM](https://github.com/g-truc/glm) | MIT |
| Model import | [Assimp](https://github.com/assimp/assimp) | BSD-3-Clause |
| Physics | [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | MIT |
| Navigation | [RecastNavigation](https://github.com/recastnavigation/recastnavigation) | zlib |
| ECS | [EnTT](https://github.com/skypjack/entt) | MIT |
| Logging | [spdlog](https://github.com/gabime/spdlog) | MIT |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) | MIT |
| Lua bindings | [sol2](https://github.com/ThePhD/sol2) | MIT |
| Lua | [Lua](https://www.lua.org/) | MIT |
| Audio | [miniaudio](https://github.com/mackron/miniaudio) | Public domain / MIT-0 |
| Font rendering | [FreeType](https://freetype.org/) | FreeType License / GPLv2 |
| UI font | [DejaVu Sans](https://dejavu-fonts.github.io/) | Bitstream Vera License / public domain |

## Credits

- bgfx for the renderer abstraction.
- Dear ImGui for the editor tooling.
- Jolt Physics for rigid bodies and soft bodies.
- Recast and Detour for navigation.
- The SSAO chain and camera motion blur implementations follow the approach used by [tobspr/renderpipeline](https://github.com/tobspr/renderpipeline).
- HBAO follows the horizon-based formulation of Bavoil and Sainz; GTAO follows the horizon-integration formulation of Jimenez et al. (2016).
- All library authors listed above.

## License

MIT. See [LICENSE](LICENSE).

All third-party libraries are distributed under their own licenses.
