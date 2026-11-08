#include "GaleshiEngine/Scripting/CppScriptApi.hpp"
#include "GaleshiEngine/Scripting/CppScriptWorkerProtocol.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <deque>
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
        m_api.attach_to_bone = &ScriptWorker::AttachToBone;
        m_api.detach_from_bone = &ScriptWorker::DetachFromBone;
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
        m_api.create_ragdoll = &ScriptWorker::CreateRagdoll;
        m_api.destroy_ragdoll = &ScriptWorker::DestroyRagdoll;
        m_api.ragdoll_active = &ScriptWorker::RagdollActive;
        m_api.apply_ragdoll_impulse = &ScriptWorker::ApplyRagdollImpulse;
        m_api.build_navmesh = &ScriptWorker::BuildNavMesh;
        m_api.find_path = &ScriptWorker::FindPath;
        m_api.path_point_count = &ScriptWorker::PathPointCount;
        m_api.path_point = &ScriptWorker::PathPoint;
        m_api.play_sound = &ScriptWorker::PlaySound;
        m_api.stop_sound = &ScriptWorker::StopSound;
        m_api.is_sound_playing = &ScriptWorker::IsSoundPlaying;
        m_api.stop_all_sounds = &ScriptWorker::StopAllSounds;
        m_api.pause_sound = &ScriptWorker::PauseSound;
        m_api.resume_sound = &ScriptWorker::ResumeSound;
        m_api.set_sound_volume = &ScriptWorker::SetSoundVolume;
        m_api.set_sound_pitch = &ScriptWorker::SetSoundPitch;
        m_api.set_sound_looping = &ScriptWorker::SetSoundLooping;
        m_api.seek_sound = &ScriptWorker::SeekSound;
        m_api.get_sound_duration = &ScriptWorker::GetSoundDuration;
        m_api.get_sound_position = &ScriptWorker::GetSoundPosition;
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
        m_api.find_ui_canvas = &ScriptWorker::FindUICanvas;
        m_api.create_ui_canvas = &ScriptWorker::CreateUICanvas;
        m_api.remove_ui_canvas = &ScriptWorker::RemoveUICanvas;
        m_api.set_ui_canvas_flag = &ScriptWorker::SetUICanvasFlag;
        m_api.get_ui_canvas_flag = &ScriptWorker::GetUICanvasFlag;
        m_api.set_ui_canvas_float = &ScriptWorker::SetUICanvasFloat;
        m_api.get_ui_canvas_float = &ScriptWorker::GetUICanvasFloat;
        m_api.set_ui_canvas_string = &ScriptWorker::SetUICanvasString;
        m_api.get_ui_canvas_string = &ScriptWorker::GetUICanvasString;
        m_api.set_ui_canvas_vec2 = &ScriptWorker::SetUICanvasVec2;
        m_api.get_ui_canvas_vec2 = &ScriptWorker::GetUICanvasVec2;
        m_api.find_ui_widget = &ScriptWorker::FindUIWidget;
        m_api.add_ui_widget = &ScriptWorker::AddUIWidget;
        m_api.remove_ui_widget = &ScriptWorker::RemoveUIWidget;
        m_api.set_ui_widget_flag = &ScriptWorker::SetUIWidgetFlag;
        m_api.get_ui_widget_flag = &ScriptWorker::GetUIWidgetFlag;
        m_api.set_ui_widget_float = &ScriptWorker::SetUIWidgetFloat;
        m_api.get_ui_widget_float = &ScriptWorker::GetUIWidgetFloat;
        m_api.set_ui_widget_int = &ScriptWorker::SetUIWidgetInt;
        m_api.get_ui_widget_int = &ScriptWorker::GetUIWidgetInt;
        m_api.set_ui_widget_string = &ScriptWorker::SetUIWidgetString;
        m_api.get_ui_widget_string = &ScriptWorker::GetUIWidgetString;
        m_api.set_ui_widget_vec2 = &ScriptWorker::SetUIWidgetVec2;
        m_api.get_ui_widget_vec2 = &ScriptWorker::GetUIWidgetVec2;
        m_api.set_ui_widget_vec4 = &ScriptWorker::SetUIWidgetVec4;
        m_api.get_ui_widget_vec4 = &ScriptWorker::GetUIWidgetVec4;
        m_api.poll_ui_event = &ScriptWorker::PollUIEvent;
        m_api.is_pointer_over_ui = &ScriptWorker::IsPointerOverUI;
        m_api.get_ui_text = &ScriptWorker::GetUIText;
        m_api.landscape_layer_count = &ScriptWorker::LandscapeLayerCount;
        m_api.landscape_add_layer = &ScriptWorker::LandscapeAddLayer;
        m_api.landscape_remove_layer =
            &ScriptWorker::LandscapeRemoveLayer;
        m_api.landscape_active_layer =
            &ScriptWorker::LandscapeActiveLayer;
        m_api.landscape_set_active_layer =
            &ScriptWorker::LandscapeSetActiveLayer;
        m_api.landscape_set_layer_texture =
            &ScriptWorker::LandscapeSetLayerTexture;
        m_api.landscape_get_layer_texture =
            &ScriptWorker::LandscapeGetLayerTexture;
        m_api.landscape_paint_layer =
            &ScriptWorker::LandscapePaintLayer;
        m_api.landscape_bake = &ScriptWorker::LandscapeBake;
        m_api.find_landscape = &ScriptWorker::FindLandscape;
        m_api.landscape_sample_height = &ScriptWorker::LandscapeSampleHeight;
        m_api.landscape_set_height = &ScriptWorker::LandscapeSetHeight;
        m_api.landscape_apply_brush = &ScriptWorker::LandscapeApplyBrush;
        m_api.landscape_reset_heights =
            &ScriptWorker::LandscapeResetHeights;
        m_api.find_vegetation = &ScriptWorker::FindVegetation;
        m_api.vegetation_regenerate =
            &ScriptWorker::VegetationRegenerate;
        m_api.vegetation_clear = &ScriptWorker::VegetationClear;
        m_api.vegetation_set_bool = &ScriptWorker::VegetationSetBool;
        m_api.vegetation_get_bool = &ScriptWorker::VegetationGetBool;
        m_api.vegetation_set_int = &ScriptWorker::VegetationSetInt;
        m_api.vegetation_get_int = &ScriptWorker::VegetationGetInt;
        m_api.vegetation_set_float = &ScriptWorker::VegetationSetFloat;
        m_api.vegetation_get_float = &ScriptWorker::VegetationGetFloat;
        m_api.vegetation_instance_count =
            &ScriptWorker::VegetationInstanceCount;
        m_api.set_animation_state = &ScriptWorker::SetAnimationState;
        m_api.set_animation_parameter =
            &ScriptWorker::SetAnimationParameter;
        m_api.get_animation_parameter =
            &ScriptWorker::GetAnimationParameter;
        m_api.trigger_animation_parameter =
            &ScriptWorker::TriggerAnimationParameter;
        m_api.play_animation_clip = &ScriptWorker::PlayAnimationClip;
        m_api.stop_animation = &ScriptWorker::StopAnimation;
        m_api.set_animation_speed = &ScriptWorker::SetAnimationSpeed;
        m_api.get_animation_speed = &ScriptWorker::GetAnimationSpeed;
        m_api.animation_clip_count = &ScriptWorker::AnimationClipCount;
        m_api.animation_clip_name = &ScriptWorker::AnimationClipName;
        m_api.animation_clip_duration =
            &ScriptWorker::AnimationClipDuration;
        m_api.asset_exists = &ScriptWorker::AssetExists;
        m_api.asset_resolve_path = &ScriptWorker::AssetResolvePath;
        m_api.asset_load_model = &ScriptWorker::AssetLoadModel;
        m_api.find_particle_emitter = &ScriptWorker::FindParticleEmitter;
        m_api.set_particle_flag = &ScriptWorker::SetParticleFlag;
        m_api.get_particle_flag = &ScriptWorker::GetParticleFlag;
        m_api.set_particle_float = &ScriptWorker::SetParticleFloat;
        m_api.get_particle_float = &ScriptWorker::GetParticleFloat;
        m_api.set_particle_int = &ScriptWorker::SetParticleInt;
        m_api.get_particle_int = &ScriptWorker::GetParticleInt;
        m_api.set_particle_vec3 = &ScriptWorker::SetParticleVec3;
        m_api.get_particle_vec3 = &ScriptWorker::GetParticleVec3;
        m_api.set_particle_vec4 = &ScriptWorker::SetParticleVec4;
        m_api.get_particle_vec4 = &ScriptWorker::GetParticleVec4;
        m_api.find_groom = &ScriptWorker::FindGroom;
        m_api.set_groom_flag = &ScriptWorker::SetGroomFlag;
        m_api.get_groom_flag = &ScriptWorker::GetGroomFlag;
        m_api.set_groom_float = &ScriptWorker::SetGroomFloat;
        m_api.get_groom_float = &ScriptWorker::GetGroomFloat;
        m_api.set_groom_int = &ScriptWorker::SetGroomInt;
        m_api.get_groom_int = &ScriptWorker::GetGroomInt;
        m_api.set_groom_vec3 = &ScriptWorker::SetGroomVec3;
        m_api.get_groom_vec3 = &ScriptWorker::GetGroomVec3;
        m_api.set_groom_string = &ScriptWorker::SetGroomString;
        m_api.get_groom_string = &ScriptWorker::GetGroomString;
        m_api.find_nav_agent = &ScriptWorker::FindNavAgent;
        m_api.find_perception = &ScriptWorker::FindPerception;
        m_api.find_behavior_tree = &ScriptWorker::FindBehaviorTree;
        m_api.find_ai_controller = &ScriptWorker::FindAIController;
        m_api.set_ai_flag = &ScriptWorker::SetAIFlag;
        m_api.get_ai_flag = &ScriptWorker::GetAIFlag;
        m_api.set_ai_float = &ScriptWorker::SetAIFloat;
        m_api.get_ai_float = &ScriptWorker::GetAIFloat;
        m_api.set_ai_int = &ScriptWorker::SetAIInt;
        m_api.get_ai_int = &ScriptWorker::GetAIInt;
        m_api.set_ai_vec3 = &ScriptWorker::SetAIVec3;
        m_api.get_ai_vec3 = &ScriptWorker::GetAIVec3;
        m_api.set_ai_string = &ScriptWorker::SetAIString;
        m_api.get_ai_string = &ScriptWorker::GetAIString;
        m_api.behavior_tree_create_default =
            &ScriptWorker::BehaviorTreeCreateDefault;
        m_api.behavior_tree_node_count =
            &ScriptWorker::BehaviorTreeNodeCount;
        m_api.behavior_tree_add_node =
            &ScriptWorker::BehaviorTreeAddNode;
        m_api.behavior_tree_add_child =
            &ScriptWorker::BehaviorTreeAddChild;
        m_api.behavior_tree_set_root =
            &ScriptWorker::BehaviorTreeSetRoot;
        m_api.behavior_tree_set_node_float =
            &ScriptWorker::BehaviorTreeSetNodeFloat;
        m_api.behavior_tree_set_node_int =
            &ScriptWorker::BehaviorTreeSetNodeInt;
        m_api.behavior_tree_set_node_string =
            &ScriptWorker::BehaviorTreeSetNodeString;
        m_api.behavior_tree_status =
            &ScriptWorker::BehaviorTreeStatus;
        m_api.set_material_float = &ScriptWorker::SetMaterialFloat;
        m_api.get_material_float = &ScriptWorker::GetMaterialFloat;
        m_api.set_material_flag = &ScriptWorker::SetMaterialFlag;
        m_api.get_material_flag = &ScriptWorker::GetMaterialFlag;
        m_api.set_material_color = &ScriptWorker::SetMaterialColor;
        m_api.get_material_color = &ScriptWorker::GetMaterialColor;
        m_api.set_material_texture = &ScriptWorker::SetMaterialTexture;
        m_api.get_material_texture = &ScriptWorker::GetMaterialTexture;
        m_api.renderer_width = &ScriptWorker::RendererWidth;
        m_api.renderer_height = &ScriptWorker::RendererHeight;
        m_api.renderer_deferred = &ScriptWorker::RendererDeferred;
        m_api.set_renderer_deferred = &ScriptWorker::SetRendererDeferred;
        m_api.renderer_lod = &ScriptWorker::RendererLod;
        m_api.set_renderer_lod = &ScriptWorker::SetRendererLod;
        m_api.renderer_occlusion = &ScriptWorker::RendererOcclusion;
        m_api.set_renderer_occlusion =
            &ScriptWorker::SetRendererOcclusion;
        m_api.spawn_skeletal_model =
            &ScriptWorker::SpawnSkeletalModel;
        m_api.apply_skeletal_model =
            &ScriptWorker::ApplySkeletalModel;
        m_api.set_audio_bus = &ScriptWorker::SetAudioBus;
        m_api.get_audio_bus_volume =
            &ScriptWorker::GetAudioBusVolume;
        m_api.get_audio_bus_muted = &ScriptWorker::GetAudioBusMuted;
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
            case CppWorkerMessage::TriggerBeginOverlap:
                m_triggerEvents.push_back(
                    {true, packet.entity, packet.other});
                break;
            case CppWorkerMessage::TriggerEndOverlap:
                m_triggerEvents.push_back(
                    {false, packet.entity, packet.other});
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
        GaleshiScriptTriggerFn triggerBegin = nullptr;
        GaleshiScriptTriggerFn triggerEnd = nullptr;
    };

    struct TriggerEvent {
        bool begin = true;
        uint32_t trigger = UINT32_MAX;
        uint32_t other = UINT32_MAX;
    };
    std::deque<TriggerEvent> m_triggerEvents;

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
                            GaleshiScriptVec3 value2 = {},
                            const char* text2 = nullptr) {
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
        if (text2) {
            std::snprintf(request.text2, sizeof(request.text2), "%s",
                          text2);
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
        auto triggerBegin = reinterpret_cast<GaleshiScriptTriggerFn>(
            dlsym(library, "OnTriggerBeginOverlap"));
        auto triggerEnd = reinterpret_cast<GaleshiScriptTriggerFn>(
            dlsym(library, "OnTriggerEndOverlap"));
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
        m_modules.push_back({library, updateFn, triggerBegin, triggerEnd});
        result.valid = 1;
        WritePacket(result);
    }

    void RunFrame(float dt) {
        while (!m_triggerEvents.empty()) {
            const TriggerEvent event = m_triggerEvents.front();
            m_triggerEvents.pop_front();
            for (const LoadedModule& module : m_modules) {
                const GaleshiScriptTriggerFn callback =
                    event.begin ? module.triggerBegin : module.triggerEnd;
                if (!callback) continue;
                try {
                    callback(event.trigger, event.other);
                } catch (const std::exception& error) {
                    SendLog(std::string("C++ trigger exception: ") +
                            error.what());
                } catch (...) {
                    SendLog("C++ trigger threw an unknown exception");
                }
            }
        }

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

    static bool AttachToBone(void* user, uint32_t entity, uint32_t character,
                             const char* bone) {
        if (!user || !bone) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::AttachToBone, entity, bone, {},
                      0.0f, character)
            .valid != 0;
    }

    static bool DetachFromBone(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::DetachFromBone, entity)
            .valid != 0;
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

    static bool CreateRagdoll(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::CreateRagdoll, entity)
            .valid != 0;
    }

    static bool DestroyRagdoll(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::DestroyRagdoll, entity)
            .valid != 0;
    }

    static bool RagdollActive(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::RagdollActive, entity)
            .valid != 0;
    }

    static bool ApplyRagdollImpulse(void* user, uint32_t entity,
                                    GaleshiScriptVec3 impulse) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::ApplyRagdollImpulse, entity, nullptr,
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

    static bool PauseSound(void* user, const char* path) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::PauseSound, UINT32_MAX, path)
            .valid != 0;
    }

    static bool ResumeSound(void* user, const char* path) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::ResumeSound, UINT32_MAX, path)
            .valid != 0;
    }

    static bool SetSoundVolume(void* user, const char* path, float volume) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetSoundVolume, UINT32_MAX, path,
                      {volume, 0.0f, 0.0f})
            .valid != 0;
    }

    static bool SetSoundPitch(void* user, const char* path, float pitch) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetSoundPitch, UINT32_MAX, path,
                      {pitch, 0.0f, 0.0f})
            .valid != 0;
    }

    static bool SetSoundLooping(void* user, const char* path, bool loop) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetSoundLooping, UINT32_MAX, path,
                      {}, 0.0f, UINT32_MAX, loop ? 1u : 0u)
            .valid != 0;
    }

    static bool SeekSound(void* user, const char* path, float seconds) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SeekSound, UINT32_MAX, path,
                      {seconds, 0.0f, 0.0f})
            .valid != 0;
    }

    static float GetSoundDuration(void* user, const char* path) {
        if (!user || !path) return 0.0f;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetSoundDuration, UINT32_MAX, path)
            .x;
    }

    static float GetSoundPosition(void* user, const char* path) {
        if (!user || !path) return 0.0f;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetSoundPosition, UINT32_MAX, path)
            .x;
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

    static uint32_t FindUICanvas(void* user, const char* canvasId) {
        if (!user || !canvasId) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindUICanvas, UINT32_MAX,
                      canvasId)
            .entity;
    }

    static uint32_t CreateUICanvas(void* user, const char* canvasId,
                                   const char* title) {
        if (!user || !canvasId) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::CreateUICanvas, UINT32_MAX,
                      canvasId, {}, 0.0f, UINT32_MAX, 0, {},
                      title ? title : "UI")
            .entity;
    }

    static bool RemoveUICanvas(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::RemoveUICanvas, entity)
            .valid != 0;
    }

    static bool SetUICanvasFlag(void* user, uint32_t entity,
                                const char* flag, bool value) {
        if (!user || !flag) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetUICanvasFlag, entity, flag,
                      {}, 0.0f, UINT32_MAX, value ? 1u : 0u)
            .valid != 0;
    }

    static bool GetUICanvasFlag(void* user, uint32_t entity,
                                const char* flag, bool* value) {
        if (!user || !flag || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUICanvasFlag, entity, flag);
        if (!response.valid) return false;
        *value = response.entity != 0;
        return true;
    }

    static bool SetUICanvasFloat(void* user, uint32_t entity,
                                 const char* property, float value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetUICanvasFloat, entity,
                      property, {value, 0.0f, 0.0f})
            .valid != 0;
    }

    static bool GetUICanvasFloat(void* user, uint32_t entity,
                                 const char* property, float* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUICanvasFloat, entity, property);
        if (!response.valid) return false;
        *value = response.x;
        return true;
    }

    static bool SetUICanvasString(void* user, uint32_t entity,
                                  const char* property,
                                  const char* value) {
        if (!user || !property || !value) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetUICanvasString, entity,
                      property, {}, 0.0f, UINT32_MAX, 0, {}, value)
            .valid != 0;
    }

    static const char* GetUICanvasString(void* user, uint32_t entity,
                                         const char* property) {
        if (!user || !property) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUICanvasString, entity, property);
        s_uiString = response.valid ? std::string(response.text)
                                    : std::string{};
        return s_uiString.c_str();
    }

    static bool SetUICanvasVec2(void* user, uint32_t entity,
                                const char* property,
                                GaleshiScriptVec2 value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetUICanvasVec2, entity,
                      property, {value.x, value.y, 0.0f})
            .valid != 0;
    }

    static bool GetUICanvasVec2(void* user, uint32_t entity,
                                const char* property,
                                GaleshiScriptVec2* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUICanvasVec2, entity, property);
        if (!response.valid) return false;
        *value = {response.x, response.y};
        return true;
    }

    static int32_t FindUIWidget(void* user, uint32_t canvasEntity,
                                const char* widgetId) {
        if (!user || !widgetId) return -1;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::FindUIWidget, canvasEntity, widgetId);
        return response.valid
            ? static_cast<int32_t>(response.entity)
            : -1;
    }

    static int32_t AddUIWidget(void* user, uint32_t canvasEntity,
                               const char* widgetId,
                               const char* type) {
        if (!user || !widgetId) return -1;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::AddUIWidget, canvasEntity, widgetId,
                {}, 0.0f, UINT32_MAX, 0, {},
                type ? type : "panel");
        return response.valid
            ? static_cast<int32_t>(response.entity)
            : -1;
    }

    static bool RemoveUIWidget(void* user, uint32_t canvasEntity,
                               const char* widgetId) {
        if (!user || !widgetId) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::RemoveUIWidget, canvasEntity,
                      widgetId)
            .valid != 0;
    }

    static bool SetUIWidgetFlag(void* user, uint32_t canvasEntity,
                                const char* widgetId,
                                const char* property, bool value) {
        if (!user || !widgetId || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetUIWidgetFlag, canvasEntity,
                      widgetId, {}, 0.0f, UINT32_MAX,
                      value ? 1u : 0u, {}, property)
            .valid != 0;
    }

    static bool GetUIWidgetFlag(void* user, uint32_t canvasEntity,
                                const char* widgetId,
                                const char* property, bool* value) {
        if (!user || !widgetId || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUIWidgetFlag, canvasEntity,
                widgetId, {}, 0.0f, UINT32_MAX, 0, {}, property);
        if (!response.valid) return false;
        *value = response.entity != 0;
        return true;
    }

    static bool SetUIWidgetFloat(void* user, uint32_t canvasEntity,
                                 const char* widgetId,
                                 const char* property, float value) {
        if (!user || !widgetId || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetUIWidgetFloat, canvasEntity,
                      widgetId, {value, 0.0f, 0.0f}, 0.0f,
                      UINT32_MAX, 0, {}, property)
            .valid != 0;
    }

    static bool GetUIWidgetFloat(void* user, uint32_t canvasEntity,
                                 const char* widgetId,
                                 const char* property, float* value) {
        if (!user || !widgetId || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUIWidgetFloat, canvasEntity,
                widgetId, {}, 0.0f, UINT32_MAX, 0, {}, property);
        if (!response.valid) return false;
        *value = response.x;
        return true;
    }

    static bool SetUIWidgetInt(void* user, uint32_t canvasEntity,
                               const char* widgetId,
                               const char* property, int value) {
        if (!user || !widgetId || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetUIWidgetInt, canvasEntity,
                      widgetId, {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(value), {}, property)
            .valid != 0;
    }

    static bool GetUIWidgetInt(void* user, uint32_t canvasEntity,
                               const char* widgetId,
                               const char* property, int* value) {
        if (!user || !widgetId || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUIWidgetInt, canvasEntity,
                widgetId, {}, 0.0f, UINT32_MAX, 0, {}, property);
        if (!response.valid) return false;
        *value = static_cast<int>(response.entity);
        return true;
    }

    static bool SetUIWidgetString(void* user, uint32_t canvasEntity,
                                  const char* widgetId,
                                  const char* property,
                                  const char* value) {
        if (!user || !widgetId || !property || !value) return false;
        std::string combined(property);
        combined += '\n';
        combined += value;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetUIWidgetString, canvasEntity,
                      widgetId, {}, 0.0f, UINT32_MAX, 0, {},
                      combined.c_str())
            .valid != 0;
    }

    static const char* GetUIWidgetString(void* user,
                                         uint32_t canvasEntity,
                                         const char* widgetId,
                                         const char* property) {
        if (!user || !widgetId || !property) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUIWidgetString, canvasEntity,
                widgetId, {}, 0.0f, UINT32_MAX, 0, {}, property);
        s_uiString = response.valid ? std::string(response.text)
                                    : std::string{};
        return s_uiString.c_str();
    }

    static bool SetUIWidgetVec2(void* user, uint32_t canvasEntity,
                                const char* widgetId,
                                const char* property,
                                GaleshiScriptVec2 value) {
        if (!user || !widgetId || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetUIWidgetVec2, canvasEntity,
                      widgetId, {value.x, value.y, 0.0f}, 0.0f,
                      UINT32_MAX, 0, {}, property)
            .valid != 0;
    }

    static bool GetUIWidgetVec2(void* user, uint32_t canvasEntity,
                                const char* widgetId,
                                const char* property,
                                GaleshiScriptVec2* value) {
        if (!user || !widgetId || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUIWidgetVec2, canvasEntity,
                widgetId, {}, 0.0f, UINT32_MAX, 0, {}, property);
        if (!response.valid) return false;
        *value = {response.x, response.y};
        return true;
    }

    static bool SetUIWidgetVec4(void* user, uint32_t canvasEntity,
                                const char* widgetId,
                                const char* property,
                                GaleshiScriptVec4 value) {
        if (!user || !widgetId || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetUIWidgetVec4, canvasEntity,
                      widgetId, {value.x, value.y, value.z}, value.w,
                      UINT32_MAX, 0, {}, property)
            .valid != 0;
    }

    static bool GetUIWidgetVec4(void* user, uint32_t canvasEntity,
                                const char* widgetId,
                                const char* property,
                                GaleshiScriptVec4* value) {
        if (!user || !widgetId || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUIWidgetVec4, canvasEntity,
                widgetId, {}, 0.0f, UINT32_MAX, 0, {}, property);
        if (!response.valid) return false;
        *value = {response.x, response.y, response.z, response.w};
        return true;
    }

    static bool PollUIEvent(void* user, GaleshiScriptUIEvent* event) {
        if (!user || !event) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::PollUIEvent, UINT32_MAX);
        if (!response.valid) return false;
        event->canvas_entity = response.entity;
        event->type = static_cast<int>(response.other);
        event->value = response.x;
        event->checked = response.w != 0.0f;
        std::snprintf(event->canvas_id, sizeof(event->canvas_id), "%s",
                      response.text);
        std::snprintf(event->widget_id, sizeof(event->widget_id), "%s",
                      response.text2);
        return true;
    }

    static bool IsPointerOverUI(void* user) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::IsPointerOverUI, UINT32_MAX)
            .valid != 0;
    }

    static const char* GetUIText(void* user, uint32_t canvasEntity,
                                 const char* canvasId,
                                 const char* widgetId) {
        if (!user || !canvasId || !widgetId) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetUIText, canvasEntity, canvasId, {},
                0.0f, UINT32_MAX, 0, {}, widgetId);
        s_uiString = response.valid ? std::string(response.text)
                                    : std::string{};
        return s_uiString.c_str();
    }

    static uint32_t FindLandscape(void* user) {
        if (!user) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindLandscape, UINT32_MAX)
            .entity;
    }

    static float LandscapeSampleHeight(void* user, float localX,
                                       float localZ) {
        if (!user) return 0.0f;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapeSampleHeight, UINT32_MAX,
                      nullptr, {localX, localZ, 0.0f})
            .x;
    }

    static bool LandscapeSetHeight(void* user, float localX, float localZ,
                                   float height) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapeSetHeight, UINT32_MAX,
                      nullptr, {localX, localZ, height})
            .valid != 0;
    }

    static bool LandscapeApplyBrush(void* user, int tool, float localX,
                                    float localZ, float radius,
                                    float strength, float falloff,
                                    float dt) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapeApplyBrush, UINT32_MAX,
                      nullptr, {localX, localZ, radius}, strength,
                      UINT32_MAX, static_cast<uint32_t>(tool),
                      {falloff, dt, 0.0f})
            .valid != 0;
    }

    static bool LandscapeResetHeights(void* user) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapeResetHeights, UINT32_MAX)
            .valid != 0;
    }

    static uint32_t FindVegetation(void* user) {
        if (!user) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindVegetation, UINT32_MAX)
            .entity;
    }

    static bool VegetationRegenerate(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::VegetationRegenerate, entity)
            .valid != 0;
    }

    static bool VegetationClear(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::VegetationClear, entity)
            .valid != 0;
    }

    static bool VegetationSetBool(void* user, uint32_t entity,
                                  const char* property, bool value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::VegetationSetBool, entity,
                      property, {}, 0.0f, UINT32_MAX,
                      value ? 1u : 0u)
            .valid != 0;
    }

    static bool VegetationGetBool(void* user, uint32_t entity,
                                  const char* property, bool* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::VegetationGetBool, entity, property);
        if (!response.valid) return false;
        *value = response.entity != 0;
        return true;
    }

    static bool VegetationSetInt(void* user, uint32_t entity,
                                 const char* property, int value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::VegetationSetInt, entity,
                      property, {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(value))
            .valid != 0;
    }

    static bool VegetationGetInt(void* user, uint32_t entity,
                                 const char* property, int* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::VegetationGetInt, entity, property);
        if (!response.valid) return false;
        *value = static_cast<int>(response.entity);
        return true;
    }

    static bool VegetationSetFloat(void* user, uint32_t entity,
                                   const char* property, float value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::VegetationSetFloat, entity,
                      property, {value, 0.0f, 0.0f})
            .valid != 0;
    }

    static bool VegetationGetFloat(void* user, uint32_t entity,
                                   const char* property, float* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::VegetationGetFloat, entity, property);
        if (!response.valid) return false;
        *value = response.x;
        return true;
    }

    static int32_t VegetationInstanceCount(void* user, uint32_t entity,
                                           bool grass, bool painted) {
        if (!user) return 0;
        const uint32_t flags =
            (grass ? 1u : 0u) | (painted ? 2u : 0u);
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::VegetationInstanceCount, entity,
                nullptr, {}, 0.0f, UINT32_MAX, flags);
        return response.valid
            ? static_cast<int32_t>(response.entity)
            : 0;
    }

    static bool SetAnimationState(void* user, uint32_t entity,
                                  const char* state) {
        if (!user || !state) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetAnimationState, entity, state)
            .valid != 0;
    }

    static bool SetAnimationParameter(void* user, uint32_t entity,
                                      const char* name, float value) {
        if (!user || !name) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetAnimationParameter, entity,
                      name, {value, 0.0f, 0.0f})
            .valid != 0;
    }

    static float GetAnimationParameter(void* user, uint32_t entity,
                                       const char* name) {
        if (!user || !name) return 0.0f;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetAnimationParameter, entity,
                      name)
            .x;
    }

    static bool TriggerAnimationParameter(void* user, uint32_t entity,
                                          const char* name) {
        if (!user || !name) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::TriggerAnimationParameter, entity,
                      name)
            .valid != 0;
    }

    static bool PlayAnimationClip(void* user, uint32_t entity, int clip) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::PlayAnimationClip, entity,
                      nullptr, {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(clip))
            .valid != 0;
    }

    static bool StopAnimation(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::StopAnimation, entity)
            .valid != 0;
    }

    static bool SetAnimationSpeed(void* user, uint32_t entity,
                                  float speed) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetAnimationSpeed, entity,
                      nullptr, {speed, 0.0f, 0.0f})
            .valid != 0;
    }

    static float GetAnimationSpeed(void* user, uint32_t entity) {
        if (!user) return 1.0f;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetAnimationSpeed, entity)
            .x;
    }

    static uint32_t AnimationClipCount(void* user, uint32_t entity) {
        if (!user) return 0;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::AnimationClipCount, entity)
            .entity;
    }

    static const char* AnimationClipName(void* user, uint32_t entity,
                                         uint32_t index) {
        if (!user) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::AnimationClipName, entity, nullptr, {},
                0.0f, UINT32_MAX, index);
        s_animationString = response.valid
            ? std::string(response.text) : std::string{};
        return s_animationString.c_str();
    }

    static float AnimationClipDuration(void* user, uint32_t entity,
                                       uint32_t index) {
        if (!user) return 0.0f;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::AnimationClipDuration, entity,
                      nullptr, {}, 0.0f, UINT32_MAX, index)
            .x;
    }

    static bool AssetExists(void* user, const char* path) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::AssetExists, UINT32_MAX, path)
            .valid != 0;
    }

    static const char* AssetResolvePath(void* user, const char* path) {
        if (!user || !path) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::AssetResolvePath, UINT32_MAX, path);
        s_animationString = response.valid
            ? std::string(response.text) : std::string{};
        return s_animationString.c_str();
    }

    static bool AssetLoadModel(void* user, const char* path) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::AssetLoadModel, UINT32_MAX, path)
            .valid != 0;
    }

    static uint32_t FindParticleEmitter(void* user) {
        if (!user) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindParticleEmitter, UINT32_MAX)
            .entity;
    }

    static bool SetParticleFlag(void* user, uint32_t entity,
                                const char* property, bool value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetParticleFlag, entity,
                      property, {}, 0.0f, UINT32_MAX,
                      value ? 1u : 0u)
            .valid != 0;
    }

    static bool GetParticleFlag(void* user, uint32_t entity,
                                const char* property, bool* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetParticleFlag, entity, property);
        if (!response.valid) return false;
        *value = response.entity != 0;
        return true;
    }

    static bool SetParticleFloat(void* user, uint32_t entity,
                                 const char* property, float value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetParticleFloat, entity,
                      property, {value, 0.0f, 0.0f})
            .valid != 0;
    }

    static bool GetParticleFloat(void* user, uint32_t entity,
                                 const char* property, float* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetParticleFloat, entity, property);
        if (!response.valid) return false;
        *value = response.x;
        return true;
    }

    static bool SetParticleInt(void* user, uint32_t entity,
                               const char* property, int value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetParticleInt, entity,
                      property, {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(value))
            .valid != 0;
    }

    static bool GetParticleInt(void* user, uint32_t entity,
                               const char* property, int* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetParticleInt, entity, property);
        if (!response.valid) return false;
        *value = static_cast<int>(response.entity);
        return true;
    }

    static bool SetParticleVec3(void* user, uint32_t entity,
                                const char* property,
                                GaleshiScriptVec3 value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetParticleVec3, entity,
                      property, value)
            .valid != 0;
    }

    static bool GetParticleVec3(void* user, uint32_t entity,
                                const char* property,
                                GaleshiScriptVec3* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetParticleVec3, entity, property);
        if (!response.valid) return false;
        *value = {response.x, response.y, response.z};
        return true;
    }

    static bool SetParticleVec4(void* user, uint32_t entity,
                                const char* property,
                                GaleshiScriptVec4 value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetParticleVec4, entity,
                      property, {value.x, value.y, value.z}, value.w)
            .valid != 0;
    }

    static bool GetParticleVec4(void* user, uint32_t entity,
                                const char* property,
                                GaleshiScriptVec4* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetParticleVec4, entity, property);
        if (!response.valid) return false;
        *value = {response.x, response.y, response.z, response.w};
        return true;
    }

    static uint32_t FindGroom(void* user) {
        if (!user) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindGroom, UINT32_MAX)
            .entity;
    }

    static bool SetGroomFlag(void* user, uint32_t entity,
                             const char* property, bool value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetGroomFlag, entity, property,
                      {}, 0.0f, UINT32_MAX, value ? 1u : 0u)
            .valid != 0;
    }

    static bool GetGroomFlag(void* user, uint32_t entity,
                             const char* property, bool* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetGroomFlag, entity, property);
        if (!response.valid) return false;
        *value = response.entity != 0;
        return true;
    }

    static bool SetGroomFloat(void* user, uint32_t entity,
                              const char* property, float value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetGroomFloat, entity, property,
                      {value, 0.0f, 0.0f})
            .valid != 0;
    }

    static bool GetGroomFloat(void* user, uint32_t entity,
                              const char* property, float* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetGroomFloat, entity, property);
        if (!response.valid) return false;
        *value = response.x;
        return true;
    }

    static bool SetGroomInt(void* user, uint32_t entity,
                            const char* property, int value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetGroomInt, entity, property,
                      {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(value))
            .valid != 0;
    }

    static bool GetGroomInt(void* user, uint32_t entity,
                            const char* property, int* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetGroomInt, entity, property);
        if (!response.valid) return false;
        *value = static_cast<int>(response.entity);
        return true;
    }

    static bool SetGroomVec3(void* user, uint32_t entity,
                             const char* property,
                             GaleshiScriptVec3 value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetGroomVec3, entity, property,
                      value)
            .valid != 0;
    }

    static bool GetGroomVec3(void* user, uint32_t entity,
                             const char* property,
                             GaleshiScriptVec3* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetGroomVec3, entity, property);
        if (!response.valid) return false;
        *value = {response.x, response.y, response.z};
        return true;
    }

    static bool SetGroomString(void* user, uint32_t entity,
                               const char* property, const char* value) {
        if (!user || !property || !value) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetGroomString, entity, property,
                      {}, 0.0f, UINT32_MAX, 0, {}, value)
            .valid != 0;
    }

    static const char* GetGroomString(void* user, uint32_t entity,
                                      const char* property) {
        if (!user || !property) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetGroomString, entity, property);
        s_groomString = response.valid ? std::string(response.text)
                                       : std::string{};
        return s_groomString.c_str();
    }

    static uint32_t FindNavAgent(void* user) {
        if (!user) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindNavAgent, UINT32_MAX)
            .entity;
    }

    static uint32_t FindPerception(void* user) {
        if (!user) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindPerception, UINT32_MAX)
            .entity;
    }

    static uint32_t FindBehaviorTree(void* user) {
        if (!user) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindBehaviorTree, UINT32_MAX)
            .entity;
    }

    static uint32_t FindAIController(void* user) {
        if (!user) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::FindAIController, UINT32_MAX)
            .entity;
    }

    static bool SetAIFlag(void* user, uint32_t entity,
                          const char* component, const char* property,
                          bool value) {
        if (!user || !component || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetAIFlag, entity, property, {},
                      0.0f, UINT32_MAX, value ? 1u : 0u, {}, component)
            .valid != 0;
    }

    static bool GetAIFlag(void* user, uint32_t entity,
                          const char* component, const char* property,
                          bool* value) {
        if (!user || !component || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetAIFlag, entity, property, {}, 0.0f,
                UINT32_MAX, 0, {}, component);
        if (!response.valid) return false;
        *value = response.entity != 0;
        return true;
    }

    static bool SetAIFloat(void* user, uint32_t entity,
                           const char* component, const char* property,
                           float value) {
        if (!user || !component || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetAIFloat, entity, property,
                      {value, 0.0f, 0.0f}, 0.0f, UINT32_MAX, 0, {},
                      component)
            .valid != 0;
    }

    static bool GetAIFloat(void* user, uint32_t entity,
                           const char* component, const char* property,
                           float* value) {
        if (!user || !component || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetAIFloat, entity, property, {}, 0.0f,
                UINT32_MAX, 0, {}, component);
        if (!response.valid) return false;
        *value = response.x;
        return true;
    }

    static bool SetAIInt(void* user, uint32_t entity,
                         const char* component, const char* property,
                         int value) {
        if (!user || !component || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetAIInt, entity, property, {},
                      0.0f, UINT32_MAX, static_cast<uint32_t>(value), {},
                      component)
            .valid != 0;
    }

    static bool GetAIInt(void* user, uint32_t entity,
                         const char* component, const char* property,
                         int* value) {
        if (!user || !component || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetAIInt, entity, property, {}, 0.0f,
                UINT32_MAX, 0, {}, component);
        if (!response.valid) return false;
        *value = static_cast<int>(response.entity);
        return true;
    }

    static bool SetAIVec3(void* user, uint32_t entity,
                          const char* component, const char* property,
                          GaleshiScriptVec3 value) {
        if (!user || !component || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetAIVec3, entity, property,
                      value, 0.0f, UINT32_MAX, 0, {}, component)
            .valid != 0;
    }

    static bool GetAIVec3(void* user, uint32_t entity,
                          const char* component, const char* property,
                          GaleshiScriptVec3* value) {
        if (!user || !component || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetAIVec3, entity, property, {}, 0.0f,
                UINT32_MAX, 0, {}, component);
        if (!response.valid) return false;
        *value = {response.x, response.y, response.z};
        return true;
    }

    static bool SetAIString(void* user, uint32_t entity,
                            const char* component, const char* property,
                            const char* value) {
        if (!user || !component || !property || !value) return false;
        std::string combined(component);
        combined += '\n';
        combined += value;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetAIString, entity, property,
                      {}, 0.0f, UINT32_MAX, 0, {}, combined.c_str())
            .valid != 0;
    }

    static const char* GetAIString(void* user, uint32_t entity,
                                   const char* component,
                                   const char* property) {
        if (!user || !component || !property) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetAIString, entity, property, {}, 0.0f,
                UINT32_MAX, 0, {}, component);
        s_aiString = response.valid ? std::string(response.text)
                                    : std::string{};
        return s_aiString.c_str();
    }

    static bool BehaviorTreeCreateDefault(void* user, uint32_t entity) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::BehaviorTreeCreateDefault, entity)
            .valid != 0;
    }

    static uint32_t BehaviorTreeNodeCount(void* user, uint32_t entity) {
        if (!user) return 0;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::BehaviorTreeNodeCount, entity)
            .entity;
    }

    static int32_t BehaviorTreeAddNode(void* user, uint32_t entity,
                                       const char* name, int type,
                                       int condition, int action) {
        if (!user || !name) return -1;
        const uint32_t packed =
            (static_cast<uint32_t>(type) & 0xFFu) |
            ((static_cast<uint32_t>(condition) & 0xFFu) << 8) |
            ((static_cast<uint32_t>(action) & 0xFFu) << 16);
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::BehaviorTreeAddNode, entity, name, {},
                0.0f, UINT32_MAX, packed);
        return response.valid
            ? static_cast<int32_t>(response.entity)
            : -1;
    }

    static bool BehaviorTreeAddChild(void* user, uint32_t entity,
                                     int parent, int child) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::BehaviorTreeAddChild, entity,
                      nullptr, {}, 0.0f,
                      static_cast<uint32_t>(child),
                      static_cast<uint32_t>(parent))
            .valid != 0;
    }

    static bool BehaviorTreeSetRoot(void* user, uint32_t entity,
                                    int root) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::BehaviorTreeSetRoot, entity,
                      nullptr, {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(root))
            .valid != 0;
    }

    static bool BehaviorTreeSetNodeFloat(void* user, uint32_t entity,
                                         int index, const char* property,
                                         float value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::BehaviorTreeSetNodeFloat, entity,
                      property, {value, 0.0f, 0.0f}, 0.0f,
                      UINT32_MAX, static_cast<uint32_t>(index))
            .valid != 0;
    }

    static bool BehaviorTreeSetNodeInt(void* user, uint32_t entity,
                                       int index, const char* property,
                                       int value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::BehaviorTreeSetNodeInt, entity,
                      property, {}, 0.0f,
                      static_cast<uint32_t>(index),
                      static_cast<uint32_t>(value))
            .valid != 0;
    }

    static bool BehaviorTreeSetNodeString(void* user, uint32_t entity,
                                          int index, const char* property,
                                          const char* value) {
        if (!user || !property || !value) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::BehaviorTreeSetNodeString, entity,
                      property, {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(index), {}, value)
            .valid != 0;
    }

    static const char* BehaviorTreeStatus(void* user, uint32_t entity) {
        if (!user) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::BehaviorTreeStatus, entity);
        s_aiString = response.valid ? std::string(response.text)
                                    : std::string{};
        return s_aiString.c_str();
    }

    static bool SetMaterialFloat(void* user, uint32_t entity,
                                 const char* property, float value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetMaterialFloat, entity,
                      property, {value, 0.0f, 0.0f})
            .valid != 0;
    }

    static bool GetMaterialFloat(void* user, uint32_t entity,
                                 const char* property, float* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetMaterialFloat, entity, property);
        if (!response.valid) return false;
        *value = response.x;
        return true;
    }

    static bool SetMaterialFlag(void* user, uint32_t entity,
                                const char* property, bool value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetMaterialFlag, entity,
                      property, {}, 0.0f, UINT32_MAX,
                      value ? 1u : 0u)
            .valid != 0;
    }

    static bool GetMaterialFlag(void* user, uint32_t entity,
                                const char* property, bool* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetMaterialFlag, entity, property);
        if (!response.valid) return false;
        *value = response.entity != 0;
        return true;
    }

    static bool SetMaterialColor(void* user, uint32_t entity,
                                 const char* property,
                                 GaleshiScriptVec4 value) {
        if (!user || !property) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetMaterialColor, entity,
                      property, {value.x, value.y, value.z}, value.w)
            .valid != 0;
    }

    static bool GetMaterialColor(void* user, uint32_t entity,
                                 const char* property,
                                 GaleshiScriptVec4* value) {
        if (!user || !property || !value) return false;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetMaterialColor, entity, property);
        if (!response.valid) return false;
        *value = {response.x, response.y, response.z, response.w};
        return true;
    }

    static bool SetMaterialTexture(void* user, uint32_t entity,
                                   const char* slot,
                                   const char* path) {
        if (!user || !slot || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetMaterialTexture, entity, slot,
                      {}, 0.0f, UINT32_MAX, 0, {}, path)
            .valid != 0;
    }

    static const char* GetMaterialTexture(void* user, uint32_t entity,
                                          const char* slot) {
        if (!user || !slot) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::GetMaterialTexture, entity, slot);
        s_materialString = response.valid
            ? std::string(response.text) : std::string{};
        return s_materialString.c_str();
    }

    static uint32_t RendererWidth(void* user) {
        if (!user) return 0;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::RendererWidth, UINT32_MAX)
            .entity;
    }

    static uint32_t RendererHeight(void* user) {
        if (!user) return 0;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::RendererHeight, UINT32_MAX)
            .entity;
    }

    static bool RendererDeferred(void* user) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::RendererDeferred, UINT32_MAX)
            .valid != 0;
    }

    static bool SetRendererDeferred(void* user, bool value) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetRendererDeferred, UINT32_MAX,
                      nullptr, {}, 0.0f, UINT32_MAX,
                      value ? 1u : 0u)
            .valid != 0;
    }

    static bool RendererLod(void* user) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::RendererLod, UINT32_MAX)
            .valid != 0;
    }

    static bool SetRendererLod(void* user, bool value) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetRendererLod, UINT32_MAX,
                      nullptr, {}, 0.0f, UINT32_MAX,
                      value ? 1u : 0u)
            .valid != 0;
    }

    static bool RendererOcclusion(void* user) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::RendererOcclusion, UINT32_MAX)
            .valid != 0;
    }

    static bool SetRendererOcclusion(void* user, bool value) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetRendererOcclusion, UINT32_MAX,
                      nullptr, {}, 0.0f, UINT32_MAX,
                      value ? 1u : 0u)
            .valid != 0;
    }

    static uint32_t LandscapeLayerCount(void* user) {
        if (!user) return 0;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapeLayerCount, UINT32_MAX)
            .entity;
    }

    static bool LandscapeAddLayer(void* user) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapeAddLayer, UINT32_MAX)
            .valid != 0;
    }

    static bool LandscapeRemoveLayer(void* user, int layer) {
        if (!user || layer < 0) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapeRemoveLayer, UINT32_MAX,
                      nullptr, {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(layer))
            .valid != 0;
    }

    static int LandscapeActiveLayer(void* user) {
        if (!user) return -1;
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::LandscapeActiveLayer, UINT32_MAX);
        return response.valid
            ? static_cast<int>(response.entity) : -1;
    }

    static bool LandscapeSetActiveLayer(void* user, int layer) {
        if (!user || layer < 0) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapeSetActiveLayer,
                      UINT32_MAX, nullptr, {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(layer))
            .valid != 0;
    }

    static bool LandscapeSetLayerTexture(void* user, int layer,
                                         const char* slot,
                                         const char* path) {
        if (!user || layer < 0 || !slot || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapeSetLayerTexture,
                      UINT32_MAX, slot, {}, 0.0f, UINT32_MAX,
                      static_cast<uint32_t>(layer), {}, path)
            .valid != 0;
    }

    static const char* LandscapeGetLayerTexture(void* user, int layer,
                                                const char* slot) {
        if (!user || layer < 0 || !slot) return "";
        const CppWorkerPacket response =
            static_cast<ScriptWorker*>(user)->Request(
                CppWorkerRequest::LandscapeGetLayerTexture, UINT32_MAX,
                slot, {}, 0.0f, UINT32_MAX,
                static_cast<uint32_t>(layer));
        s_landscapeString = response.valid
            ? std::string(response.text) : std::string{};
        return s_landscapeString.c_str();
    }

    static bool LandscapePaintLayer(void* user, int layer, float localX,
                                    float localZ, float radius,
                                    float strength, float falloff,
                                    float dt, bool erase) {
        if (!user || layer < 0) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapePaintLayer, UINT32_MAX,
                      nullptr, {localX, localZ, radius}, strength,
                      erase ? 1u : 0u, static_cast<uint32_t>(layer),
                      {falloff, dt, 0.0f})
            .valid != 0;
    }

    static bool LandscapeBake(void* user) {
        if (!user) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::LandscapeBake, UINT32_MAX)
            .valid != 0;
    }

    static uint32_t SpawnSkeletalModel(void* user, const char* path,
                                       const char* name, float x, float y,
                                       float z) {
        if (!user || !path) return UINT32_MAX;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SpawnSkeletalModel, UINT32_MAX,
                      path, {x, y, z}, 0.0f, UINT32_MAX, 0, {},
                      name ? name : "SkeletalModel")
            .entity;
    }

    static bool ApplySkeletalModel(void* user, uint32_t entity,
                                   const char* path) {
        if (!user || !path) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::ApplySkeletalModel, entity, path)
            .valid != 0;
    }

    static bool SetAudioBus(void* user, const char* name, float volume,
                            bool muted) {
        if (!user || !name) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::SetAudioBus, UINT32_MAX, name,
                      {volume, 0.0f, 0.0f}, 0.0f, UINT32_MAX,
                      muted ? 1u : 0u)
            .valid != 0;
    }

    static float GetAudioBusVolume(void* user, const char* name) {
        if (!user || !name) return 0.0f;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetAudioBusVolume, UINT32_MAX,
                      name)
            .x;
    }

    static bool GetAudioBusMuted(void* user, const char* name) {
        if (!user || !name) return false;
        return static_cast<ScriptWorker*>(user)
            ->Request(CppWorkerRequest::GetAudioBusMuted, UINT32_MAX,
                      name)
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
    inline static std::string s_uiString;
    inline static std::string s_animationString;
    inline static std::string s_groomString;
    inline static std::string s_aiString;
    inline static std::string s_materialString;
    inline static std::string s_landscapeString;

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
