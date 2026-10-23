# --- Auto-added: sol2/lua includes ---
include_directories(${CMAKE_BINARY_DIR}/_deps/sol2-src/include)
include_directories(${CMAKE_BINARY_DIR}/_deps/lua-src/src)
# --- End ---

file(GLOB_RECURSE GALESHI_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE GALESHI_ENGINE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

# "AssetManager copy.cpp" is a historical backup that defines the same
# AssetManager symbols. It sits before AssetManager.cpp in the archive and
# therefore shadows the maintained implementation during linking. Keep the
# file for reference, but never compile it.
list(FILTER GALESHI_ENGINE_SOURCES EXCLUDE REGEX "/AssetManager copy\\.cpp$")

add_library(GaleshiEngine STATIC
    ${GALESHI_ENGINE_SOURCES}
    ${GALESHI_ENGINE_HEADERS})

target_include_directories(GaleshiEngine

    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bgfx/include
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bimg/include
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bimg/3rdparty/stb
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/include
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/3rdparty
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/include/compat/linux
    
    )

target_compile_definitions(GaleshiEngine
    PUBLIC GLM_ENABLE_EXPERIMENTAL)



target_link_libraries(GaleshiEngine
    PUBLIC
        SDL3::SDL3
        glm::glm
        spdlog::spdlog
        EnTT::EnTT
        nlohmann_json::nlohmann_json
        imgui
        imguizmo
        bgfx
        bimg
        bx
    PRIVATE
        assimp::assimp
        Recast
        Detour
        DetourCrowd Jolt)

galeshi_set_warnings(GaleshiEngine)

set_target_properties(GaleshiEngine PROPERTIES
    FOLDER "GaleshiEngine"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

target_link_libraries(GaleshiEngine PUBLIC lua)
