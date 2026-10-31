# Galeshi Lua API

`ScriptEngine` exposes one namespaced Lua table, `Galeshi`.  It is a facade over
the gameplay-facing C++ subsystems.  The older global functions remain
available for compatibility, but new scripts should use `Galeshi`.

The Content Browser can generate starter scripts: right-click inside the file
list and choose a template under **Create Script**. Files are written to
`assets/scripts`; the editor opens the new file in the Lua Editor. Existing
files are never overwritten — a numeric suffix is added instead.

```lua
local player = Galeshi.world.create("Player")
player:set_position(0, 2, 0)

function RunAlways(dt)
    local move = Galeshi.input.axis("Horizontal")
    player:translate(move * 5 * dt, 0, 0)
end
```

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
| `children()` | Array of child handles. |
| `has_component(name)` | Check for an engine component, for example `"rigid_body"`, `"nav_agent"`, `"ui_canvas"`, `"particle_emitter"`. |
| `destroy()` | Destroy the entity. |

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

### `Galeshi.audio`

```lua
Galeshi.audio.play("assets/sounds/hit.wav", false)
Galeshi.audio.stop("assets/sounds/hit.wav")
Galeshi.audio.is_playing("assets/sounds/hit.wav")
Galeshi.audio.stop_all()
Galeshi.audio.set_bus("SFX", 0.8, false)
local bus = Galeshi.audio.get_bus("SFX") -- {name, volume, muted}
```

Buses are `Master`, `Music`, `SFX`, `Voice`, and `Ambient`.

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
    color = {1.0, 0.8, 0.2, 1.0},
    on_click = "on_start",
})

local widget = Galeshi.ui.widget("HUD", "start")
Galeshi.ui.set_value("HUD", "start", 0.75)
Galeshi.ui.show_canvas("HUD", false)
```

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
