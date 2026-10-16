file(GLOB_RECURSE GALESHI_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE GALESHI_ENGINE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

add_library(GaleshiEngine STATIC
    ${GALESHI_ENGINE_SOURCES}
    ${GALESHI_ENGINE_HEADERS})

target_include_directories(GaleshiEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bgfx/include
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bimg/include
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/include
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/3rdparty
        ${CMAKE_BINARY_DIR}/_deps/bgfx-src/bx/include/compat/linux)

target_link_libraries(GaleshiEngine
    PUBLIC
        SDL3::SDL3
        glm::glm
        spdlog::spdlog
        EnTT::EnTT
        imgui
        imguizmo
        bgfx
        bimg
        bx
    PRIVATE
        assimp::assimp
        # Jolt
        Recast
        Detour
        DetourCrowd)

galeshi_set_warnings(GaleshiEngine)

set_target_properties(GaleshiEngine PROPERTIES
    FOLDER "GaleshiEngine"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
