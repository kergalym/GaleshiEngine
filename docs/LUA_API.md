# Galeshi Lua API

`ScriptEngine` exposes one namespaced Lua table, `Galeshi`.  It is a facade over
the gameplay-facing C++ subsystems.  The older global functions remain
available for compatibility, but new scripts should use `Galeshi`.

The Content Browser can generate starter scripts: right-click inside the file
list and choose a template under **Create Script**. Files are written to
`assets/scripts`; the editor opens the new file in the Code Editor. Existing
files are never overwritten — a numeric suffix is added instead.

```lua
local player = Galeshi.world.create("Player")
player:set_position(0, 2, 0)

function RunAlways(dt)
    local move = Galeshi.input.axis("Horizontal")
    player:translate(move * 5 * dt, 0, 0)
end
```

## Interpolation

Frame-rate independent exponential interpolation is available in both forms:

```lua
local f = FInterp(0, 10, dt, 8)          -- number
local v = VInterp(a, b, dt, 8)            -- {x, y, z}
local f2 = Galeshi.math.FInterp(0, 10, dt, 8)
```

`f_interp` / `v_interp` lowercase aliases are also available.

## Entity handles

`Galeshi.world.create()` and `Galeshi.world.find()` return entity handles.
Handles are compared by entity id and also accept their `.id` field:

```lua
local a = Galeshi.world.create("A")
local b = Galeshi.world.find("A")
assert(a == b)
print(a.id, tostring(a))
```

Methods use Lua colon syntax:

| Method | Description |
| --- | --- |
| `valid()` | Entity is still alive. |
| `get_id()` | Raw ECS entity id. |
| `name()` / `set_name(name)` | Display name. |
| `position()` / `set_position(x, y, z)` | Local position. A `{x,y,z}` table is also accepted. |
| `translate(dx, dy, dz)` | Add to local position. |
| `rotation()` / `set_rotation(x, y, z, w)` | Quaternion. With three numbers the values are treated as Euler angles in degrees. |
| `euler()` / `set_euler(x, y, z)` | Euler angles in degrees. |
| `scale()` / `set_scale(x, y, z)` | Local scale. |
| `scale_by(x, y, z)` | Multiply local scale. |
| `parent()` / `set_parent(entity)` | Parent access. `set_parent(nil)` detaches. |
| `detach()` | Remove the parent component. |
| `attach_to_bone(character, bone)` | Attach the entity to a bone of a skeletal character. The current world transform is preserved; Position/Rotation/Scale become bone-local. |
| `detach_from_bone()` | Detach from the bone and keep the current world transform. |
| `bone()` / `attached_character()` | Bone name and character handle for an attached entity. |
| `children()` | Array of child handles. |
| `has_component(name)` | Check for an engine component, for example `"rigid_body"`, `"nav_agent"`, `"ui_canvas"`, `"particle_emitter"`. |
| `destroy()` | Destroy the entity. |

The Outliner selection is editor-only and is not exposed to scripts. Use
`Galeshi.world.find("Name")` to get an object shown in the Outliner. If
multiple objects share a name, use `find_all`. Keep the name unique in the
Outliner or store the numeric id:

```lua
local crate = Galeshi.world.find("Crate")
local crates = Galeshi.world.find_all("Crate")
if crate then
    local same = Galeshi.world.entity(crate:get_id())
end
```

Entity handles are invalidated when a scene is reloaded or an entity is
destroyed; check `valid()` again or find the object by name. Trigger callbacks
receive raw numeric ids, which can be wrapped with
`Galeshi.world.entity(id)`.

## Modules

### `Galeshi.world`

```lua
Galeshi.world.create(name)             -- entity
Galeshi.world.entity(idOrHandle)       -- wrap an existing entity
Galeshi.world.find(name)               -- entity or nil
Galeshi.world.find_all([name])         -- array of entities
Galeshi.world.all([name])              -- alias of find_all
Galeshi.world.exists(entity)           -- boolean
Galeshi.world.destroy(entity)          -- boolean
Galeshi.world.count()                  -- named entities in the scene
Galeshi.world.clear_script_entities()  -- remove entities spawned by Lua
```

### `Galeshi.input`

```lua
Galeshi.input.key_down("Space")
Galeshi.input.axis("Horizontal")
Galeshi.input.axis_raw("Vertical")
Galeshi.input.mouse_down("left")
Galeshi.input.mouse_position() -- {x, y}
Galeshi.input.mouse_delta()    -- {x, y}
Galeshi.input.mouse_wheel()
```

### `Galeshi.camera`

Switches the player camera between third-person orbit and first-person view.
The camera must be attached to the player character, or the character must
carry a `PlayerTagComponent`.

```lua
Galeshi.camera.set_mode(Galeshi.camera.THIRD_PERSON)
Galeshi.camera.set_mode(Galeshi.camera.FIRST_PERSON)
Galeshi.camera.set_mode("first_person") -- strings are accepted too
local mode = Galeshi.camera.mode()

Galeshi.camera.forward()  -- horizontal camera forward {x,y,z}
Galeshi.camera.right()    -- horizontal camera right {x,y,z}
Galeshi.camera.movement(horizontal, vertical)
```

`movement()` returns a horizontal, camera-relative direction with length at
most 1. Use it for camera-relative player movement:

```lua
local move = Galeshi.camera.movement(
    Galeshi.input.axis("Horizontal"),
    Galeshi.input.axis("Vertical"))
Galeshi.physics.set_velocity(player, move.x * speed, velocity.y, move.z * speed)
```

The legacy aliases `set_camera_mode(mode)`, `get_camera_mode()`,
`get_camera_forward()`, `get_camera_right()` and `get_camera_movement()` are
also available. The host must bind a gameplay camera; the editor does this
automatically.

### `Galeshi.player`

```lua
Galeshi.player.strafing()        -- A/D moves sideways when true
Galeshi.player.set_strafing(true)
```

When strafing is disabled, A/D turns the player; W/S faces the player along
the camera direction. The Camera inspector exposes the same toggle.

### `Galeshi.shader`

```lua
local ok = Galeshi.shader.compile("assets/scripts/my_effect.glsl")
print(Galeshi.shader.error())
```

Compiles a GLSL/HLSL fragment shader using the same fullscreen interface as
the Code Editor preview. The Content Browser can create shader templates in
`scripts/`; opening one shows a live preview in the Code Editor.

### `Galeshi.physics`

Physics objects are ECS entities.  The host application creates the native
Jolt body; the script owns the entity/components.

```lua
local box = Galeshi.physics.create_box("Crate", 0, 3, 0, 1.5)
local sphere = Galeshi.physics.create_sphere("Ball", 2, 3, 0, 0.5, true)
local capsule = Galeshi.physics.create_capsule("Player", 0, 3, 0, 0.8, 0.4)
Galeshi.physics.add_impulse(box, 0, 10, 0)
Galeshi.physics.set_velocity(box, 1, 0, 0)
local velocity = Galeshi.physics.get_velocity(box)
Galeshi.physics.destroy_body(box)
```

The optional final boolean marks a body as static.  `create_box` accepts either
a scalar full size or a `{x,y,z}` full-size vector.

A skeletal entity can be switched to a Jolt ragdoll at runtime. The entity must
own a `SkeletalComponent` whose model contains a bone hierarchy; the ragdoll is
runtime-only and is not saved with the scene.

```lua
local character = Galeshi.world.find("Player")
Galeshi.physics.create_ragdoll(character)
Galeshi.physics.apply_ragdoll_impulse(character, 0, 4, 0)
if Galeshi.physics.ragdoll_active(character) then
    -- character is being driven by the ragdoll
end
Galeshi.physics.destroy_ragdoll(character)
```

`create_ragdoll` returns false when the entity has no usable skeletal model.
Activation is explicit: there is no automatic death/hit trigger, so call it
from gameplay logic (for example on death, a strong hit or a key press).
While active, animation is skipped and the skeleton follows the simulated
bodies. `destroy_ragdoll` restores normal animation.

The editor host binds these callbacks automatically. A custom runtime host must
bind `ScriptEngine::PhysicsBindings::createRagdoll`, `destroyRagdoll`,
`ragdollActive` and `applyRagdollImpulse` before Lua scripts can use them.

### `Galeshi.audio`

```lua
Galeshi.audio.play("assets/sounds/hit.wav", false)
Galeshi.audio.stop("assets/sounds/hit.wav")
Galeshi.audio.is_playing("assets/sounds/hit.wav")
Galeshi.audio.pause("assets/sounds/hit.wav")
Galeshi.audio.resume("assets/sounds/hit.wav")
Galeshi.audio.set_volume("assets/sounds/hit.wav", 0.6)
Galeshi.audio.set_pitch("assets/sounds/hit.wav", 1.1)
Galeshi.audio.set_looping("assets/sounds/hit.wav", true)
Galeshi.audio.seek("assets/sounds/hit.wav", 2.5)
local duration = Galeshi.audio.duration("assets/sounds/music.mp3")
local position = Galeshi.audio.position("assets/sounds/music.mp3")
Galeshi.audio.stop_all()
Galeshi.audio.set_bus("SFX", 0.8, false)
local bus = Galeshi.audio.get_bus("SFX") -- {name, volume, muted}
```

`play` supports WAV and MP3. Its controls are keyed by file path, matching
`play`/`stop`; use one player entity per independent 3D source instead.
Buses are `Master`, `Music`, `SFX`, `Voice`, and `Ambient`.

### Trigger overlap callbacks

Define either optional global function. They receive raw entity ids; the first
entity is the collider whose `Is Trigger` flag is set.

```lua
function OnTriggerBeginOverlap(trigger, other)
    print("entered", trigger, other)
end

function OnTriggerEndOverlap(trigger, other)
    print("left", trigger, other)
end
```

The end callback fires when the last contact between the pair is removed.
Entities need a collider with `Is Trigger` enabled; add a rigid body to moving
objects.

### `Galeshi.animation`

State-machine helpers:

```lua
Galeshi.animation.set_state(entity, "Run")
Galeshi.animation.set_parameter(entity, "Speed", 1.0)
Galeshi.animation.get_parameter(entity, "Speed", 0.0)
Galeshi.animation.trigger(entity, "Jump")
```

#### Skeletal animation models

A skeletal model can be declared once and then spawned or passed directly to
playback. Clip indices are **1-based** in Lua; clip names are also accepted.

```lua
local model = Galeshi.animation.model("assets/models/character.fbx")
-- or: Galeshi.animation.load("assets/models/character.fbx")

print(model.path, model.mesh_count, model.bone_count)
print(model:clip_count(), model:clip_name(1), model:duration(1))

-- Create the full imported mesh hierarchy, SkeletalComponent and state machine.
local actor = model:spawn("Character", 0.0, 0.0, 0.0)

-- Start the first clip (index 1) or select it by name.
Galeshi.animation.play(actor, model, 1)
-- Galeshi.animation.play(actor, model, "Run")

-- Attach a model to an existing entity and play from its skeletal component.
local other = Galeshi.world.find("ExistingCharacter")
Galeshi.animation.apply(other, model)
Galeshi.animation.play_clip(other, "Run")

Galeshi.animation.stop(actor)
local playing = Galeshi.animation.is_playing(actor)
```

Model object fields: `path`, `name`, `valid`, `has_animation`, `mesh_count`,
`bone_count`, `num_clips`, `clips` (1-based array), `durations` (1-based array).
Methods: `clip_count()`, `clip_name(index)`, `duration(index)`, `spawn(...)`,
`apply(entity)`, `play(entity[, clip])`.

The host must bind an `AssetManager` (`ScriptEngine::BindAssets`); the editor
and `GaleshiRuntime` do this automatically.

### `Galeshi.ai` and `Galeshi.navigation`

```lua
Galeshi.ai.set_target(agent, x, y, z)
Galeshi.ai.path_done(agent)
Galeshi.ai.set_speed(agent, 4.0)
Galeshi.ai.speed(agent)
Galeshi.ai.clear_target(agent)
Galeshi.ai.set_stopping_distance(agent, 0.75)

local path = Galeshi.navigation.find_path({x=0,y=0,z=0}, {x=10,y=0,z=5})
local built = Galeshi.navigation.build()
```

`find_path` returns an array of `{x,y,z}` points or `nil` when no path exists.
Navigation callbacks are only available when the host application has bound a
`NavigationSystem` (the editor does).

Per-entity AI scripts receive a raw numeric entity id in `update_ai(eid, dt)`.
Wrap it when you want the entity methods:

```lua
function update_ai(eid, dt)
    local agent = Galeshi.world.entity(eid)
    if agent:valid() then
        Galeshi.ai.set_speed(agent, 3.5)
    end
end
```

### `Galeshi.ui`

`Galeshi.ui` reads and edits the UI canvas/widget model at runtime.  UI event
handlers can be registered directly with `Galeshi.ui.on`, or be global Lua
functions as before.

```lua
Galeshi.ui.on("on_start", function(widgetId, value, checked)
    print("clicked", widgetId)
end)

Galeshi.ui.set_widget("HUD", "start", {
    label = "Play",
    visible = true,
    value = 0.5,
    checked = true,
    font_size = 28,
    font = "assets/fonts/DejaVuSans.ttf",
    text_align = "center",
    wrap_width = 640,
    text_offset = {0, 4},
    rotation = -8.0,
    slider_step = 0.1,
    tooltip = "Start the game",
    image = "assets/textures/button.png",
    color = {1.0, 0.8, 0.2, 1.0},
    use_color = true,
    text_color = {0.1, 0.1, 0.1, 1.0},
    use_text_color = true,
    border_color = {0.4, 0.7, 1.0, 1.0},
    use_border_color = true,
    options = {"Low", "Medium", "High"},
    selected_option = 2,
    on_click = "on_start",
})

local widget = Galeshi.ui.widget("HUD", "start")
Galeshi.ui.set_value("HUD", "start", 0.75)
Galeshi.ui.show_canvas("HUD", false)
```

`image` supplies the background for Button and the texture for Image widgets.
`font`, `text_offset` and `rotation` affect Text widgets (button labels also use
them); `font_size` is in pixels. `color` is the widget background and `text_color`
is the label color, enabled by `use_color` and `use_text_color` respectively.
Text keeps `color`/`use_color` as a legacy text-color fallback.

Available helpers: `find_canvas`, `canvases`, `canvas`, `widget`, `set_widget`,
`set_value`, `value`, `show_canvas`, `on`, `off`, `call`.

### `Galeshi.particles`

```lua
local smoke = Galeshi.particles.create("Smoke", 0, 1, 0, {
    emit_rate = 20,
    lifetime = 3.0,
    max_particles = 128,
    velocity = {0, 1, 0},
    gravity = {0, -0.5, 0},
    start_color = {1, 0.5, 0.2, 1},
    end_color = {0.2, 0.2, 0.2, 0},
})

Galeshi.particles.set(smoke, {emit_rate = 40})
local settings = Galeshi.particles.get(smoke)
```

### `Galeshi.engine`

```lua
Galeshi.engine.version    -- string
Galeshi.engine.time()     -- seconds since script engine start
Galeshi.engine.delta_time()
Galeshi.engine.frame()
Galeshi.engine.quit()
```

### Math and logging helpers

```lua
Galeshi.log("message")
Galeshi.warn("warning")
Galeshi.error("error")
Galeshi.vec2(x, y)
Galeshi.vec3(x, y, z)
Galeshi.color(r, g, b [, a])
Galeshi.quat(x, y, z [, w])
```

Returned vectors are tables with both named and numeric fields, so
`v.x`/`v[1]` and `{x=..., y=..., z=...}`/`{..., ..., ...}` are interchangeable.

## Scope and limitations

The facade intentionally exposes engine-owned handles and components, not raw
third-party pointers or GPU/native handles.  Direct binding of every API from
SDL3, bgfx, Jolt, Recast/Detour, Assimp, miniaudio, ImGui, EnTT and sol2 would
expose templates, callbacks, ownership-sensitive references and native handles
that Lua cannot manage safely.  Instead, the host application binds the small
number of C++ callbacks that each subsystem needs, and scripts use the stable
entity/component facade above.

Editor-only systems (asset import, scene serialization, render-pass
configuration, landscape editing and post-process authoring) remain C++/editor
operations.  They are not safe to invoke from a per-frame gameplay script.
