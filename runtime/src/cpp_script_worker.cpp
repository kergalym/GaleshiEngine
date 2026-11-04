#include "GaleshiEngine/Scripting/CppScriptApi.hpp"
#include "GaleshiEngine/Scripting/CppScriptWorkerProtocol.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

bool ReadExact(int fd, void* data, std::size_t size) {
    auto* bytes = static_cast<unsigned char*>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t count = read(fd, bytes + offset, size - offset);
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

bool WriteExact(int fd, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t count = write(fd, bytes + offset, size - offset);
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

class ScriptWorker {
public:
    ScriptWorker() : m_protocolOut(dup(STDOUT_FILENO)) {
        if (m_protocolOut < 0) m_protocolOut = STDOUT_FILENO;
        // Script print statements must not corrupt the protocol stream.
        dup2(STDERR_FILENO, STDOUT_FILENO);

        m_api.user = this;
        m_api.log = &ScriptWorker::Log;
        m_api.is_key_down = &ScriptWorker::IsKeyDown;
        m_api.get_axis = &ScriptWorker::GetAxis;
        m_api.find_entity = &ScriptWorker::FindEntity;
        m_api.entity_valid = &ScriptWorker::EntityValid;
        m_api.get_position = &ScriptWorker::GetPosition;
        m_api.set_position = &ScriptWorker::SetPosition;
        m_api.get_velocity = &ScriptWorker::GetVelocity;
        m_api.set_velocity = &ScriptWorker::SetVelocity;
        m_api.f_interp = [](float current, float target, float dt,
                            float speed) {
            return current + (target - current) *
                                 GaleshiInterpFactor(dt, speed);
        };
        m_api.v_interp = [](GaleshiScriptVec3 current,
                            GaleshiScriptVec3 target, float dt,
                            float speed) {
            const float t = GaleshiInterpFactor(dt, speed);
            return GaleshiScriptVec3{
                current.x + (target.x - current.x) * t,
                current.y + (target.y - current.y) * t,
                current.z + (target.z - current.z) * t,
            };
        };
    }

    ~ScriptWorker() {
        for (const LoadedModule& module : m_modules) {
            if (module.library) dlclose(module.library);
        }
        if (m_protocolOut != STDOUT_FILENO) close(m_protocolOut);
    }

    int Run() {
        CppWorkerPacket packet;
        while (ReadPacket(packet)) {
            switch (static_cast<CppWorkerMessage>(packet.type)) {
            case CppWorkerMessage::Load:
                LoadModule(packet.text);
                break;
            case CppWorkerMessage::Start:
                Send(CppWorkerMessage::Ready);
                break;
            case CppWorkerMessage::Frame:
                RunFrame(packet.x);
                break;
            case CppWorkerMessage::Quit:
                return 0;
            default:
                break;
            }
        }
        return 0;
    }

private:
    struct LoadedModule {
        void* library = nullptr;
        GaleshiScriptUpdateFn update = nullptr;
    };

    bool ReadPacket(CppWorkerPacket& packet) {
        return ReadExact(STDIN_FILENO, &packet, sizeof(packet));
    }

    bool WritePacket(const CppWorkerPacket& packet) {
        return WriteExact(m_protocolOut, &packet, sizeof(packet));
    }

    void Send(CppWorkerMessage type, uint32_t id = 0,
              uint32_t kind = 0, uint32_t entity = UINT32_MAX) {
        CppWorkerPacket packet;
        packet.type = static_cast<uint32_t>(type);
        packet.id = id;
        packet.kind = kind;
        packet.entity = entity;
        WritePacket(packet);
    }

    void SendLog(const std::string& text) {
        CppWorkerPacket packet;
        packet.type = static_cast<uint32_t>(CppWorkerMessage::Log);
        std::snprintf(packet.text, sizeof(packet.text), "%s", text.c_str());
        WritePacket(packet);
    }

    CppWorkerPacket Request(CppWorkerRequest kind, uint32_t entity,
                            const char* text = nullptr,
                            GaleshiScriptVec3 value = {}) {
        CppWorkerPacket request;
        request.type = static_cast<uint32_t>(CppWorkerMessage::Request);
        request.id = m_nextRequestId++;
        request.kind = static_cast<uint32_t>(kind);
        request.entity = entity;
        request.x = value.x;
        request.y = value.y;
        request.z = value.z;
        if (text) {
            std::snprintf(request.text, sizeof(request.text), "%s", text);
        }
        if (!WritePacket(request)) return {};
        CppWorkerPacket response;
        while (ReadPacket(response)) {
            if (response.type == static_cast<uint32_t>(
                                     CppWorkerMessage::Response) &&
                response.id == request.id) {
                return response;
            }
        }
        return {};
    }

    void LoadModule(const char* path) {
        CppWorkerPacket result;
        result.type = static_cast<uint32_t>(CppWorkerMessage::LoadResult);
        result.valid = 0;
        if (!path || !*path) {
            std::snprintf(result.text, sizeof(result.text),
                          "Empty C++ script path");
            WritePacket(result);
            return;
        }

        void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            std::snprintf(result.text, sizeof(result.text), "%s", dlerror());
            WritePacket(result);
            return;
        }
        auto registerFn = reinterpret_cast<GaleshiScriptRegisterFn>(
            dlsym(library, "GaleshiScriptRegister"));
        auto updateFn = reinterpret_cast<GaleshiScriptUpdateFn>(
            dlsym(library, "GaleshiScriptUpdate"));
        if (!registerFn || !updateFn) {
            std::snprintf(result.text, sizeof(result.text),
                          "Missing GaleshiScriptRegister/GaleshiScriptUpdate exports");
            dlclose(library);
            WritePacket(result);
            return;
        }
        try {
            registerFn(&m_api);
        } catch (const std::exception& error) {
            std::snprintf(result.text, sizeof(result.text),
                          "Register exception: %s", error.what());
            dlclose(library);
            WritePacket(result);
            return;
        } catch (...) {
            std::snprintf(result.text, sizeof(result.text),
                          "Register threw an unknown exception");
            dlclose(library);
            WritePacket(result);
            return;
        }
        m_modules.push_back({library, updateFn});
        result.valid = 1;
        WritePacket(result);
    }

    void RunFrame(float dt) {
        for (const LoadedModule& module : m_modules) {
            if (!module.update) continue;
            try {
                module.update(dt);
            } catch (const std::exception& error) {
                SendLog(std::string("C++ script exception: ") + error.what());
            } catch (...) {
                SendLog("C++ script threw an unknown exception");
            }
        }
        Send(CppWorkerMessage::FrameDone);
    }

    static void Log(void* user, const char* message) {
        if (user && message) {
            static_cast<ScriptWorker*>(user)->SendLog(message);
        }
    }

    static bool IsKeyDown(void* user, const char* key) {
        if (!user || !key) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::KeyDown, UINT32_MAX, key)
            .valid != 0;
    }

    static float GetAxis(void* user, const char* axis) {
        if (!user || !axis) return 0.0f;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::Axis, UINT32_MAX, axis)
            .x;
    }

    static uint32_t FindEntity(void* user, const char* name) {
        if (!user || !name) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindEntity, UINT32_MAX, name)
            .entity;
    }

    static bool EntityValid(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::EntityValid, entity)
            .valid != 0;
    }

    static GaleshiScriptVec3 GetPosition(void* user, uint32_t entity) {
        if (!user) return {};
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetPosition, entity);
        return {response.x, response.y, response.z};
    }

    static void SetPosition(void* user, uint32_t entity,
                            GaleshiScriptVec3 position) {
        if (!user) return;
        static_cast<ScriptWorker*>(user)->Request(
            CppWorkerRequest::SetPosition, entity, nullptr, position);
    }

    static GaleshiScriptVec3 GetVelocity(void* user, uint32_t entity) {
        if (!user) return {};
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetVelocity, entity);
        return {response.x, response.y, response.z};
    }

    static void SetVelocity(void* user, uint32_t entity,
                            GaleshiScriptVec3 velocity) {
        if (!user) return;
        static_cast<ScriptWorker*>(user)->Request(
            CppWorkerRequest::SetVelocity, entity, nullptr, velocity);
    }

    int m_protocolOut = STDOUT_FILENO;
    uint32_t m_nextRequestId = 1;
    GaleshiScriptApi m_api;
    std::vector<LoadedModule> m_modules;
};

} // namespace

int main() {
    ScriptWorker worker;
    return worker.Run();
}
