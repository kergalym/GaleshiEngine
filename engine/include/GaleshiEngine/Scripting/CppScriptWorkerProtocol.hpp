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
    char text[kCppWorkerTextSize] = {};
};
