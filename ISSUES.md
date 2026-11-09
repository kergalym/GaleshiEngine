# Known Issues

## Segfault after reloading a scene (NVIDIA Vulkan timer query)

- The editor segfaults after reloading a scene.
- The same crash was previously observed after dropping a model into the viewport.
- The crash is in the render thread, not in scene loading or editor code.

### Root cause

The crash occurs inside the NVIDIA Vulkan driver during bgfx GPU timestamp-query handling:

- `vkCmdResetQueryPool`
- `vkCmdWriteTimestamp`

The scene reload creates and destroys a large number of GPU resources, after which the driver crashes on the next frame submit. There are no application stack frames involved in the fault, so this is not a null-pointer error in `SceneSerializer`, `AssetManager`, or the vegetation/render code.

