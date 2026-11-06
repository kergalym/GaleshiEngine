# Third-Person Demo Script

A practical hot-reload C++ gameplay example for a third-person demo. It uses
only the public C++ scripting API and is intended as a starting point, not a
complete combat or character controller.

- Script: [third_person_demo.cpp](third_person_demo.cpp)
- API background: [CPP_API_MODES.md](CPP_API_MODES.md)
- Script workflow: [CPP_SCRIPTS.md](CPP_SCRIPTS.md)

## What the script does

- Finds or optionally creates a non-physical `Player` visual root.
- Finds or optionally creates a separate `PlayerPhysics` entity with a
  dynamic capsule body.
- Switches the gameplay camera to third-person orbit.
- Moves the physics proxy relative to the camera with acceleration, then
  copies its position to the visual Player root.
- Supports sprinting, jumping, respawning and a simple attack trigger.
- Rotates the Player root toward the movement direction. Physics cannot
  overwrite this rotation because the Player itself has no rigid body.
- Sends `Speed`, `Jump`, `Attack` and `Respawn` signals to the animation state
  machine when those parameters/states exist.
- Plays optional footstep and attack sounds.
- Creates a small HUD canvas with speed/ground state.

## Scene setup

The script can create the Player and physics proxy, but a scene needs a ground
and a camera.

1. Create a scene with a ground.
   - Use a landscape or a large ground mesh with a static collider.
   - The physics proxy needs collision to stand and jump.

2. Create a camera:
   - **Create → Camera**.
   - Keep the default name `Camera`, or change `kCameraName` in the script.
   - Mark the camera as **Primary**.
   - If the camera is not parented yet, the script attaches it to `Player` and
     positions it at eye height and third-person distance.
   - If you prefer manual setup, parent the camera to `Player` yourself.

3. Create the player:
   - Option A: create an entity named `Player`, give it a visible model/mesh,
     and **do not add a Rigid Body or Collider**. The Player root is the visual
     and camera target; it must remain non-physical.
   - Option B: let the script create a non-physical Player root. Set
     `kPlayerModel` to a model path that exists in your project. The script
     spawns it as a visual child. If the model path is missing, the player is
     invisible but movement and camera tracking still work.
   - Keep the Player at the scene root. The script writes its position in world
     coordinates.

4. Physics proxy:
   - The script creates an entity named `PlayerPhysics` at the Player's feet
     plus the capsule center offset, and attaches a dynamic capsule.
   - If you want to author it manually, make it a root entity with a dynamic
     **Capsule Collider** and **Rigid Body**. The script reuses it.
   - An older `Player` with a Rigid Body is cleaned up when the scene loads:
     the loader strips its `RigidBodyComponent`, `ColliderComponent` and
     `SoftBodyComponent`, so the visual root stays non-physical.

5. Copy `third_person_demo.cpp` into the project:

   ```text
   <project>/scripts/third_person_demo.cpp
   ```

   or:

   ```text
   <project>/assets/scripts/third_person_demo.cpp
   ```

6. Open the project in the editor, wait for the C++ script to compile, and
   press **Play**.

## Controls

| Input | Action |
|---|---|
| `WASD` | Move relative to the third-person camera |
| `Left Shift` | Sprint |
| `Space` | Jump |
| Left mouse | Attack trigger |
| `R` | Respawn at the spawn point |

Input is read only while Play mode is active, matching the editor's existing
input binding behavior.

## Tuning

The top of `third_person_demo.cpp` contains the main constants:

```cpp
constexpr const char* kPlayerName = "Player";
constexpr const char* kPlayerPhysicsName = "PlayerPhysics";
constexpr const char* kCameraName = "Camera";
constexpr const char* kPlayerModel = "assets/models/player.glb";

constexpr bool kAutoCreatePlayer = true;
constexpr bool kAutoCreatePhysicsProxy = true;
constexpr bool kAutoParentCamera = true;

constexpr Vec3 kSpawn{0.0f, 2.0f, 0.0f}; // Player feet
constexpr float kWalkSpeed = 4.5f;
constexpr float kRunSpeed = 8.5f;
constexpr float kJumpSpeed = 7.0f;
constexpr float kCapsuleHalfHeight = 0.85f;
constexpr float kCapsuleRadius = 0.35f;
constexpr float kCapsuleCenterY = kCapsuleHalfHeight + kCapsuleRadius;
```

`kSpawn` is the visual Player position (feet). The script sets the physics
proxy at `kSpawn.y + kCapsuleCenterY` because a Jolt capsule is centered on its
body origin.

### Model facing direction

The script rotates the player with:

```cpp
const float yaw = std::atan2(move.x, move.z) + kModelYawOffset;
```

If your character faces `-Z`, set:

```cpp
constexpr float kModelYawOffset = 3.14159265f;
```

If the character runs sideways, adjust the offset further.

## Animation setup

The script sends these optional animation signals:

- float parameter `Speed` — normalized horizontal speed.
- trigger/parameter `Jump`.
- trigger/parameter `Attack`.
- trigger/parameter `Respawn`.

They work when the player's animation state machine has matching parameters
and transitions. If the player has no animation component, the calls are
ignored safely.

For an imported character, the editor's animation state machine can map these
parameters to `Idle`, `Walk/Run`, `Jump` and `Attack` states. The demo does not
assume specific clip names.

## Audio setup

Optional files:

```text
assets/sounds/footstep.wav
assets/sounds/attack.wav
```

If the files are absent, playback simply produces no audible result. Set the
path constants to empty strings to disable the calls.

## HUD

The script creates a canvas named `ThirdPersonHUD` with a text widget named
`status`. The HUD displays:

- horizontal speed,
- grounded/airborne state,
- whether `PlayerPhysics` has a rigid body.

The UI API is idempotent: reloading the script reuses an existing canvas and
widget rather than creating duplicates.

## Compile check

The script is C++ and does not link the engine. Verify it against the public
headers with:

```bash
c++ -std=c++20 -fsyntax-only -Iengine/include docs/third_person_demo.cpp
```

The repository check also does this:

```bash
./scripts/check_cpp_api.sh
```

## Limitations

This is a demo controller, not a production character system:

- Ground detection uses vertical velocity rather than a raycast.
- Jumping is a simple vertical velocity impulse.
- Attack has no hit detection, damage, combo system or networking.
- The camera auto-parent heuristic only looks for a camera named `Camera`
  that has no parent.
- The Player root must stay unparented; position is copied from the proxy in
  world space.
- Legacy Player body/collider components are stripped when a scene loads.
  Bodies attached to the Player after loading are not migrated automatically.
- Only one player and one `PlayerPhysics` are controlled.
- The player needs a suitable model or mesh for a visible result.
- Camera collision, animation root motion and network prediction are outside
  this sample.

These limitations are deliberate: the example is small enough to read and
adapt, while still exercising world, input, physics, camera, animation, UI and
audio APIs.
