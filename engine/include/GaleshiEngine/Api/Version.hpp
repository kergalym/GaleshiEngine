#pragma once

#include <cstdint>

namespace Galeshi::Api {

inline constexpr uint32_t kVersionMajor = 0;
inline constexpr uint32_t kVersionMinor = 2;
inline constexpr uint32_t kVersionPatch = 0;

// Version of the public C++ surface. Increment the minor component when new
// classes/methods are added; increment the major component for removals or
// source-incompatible changes.
inline constexpr uint32_t kApiVersion = 2;

// The isolated C++ script ABI is versioned separately from the engine library.
inline constexpr uint32_t kScriptApiVersion = 17;

const char* VersionString();

} // namespace Galeshi::Api
