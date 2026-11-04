#pragma once

#include <cstddef>
#include <cstdint>

// Fixed-layout packets used between the editor and the isolated C++ script
// worker. Both processes are built from this header, so the ABI stays simple.
enum class CppWorkerMessage : uint32_t {
    Load = 1,
    Start = 2,
    Frame = 3,
    Quit = 4,
    Log = 5,
    Request = 6,
    Response = 7,
    LoadResult = 8,
    Ready = 9,
    FrameDone = 10,
};

enum class CppWorkerRequest : uint32_t {
    KeyDown = 1,
    Axis = 2,
    FindEntity = 3,
    EntityValid = 4,
    GetPosition = 5,
    SetPosition = 6,
    GetVelocity = 7,
    SetVelocity = 8,
    SetCameraMode = 9,
    GetCameraMode = 10,
    GetCameraForward = 11,
    GetCameraRight = 12,
    GetPlayerStrafing = 13,
    SetPlayerStrafing = 14,
    CompileShader = 15,
    CreateEntity = 16,
    DestroyEntity = 17,
    GetName = 18,
    SetName = 19,
    GetRotation = 20,
    SetRotation = 21,
    GetScale = 22,
    SetScale = 23,
    GetParent = 24,
    SetParent = 25,
    HasComponent = 26,
    GetAxisRaw = 27,
    MouseButtonDown = 28,
    GetMousePosition = 29,
    GetMouseDelta = 30,
    GetMouseWheel = 31,
    AttachBox = 32,
    AttachSphere = 33,
    AttachCapsule = 34,
    DestroyBody = 35,
    ApplyImpulse = 36,
    BuildNavMesh = 37,
    FindPath = 38,
    PathPointCount = 39,
    PathPoint = 40,
    PlaySound = 41,
    StopSound = 42,
    IsSoundPlaying = 43,
    StopAllSounds = 44,
    AttachAudioSource = 45,
    PlaySource = 46,
    StopSource = 47,
    IsSourcePlaying = 48,
    SetPostProcessFlag = 49,
    GetPostProcessFlag = 50,
    SetPostProcessFloat = 51,
    GetPostProcessFloat = 52,
    SetPostProcessInt = 53,
    GetPostProcessInt = 54,
    SetPostProcessVec3 = 55,
    GetPostProcessVec3 = 56,
    SetPostProcessEnabled = 57,
    GetPostProcessEnabled = 58,
};

constexpr std::size_t kCppWorkerTextSize = 512;

struct CppWorkerPacket {
    uint32_t type = 0;
    uint32_t id = 0;
    uint32_t kind = 0;
    uint32_t entity = UINT32_MAX;
    uint32_t valid = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
    // Second vector payload, used by two-point requests such as FindPath.
    float x2 = 0.0f;
    float y2 = 0.0f;
    float z2 = 0.0f;
    // Secondary entity/handle used by requests with two targets (for example
    // SetParent). UINT32_MAX means "none".
    uint32_t other = UINT32_MAX;
    char text[kCppWorkerTextSize] = {};
};
