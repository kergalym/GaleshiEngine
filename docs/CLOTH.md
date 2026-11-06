# Cloth painting and simulation

Cloth is not a separate entity. It is a `SoftBodyComponent` added to an
existing static or skeletal mesh entity. The entity keeps its normal mesh,
material, transform and hierarchy; only the painted vertices acquire cloth
simulation.

## Painting cloth on a mesh

1. Select an entity with a `MeshComponent` in the Outliner or viewport.
2. Open **Details → Cloth Painting**.
3. Enable the **Painting cloth** checkbox.
4. Paint on the mesh with the left mouse button.
5. Hold **Alt** while painting to erase.
6. Release the mouse button. The editor rebuilds the Jolt soft-body body from
   the painted vertices.

Enabling the checkbox creates `SoftBodyComponent` on the selected mesh entity
if it does not exist yet. Disabling the checkbox only leaves paint mode; the
cloth data stays on the mesh.

Painted vertices are displayed as small green crosses while the mesh is
selected. The brush uses **Brush Radius** and **Brush Strength** from the
Landscape sculpting controls.

Only faces containing at least one painted vertex are added to the simulation.
Unpainted vertices have zero inverse mass and remain pinned. Painted vertices
become dynamic.

## Cloth properties

Once **Painting cloth** has been enabled, **Cloth Properties** exposes:

| Parameter | Default | Meaning |
|---|---:|---|
| Mass | 1.0 | Per-vertex mass used for dynamic vertices |
| Friction | 0.5 | Jolt soft-body friction |
| Restitution | 0.0 | Collision restitution |
| Gravity Factor | 1.0 | Multiplier for world gravity |
| Linear Damping | 0.1 | Per-vertex velocity damping |
| Pressure | 0.0 | Jolt soft-body pressure |
| Stiffness | 0.8 | Scales stretch, shear and bend compliance |
| Stretch Compliance | 1e-4 | Edge stretch softness |
| Shear Compliance | 1e-4 | Shear softness |
| Bend Compliance | 1e-3 | Bending softness |

Changing parameters does not mutate the running Jolt body. Press
**Rebuild With Properties** to recreate it.

Other actions:

- **Paint Cloth** — re-enter paint mode.
- **Clear Paint** — remove all weights and destroy the active cloth body.
- **Rebuild Cloth** — recreate the body from the current mesh and weights.

## Simulation

- Cloth is simulated with Jolt soft bodies.
- Duplicate mesh vertices at UV/normal seams are welded by position for
  simulation, then mapped back to the render mesh.
- `UpdateSoftBodies()` copies simulated positions back into the mesh and
  recomputes normals every frame.
- Cloth simulation runs in both the editor and during Play.
- Soft-body mesh updates run after skeletal animation, so a cloth painted on a
  skinned mesh is not immediately overwritten by animation.

## Saved data

`SoftBodyComponent` is serialized with the entity and stores:

- `meshCloth`;
- `paintWeights` per source mesh vertex;
- mass, friction, restitution and gravity factor;
- damping, pressure and compliance values;
- legacy grid fields for old scenes.

The Jolt body id is runtime-only. A mesh with paint weights but no runtime body
is rebuilt automatically when Play starts.

## Limitations

- Paint before starting simulation. Repainting an already deformed mesh uses
  the deformed vertices as the new rest pose.
- Non-uniform entity scale is not baked into the soft body; keep cloth meshes
  at a uniform scale where possible.
- Tearing, wind forces and self-collision controls are not exposed.
- Painted cloth on an animated skeletal mesh drives the mesh vertices from the
  cloth simulation; that mesh no longer follows the skeletal animation for the
  simulated region.
- Dense meshes produce large paint-weight arrays and should be used carefully.
