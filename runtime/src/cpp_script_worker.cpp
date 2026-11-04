#include "GaleshiEngine/Scripting/CppScriptApi.hpp"
#include "GaleshiEngine/Scripting/CppScriptWorkerProtocol.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

namespace {

#if defined(__linux__)
void TryIsolateNamespaces() {
    // Best-effort: unprivileged network/IPC/UTS isolation. Seccomp below also
    // blocks network syscalls even when namespaces are unavailable.
    if (unshare(CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWUTS) != 0) {
        std::fprintf(stderr,
                     "[ScriptWorker] namespace isolation unavailable: %s\n",
                     std::strerror(errno));
    }
}

void BlockSyscall(std::vector<sock_filter>& filter, int syscallNumber) {
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                              static_cast<uint32_t>(syscallNumber), 0, 1));
    filter.push_back(BPF_STMT(
        BPF_RET | BPF_K,
        SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
}

void InstallSeccompFilter() {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        std::fprintf(stderr,
                     "[ScriptWorker] no_new_privs failed: %s\n",
                     std::strerror(errno));
        return;
    }

    std::vector<sock_filter> filter;
    filter.push_back(BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS,
        static_cast<uint32_t>(offsetof(struct seccomp_data, arch))));
#if defined(__x86_64__)
    constexpr uint32_t kNativeArch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    constexpr uint32_t kNativeArch = AUDIT_ARCH_AARCH64;
#else
    constexpr uint32_t kNativeArch = 0;
#endif
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                              kNativeArch, 1, 0));
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    filter.push_back(BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS,
        static_cast<uint32_t>(offsetof(struct seccomp_data, nr))));

    // Block process creation, signals to the parent, networking and dangerous
    // kernel attack surface. The script API does not need any of these.
#define GALESHI_BLOCK(syscall_name) \
    BlockSyscall(filter, SYS_##syscall_name)
#if defined(SYS_execve)
    GALESHI_BLOCK(execve);
#endif
#if defined(SYS_execveat)
    GALESHI_BLOCK(execveat);
#endif
#if defined(SYS_fork)
    GALESHI_BLOCK(fork);
#endif
#if defined(SYS_vfork)
    GALESHI_BLOCK(vfork);
#endif
#if defined(SYS_clone)
    GALESHI_BLOCK(clone);
#endif
#if defined(SYS_clone3)
    GALESHI_BLOCK(clone3);
#endif
#if defined(SYS_socket)
    GALESHI_BLOCK(socket);
#endif
#if defined(SYS_socketpair)
    GALESHI_BLOCK(socketpair);
#endif
#if defined(SYS_connect)
    GALESHI_BLOCK(connect);
#endif
#if defined(SYS_bind)
    GALESHI_BLOCK(bind);
#endif
#if defined(SYS_listen)
    GALESHI_BLOCK(listen);
#endif
#if defined(SYS_accept)
    GALESHI_BLOCK(accept);
#endif
#if defined(SYS_accept4)
    GALESHI_BLOCK(accept4);
#endif
#if defined(SYS_sendto)
    GALESHI_BLOCK(sendto);
#endif
#if defined(SYS_sendmsg)
    GALESHI_BLOCK(sendmsg);
#endif
#if defined(SYS_recvfrom)
    GALESHI_BLOCK(recvfrom);
#endif
#if defined(SYS_recvmsg)
    GALESHI_BLOCK(recvmsg);
#endif
#if defined(SYS_sendmmsg)
    GALESHI_BLOCK(sendmmsg);
#endif
#if defined(SYS_recvmmsg)
    GALESHI_BLOCK(recvmmsg);
#endif
#if defined(SYS_ptrace)
    GALESHI_BLOCK(ptrace);
#endif
#if defined(SYS_process_vm_readv)
    GALESHI_BLOCK(process_vm_readv);
#endif
#if defined(SYS_process_vm_writev)
    GALESHI_BLOCK(process_vm_writev);
#endif
#if defined(SYS_mount)
    GALESHI_BLOCK(mount);
#endif
#if defined(SYS_umount2)
    GALESHI_BLOCK(umount2);
#endif
#if defined(SYS_pivot_root)
    GALESHI_BLOCK(pivot_root);
#endif
#if defined(SYS_chroot)
    GALESHI_BLOCK(chroot);
#endif
#if defined(SYS_setns)
    GALESHI_BLOCK(setns);
#endif
#if defined(SYS_unshare)
    GALESHI_BLOCK(unshare);
#endif
#if defined(SYS_bpf)
    GALESHI_BLOCK(bpf);
#endif
#if defined(SYS_perf_event_open)
    GALESHI_BLOCK(perf_event_open);
#endif
#if defined(SYS_userfaultfd)
    GALESHI_BLOCK(userfaultfd);
#endif
#if defined(SYS_keyctl)
    GALESHI_BLOCK(keyctl);
#endif
#if defined(SYS_add_key)
    GALESHI_BLOCK(add_key);
#endif
#if defined(SYS_request_key)
    GALESHI_BLOCK(request_key);
#endif
#if defined(SYS_init_module)
    GALESHI_BLOCK(init_module);
#endif
#if defined(SYS_finit_module)
    GALESHI_BLOCK(finit_module);
#endif
#if defined(SYS_delete_module)
    GALESHI_BLOCK(delete_module);
#endif
#if defined(SYS_reboot)
    GALESHI_BLOCK(reboot);
#endif
#if defined(SYS_swapon)
    GALESHI_BLOCK(swapon);
#endif
#if defined(SYS_swapoff)
    GALESHI_BLOCK(swapoff);
#endif
#if defined(SYS_kexec_load)
    GALESHI_BLOCK(kexec_load);
#endif
#if defined(SYS_kexec_file_load)
    GALESHI_BLOCK(kexec_file_load);
#endif
#if defined(SYS_open_by_handle_at)
    GALESHI_BLOCK(open_by_handle_at);
#endif
#if defined(SYS_name_to_handle_at)
    GALESHI_BLOCK(name_to_handle_at);
#endif
#if defined(SYS_pidfd_open)
    GALESHI_BLOCK(pidfd_open);
#endif
#if defined(SYS_pidfd_getfd)
    GALESHI_BLOCK(pidfd_getfd);
#endif
#if defined(SYS_pidfd_send_signal)
    GALESHI_BLOCK(pidfd_send_signal);
#endif
#if defined(SYS_kill)
    GALESHI_BLOCK(kill);
#endif
#if defined(SYS_tkill)
    GALESHI_BLOCK(tkill);
#endif
#if defined(SYS_tgkill)
    GALESHI_BLOCK(tgkill);
#endif
#if defined(SYS_setuid)
    GALESHI_BLOCK(setuid);
#endif
#if defined(SYS_setgid)
    GALESHI_BLOCK(setgid);
#endif
#if defined(SYS_setreuid)
    GALESHI_BLOCK(setreuid);
#endif
#if defined(SYS_setregid)
    GALESHI_BLOCK(setregid);
#endif
#if defined(SYS_setresuid)
    GALESHI_BLOCK(setresuid);
#endif
#if defined(SYS_setresgid)
    GALESHI_BLOCK(setresgid);
#endif
#if defined(SYS_setgroups)
    GALESHI_BLOCK(setgroups);
#endif
#undef GALESHI_BLOCK

    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    sock_fprog program{};
    program.len = static_cast<unsigned short>(filter.size());
    program.filter = filter.data();
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &program) != 0) {
        std::fprintf(stderr,
                     "[ScriptWorker] seccomp filter failed: %s\n",
                     std::strerror(errno));
    }
}
#endif

} // namespace


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
        m_api.set_camera_mode = &ScriptWorker::SetCameraMode;
        m_api.get_camera_mode = &ScriptWorker::GetCameraMode;
        m_api.get_camera_forward = &ScriptWorker::GetCameraForward;
        m_api.get_camera_right = &ScriptWorker::GetCameraRight;
        m_api.get_camera_movement = &ScriptWorker::GetCameraMovement;
        m_api.get_player_strafing = &ScriptWorker::GetPlayerStrafing;
        m_api.set_player_strafing = &ScriptWorker::SetPlayerStrafing;
        m_api.compile_shader = &ScriptWorker::CompileShader;
        m_api.get_shader_error = &ScriptWorker::GetShaderError;
        m_api.create_entity = &ScriptWorker::CreateEntity;
        m_api.destroy_entity = &ScriptWorker::DestroyEntity;
        m_api.get_name = &ScriptWorker::GetName;
        m_api.set_name = &ScriptWorker::SetName;
        m_api.get_rotation = &ScriptWorker::GetRotation;
        m_api.set_rotation = &ScriptWorker::SetRotation;
        m_api.get_scale = &ScriptWorker::GetScale;
        m_api.set_scale = &ScriptWorker::SetScale;
        m_api.get_parent = &ScriptWorker::GetParent;
        m_api.set_parent = &ScriptWorker::SetParent;
        m_api.has_component = &ScriptWorker::HasComponent;
        m_api.get_axis_raw = &ScriptWorker::GetAxisRaw;
        m_api.is_mouse_button_down = &ScriptWorker::MouseButtonDown;
        m_api.get_mouse_position = &ScriptWorker::GetMousePosition;
        m_api.get_mouse_delta = &ScriptWorker::GetMouseDelta;
        m_api.get_mouse_wheel = &ScriptWorker::GetMouseWheel;
        m_api.attach_box = &ScriptWorker::AttachBox;
        m_api.attach_sphere = &ScriptWorker::AttachSphere;
        m_api.attach_capsule = &ScriptWorker::AttachCapsule;
        m_api.destroy_body = &ScriptWorker::DestroyBody;
        m_api.apply_impulse = &ScriptWorker::ApplyImpulse;
        m_api.build_navmesh = &ScriptWorker::BuildNavMesh;
        m_api.find_path = &ScriptWorker::FindPath;
        m_api.path_point_count = &ScriptWorker::PathPointCount;
        m_api.path_point = &ScriptWorker::PathPoint;
        m_api.play_sound = &ScriptWorker::PlaySound;
        m_api.stop_sound = &ScriptWorker::StopSound;
        m_api.is_sound_playing = &ScriptWorker::IsSoundPlaying;
        m_api.stop_all_sounds = &ScriptWorker::StopAllSounds;
        m_api.attach_audio_source = &ScriptWorker::AttachAudioSource;
        m_api.play_source = &ScriptWorker::PlaySource;
        m_api.stop_source = &ScriptWorker::StopSource;
        m_api.is_source_playing = &ScriptWorker::IsSourcePlaying;
        m_api.set_post_process_flag = &ScriptWorker::SetPostProcessFlag;
        m_api.get_post_process_flag = &ScriptWorker::GetPostProcessFlag;
        m_api.set_post_process_float = &ScriptWorker::SetPostProcessFloat;
        m_api.get_post_process_float = &ScriptWorker::GetPostProcessFloat;
        m_api.set_post_process_int = &ScriptWorker::SetPostProcessInt;
        m_api.get_post_process_int = &ScriptWorker::GetPostProcessInt;
        m_api.set_post_process_vec3 = &ScriptWorker::SetPostProcessVec3;
        m_api.get_post_process_vec3 = &ScriptWorker::GetPostProcessVec3;
        m_api.set_post_process_enabled =
            &ScriptWorker::SetPostProcessEnabled;
        m_api.get_post_process_enabled =
            &ScriptWorker::GetPostProcessEnabled;
        m_api.abi_version = kGaleshiScriptApiVersion;
        m_api.struct_size = static_cast<uint32_t>(sizeof(GaleshiScriptApi));
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
                            GaleshiScriptVec3 value = {},
                            float w = 0.0f,
                            uint32_t other = UINT32_MAX,
                            uint32_t valid = 0,
                            GaleshiScriptVec3 value2 = {}) {
        CppWorkerPacket request;
        request.type = static_cast<uint32_t>(CppWorkerMessage::Request);
        request.id = m_nextRequestId++;
        request.kind = static_cast<uint32_t>(kind);
        request.entity = entity;
        request.x = value.x;
        request.y = value.y;
        request.z = value.z;
        request.w = w;
        request.x2 = value2.x;
        request.y2 = value2.y;
        request.z2 = value2.z;
        request.other = other;
        request.valid = valid;
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

    static float GetAxisRaw(void* user, const char* axis) {
        if (!user || !axis) return 0.0f;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetAxisRaw, UINT32_MAX, axis)
            .x;
    }

    static bool MouseButtonDown(void* user, const char* button) {
        if (!user || !button) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::MouseButtonDown, UINT32_MAX,
                      button)
            .valid != 0;
    }

    static GaleshiScriptVec2 GetMousePosition(void* user) {
        if (!user) return {};
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetMousePosition, UINT32_MAX);
        return {response.x, response.y};
    }

    static GaleshiScriptVec2 GetMouseDelta(void* user) {
        if (!user) return {};
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetMouseDelta, UINT32_MAX);
        return {response.x, response.y};
    }

    static float GetMouseWheel(void* user) {
        if (!user) return 0.0f;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetMouseWheel, UINT32_MAX)
            .x;
    }

    static uint32_t CreateEntity(void* user, const char* name) {
        if (!user) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::CreateEntity, UINT32_MAX,
                      name ? name : "Entity")
            .entity;
    }

    static bool DestroyEntity(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::DestroyEntity, entity)
            .valid != 0;
    }

    static const char* GetName(void* user, uint32_t entity) {
        if (!user) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetName, entity);
        s_name = response.valid ? std::string(response.text) : std::string{};
        return s_name.c_str();
    }

    static bool SetName(void* user, uint32_t entity, const char* name) {
        if (!user || !name) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetName, entity, name)
            .valid != 0;
    }

    static GaleshiScriptQuat GetRotation(void* user, uint32_t entity) {
        if (!user) return {};
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetRotation, entity);
        return response.valid
            ? GaleshiScriptQuat{response.x, response.y, response.z,
                                response.w}
            : GaleshiScriptQuat{};
    }

    static void SetRotation(void* user, uint32_t entity,
                            GaleshiScriptQuat rotation) {
        if (!user) return;
        static_cast<ScriptWorker*>(user)->Request(
            CppWorkerRequest::SetRotation, entity, nullptr,
            GaleshiScriptVec3{rotation.x, rotation.y, rotation.z},
            rotation.w, UINT32_MAX);
    }

    static GaleshiScriptVec3 GetScale(void* user, uint32_t entity) {
        if (!user) return {};
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetScale, entity);
        return response.valid
            ? GaleshiScriptVec3{response.x, response.y, response.z}
            : GaleshiScriptVec3{1.0f, 1.0f, 1.0f};
    }

    static void SetScale(void* user, uint32_t entity,
                         GaleshiScriptVec3 scale) {
        if (!user) return;
        static_cast<ScriptWorker*>(user)->Request(
            CppWorkerRequest::SetScale, entity, nullptr, scale);
    }

    static uint32_t GetParent(void* user, uint32_t entity) {
        if (!user) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetParent, entity)
            .entity;
    }

    static bool SetParent(void* user, uint32_t entity, uint32_t parent) {
        if (!user) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::SetParent, entity, nullptr, {},
                0.0f, parent);
        return response.valid != 0;
    }

    static bool HasComponent(void* user, uint32_t entity,
                             const char* component) {
        if (!user || !component) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::HasComponent, entity, component)
            .valid != 0;
    }

    static bool AttachBox(void* user, uint32_t entity,
                          GaleshiScriptVec3 halfExtents, bool isStatic) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::AttachBox, entity, nullptr,
                      halfExtents, 0.0f, UINT32_MAX,
                      isStatic ? 1u : 0u)
            .valid != 0;
    }

    static bool AttachSphere(void* user, uint32_t entity, float radius,
                             bool isStatic) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::AttachSphere, entity, nullptr,
                      GaleshiScriptVec3{radius, 0.0f, 0.0f}, 0.0f,
                      UINT32_MAX, isStatic ? 1u : 0u)
            .valid != 0;
    }

    static bool AttachCapsule(void* user, uint32_t entity, float halfHeight,
                              float radius, bool isStatic) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::AttachCapsule, entity, nullptr,
                      GaleshiScriptVec3{halfHeight, radius, 0.0f}, 0.0f,
                      UINT32_MAX, isStatic ? 1u : 0u)
            .valid != 0;
    }

    static bool DestroyBody(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::DestroyBody, entity)
            .valid != 0;
    }

    static bool ApplyImpulse(void* user, uint32_t entity,
                             GaleshiScriptVec3 impulse) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::ApplyImpulse, entity, nullptr,
                      impulse)
            .valid != 0;
    }

    static bool BuildNavMesh(void* user) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::BuildNavMesh, UINT32_MAX)
            .valid != 0;
    }

    static bool FindPath(void* user, GaleshiScriptVec3 start,
                         GaleshiScriptVec3 end) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindPath, UINT32_MAX, nullptr,
                      start, 0.0f, UINT32_MAX, 0, end)
            .valid != 0;
    }

    static uint32_t PathPointCount(void* user) {
        if (!user) return 0;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::PathPointCount, UINT32_MAX)
            .entity;
    }

    static GaleshiScriptVec3 PathPoint(void* user, uint32_t index) {
        if (!user) return {};
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::PathPoint, index);
        return response.valid
            ? GaleshiScriptVec3{response.x, response.y, response.z}
            : GaleshiScriptVec3{};
    }

    static bool PlaySound(void* user, const char* path, bool loop) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::PlaySound, UINT32_MAX, path,
                      {}, 0.0f, UINT32_MAX, loop ? 1u : 0u)
            .valid != 0;
    }

    static bool StopSound(void* user, const char* path) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::StopSound, UINT32_MAX, path)
            .valid != 0;
    }

    static bool IsSoundPlaying(void* user, const char* path) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::IsSoundPlaying, UINT32_MAX, path)
            .valid != 0;
    }

    static void StopAllSounds(void* user) {
        if (!user) return;
        static_cast<ScriptWorker*>(user)->Request(
            CppWorkerRequest::StopAllSounds, UINT32_MAX);
    }

    static bool AttachAudioSource(void* user, uint32_t entity,
                                  const char* path, bool loop,
                                  bool spatial, int bus) {
        if (!user || !path) return false;
        const uint32_t flags =
            (loop ? 1u : 0u) | (spatial ? 2u : 0u);
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::AttachAudioSource, entity, path,
                      {}, 0.0f, static_cast<uint32_t>(bus), flags)
            .valid != 0;
    }

    static bool PlaySource(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::PlaySource, entity)
            .valid != 0;
    }

    static bool StopSource(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::StopSource, entity)
            .valid != 0;
    }

    static bool IsSourcePlaying(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::IsSourcePlaying, entity)
            .valid != 0;
    }

    static bool SetPostProcessFlag(void* user, const char* name,
                                   bool value) {
        if (!user || !name) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetPostProcessFlag, UINT32_MAX,
                      name, {}, 0.0f, UINT32_MAX, value ? 1u : 0u)
            .valid != 0;
    }

    static bool GetPostProcessFlag(void* user, const char* name,
                                   bool* value) {
        if (!user || !name || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetPostProcessFlag, UINT32_MAX, name);
        if (!response.valid) return false;
        *value = response.entity != 0;
        return true;
    }

    static bool SetPostProcessFloat(void* user, const char* name,
                                    float value) {
        if (!user || !name) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetPostProcessFloat, UINT32_MAX,
                      name, GaleshiScriptVec3{value, 0.0f, 0.0f})
            .valid != 0;
    }

    static bool GetPostProcessFloat(void* user, const char* name,
                                    float* value) {
        if (!user || !name || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetPostProcessFloat, UINT32_MAX, name);
        if (!response.valid) return false;
        *value = response.x;
        return true;
    }

    static bool SetPostProcessInt(void* user, const char* name,
                                  int value) {
        if (!user || !name) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetPostProcessInt, UINT32_MAX,
                      name, {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(value))
            .valid != 0;
    }

    static bool GetPostProcessInt(void* user, const char* name,
                                  int* value) {
        if (!user || !name || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetPostProcessInt, UINT32_MAX, name);
        if (!response.valid) return false;
        *value = static_cast<int>(response.entity);
        return true;
    }

    static bool SetPostProcessVec3(void* user, const char* name,
                                   GaleshiScriptVec3 value) {
        if (!user || !name) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetPostProcessVec3, UINT32_MAX,
                      name, value)
            .valid != 0;
    }

    static bool GetPostProcessVec3(void* user, const char* name,
                                   GaleshiScriptVec3* value) {
        if (!user || !name || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetPostProcessVec3, UINT32_MAX, name);
        if (!response.valid) return false;
        *value = {response.x, response.y, response.z};
        return true;
    }

    static bool SetPostProcessEnabled(void* user, bool value) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetPostProcessEnabled, UINT32_MAX,
                      nullptr, {}, 0.0f, UINT32_MAX,
                      value ? 1u : 0u)
            .valid != 0;
    }

    static bool GetPostProcessEnabled(void* user) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetPostProcessEnabled, UINT32_MAX)
            .valid != 0;
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

    static bool SetCameraMode(void* user, int mode) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetCameraMode,
                      static_cast<uint32_t>(mode))
            .valid != 0;
    }

    static int GetCameraMode(void* user) {
        if (!user) return GALESHI_CAMERA_THIRD_PERSON;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetCameraMode, UINT32_MAX);
        return response.valid
            ? static_cast<int>(response.entity)
            : static_cast<int>(GALESHI_CAMERA_THIRD_PERSON);
    }

    static GaleshiScriptVec3 GetCameraForward(void* user) {
        if (!user) return {};
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetCameraForward, UINT32_MAX);
        return response.valid
            ? GaleshiScriptVec3{response.x, response.y, response.z}
            : GaleshiScriptVec3{0.0f, 0.0f, -1.0f};
    }

    static GaleshiScriptVec3 GetCameraRight(void* user) {
        if (!user) return {};
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetCameraRight, UINT32_MAX);
        return response.valid
            ? GaleshiScriptVec3{response.x, response.y, response.z}
            : GaleshiScriptVec3{1.0f, 0.0f, 0.0f};
    }

    static GaleshiScriptVec3 GetCameraMovement(
        void* user, float horizontal, float vertical) {
        if (!user) return {};
        const GaleshiScriptVec3 forward = GetCameraForward(user);
        const GaleshiScriptVec3 right = GetCameraRight(user);
        GaleshiScriptVec3 movement{
            forward.x * vertical + right.x * horizontal,
            forward.y * vertical + right.y * horizontal,
            forward.z * vertical + right.z * horizontal,
        };
        const float length = std::sqrt(
            movement.x * movement.x +
            movement.y * movement.y +
            movement.z * movement.z);
        if (length > 1.0f) {
            movement.x /= length;
            movement.y /= length;
            movement.z /= length;
        }
        return movement;
    }

    static bool GetPlayerStrafing(void* user) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetPlayerStrafing, UINT32_MAX)
            .valid != 0;
    }

    static void SetPlayerStrafing(void* user, bool enabled) {
        if (!user) return;
        static_cast<ScriptWorker*>(user)->Request(
            CppWorkerRequest::SetPlayerStrafing,
            enabled ? 1u : 0u);
    }

    static bool CompileShader(void* user, const char* path) {
        if (!user || !path) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::CompileShader, UINT32_MAX, path);
        s_shaderError = response.valid ? std::string{} : response.text;
        return response.valid != 0;
    }

    static const char* GetShaderError(void* user) {
        (void)user;
        return s_shaderError.c_str();
    }

    inline static std::string s_shaderError;
    inline static std::string s_name;

    int m_protocolOut = STDOUT_FILENO;
    uint32_t m_nextRequestId = 1;
    GaleshiScriptApi m_api;
    std::vector<LoadedModule> m_modules;
};

} // namespace

int main() {
#if defined(__linux__)
    TryIsolateNamespaces();
    InstallSeccompFilter();
#endif
    ScriptWorker worker;
    return worker.Run();
}
