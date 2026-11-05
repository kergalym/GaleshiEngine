#pragma once

#include <bgfx/bgfx.h>

namespace Galeshi {

class ImGuiBgfx {
public:
    static void Init();
    static void Shutdown();
    // Re-uploads the ImGui font atlas after fonts are added at runtime.
    static void ReloadFontTexture();
    static void Render(bgfx::ViewId viewId);
};

} // namespace Galeshi
