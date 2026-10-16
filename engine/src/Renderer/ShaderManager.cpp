#include "GaleshiEngine/Renderer/ShaderManager.hpp"

#include <cstdio>
#include <spdlog/spdlog.h>

namespace Galeshi {

static const bgfx::Memory* LoadMem(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        spdlog::error("[ShaderManager] Cannot open: {}", path);
        return nullptr;
    }

    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        spdlog::error("[ShaderManager] Empty or invalid shader file: {} (size={})",
                      path, size);
        std::fclose(f);
        return nullptr;
    }

    spdlog::info("[ShaderManager] Loading {} ({} bytes)", path, size);

    // bgfx ОЖИДАЕТ ТОЧНЫЙ размер бинарника. Никаких null-терминаторов!
    const bgfx::Memory* mem = bgfx::alloc(static_cast<uint32_t>(size));
    const size_t read = std::fread(mem->data, 1, static_cast<size_t>(size), f);
    std::fclose(f);

    if (read != static_cast<size_t>(size)) {
        spdlog::error("[ShaderManager] Short read: {} ({} of {} bytes)",
                      path, read, size);
        return nullptr;
    }

    return mem;
}

bgfx::ShaderHandle ShaderManager::LoadShader(const std::string& path) {
    const bgfx::Memory* mem = LoadMem(path);
    if (!mem) return BGFX_INVALID_HANDLE;
    return bgfx::createShader(mem);
}

bgfx::ProgramHandle ShaderManager::LoadProgram(const std::string& vsBin,
                                               const std::string& fsBin)
{
    const bgfx::ShaderHandle vs = LoadShader(vsBin);
    const bgfx::ShaderHandle fs = LoadShader(fsBin);
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        spdlog::error("[ShaderManager] Failed loading program: {} / {}", vsBin, fsBin);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(vs, fs, true);
}

} // namespace Galeshi
