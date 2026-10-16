#pragma once

#include <bgfx/bgfx.h>
#include <string>

namespace Galeshi {

class ShaderManager {
public:
    static bgfx::ProgramHandle LoadProgram(const std::string& vsBin,
                                           const std::string& fsBin);
private:
    static bgfx::ShaderHandle LoadShader(const std::string& path);
};

} // namespace Galeshi
