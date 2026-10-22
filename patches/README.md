# Local dependency patches

## bgfx Vulkan cubemap workaround

`bgfx-vulkan-cubemap-workaround.patch` fixes cubemap `resolve`/`createView`
layer counts in the Vulkan renderer. BGFX passes the cube count instead of the
face count to cube views, which triggers validation errors and a Vulkan device
loss when IBL cubemaps are created.

The patch was generated against:

- `bgfx.cmake` `fbe794fa4c3424d61cd4f5029e7b4abc7a30580e`
- `bgfx` `9ca7782405427306ed33d665f1583199b45699a4`

Apply it inside the nested `bgfx` checkout:

```sh
cd build/debug/_deps/bgfx-src/bgfx
git apply /path/to/GaleshiEngine/patches/bgfx-vulkan-cubemap-workaround.patch
```

Re-apply the patch after recreating the CMake build directory. Do not replace
it with `numLayers = 1` at call sites: the renderer itself must normalize cube
views to six faces.
