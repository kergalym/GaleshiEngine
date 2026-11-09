# Known Issues

## Segfault after reloading a scene (NVIDIA Vulkan timer query)

- The editor segfaults after reloading a scene.
- The same crash was previously observed after dropping a model into the viewport.
- Also reproduced while loading a missing skeletal model (`Assimp failed: Unable to open file`).
- The crash is in the render thread, not in scene loading or editor code.

### Root cause

The crash occurs inside the NVIDIA Vulkan driver during bgfx GPU timestamp-query handling:

- `vkCmdResetQueryPool`
- `vkCmdWriteTimestamp`

The scene reload creates and destroys a large number of GPU resources, after which the driver crashes on the next frame submit. There are no application stack frames involved in the fault, so this is not a null-pointer error in `SceneSerializer`, `AssetManager`, or the vegetation/render code.

### Workaround

- Applied `patches/bgfx-vulkan-disable-gpu-timer.patch`: GPU timestamp queries are disabled in bgfx's Vulkan renderer (`m_timerQuerySupport = false`).
- The profiler reports GPU timers as unavailable until the NVIDIA driver is updated.
- Re-apply the patch after recreating the CMake build directory.

