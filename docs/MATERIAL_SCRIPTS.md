# Material scripts (.mat)

A `.mat` file is a small JSON recipe that tunes the properties of a material
slot without changing its textures. Applying a recipe creates a material
instance: the slot keeps its original material as the instance base, and only
the properties listed in the recipe are changed. The same recipe can be
applied to any material slot, so one master can drive many meshes.

## Format

```json
{
  "name": "Rough painted metal",
  "properties": {
    "metallicFactor": { "set": 1.0 },
    "roughnessFactor": { "add": 0.12 },
    "albedoFactor": { "mul": [0.82, 0.86, 0.92, 1.0] },
    "emissiveFactor": { "set": [0.0, 0.0, 0.0] }
  }
}
```

A plain number, array or boolean is shorthand for `"set"`. An object with an
operator key is also accepted, for example `{ "mul": 0.5 }`, as is
`{ "op": "mul", "value": 0.5 }`.

Supported operators:

| Operator | Meaning |
| --- | --- |
| `set` | Replace the value. |
| `add` / `sub` / `mul` / `div` | Component-wise arithmetic. |
| `min` / `max` | Component-wise minimum or maximum. |
| `clamp` | Clamp to `[min, max]`; the value is a two-element array. |

Scalar values apply to one component. Vector values may have 1, 3 or 4
components; a one-component value broadcasts to every component. Booleans
support `set` only.

Supported properties include:

- scalars: `metallicFactor`, `specularFactor`, `roughnessFactor`,
  `normalStrength`, `occlusionStrength`, `alphaCutoff`, `glassOpacity`,
  `subsurfaceStrength`, `subsurfaceRadius`, `translucencyOpacity`,
  `translucencyStrength`, `translucencyThickness`,
  `translucencyDistortion`, `translucencyShadowDepth`, `shadowDepthOffset`,
  `landscapeUvScale`, `landscapeDetailScale`, `landscapeDetailStrength`,
  `landscapeNormalStrength`;
- colors/vectors: `albedoFactor`, `emissiveFactor`, `subsurfaceColor`,
  `translucencyColor`;
- booleans: `isGlass`, `twoSided`, `backfaceCulling`, `alphaTest`,
  `alphaBlend`, `subsurfaceEnabled`, `translucencyEnabled`, `castsShadow`.

Textures are inherited from the slot's base material and are never changed by
a recipe.

## Editor workflow

1. Right-click in the Content Browser and choose
   **Create Script Templates → Master Material (.mat)**.
2. Edit the generated JSON.
3. Drag the `.mat` file from the Content Browser onto a material slot in
   Details. The slot becomes an instance and its listed properties are tuned.
4. Use **Apply to All Slots** on a selected mesh/model to instance the recipe
   on every material slot in its hierarchy.
5. Use **Reapply Master** after editing the `.mat` file, or **Clear Master**
   to return to the instance base.

The active recipe path is stored with the scene material table, so it survives
save/load. The assigned values are also serialized; use **Reapply Master** to
pick up later edits to the `.mat` file.
