#pragma once

#include <bgfx/bgfx.h>

namespace Galeshi {

class ImGuiBgfx {
public:
    static void Init();
    static void Shutdown();
    static void Render(bgfx::ViewId viewId);
};

} // namespace Galeshi
