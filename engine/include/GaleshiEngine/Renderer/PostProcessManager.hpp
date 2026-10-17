#pragma once
#include <bgfx/bgfx.h>

namespace Galeshi {
class PostProcessManager {
public:
    void Init() {} // Заглушка
    void Render(bgfx::ViewId viewId, bgfx::TextureHandle inputColor) {} // Заглушка
    void Shutdown() {} // Заглушка
};
}
